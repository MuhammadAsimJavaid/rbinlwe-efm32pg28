"""GW Instek GDS-3154 transport and waveform parsing for TVLA capture."""

from __future__ import annotations

import socket
import time

import numpy as np


DEFAULT_PORT = 3000
ADC_CODES_PER_DIVISION = 25.0


def parse_waveform_header(text: str) -> dict[str, str]:
    """Parse the semicolon-delimited header before a GDS waveform block."""
    values: dict[str, str] = {}
    for field in text.strip().strip(";").split(";"):
        field = field.strip()
        if not field or field.lower() == "waveform data":
            continue
        if "," not in field:
            raise ValueError(f"malformed GDS waveform header field: {field!r}")
        key, value = field.split(",", 1)
        values[key.strip()] = value.strip()
    return values


def waveform_metadata(header: dict[str, str]) -> dict[str, float | int | str]:
    """Convert the GDS header fields used by capture and TVLA analysis."""
    required = (
        "Memory Length",
        "Trigger Address",
        "Vertical Scale",
        "Vertical Position",
        "Sampling Period",
        "Source",
    )
    missing = [key for key in required if key not in header]
    if missing:
        raise ValueError(f"GDS waveform header is missing: {', '.join(missing)}")

    sample_count = int(header["Memory Length"])
    trigger_address = int(header["Trigger Address"])
    sample_interval_s = float(header["Sampling Period"])
    vertical_scale_v = float(header["Vertical Scale"])
    vertical_position_v = float(header["Vertical Position"])
    return {
        "sample_count": sample_count,
        "trigger_address": trigger_address,
        "vertical_scale_v": vertical_scale_v,
        "vertical_position_v": vertical_position_v,
        # GW Instek documents 25 ADC codes per vertical division. The raw
        # values already include the channel-position displacement.
        "vertical_gain_v": vertical_scale_v / ADC_CODES_PER_DIVISION,
        "vertical_offset_v": 0.0,
        "sample_interval_s": sample_interval_s,
        "first_sample_time_s": -trigger_address * sample_interval_s,
        "source": header["Source"],
        "trigger_level_v": float(header.get("Trigger Level", "nan")),
        "firmware": header.get("Firmware", ""),
        "acquisition_time": header.get("Time", ""),
    }


