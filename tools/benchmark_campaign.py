#!/usr/bin/env python3
"""Build, flash, collect, and summarize RLWE cycle/footprint campaigns."""

from __future__ import annotations

import argparse
import csv
import hashlib
import shutil
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

import serial


def discover_tool(executable: str) -> Path:
    """Use PATH when possible and retain a clear missing-tool name."""
    located = shutil.which(executable)
    return Path(located) if located else Path(executable)


DEFAULT_CMAKE = discover_tool("cmake")
DEFAULT_GDB = discover_tool("arm-none-eabi-gdb")
DEFAULT_SIZE = discover_tool("arm-none-eabi-size")
DEFAULT_SERVER = discover_tool("JLinkGDBServerCL")
DEFAULT_DEVICE = "EFM32PG28BxxxF1024"
DEFAULT_SERIAL_PORT = None
SERIAL_BAUD = 115200
RESULT_FIELDS = [
    "parameter_set", "stage", "operation", "mode", "trial",
    "random_bytes", "se_calls", "cycles", "status",
]
STATUS_VALIDATION_FAILURE = 16
STATUS_FATAL_MASK = 1 | 2 | 4 | 8
MEMORY_FIELDS = [
    "parameter_set", "stage", "text_bytes", "data_bytes", "bss_bytes",
    "stack_bytes", "heap_reservation_bytes", "flash_bytes",
    "ram_without_heap_bytes", "linked_ram_bytes", "elf_file_bytes",
]
PARAMETER_SETS = [
    (1, "N256_Q256"),
    (2, "N256_Q128"),
    (3, "N512_Q256"),
]


@dataclass(frozen=True)
class Stage:
    name: str
    branch_free: int
    cca2: int
    masked: int
    packed_arithmetic: int = 1
    packed_codec: int = 1
    buffered_trng: int = 1
    combined_benchmark: int = 0

    @property
    def expected_modes(self) -> tuple[str, ...]:
        return ("native", "trng_only", "end_to_end") \
            if self.combined_benchmark else (
            "buffered", "trng_only", "cold",
        )


ACCELERATION_STAGES = [
    Stage("scalar_baseline", 0, 0, 0, 0, 0, 0, 1),
    Stage("packed_arithmetic", 0, 0, 0, 1, 0, 0, 1),
    Stage("packed_codec", 0, 0, 0, 1, 1, 0, 1),
    Stage("cpa_baseline", 0, 0, 0, 1, 1, 1, 1),
]
SECURITY_STAGES = [
    Stage("cpa_baseline", 0, 0, 0),
    Stage("cpa_branch_free", 1, 0, 0),
    Stage("cca2_unmasked", 1, 1, 0),
    Stage("cca2_masked", 1, 1, 1),
]
COMBINED_STAGES = ACCELERATION_STAGES + [
    Stage("cpa_branch_free", 1, 0, 0, 1, 1, 1, 1),
    Stage("cca2_unmasked", 1, 1, 0, 1, 1, 1, 1),
    Stage("cca2_masked", 1, 1, 1, 1, 1, 1, 1),
]


def resolve_from_root(root: Path, path: Path) -> Path:
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def run_checked(command: list[str], *, cwd: Path, log: Path) -> None:
    with log.open("w", encoding="utf-8") as output:
        completed = subprocess.run(
            command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
            text=True,
        )
    if completed.returncode:
        raise RuntimeError(
            f"command failed ({completed.returncode}); see {log}: "
            + " ".join(command)
        )


