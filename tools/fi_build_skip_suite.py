#!/usr/bin/env python3
"""Build the focused 36-trial single-instruction-skip campaign."""

import csv
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "FI_SKIP_TARGETS.csv"
GENERATOR = ROOT / "tools" / "fi_generate_gdb.py"
ELF = ROOT / "cmake_gcc" / "build" / "base" / \
    "RBLWE_on_EFM32_ShAd_CBD0.out"
SUITE = ROOT / "fi_results" / "skip"
TRIALS_PER_SITE = 3


def main():
    if not ELF.exists():
        raise SystemExit(f"ELF not found: {ELF}")

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
        numeric_id = int(target["fault_id"][1:])
        script = SUITE / (fault_id + ".gdb")
        output_dir = SUITE / fault_id
        subprocess.run([
            sys.executable, str(GENERATOR),
            "--model", "skip",
            "--operation", target["operation"],
            "--trials", str(TRIALS_PER_SITE),
            "--fault-id", str(numeric_id),
            "--at", target["address"],
            "--next", target["next"],
            "--script", str(script),
        ], check=True)

        name = f"{fault_id}_{TRIALS_PER_SITE}"
        powershell.extend([
            "",
            f"Write-Host 'Running {target['fault_id']}: "
            f"{target['instruction']}'",
            "python tools/fi_batch_runner.py `",
            f"  --elf '{ELF.relative_to(ROOT).as_posix()}' `",
            f"  --campaign '{script.relative_to(ROOT).as_posix()}' `",
            f"  --output-dir '{output_dir.relative_to(ROOT).as_posix()}' `",
            f"  --name '{name}' `",
            "  --timeout 900",
        ])

    total = len(targets) * TRIALS_PER_SITE
    powershell.extend([
        "",
        f"Write-Host 'Instruction-skip suite complete: {total} trials.'",
    ])
    launcher = SUITE / "run_skip_suite.ps1"
    launcher.write_text("\n".join(powershell) + "\n", encoding="utf-8")
    print(f"Built {len(targets)} sites containing {total} trials")
    print(f"Launcher: {launcher}")


if __name__ == "__main__":
    main()
