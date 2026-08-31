#!/usr/bin/env python3
"""Build the selected zeroing campaigns and a PowerShell suite launcher."""

import csv
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "FI_ZERO_TARGETS.csv"
GENERATOR = ROOT / "tools" / "fi_generate_gdb.py"
RUNNER = ROOT / "tools" / "fi_batch_runner.py"
ELF = ROOT / "cmake_gcc" / "build" / "base" / \
    "RBLWE_on_EFM32_ShAd_CBD0.out"
SUITE = ROOT / "fi_results" / "zero"


def main():
    SUITE.mkdir(parents=True, exist_ok=True)
    powershell = [
        "$ErrorActionPreference = 'Stop'",
        "$projectRoot = Resolve-Path "
        f"'{ROOT.as_posix()}'",
        "Set-Location $projectRoot",
    ]

    with MANIFEST.open(newline="", encoding="utf-8") as source:
        targets = list(csv.DictReader(source))

    for target in targets:
        fault_id = target["fault_id"].lower()
        script = SUITE / (fault_id + ".gdb")
        output_dir = SUITE / fault_id
        command = [
            sys.executable, str(GENERATOR),
            "--model", "zero",
            "--operation", target["operation"],
            "--trials", "3",
            "--fault-id", str(int(target["fault_id"][1:])),
            "--at", target["address"],
            "--size", target["size"],
            "--script", str(script),
        ]
        if target["target"].lower() in {
                "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
                "r8", "r9", "r10", "r11", "r12"}:
            command.extend(["--register", target["target"]])
        else:
            command.extend(["--target", target["target"]])
        subprocess.run(command, check=True)

        powershell.extend([
            "",
            f"Write-Host 'Running {target['fault_id']}: "
            f"{target['security_question']}'",
            "python tools/fi_batch_runner.py `",
            f"  --elf '{ELF.relative_to(ROOT).as_posix()}' `",
            f"  --campaign '{script.relative_to(ROOT).as_posix()}' `",
            f"  --output-dir '{output_dir.relative_to(ROOT).as_posix()}' `",
            f"  --name '{fault_id}_3' `",
            "  --timeout 900",
        ])

    launcher = SUITE / "run_zero_suite.ps1"
    launcher.write_text("\n".join(powershell) + "\n", encoding="utf-8")
    print("Built {} zeroing campaigns".format(len(targets)))
    print("Launcher: {}".format(launcher))


if __name__ == "__main__":
    main()
