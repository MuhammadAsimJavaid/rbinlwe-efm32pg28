"""Capture and validate one BK 2569B-MSO TVLA waveform.

The scope must already be configured for CH1=AEM and CH2=PD12 trigger.
This first-stage utility deliberately uses a manual board reset so waveform
transport and scaling can be validated before adding bulk synchronization.
"""

from __future__ import annotations

import json
import socket
import struct
import time
from pathlib import Path

import numpy as np


SCOPE_IP = "10.11.13.220"
SCOPE_PORT = 5025
OUTPUT_DIR = Path(__file__).resolve().parent.parent / "tvla_results" / "pilot"


class Scope:
    def __init__(self, host: str, port: int = 5025) -> None:
        self.host = host
        self.port = port
        self.sock: socket.socket
        self.reconnect()

    def reconnect(self) -> None:
        old_sock = getattr(self, "sock", None)
        if old_sock is not None:
            try:
                old_sock.close()
            except OSError:
                pass
        self.sock = socket.create_connection(
            (self.host, self.port), timeout=10.0
        )
        # Waveform blocks are modest, but the scope can occasionally pause
        # during a transfer. Give it enough time before abandoning the stream.
        self.sock.settimeout(15.0)

    def close(self) -> None:
        self.sock.close()

    def write(self, command: str) -> None:
        self.sock.sendall(command.encode("ascii") + b"\n")

    def read_line(self) -> bytes:
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = self.sock.recv(1)
            if not chunk:
                raise ConnectionError("scope closed the connection")
            data.extend(chunk)
        return bytes(data)

    def query(self, command: str) -> str:
        self.write(command)
        while True:
            response = self.read_line().decode("ascii", errors="replace").strip()
            if response:
                return response

    def _query_block_once(self, command: str) -> bytes:
        self.write(command)
        prefix = bytearray()
        while b"#9" not in prefix:
            chunk = self.sock.recv(1)
            if not chunk:
                raise ConnectionError("scope closed during binary header")
            prefix.extend(chunk)
            if len(prefix) > 128:
                raise ValueError(f"binary header not found: {prefix!r}")
        marker = prefix.index(b"#9")
        length_text = bytes(prefix[marker + 2 : marker + 11])
        while len(length_text) < 9:
            length_text += self.sock.recv(9 - len(length_text))
        payload_length = int(length_text)
        payload = bytearray()
        while len(payload) < payload_length:
            chunk = self.sock.recv(min(65536, payload_length - len(payload)))
            if not chunk:
                raise ConnectionError(
                    f"scope closed after {len(payload)}/{payload_length} bytes"
                )
            payload.extend(chunk)
        # Consume the response terminator before the next ASCII query.
        self.sock.settimeout(0.02)
        try:
            self.sock.recv(2)
        except TimeoutError:
            pass
        finally:
            self.sock.settimeout(15.0)
        return bytes(payload)

    def query_block(self, command: str, attempts: int = 3) -> bytes:
        """Read a binary response, reconnecting after a broken TCP stream."""
        for attempt in range(1, attempts + 1):
            try:
                return self._query_block_once(command)
            except (ConnectionError, OSError, ValueError) as exc:
                if attempt == attempts:
                    raise
                print(
                    f"scope block transfer failed ({exc}); reconnecting "
                    f"and retrying {attempt + 1}/{attempts}"
                )
                time.sleep(0.25)
                self.reconnect()
        raise RuntimeError("unreachable")


def descriptor_values(desc: bytes) -> dict[str, float | int]:
    if len(desc) < 188 or not desc.startswith(b"WAVEDESC"):
        raise ValueError(f"unexpected descriptor ({len(desc)} bytes)")
    return {
        "sample_count": struct.unpack_from("<I", desc, 116)[0],
        "vertical_gain_v": struct.unpack_from("<f", desc, 156)[0],
        "vertical_offset_v": struct.unpack_from("<f", desc, 160)[0],
        "sample_interval_s": struct.unpack_from("<f", desc, 176)[0],
        "first_sample_time_s": struct.unpack_from("<d", desc, 180)[0],
    }


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    scope = Scope(SCOPE_IP, SCOPE_PORT)
    try:
        identity = scope.query("*IDN?")
        settings = {
            "identity": identity,
            "time_div": scope.query("TDIV?"),
            "memory": scope.query("MSIZ?"),
            "sample_rate": scope.query("SARA?"),
            "ch1_vdiv": scope.query("C1:VDIV?"),
            "ch1_offset": scope.query("C1:OFST?"),
            "trigger": scope.query("TRSE?"),
        }
        print(json.dumps(settings, indent=2))

        if "100k" not in settings["memory"]:
            raise RuntimeError("scope memory is not set to 100 kpoints")
        if "2.00E+06" not in settings["sample_rate"]:
            raise RuntimeError("scope sample rate is not 2 MSa/s")

        scope.write("WFSU SP,1,NP,100000,FP,0")
        scope.write("TRMD SINGLE")
        input("Scope armed. Press RESET on the PG28 board, then press Enter here... ")

        deadline = time.monotonic() + 90.0
        while True:
            status = scope.query("SAST?")
            print(f"scope status: {status}")
            if status.lower().endswith("stop"):
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("no completed trigger within 90 seconds")
            time.sleep(0.2)

        desc = scope.query_block("C1:WF? DESC")
        meta = descriptor_values(desc)
        raw_block = scope.query_block("C1:WF? DAT2")
        if not raw_block:
            raise RuntimeError("scope returned zero waveform samples")

        raw = np.frombuffer(raw_block, dtype=np.int8).copy()
        expected = int(meta["sample_count"])
        if raw.size != expected:
            raise RuntimeError(f"received {raw.size} samples, expected {expected}")

        voltage = (
            raw.astype(np.float32) * float(meta["vertical_gain_v"])
            - float(meta["vertical_offset_v"])
        )
        time_axis = (
            float(meta["first_sample_time_s"])
            + np.arange(raw.size, dtype=np.float64) * float(meta["sample_interval_s"])
        )

        metadata = {**settings, **meta, "raw_dtype": "int8"}
        np.savez_compressed(
            OUTPUT_DIR / "decryption_trace_0000.npz",
            raw=raw,
            voltage_v=voltage,
            time_s=time_axis,
        )
        (OUTPUT_DIR / "decryption_trace_0000.json").write_text(
            json.dumps(metadata, indent=2), encoding="utf-8"
        )
        print(
            f"saved {raw.size} samples; "
            f"CH1 range {voltage.min():.6f} to {voltage.max():.6f} V"
        )
        print(OUTPUT_DIR / "decryption_trace_0000.npz")
    finally:
        scope.close()


if __name__ == "__main__":
    main()