def wait_for_server(log: Path, process: subprocess.Popen[str], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"J-Link server exited early; see {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        if "Listening on TCP/IP port" in text:
            return
        time.sleep(0.1)
    raise TimeoutError(f"J-Link server did not start; see {log}")


def flash_and_run(
    elf: Path,
    output: Path,
    gdb: Path,
    server: Path,
    device: str,
    probe_serial: str | None,
    gdb_port: int,
    server_timeout: float,
) -> None:
    output = output.resolve()
    elf = elf.resolve()
    gdb = gdb.resolve()
    server = server.resolve()
    server_log = output / "jlink_server.log"
    gdb_log = output / "flash_gdb.log"
    command_file = output / "flash.gdb"
    command_file.write_text(
        "\n".join([
            "set pagination off",
            "set confirm off",
            f"target remote 127.0.0.1:{gdb_port}",
            "monitor reset",
            "load",
            "monitor reset",
            "monitor go",
            "disconnect",
            "quit",
            "",
        ]),
        encoding="ascii",
    )

    server_command = [
        str(server), "-singlerun", "-nogui", "-if", "swd", "-speed", "4000",
        "-port", str(gdb_port), "-swoport", str(gdb_port + 1),
        "-telnetport", str(gdb_port + 2), "-device", device,
    ]
    if probe_serial:
        server_command.extend(["-select", f"usb={probe_serial}"])

    process = None
    try:
        with server_log.open("w", encoding="utf-8") as server_output:
            process = subprocess.Popen(
                server_command, stdout=server_output,
                stderr=subprocess.STDOUT, text=True,
            )
            wait_for_server(server_log, process, server_timeout)
            run_checked(
                [str(gdb), "--batch", "-x", str(command_file), str(elf)],
                cwd=output, log=gdb_log,
            )
            gdb_output = gdb_log.read_text(encoding="utf-8", errors="replace")
            if "Start address " not in gdb_output or "Transfer rate:" not in gdb_output:
                raise RuntimeError(
                    f"GDB did not report a successful firmware load; see {gdb_log}"
                )
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # The firmware was loaded and started.  Some J-Link versions
                # remain alive after GDB disconnects; finally terminates them.
                pass
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def capture_uart(
    uart: serial.Serial,
    expected_parameter: str,
    expected_stage: str,
    expected_modes: tuple[str, ...],
    trials: int,
    timeout: float,
) -> tuple[list[dict[str, int | str]], list[str]]:
    deadline = time.monotonic() + timeout
    raw_lines: list[str] = []
    rows: list[dict[str, int | str]] = []
    saw_start = False

    while time.monotonic() < deadline:
        text = uart.readline().decode("ascii", errors="replace").strip()
        if not text:
            continue
        raw_lines.append(text)
        if text.startswith("# start,trng_operation_cycles,"):
            fields = text.split(",")
            if len(fields) != 5:
                raise RuntimeError(f"malformed start record: {text}")
            if fields[2:] != [expected_parameter, expected_stage, str(trials)]:
                raise RuntimeError(
                    f"flashed configuration mismatch: received {text!r}"
                )
            saw_start = True
            continue
        if not saw_start:
            continue
        if text == "# end":
            break
        if text.startswith("#"):
            continue

        values = next(csv.reader([text]))
        if len(values) != len(RESULT_FIELDS):
            raise RuntimeError(f"malformed result row: {text}")
        row: dict[str, int | str] = dict(zip(RESULT_FIELDS, values))
        for field in ("trial", "random_bytes", "se_calls", "cycles", "status"):
            row[field] = int(row[field])
        if row["parameter_set"] != expected_parameter or row["stage"] != expected_stage:
            raise RuntimeError(f"result configuration mismatch: {text}")
        rows.append(row)
    else:
        raise TimeoutError(
            f"UART capture timed out for {expected_parameter}/{expected_stage}"
        )

    expected_rows = 3 * len(expected_modes) * trials
    if len(rows) != expected_rows:
        raise RuntimeError(f"received {len(rows)} result rows; expected {expected_rows}")
    return rows, raw_lines


def write_rows(path: Path, rows: list[dict[str, int | str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=RESULT_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def read_rows(path: Path) -> list[dict[str, int | str]]:
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != RESULT_FIELDS:
            raise RuntimeError(f"unexpected columns in {path}")
        rows: list[dict[str, int | str]] = []
        for source_row in reader:
            row: dict[str, int | str] = dict(source_row)
            for field in ("trial", "random_bytes", "se_calls", "cycles", "status"):
                row[field] = int(row[field])
            rows.append(row)
    return rows


def validate_rows(
    rows: list[dict[str, int | str]],
    expected_parameter: str,
    expected_stage: str,
    expected_modes: tuple[str, ...],
    trials: int,
) -> tuple[list[dict[str, int | str]], list[dict[str, int | str]]]:
    expected_rows = 3 * len(expected_modes) * trials
    if len(rows) != expected_rows:
        raise RuntimeError(
            f"received {len(rows)} result rows for "
            f"{expected_parameter}/{expected_stage}; expected {expected_rows}"
        )
    mismatched = [
        row for row in rows
        if row["parameter_set"] != expected_parameter
        or row["stage"] != expected_stage
    ]
    if mismatched:
        raise RuntimeError(
            f"result configuration mismatch for {expected_parameter}/{expected_stage}"
        )
    actual_modes = {str(row["mode"]) for row in rows}
    if actual_modes != set(expected_modes):
        raise RuntimeError(
            f"unexpected modes for {expected_parameter}/{expected_stage}: "
            f"{sorted(actual_modes)}; expected {list(expected_modes)}"
        )
    fatal = [row for row in rows if int(row["status"]) & STATUS_FATAL_MASK]
    validation = [
        row for row in rows if int(row["status"]) & STATUS_VALIDATION_FAILURE
    ]
    return fatal, validation


def summarize(rows: list[dict[str, int | str]]) -> list[dict[str, int | str | float]]:
    groups: dict[tuple[str, str, str, str], list[dict[str, int | str]]] = {}
    for row in rows:
        key = (
            str(row["parameter_set"]), str(row["stage"]),
            str(row["operation"]), str(row["mode"]),
        )
        groups.setdefault(key, []).append(row)

    summary = []
    for key in sorted(groups):
        group = groups[key]
        cycles = [int(row["cycles"]) for row in group]
        summary.append({
            "parameter_set": key[0],
            "stage": key[1],
            "operation": key[2],
            "mode": key[3],
            "random_bytes": int(group[0]["random_bytes"]),
            "se_calls": int(group[0]["se_calls"]),
            "samples": len(cycles),
            "minimum_cycles": min(cycles),
            "maximum_cycles": max(cycles),
            "average_cycles": sum(cycles) / len(cycles),
            "median_cycles": statistics.median(cycles),
            "validation_failures": sum(
                1 for row in group
                if int(row["status"]) & STATUS_VALIDATION_FAILURE
            ),
            "fatal_failures": sum(
                1 for row in group if int(row["status"]) & STATUS_FATAL_MASK
            ),
        })
    return summary


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def measure_memory(
    size_tool: Path,
    elf: Path,
    parameter_name: str,
    stage_name: str,
    destination: Path,
) -> dict[str, int | str]:
    completed = subprocess.run(
        [str(size_tool), str(elf)], cwd=destination,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    sections = subprocess.run(
        [str(size_tool), "-A", str(elf)], cwd=destination,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    (destination / "size_raw.txt").write_text(
        completed.stdout + "\n--- sections ---\n" + sections.stdout,
        encoding="utf-8",
    )
    if completed.returncode or sections.returncode:
        raise RuntimeError(
            "arm-none-eabi-size failed; see "
            f"{destination / 'size_raw.txt'}"
        )
    lines = [line.split() for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) < 2 or len(lines[-1]) < 6:
        raise RuntimeError(f"could not parse memory footprint for {elf}")
    try:
        text_bytes, data_bytes, _linked_bss_bytes = map(int, lines[-1][:3])
    except ValueError as error:
        raise RuntimeError(f"could not parse memory footprint for {elf}") from error

    section_sizes: dict[str, int] = {}
    linked_ram_bytes = 0
    ram_without_heap_bytes = 0
    for line in sections.stdout.splitlines():
        fields = line.split()
        if len(fields) != 3:
            continue
        try:
            size = int(fields[1], 10)
            address = int(fields[2], 10)
        except ValueError:
            continue
        section_sizes[fields[0]] = size
        if 0x20000000 <= address < 0x40000000:
            linked_ram_bytes += size
            if fields[0] != ".memory_manager_heap":
                ram_without_heap_bytes += size

    bss_bytes = section_sizes.get(".bss", 0)
    stack_bytes = section_sizes.get(".stack", 0)
    heap_reservation_bytes = section_sizes.get(".memory_manager_heap", 0)
    if linked_ram_bytes == 0 or bss_bytes == 0:
        raise RuntimeError(f"could not identify RAM sections for {elf}")
    return {
        "parameter_set": parameter_name,
        "stage": stage_name,
        "text_bytes": text_bytes,
        "data_bytes": data_bytes,
        "bss_bytes": bss_bytes,
        "stack_bytes": stack_bytes,
        "heap_reservation_bytes": heap_reservation_bytes,
        "flash_bytes": text_bytes + data_bytes,
        "ram_without_heap_bytes": ram_without_heap_bytes,
        "linked_ram_bytes": linked_ram_bytes,
        "elf_file_bytes": elf.stat().st_size,
    }


def write_memory(path: Path, rows: list[dict[str, int | str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=MEMORY_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def read_memory(path: Path) -> dict[str, int | str]:
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != MEMORY_FIELDS:
            raise RuntimeError(f"unexpected columns in {path}")
        rows = list(reader)
    if len(rows) != 1:
        raise RuntimeError(f"expected one footprint row in {path}")
    row: dict[str, int | str] = dict(rows[0])
    for field in MEMORY_FIELDS[2:]:
        row[field] = int(row[field])
    return row


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    parser.add_argument("--trials", type=int, default=10)
    parser.add_argument("--cmake", type=Path, default=DEFAULT_CMAKE)
    parser.add_argument("--gdb", type=Path, default=DEFAULT_GDB)
    parser.add_argument("--size", type=Path, default=DEFAULT_SIZE)
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--device", default=DEFAULT_DEVICE)
    parser.add_argument("--probe-serial")
    parser.add_argument("--gdb-port", type=int, default=50000)
    parser.add_argument("--capture-timeout", type=float, default=600.0)
    parser.add_argument("--server-timeout", type=float, default=15.0)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument(
        "--campaign",
        choices=("combined", "acceleration", "security", "all"),
        default="combined",
        help="configuration matrix to run (default: combined; all is an alias)",
    )
    parser.add_argument(
        "--resume", action="store_true",
        help="reuse completed configurations in an existing --output-dir",
    )
    args = parser.parse_args()
    if args.trials < 2:
        parser.error("--trials must be at least 2")

    root = Path(__file__).resolve().parent.parent
    cmake_source = root / "cmake_gcc"
    build_dir = (
        resolve_from_root(root, args.build_dir)
        if args.build_dir is not None
        else cmake_source / "benchmark_build"
    )
    stamp = time.strftime("%Y%m%d_%H%M%S")
    output_root = (
        resolve_from_root(root, args.output_dir)
        if args.output_dir is not None
        else root / "benchmark_results" / stamp
    )
    if args.resume and args.output_dir is None:
        parser.error("--resume requires --output-dir")
    if not args.serial_port:
        parser.error("--serial-port is required (for example COM4 or /dev/ttyACM0)")
    output_root.mkdir(parents=True, exist_ok=args.resume)

    args.cmake = resolve_from_root(root, args.cmake)
    args.gdb = resolve_from_root(root, args.gdb)
    args.size = resolve_from_root(root, args.size)
    args.server = resolve_from_root(root, args.server)
    for label, path in (("CMake", args.cmake), ("GDB", args.gdb),
                        ("GNU size", args.size),
                        ("J-Link server", args.server)):
        if not path.is_file():
            parser.error(f"{label} not found: {path}")

    if args.campaign in ("combined", "all"):
        stages = COMBINED_STAGES
    elif args.campaign == "acceleration":
        stages = ACCELERATION_STAGES
    else:
        stages = SECURITY_STAGES

    all_rows: list[dict[str, int | str]] = []
    all_memory: list[dict[str, int | str]] = []
    with serial.Serial(args.serial_port, SERIAL_BAUD, timeout=0.2,
                       write_timeout=2.0) as uart:
        for parameter_value, parameter_name in PARAMETER_SETS:
            for stage in stages:
                name = f"{parameter_name}_{stage.name}"
                destination = output_root / name
                results_path = destination / "results.csv"
                memory_path = destination / "memory.csv"
                if (args.resume and results_path.is_file()
                        and memory_path.is_file()):
                    rows = read_rows(results_path)
                    fatal, validation = validate_rows(
                        rows, parameter_name, stage.name,
                        stage.expected_modes, args.trials,
                    )
                    if fatal:
                        raise RuntimeError(
                            f"[{name}] saved results contain {len(fatal)} fatal "
                            f"rows; remove {results_path} to rerun this configuration"
                        )
                    all_rows.extend(rows)
                    memory = read_memory(memory_path)
                    if (memory["parameter_set"] != parameter_name
                            or memory["stage"] != stage.name):
                        raise RuntimeError(
                            f"footprint configuration mismatch in {memory_path}"
                        )
                    all_memory.append(memory)
                    note = (
                        f", {len(validation)} expected correctness failures recorded"
                        if validation else ""
                    )
                    print(f"[{name}] reused ({len(rows)} rows{note})", flush=True)
                    continue
                destination.mkdir(exist_ok=args.resume)
                print(f"[{name}] configuring and building", flush=True)
                configure = [
                    str(args.cmake), "-S", str(cmake_source), "-B", str(build_dir),
                    "-G", "Ninja Multi-Config",
                    f"-DCMAKE_TOOLCHAIN_FILE={cmake_source / 'toolchain.cmake'}",
                    "-DCMAKE_CONFIGURATION_TYPES=base",
                    f"-DRLWE_PARAMETER_SET={parameter_value}",
                    "-DRLWE_ERROR_DISTRIBUTION=0",
                    f"-DRLWE_BRANCH_FREE_MUL_BINARY={stage.branch_free}",
                    f"-DRLWE_PACKED_ARITHMETIC={stage.packed_arithmetic}",
                    f"-DRLWE_PACKED_CODEC={stage.packed_codec}",
                    f"-DRLWE_BUFFERED_TRNG={stage.buffered_trng}",
                    "-DRLWE_COMBINED_BENCHMARK="
                    f"{stage.combined_benchmark}",
                    f"-DRLWE_CCA2_PKE_ENABLE={stage.cca2}",
                    f"-DRLWE_FULL_MASKED_DECRYPT_ENABLE={stage.masked}",
                    "-DTIMING_TEST_TARGET=10",
                    f"-DTIMING_TEST_TRIALS={args.trials}",
                    "-DFI_TEST_ENABLE=0",
                ]
                run_checked(configure, cwd=root, log=destination / "configure.log")
                run_checked(
                    [str(args.cmake), "--build", str(build_dir), "--config", "base",
                     "--target", "RBLWE_on_EFM32_ShAd_CBD0", "--parallel", "4"],
                    cwd=root, log=destination / "build.log",
                )
                elf = build_dir / "base" / "RBLWE_on_EFM32_ShAd_CBD0.out"
                if not elf.is_file():
                    raise RuntimeError(f"firmware was not produced: {elf}")
                (destination / "firmware_sha256.txt").write_text(
                    file_sha256(elf) + "\n", encoding="ascii"
                )
                memory = measure_memory(
                    args.size, elf, parameter_name, stage.name, destination,
                )
                write_memory(memory_path, [memory])
                all_memory.append(memory)
                map_file = elf.with_suffix(".map")
                if not map_file.is_file():
                    raise RuntimeError(f"linker map was not produced: {map_file}")
                shutil.copy2(map_file, destination / "firmware.map")

                print(f"[{name}] flashing and collecting", flush=True)
                uart.reset_input_buffer()
                uart.reset_output_buffer()
                flash_and_run(
                    elf, destination, args.gdb, args.server, args.device,
                    args.probe_serial, args.gdb_port, args.server_timeout,
                )
                rows, raw_lines = capture_uart(
                    uart, parameter_name, stage.name, stage.expected_modes,
                    args.trials,
                    args.capture_timeout,
                )
                (destination / "uart_raw.txt").write_text(
                    "\n".join(raw_lines) + "\n", encoding="ascii"
                )
                write_rows(destination / "results.csv", rows)
                fatal, validation = validate_rows(
                    rows, parameter_name, stage.name, stage.expected_modes,
                    args.trials,
                )
                if fatal:
                    raise RuntimeError(
                        f"[{name}] firmware reported {len(fatal)} fatal result "
                        f"rows; details saved in {destination / 'results.csv'}"
                    )
                all_rows.extend(rows)
                note = (
                    f", {len(validation)} expected correctness failures recorded"
                    if validation else ""
                )
                print(f"[{name}] complete ({len(rows)} rows{note})", flush=True)

    write_rows(output_root / "benchmark_all_raw.csv", all_rows)
    write_memory(output_root / "memory_footprint.csv", all_memory)
    summary = summarize(all_rows)
    summary_fields = list(summary[0])
    with (output_root / "benchmark_summary.csv").open(
            "w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=summary_fields)
        writer.writeheader()
        writer.writerows(summary)
    print(f"Campaign complete: {output_root}")


if __name__ == "__main__":
    main()
