#!/usr/bin/env python3
"""Capture linked memory footprint in the RBLWE campaign column style."""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
from pathlib import Path

FIELDS = [
    "scheme",
    "implementation",
    "text_bytes",
    "data_bytes",
    "bss_bytes",
    "stack_bytes",
    "heap_reservation_bytes",
    "flash_bytes",
    "ram_without_heap_bytes",
    "linked_ram_bytes",
    "elf_file_bytes",
]


def discover_size() -> str:
    found = shutil.which("arm-none-eabi-size")
    if found:
        return found
    raise SystemExit("arm-none-eabi-size not found on PATH")


def profile(elf: Path, size_tool: str) -> dict[str, int | str]:
    completed = subprocess.run(
        [size_tool, str(elf)],
        check=False,
        capture_output=True,
        text=True,
    )
    sections = subprocess.run(
        [size_tool, "-A", str(elf)],
        check=False,
        capture_output=True,
        text=True,
    )
    raw = completed.stdout + "\n--- sections ---\n" + sections.stdout
    if completed.returncode or sections.returncode:
        raise SystemExit(f"arm-none-eabi-size failed:\n{raw}")

    lines = [line.split() for line in completed.stdout.splitlines() if line.strip()]
    text_bytes, data_bytes, _ = map(int, lines[-1][:3])

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

    return {
        "scheme": "ML-KEM-512",
        "implementation": "PQClean-clean",
        "text_bytes": text_bytes,
        "data_bytes": data_bytes,
        "bss_bytes": section_sizes.get(".bss", 0),
        "stack_bytes": section_sizes.get(".stack", 0),
        "heap_reservation_bytes": section_sizes.get(".memory_manager_heap", 0),
        "flash_bytes": text_bytes + data_bytes,
        "ram_without_heap_bytes": ram_without_heap_bytes,
        "linked_ram_bytes": linked_ram_bytes,
        "elf_file_bytes": elf.stat().st_size,
        "_raw": raw,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    row = profile(args.elf, discover_size())
    raw = row.pop("_raw")
    (args.out_dir / "size_raw.txt").write_text(raw, encoding="utf-8")
    with (args.out_dir / "memory_footprint.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerow(row)
    print(f"wrote {args.out_dir / 'memory_footprint.csv'}")


if __name__ == "__main__":
    main()
