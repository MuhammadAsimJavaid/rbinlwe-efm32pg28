"""Synchronized fixed-vs-random TVLA collector for BK and GDS scopes."""

from __future__ import annotations

import argparse
import json
import csv
import re
import time
from pathlib import Path

import numpy as np
import serial
from serial.tools import list_ports

from tvla_capture_one import Scope as BKScope, descriptor_values
from tvla_scope_gds3154 import GDS3154Scope


GDS_DEFAULT_SCOPE_IP = "172.16.5.195"
BK_DEFAULT_SCOPE_IP = "10.11.13.220"
SERIAL_BAUD = 115200
BK_DEFAULT_FIRST_POINT = 16000
BK_DEFAULT_EXPECTED_SAMPLE_RATE = 20_000_000.0
TESTS = {
    "masked-mul": {
        "firmware_name": "tvla_mul_one_coeff_masked",
        "firmware_trials": 100_000,
        "point_count": 40_000,
    },
    "unmasked-mul": {
        "firmware_name": "tvla_mul_one_coeff_unmasked",
        "firmware_trials": 10_000,
        "point_count": 40_000,
    },
    "masked-decode": {
        "firmware_name": "tvla_decode_one_coeff_masked",
        "firmware_trials": 100_000,
        "point_count": 10_000,
    },
    "unmasked-decode": {
        "firmware_name": "tvla_decode_one_coeff_unmasked",
        "firmware_trials": 10_000,
        "point_count": 10_000,
    },
    "masked-cca2-decrypt": {
        "firmware_name": "cca2_pke_decrypt",
        "firmware_trials": 5_000,
        "point_count": 10_000,
    },
    "unmasked-cca2-decrypt": {
        "firmware_name": "cca2_pke_decrypt",
        "firmware_trials": 5_000,
        "point_count": 10_000,
    },
    "cca2-encrypt": {
        "firmware_name": "cca2_pke_encrypt",
        "firmware_trials": 5_000,
        "point_count": 25_000,
    },
}
RESULT_RE = re.compile(r"^([01]),(\d+)$")


def serial_line(port: serial.Serial, deadline: float) -> str:
    while time.monotonic() < deadline:
        line = (port.readline().decode("ascii", errors="replace")
            .replace("\x00", "").strip())
        if line:
            return line
    raise TimeoutError("timed out waiting for firmware VCOM output")


def wait_bk_scope_stop(scope: BKScope, timeout_s: float = 10.0) -> None:
    deadline = time.monotonic() + timeout_s
    last_state = ""
    while time.monotonic() < deadline:
        last_state = scope.query("SAST?").strip().lower()
        state = last_state.rsplit(" ", 1)[-1].replace("’", "'")
        if state in {"stop", "trig'd", "triggered"}:
            return
        time.sleep(0.02)
    raise TimeoutError(
        "scope did not complete its single acquisition; "
        f"last SAST response was {last_state!r}"
    )


def rising_edge_index(
    block: bytes | np.ndarray, expected_index: int | None = None
) -> int:
    if isinstance(block, bytes):
        samples = np.frombuffer(block, dtype=np.int8).astype(np.float32)
    else:
        samples = np.asarray(block, dtype=np.float32)
    # PD12 can be high for far less than 5% of a decoder record, so 5th/95th
    # percentiles collapse onto the low level and turn noise into false edges.
    # The trigger is a digital signal with a large ADC-code swing; extreme
    # percentiles retain short pulses while rejecting isolated extrema.
    low, high = np.percentile(samples, [0.1, 99.9])
    if high - low < 10.0:
        return -1
    threshold = (low + high) / 2.0
    edges = np.flatnonzero((samples[:-1] <= threshold)
                           & (samples[1:] > threshold)) + 1
    if len(edges) == 0:
        return -1
    if expected_index is not None:
        return int(edges[np.argmin(np.abs(edges - expected_index))])
    # Prefer the largest positive transition if ringing produces more than
    # one threshold crossing. This avoids relying on an old fixed edge index.
    steps = samples[edges] - samples[edges - 1]
    return int(edges[np.argmax(steps)])


