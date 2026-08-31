#!/usr/bin/env python3
"""Run an FI GDB command file through standalone J-Link and Arm GDB."""

import argparse
import csv
import hashlib
import shutil
import subprocess
import sys
import time
from pathlib import Path


def discover_tool(executable):
    """Use PATH when possible and retain a clear missing-tool name."""
    located = shutil.which(executable)
    return Path(located) if located else Path(executable)


DEFAULT_GDB = discover_tool("arm-none-eabi-gdb")
DEFAULT_SERVER = discover_tool("JLinkGDBServerCL")
DEFAULT_DEVICE = "EFM32PG28BxxxF1024"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def wait_for_server(log_path, process, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                "J-Link server exited before accepting a connection")
        try:
            text = log_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        if "Listening on TCP/IP port" in text:
            return
        time.sleep(0.1)
    raise TimeoutError("J-Link server did not become ready")


def write_launcher(path, port, campaign):
    commands = [
        "set pagination off",
        "set confirm off",
        "set breakpoint pending on",
        "target remote 127.0.0.1:{}".format(port),
        "monitor reset",
        "load",
        "tbreak fi_trial_ready",
        "continue",
        "source {}".format(campaign.resolve().as_posix()),
        "detach",
        "quit",
    ]
    path.write_text("\n".join(commands) + "\n", encoding="ascii")


def extract_tagged_csv(gdb_log, output_csv):
    header = None
    rows = []
    completed = False
    with gdb_log.open(encoding="utf-8", errors="replace") as source:
        for line in source:
            line = line.strip()
            if line.startswith("FIHEADER,"):
                header = line[len("FIHEADER,"):]
            elif line.startswith("FIROW,"):
                rows.append(line[len("FIROW,"):])
            elif line.startswith("FICOMPLETE,"):
                completed = True

    if header is None:
        raise RuntimeError("GDB log contains no FIHEADER record")
    if not rows:
        raise RuntimeError("GDB log contains no FIROW records")
    if not completed:
        raise RuntimeError("campaign did not emit FICOMPLETE")

    fields = next(csv.reader([header]))
    parsed = [next(csv.reader([row])) for row in rows]
    for index, row in enumerate(parsed):
        if len(row) != len(fields):
            raise RuntimeError(
                "result row {} has {} fields; expected {}".format(
                    index, len(row), len(fields)))

    with output_csv.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.writer(destination)
        writer.writerow(fields)
        writer.writerows(parsed)
    return len(parsed)


def terminate(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--gdb", type=Path, default=DEFAULT_GDB)
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--device", default=DEFAULT_DEVICE)
    parser.add_argument("--serial", help="J-Link serial number")
    parser.add_argument("--port", type=int, default=50000)
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--server-timeout", type=float, default=15.0)
    args = parser.parse_args()

    for label, path in (
            ("ELF", args.elf), ("campaign", args.campaign),
            ("GDB", args.gdb), ("J-Link server", args.server)):
        if not path.is_file():
            parser.error("{} not found: {}".format(label, path))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    launcher = args.output_dir / (args.name + "_launcher.gdb")
    server_log = args.output_dir / (args.name + "_jlink.log")
    gdb_log = args.output_dir / (args.name + "_gdb.log")
    csv_path = args.output_dir / (args.name + ".csv")
    metadata = args.output_dir / (args.name + "_metadata.txt")
    write_launcher(launcher, args.port, args.campaign)

    server_command = [
        str(args.server),
        "-singlerun", "-nogui", "-if", "swd",
        "-speed", "4000",
        "-port", str(args.port),
        "-swoport", str(args.port + 1),
        "-telnetport", str(args.port + 2),
        "-device", args.device,
    ]
    if args.serial:
        server_command.extend(["-select", "usb={}".format(args.serial)])

    server_process = None
    started = time.time()
    try:
        with server_log.open("w", encoding="utf-8") as server_output:
            server_process = subprocess.Popen(
                server_command,
                stdout=server_output,
                stderr=subprocess.STDOUT,
                text=True,
            )
            wait_for_server(server_log, server_process, args.server_timeout)

            gdb_command = [
                str(args.gdb), "--batch",
                "-x", str(launcher),
                str(args.elf),
            ]
            with gdb_log.open("w", encoding="utf-8") as gdb_output:
                try:
                    completed = subprocess.run(
                        gdb_command,
                        stdout=gdb_output,
                        stderr=subprocess.STDOUT,
                        text=True,
                        timeout=args.timeout,
                        check=False,
                    )
                except subprocess.TimeoutExpired as error:
                    raise TimeoutError(
                        "GDB campaign exceeded {:.1f} seconds".format(
                            args.timeout)) from error
            if completed.returncode != 0:
                raise RuntimeError(
                    "GDB exited with status {}; inspect {}".format(
                        completed.returncode, gdb_log))
    finally:
        terminate(server_process)

    row_count = extract_tagged_csv(gdb_log, csv_path)
    metadata.write_text(
        "\n".join([
            "name={}".format(args.name),
            "timestamp_unix={}".format(int(started)),
            "elapsed_seconds={:.3f}".format(time.time() - started),
            "elf={}".format(args.elf.resolve()),
            "elf_sha256={}".format(sha256(args.elf)),
            "campaign={}".format(args.campaign.resolve()),
            "campaign_sha256={}".format(sha256(args.campaign)),
            "device={}".format(args.device),
            "jlink_serial={}".format(args.serial or "auto"),
            "gdb={}".format(args.gdb.resolve()),
            "server={}".format(args.server.resolve()),
            "rows={}".format(row_count),
        ]) + "\n",
        encoding="utf-8",
    )
    print("Campaign complete: {} rows".format(row_count))
    print("CSV: {}".format(csv_path))
    print("GDB log: {}".format(gdb_log))
    print("J-Link log: {}".format(server_log))
    print("Metadata: {}".format(metadata))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("ERROR: {}".format(error), file=sys.stderr)
        raise