class GDS3154Scope:
    """Minimal raw-socket SCPI client for the GDS-3000 waveform protocol."""

    def __init__(self, host: str, port: int = DEFAULT_PORT) -> None:
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
        self.sock = socket.create_connection((self.host, self.port), timeout=10.0)
        self.sock.settimeout(15.0)

    def close(self) -> None:
        self.sock.close()

    def write(self, command: str) -> None:
        self.sock.sendall(command.encode("ascii") + b"\n")

    def _recv_exact(self, count: int) -> bytes:
        data = bytearray()
        while len(data) < count:
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise ConnectionError(
                    f"scope closed after {len(data)}/{count} response bytes"
                )
            data.extend(chunk)
        return bytes(data)

    def read_line(self) -> bytes:
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = self.sock.recv(1)
            if not chunk:
                raise ConnectionError("scope closed the connection")
            data.extend(chunk)
        return bytes(data)

    def query(self, command: str) -> str:
        for attempt in range(1, 4):
            try:
                self.write(command)
                while True:
                    response = self.read_line().decode(
                        "ascii", errors="replace"
                    ).strip()
                    if response:
                        return response
            except (ConnectionError, OSError, TimeoutError) as exc:
                if attempt == 3:
                    raise
                print(
                    f"GDS query failed for {command!r} ({exc}); reconnecting "
                    f"and retrying {attempt + 1}/3"
                )
                time.sleep(0.25)
                self.reconnect()
        raise RuntimeError("GDS query retry loop ended unexpectedly")

    def acquisition_ready(self, channel: int = 1) -> bool:
        response = self.query(f":ACQUIRE{channel}:STATE?")
        if response not in {"0", "1"}:
            raise RuntimeError(f"unexpected acquisition state: {response!r}")
        return response == "1"

    def arm_single(self, channel: int = 1, timeout_s: float = 5.0) -> None:
        for attempt in range(1, 4):
            try:
                self.write(":SINGLE")
                deadline = time.monotonic() + timeout_s
                while time.monotonic() < deadline:
                    if not self.acquisition_ready(channel):
                        return
                    time.sleep(0.05)
                raise TimeoutError(
                    "GDS-3154 did not remain armed; check that trigger mode is "
                    "Normal and the trigger input is low before the firmware "
                    "handshake"
                )
            except (ConnectionError, OSError) as exc:
                if attempt == 3:
                    raise
                print(
                    f"GDS arm status failed ({exc}); reconnecting and retrying "
                    f"{attempt + 1}/3"
                )
                for reconnect_attempt in range(1, 11):
                    try:
                        self.reconnect()
                        break
                    except OSError as reconnect_exc:
                        if reconnect_attempt == 10:
                            raise
                        print(
                            f"GDS reconnect unavailable ({reconnect_exc}); "
                            f"retrying {reconnect_attempt + 1}/10"
                        )
                        time.sleep(1.0)
        raise TimeoutError(
            "GDS-3154 arm retry loop ended unexpectedly"
        )

    def wait_complete(self, channel: int = 1, timeout_s: float = 10.0) -> None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.acquisition_ready(channel):
                return
            time.sleep(0.02)
        raise TimeoutError("GDS-3154 did not complete its single acquisition")

    def _query_waveform_once(
        self, channel: int
    ) -> tuple[np.ndarray, dict[str, float | int | str], dict[str, str]]:
        self.write(f":ACQUIRE{channel}:MEMORY?")

        prefix = bytearray()
        while not prefix.endswith(b"#"):
            chunk = self.sock.recv(1)
            if not chunk:
                raise ConnectionError("scope closed during waveform header")
            prefix.extend(chunk)
            if len(prefix) > 4096:
                raise ValueError("GDS waveform header exceeded 4096 bytes")

        digit_byte = self._recv_exact(1)
        if not digit_byte.isdigit():
            raise ValueError(f"invalid IEEE block digit count: {digit_byte!r}")
        digit_count = int(digit_byte)
        if digit_count < 1:
            raise ValueError("indefinite-length waveform blocks are unsupported")
        length_text = self._recv_exact(digit_count)
        if not length_text.isdigit():
            raise ValueError(f"invalid IEEE block length: {length_text!r}")
        payload_length = int(length_text)
        payload = self._recv_exact(payload_length)

        # The scope terminates the block with LF. Consume it so the next
        # line-oriented query begins on a clean stream.
        terminator = self._recv_exact(1)
        if terminator == b"\r":
            terminator += self._recv_exact(1)
        if not terminator.endswith(b"\n"):
            raise ValueError(f"unexpected waveform terminator: {terminator!r}")

        header_text = prefix[:-1].decode("ascii", errors="strict")
        header = parse_waveform_header(header_text)
        metadata = waveform_metadata(header)
        expected_bytes = int(metadata["sample_count"]) * 2
        if payload_length != expected_bytes:
            raise ValueError(
                f"GDS waveform has {payload_length} bytes; expected {expected_bytes}"
            )

        # GDS waveform points are signed, big-endian 16-bit values.
        samples = np.frombuffer(payload, dtype=">i2").astype(np.int16)
        return samples, metadata, header

    def query_waveform(
        self, channel: int, attempts: int = 3
    ) -> tuple[np.ndarray, dict[str, float | int | str], dict[str, str]]:
        """Download a waveform, reconnecting after a broken TCP stream."""
        for attempt in range(1, attempts + 1):
            try:
                return self._query_waveform_once(channel)
            except (ConnectionError, OSError, TimeoutError, ValueError) as exc:
                if attempt == attempts:
                    raise
                print(
                    f"GDS waveform transfer failed ({exc}); reconnecting "
                    f"and retrying {attempt + 1}/{attempts}"
                )
                for reconnect_attempt in range(1, 11):
                    try:
                        self.reconnect()
                        break
                    except OSError as reconnect_exc:
                        if reconnect_attempt == 10:
                            raise
                        print(
                            f"GDS reconnect unavailable ({reconnect_exc}); "
                            f"retrying {reconnect_attempt + 1}/10"
                        )
                        time.sleep(1.0)
        raise RuntimeError("unreachable")

    def configure_tvla_trigger(self) -> None:
        """Configure the TVLA frontend except the manual edge-trigger level."""
        for command in (
            ":ACQUIRE:MODE SAMPLE",
            ":CHANNEL1:DISPLAY ON",
            ":CHANNEL1:COUPLING DC",
            ":CHANNEL1:SCALE 0.02",
            ":CHANNEL1:POSITION 0",
            ":CHANNEL2:DISPLAY ON",
            ":CHANNEL2:COUPLING DC",
            ":CHANNEL2:SCALE 1",
            ":CHANNEL2:POSITION 0",
            ":TIMEBASE:SCALE 2E-4",
            # At 10 MSa/s the 25k-point record spans 2.5 ms.  Put the
            # trigger 0.25 ms from the start so the ~1.22 ms masked
            # multiplication fits with comfortable post-trigger margin.
            ":TIMEBASE:POSITION 0.001",
            ":TRIGGER:TYPE EDGE",
            ":TRIGGER:SOURCE CH2",
            ":TRIGGER:COUPLE DC",
            ":TRIGGER:MODE NORMAL",
            ":TRIGGER:EDGE:SLOP RISE",
        ):
            self.write(command)

    def settings(self) -> dict[str, str | int]:
        return {
            "identity": self.query("*IDN?"),
            "acquisition_mode": self.query(":ACQUIRE:MODE?"),
            "time_div": self.query(":TIMEBASE:SCALE?"),
            "time_position": self.query(":TIMEBASE:POSITION?"),
            "memory_points": 25_000,
            "ch1_display": self.query(":CHANNEL1:DISPLAY?"),
            "ch1_scale": self.query(":CHANNEL1:SCALE?"),
            "ch1_position": self.query(":CHANNEL1:POSITION?"),
            "ch2_display": self.query(":CHANNEL2:DISPLAY?"),
            "ch2_scale": self.query(":CHANNEL2:SCALE?"),
            "ch2_position": self.query(":CHANNEL2:POSITION?"),
            "trigger_type": self.query(":TRIGGER:TYPE?"),
            "trigger_source": self.query(":TRIGGER:SOURCE?"),
            "trigger_coupling": self.query(":TRIGGER:COUPLE?"),
            "trigger_mode": self.query(":TRIGGER:MODE?"),
            "trigger_slope": self.query(":TRIGGER:EDGE:SLOP?"),
        }