def parse_sample_rate(text: str) -> float:
    match = re.search(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?", text)
    if not match:
        raise RuntimeError(f"could not parse scope sample rate: {text!r}")
    return float(match.group(0))


def arm_bk_scope(scope: BKScope, timeout_s: float = 10.0) -> None:
    scope.write("TRMD SINGLE")
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        state = scope.query("SAST?").rsplit(" ", 1)[-1].lower()
        if state in {"arm", "ready"}:
            return
        time.sleep(0.02)
    raise TimeoutError("scope did not reach Ready after Single arm")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", required=True, choices=sorted(TESTS),
                        help="firmware variant currently flashed")
    parser.add_argument("--traces", type=int,
                        help="traces to capture (default: full firmware set)")
    parser.add_argument("--point-count", type=int,
                        help="samples saved per trace (default depends on test)")
    parser.add_argument("--scope-type", choices=("gds3154", "bk2569b"),
                        default="gds3154",
                        help="oscilloscope backend (default: gds3154)")
    parser.add_argument("--scope-ip",
                        help="scope address (backend default if omitted)")
    parser.add_argument("--scope-port", type=int,
                        help="SCPI socket port (default: 3000 for GDS, 5025 for BK)")
    parser.add_argument("--memory-points", type=int,
                        choices=(10_000, 25_000, 100_000),
                        help="scope acquisition memory depth (backend default if omitted)")
    parser.add_argument("--first-point", type=int,
                        help="first scope memory point saved (backend default if omitted)")
    parser.add_argument("--serial-port",
                        help="board VCOM port (auto-detected if exactly one exists)")
    parser.add_argument("--expected-sample-rate", type=float,
                        help="required scope sample rate in Sa/s; the GDS rate "
                             "is read from the first waveform")
    parser.add_argument("--capture-trigger", action="store_true",
                        help="also save CH2 for pilot alignment validation")
    parser.add_argument("--configure-trigger", action="store_true",
                        help="program CH2 trigger settings through SCPI; by "
                             "default all front-panel settings are preserved")
    parser.add_argument("--arm-settle-ms", type=float, default=50.0,
                        help="delay after scope reports Ready before sending G "
                             "(default: 50 ms)")
    parser.add_argument("--progress-every", type=int, default=100,
                        help="print progress every N traces (default: 100)")
    args = parser.parse_args()
    is_gds = args.scope_type == "gds3154"
    scope_ip = args.scope_ip or (
        GDS_DEFAULT_SCOPE_IP if is_gds else BK_DEFAULT_SCOPE_IP
    )
    if args.serial_port:
        serial_port = args.serial_port
    else:
        port_info = list(list_ports.comports())
        jlink_ports = sorted(
            port.device for port in port_info
            if "jlink cdc" in port.description.lower()
        )
        detected_ports = sorted(port.device for port in port_info)
        if len(jlink_ports) == 1:
            serial_port = jlink_ports[0]
        elif len(detected_ports) == 1:
            serial_port = detected_ports[0]
        else:
            parser.error(
                "--serial-port is required unless one J-Link CDC port or "
                "exactly one COM port exists; "
                f"detected {detected_ports}"
            )
    scope_port = args.scope_port or (3000 if is_gds else 5025)
    memory_points = args.memory_points or (25_000 if is_gds else 100_000)
    first_point = args.first_point
    if first_point is None:
        first_point = 0 if is_gds else BK_DEFAULT_FIRST_POINT
    expected_sample_rate = args.expected_sample_rate
    if expected_sample_rate is None and not is_gds:
        expected_sample_rate = BK_DEFAULT_EXPECTED_SAMPLE_RATE
    test = TESTS[args.test]
    firmware_trials = int(test["firmware_trials"])
    traces_to_capture = (firmware_trials if args.traces is None
                         else args.traces)
    if args.point_count is None:
        point_count = (
            memory_points - first_point
            if is_gds else int(test["point_count"])
        )
    else:
        point_count = args.point_count
    if not 1 <= traces_to_capture <= firmware_trials:
        parser.error(f"--traces must be between 1 and {firmware_trials}")
    if point_count < 1_000 or first_point < 0:
        parser.error("--point-count must be >= 1000 and --first-point >= 0")
    if not 0.0 <= args.arm_settle_ms <= 5_000.0:
        parser.error("--arm-settle-ms must be between 0 and 5000")
    if first_point + point_count > memory_points:
        parser.error("first point plus point count must fit in scope memory")
    if args.progress_every < 1:
        parser.error("--progress-every must be positive")
    start_re = re.compile(
        rf"^# start,{re.escape(str(test['firmware_name']))},(\d+)$"
    )

    root = Path(__file__).resolve().parent.parent
    stamp = time.strftime("%Y%m%d_%H%M%S")
    output = root / "tvla_results" / f"{args.test}_{stamp}"
    output.mkdir(parents=True)

    trace_dtype = np.int16 if is_gds else np.int8
    trace_filename = "traces_int16.npy" if is_gds else "traces_int8.npy"
    trigger_filename = (
        "trigger_ch2_int16.npy" if is_gds else "trigger_ch2_int8.npy"
    )
    traces = np.lib.format.open_memmap(
        output / trace_filename,
        mode="w+",
        dtype=trace_dtype,
        shape=(traces_to_capture, point_count),
    )
    trigger_traces = None
    if args.capture_trigger:
        trigger_traces = np.lib.format.open_memmap(
            output / trigger_filename,
            mode="w+",
            dtype=trace_dtype,
            shape=(traces_to_capture, point_count),
        )
    labels = np.empty(traces_to_capture, dtype=np.uint8)
    cycles = np.empty(traces_to_capture, dtype=np.uint32)
    trigger_edges = np.empty(traces_to_capture, dtype=np.uint32)
    log_handle = (output / "capture_log.csv").open(
        "w", newline="", encoding="utf-8", buffering=1
    )
    log = csv.writer(log_handle)
    log.writerow(["trace_index", "class", "cycles", "samples",
                  "trigger_edge_index", "status"])

    with serial.Serial(serial_port, SERIAL_BAUD, timeout=0.2,
                       write_timeout=10.0) as uart:
        uart.reset_input_buffer()
        uart.reset_output_buffer()
        scope = (
            GDS3154Scope(scope_ip, scope_port)
            if is_gds else BKScope(scope_ip, scope_port)
        )
        try:
            if is_gds:
                settings = scope.settings()
                if "GDS-3154" not in str(settings["identity"]):
                    raise RuntimeError(
                        f"expected GDS-3154, received {settings['identity']!r}"
                    )
                if memory_points != 25_000:
                    raise RuntimeError("GDS-3154 acquisition memory is 25000 points")
                if str(settings["acquisition_mode"]).upper() != "SAMPLE":
                    raise RuntimeError(
                        "set GDS acquisition mode to Sample before TVLA capture"
                    )
            else:
                settings = {
                    "identity": scope.query("*IDN?"),
                    "time_div": scope.query("TDIV?"),
                    "memory": scope.query("MSIZ?"),
                    "sample_rate": scope.query("SARA?"),
                    "trigger": scope.query("TRSE?"),
                }

            settings.update({
                "test": args.test,
                "firmware_name": test["firmware_name"],
                "firmware_trials": firmware_trials,
                "serial_port": serial_port,
                "serial_baud": SERIAL_BAUD,
                "scope_type": args.scope_type,
                "scope_ip": scope_ip,
                "scope_port": scope_port,
                "first_point": first_point,
                "point_count": point_count,
                "memory_points": memory_points,
                "trace_file": trace_filename,
                "raw_dtype": np.dtype(trace_dtype).name,
            })

            if is_gds:
                if expected_sample_rate is not None:
                    settings["expected_sample_rate_hz"] = expected_sample_rate
                if args.configure_trigger:
                    scope.configure_tvla_trigger()
                    # Record the settings actually used, not the values that
                    # were present before automated configuration.
                    settings.update(scope.settings())
                settings["configured_trigger"] = scope.query(":TRIGGER:SOURCE?")
                settings["configured_trigger_mode"] = scope.query(
                    ":TRIGGER:MODE?"
                )
                settings["configured_trigger_slope"] = scope.query(
                    ":TRIGGER:EDGE:SLOP?"
                )
                settings["configured_trigger_level"] = "from waveform header"
                settings["ch2_display"] = scope.query(":CHANNEL2:DISPLAY?")
                if (
                    settings["configured_trigger"] != "CH2"
                    or settings["configured_trigger_mode"] != "NORMAL"
                    or settings["configured_trigger_slope"] != "RISE"
                    or settings["ch2_display"] != "ON"
                ):
                    raise RuntimeError(
                        "GDS TVLA trigger must be CH2, Normal, rising edge, "
                        "with CH2 displayed; configure the front panel or use "
                        "--configure-trigger"
                    )
            else:
                expected_memory_text = f"{memory_points // 1000}k"
                if expected_memory_text not in str(settings["memory"]).lower():
                    raise RuntimeError(
                        f"scope must be set to {expected_memory_text} points; "
                        f"reported {settings['memory']!r}"
                    )
                measured_sample_rate = parse_sample_rate(str(settings["sample_rate"]))
                if not (
                    0.98 * float(expected_sample_rate) <= measured_sample_rate
                    <= 1.02 * float(expected_sample_rate)
                ):
                    raise RuntimeError(
                        f"scope must be at {expected_sample_rate:g} Sa/s; "
                        f"reported {settings['sample_rate']!r}"
                    )
                settings["expected_sample_rate_hz"] = expected_sample_rate
                settings["parsed_sample_rate_hz"] = measured_sample_rate
                scope.write(f"WFSU SP,1,NP,{point_count},FP,{first_point}")
                if args.configure_trigger:
                    scope.write("C2:TRA ON")
                    scope.write("C2:VDIV 1V")
                    scope.write("C2:TRCP DC")
                    scope.write("C2:TRSL POS")
                    scope.write("C2:TRLV 1.5V")
                    scope.write("TRSE EDGE,SR,C2,HT,OFF")
                    scope.write("TRDL 0S")
                settings["configured_trigger"] = scope.query("TRSE?")
                settings["configured_trigger_slope"] = scope.query("C2:TRSL?")
                settings["configured_trigger_level"] = scope.query("C2:TRLV?")
            print("Scope trigger:", settings["configured_trigger"])
            if is_gds:
                print("Trigger mode:", settings["configured_trigger_mode"])
            print("CH2 slope:", settings["configured_trigger_slope"])
            print("CH2 level:", settings["configured_trigger_level"])
            input("Collector ready. Press RESET on PG28, then press Enter... ")

            start = serial_line(uart, time.monotonic() + 90.0)
            match = start_re.match(start)
            if not match:
                raise RuntimeError(f"unexpected firmware header: {start!r}")
            if int(match.group(1)) != firmware_trials:
                raise RuntimeError(f"firmware trial count mismatch: {start}")
            print(start)

            descriptor = None
            previous_block = None
            collection_started = time.monotonic()
            for index in range(traces_to_capture):
                if is_gds:
                    scope.arm_single()
                else:
                    arm_bk_scope(scope)
                time.sleep(args.arm_settle_ms / 1000.0)
                uart.write(b"G")
                uart.flush()

                result = serial_line(uart, time.monotonic() + 30.0)
                while result == start:
                    result = serial_line(uart, time.monotonic() + 30.0)
                parsed = RESULT_RE.match(result)
                if not parsed:
                    raise RuntimeError(f"bad result line at trace {index}: {result!r}")
                labels[index] = int(parsed.group(1))
                cycles[index] = int(parsed.group(2))
                if is_gds:
                    # ACQUIRE<n>:STATE? returns 1 only when that channel's raw
                    # acquisition memory is ready.  Reading before this point
                    # can return the previous waveform.
                    scope.wait_complete(1)
                    if trigger_traces is not None:
                        scope.wait_complete(2)
                    full_block, current_descriptor, waveform_header = (
                        scope.query_waveform(1)
                    )
                    block = full_block[first_point : first_point + point_count]
                    if descriptor is None:
                        descriptor = current_descriptor
                        descriptor["transfer_sample_count"] = point_count
                        descriptor["transfer_first_time_s"] = (
                            float(descriptor["first_sample_time_s"])
                            + first_point * float(descriptor["sample_interval_s"])
                        )
                        descriptor["gds_waveform_header"] = waveform_header
                        measured_sample_rate = (
                            1.0 / float(descriptor["sample_interval_s"])
                        )
                        settings["parsed_sample_rate_hz"] = measured_sample_rate
                        settings["configured_trigger_level"] = descriptor[
                            "trigger_level_v"
                        ]
                        if expected_sample_rate is not None and not (
                            0.98 * expected_sample_rate <= measured_sample_rate
                            <= 1.02 * expected_sample_rate
                        ):
                            raise RuntimeError(
                                f"scope must be at {expected_sample_rate:g} Sa/s; "
                                f"waveform reports {measured_sample_rate:g} Sa/s"
                            )
                    elif (
                        float(current_descriptor["sample_interval_s"])
                        != float(descriptor["sample_interval_s"])
                    ):
                        raise RuntimeError(
                            "GDS sample interval changed during capture: "
                            f"{descriptor['sample_interval_s']} -> "
                            f"{current_descriptor['sample_interval_s']}"
                        )
                else:
                    wait_bk_scope_stop(scope)
                    if descriptor is None:
                        descriptor = descriptor_values(
                            scope.query_block("C1:WF? DESC")
                        )
                        descriptor["transfer_sample_count"] = point_count
                        descriptor["transfer_first_time_s"] = (
                            float(descriptor["first_sample_time_s"])
                            + first_point * float(descriptor["sample_interval_s"])
                        )
                    raw_block = scope.query_block("C1:WF? DAT2")
                    block = np.frombuffer(raw_block, dtype=np.int8)
                if len(block) != point_count:
                    raise RuntimeError(
                        f"trace {index}: received {len(block)}, expected {point_count} samples"
                    )
                if previous_block is not None and np.array_equal(
                    block, previous_block
                ):
                    raise RuntimeError(
                        f"trace {index}: waveform is byte-identical to the "
                        "previous acquisition; refusing possible stale GDS data"
                    )
                traces[index] = block
                previous_block = np.asarray(block).copy()
                if trigger_traces is not None:
                    if is_gds:
                        trigger_full, _, _ = scope.query_waveform(2)
                        trigger_block = trigger_full[
                            first_point : first_point + point_count
                        ]
                    else:
                        trigger_raw = scope.query_block("C2:WF? DAT2")
                        trigger_block = np.frombuffer(trigger_raw, dtype=np.int8)
                    if len(trigger_block) != point_count:
                        raise RuntimeError(
                            f"trace {index}: CH2 received {len(trigger_block)}, "
                            f"expected {point_count} samples"
                        )
                    trigger_traces[index] = trigger_block
                    expected_trigger = None
                    if is_gds:
                        expected_trigger = int(descriptor["trigger_address"]) - first_point
                    edge = rising_edge_index(trigger_block, expected_trigger)
                    status_text = "ok"
                    if edge < 0:
                        trigger_edges[index] = np.iinfo(np.uint32).max
                        status_text = "invalid_missing_trigger_edge"
                    else:
                        trigger_edges[index] = edge
                else:
                    # Hardware triggering aligns every bulk trace. Avoid a
                    # second waveform transfer solely to rediscover the fixed
                    # trigger index; pilots with --capture-trigger validate it.
                    interval = float(descriptor["sample_interval_s"])
                    first_time = float(descriptor["transfer_first_time_s"])
                    expected_edge = int(round(-first_time / interval))
                    trigger_edges[index] = max(
                        0, min(point_count - 1, expected_edge)
                    )
                    status_text = "ok_hardware_aligned"
                log.writerow([index, int(labels[index]), int(cycles[index]),
                              len(block), int(trigger_edges[index]), status_text])
                if (index + 1) % 100 == 0:
                    traces.flush()
                    if trigger_traces is not None:
                        trigger_traces.flush()
                    np.save(output / "labels_partial.npy", labels[: index + 1])
                    np.save(output / "cycles_partial.npy", cycles[: index + 1])
                    np.save(output / "trigger_edges_partial.npy",
                            trigger_edges[: index + 1])
                completed = index + 1
                elapsed = time.monotonic() - collection_started
                rate = completed / elapsed
                eta = (traces_to_capture - completed) / rate
                if (completed == 1 or completed == traces_to_capture
                        or completed % args.progress_every == 0):
                    print(
                        f"{completed:6d}/{traces_to_capture}: "
                        f"class={labels[index]} cycles={cycles[index]} "
                        f"rate={rate:.2f} traces/s "
                        f"ETA={eta / 60.0:.1f} min status={status_text}"
                    )

            traces.flush()
            if trigger_traces is not None:
                trigger_traces.flush()
            np.save(output / "labels.npy", labels)
            np.save(output / "cycles.npy", cycles)
            np.save(output / "trigger_edges.npy", trigger_edges)
            metadata = {
                **settings,
                **(descriptor or {}),
                "captured_traces": traces_to_capture,
                "class_0": int((labels == 0).sum()),
                "class_1": int((labels == 1).sum()),
                "invalid_trigger_traces": int(
                    (trigger_edges == np.iinfo(np.uint32).max).sum()
                ),
                "tvla_threshold": 4.5,
                "raw_trace_bytes": int(traces.nbytes),
                "trigger_channel_saved": trigger_traces is not None,
                "trigger_validation_points": (
                    point_count if trigger_traces is not None else 0
                ),
            }
            (output / "metadata.json").write_text(
                json.dumps(metadata, indent=2), encoding="utf-8"
            )
            print(json.dumps(metadata, indent=2))
            print(f"saved: {output}")
        finally:
            scope.close()
            traces.flush()
            if trigger_traces is not None:
                trigger_traces.flush()
            log_handle.close()


if __name__ == "__main__":
    main()
