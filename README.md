# Fault- and Side-Channel-Aware RBin-LWE on EFM32PG28

This repository contains research firmware and reproducibility tooling for an
RBin-LWE public-key encryption implementation on a constrained embedded
platform. The implementation includes selectable parameter sets, a CCA2 PKE
wrapper, branch-free polynomial operations, first-order masked decryption
components, benchmark instrumentation, logical fault-injection harnesses, and
timing/power leakage evaluation support.

This is research code, not a production cryptographic library. The included
measurements characterize specific builds and experimental setups; they are
not a general proof of side-channel or fault resistance.

## Target platform

- Silicon Labs **BRD2506A** development board
- **EFM32PG28B310F1024IM68** microcontroller
- Arm **Cortex-M33**, hard-float build
- Silicon Labs Simplicity SDK **2025.12.2**
- GCC Arm Embedded toolchain **12.2.1** in the recorded project configuration

The generated project configuration is in
`RBLWE_on_EFM32_ShAd_CBD0.slcp`; pin assignments are recorded in the
corresponding `.pintool` file.

## Repository structure

| Path | Contents |
|---|---|
| `*.c`, `*.h`, `masked_gadgets.S` | Firmware, cryptographic implementation, instrumentation, and FI harness |
| `autogen/`, `config/` | Generated Silicon Labs sources, linker script, configuration headers, and SDK SBOM |
| `cmake_gcc/` | GCC/CMake build definition and toolchain configuration |
| `tools/` | Benchmark, dudect-style analysis, TVLA capture/analysis, and logical FI automation |
| `benchmark_results/` | Curated cycle and memory-footprint summaries |
| `fi_results/` | Curated fault-injection summaries |
| `tvla_results/` | Curated statistical summaries and plots; raw oscilloscope traces are intentionally omitted |
| `PARAMETER_SETS.md` | Parameter-set and compile-time option reference |
| `BENCHMARK_CAMPAIGN.md` | Benchmark methodology and output schema |
| `CONSTANT_TIME_ANALYSIS.md` | Timing-test methodology and evaluated claims |
| `FAULT_INJECTION_TEST_PLAN.md` | Fault models, classifications, and acceptance criteria |

## Prerequisites

The checked-in configuration records the following toolchain. Equivalent
newer versions may work but have not been used for the included results.

- Simplicity Studio 6 with Simplicity SDK 2025.12.2
- CMake 3.25 or newer (the recorded installation used 3.30.2)
- Ninja with the `Ninja Multi-Config` generator
- GCC Arm Embedded 12.2.1 (`arm-none-eabi-*`)
- Simplicity Commander for flashing
- J-Link GDB Server and Arm GDB for debugger-assisted FI campaigns
- Python 3 for the experiment tooling

Install the Python analysis dependencies in an isolated environment:

```text
python -m venv .venv
python -m pip install -r requirements-analysis.txt
```

The GDB automation module `tools/fi_gdb.py` is loaded by Arm GDB and imports
GDB's built-in Python module; it is not an installable PyPI dependency.

## Build

The CMake project references the installed Silicon Labs SDK rather than
vendoring it. Set `SIMPLICITY_SDK_DIR` to the SDK root. Set `ARM_GCC_DIR` to
the Arm toolchain root and `NINJA_EXE_PATH` to the Ninja executable when the
Silicon Labs `slt` helper is not available on `PATH`.

PowerShell example:

```powershell
$env:SIMPLICITY_SDK_DIR = 'C:\path\to\simplicity_sdk_2025.12.2'
$env:ARM_GCC_DIR = 'C:\path\to\gcc-arm-none-eabi'
$env:NINJA_EXE_PATH = 'C:\path\to\ninja.exe'

Set-Location cmake_gcc
cmake --preset project
cmake --build --preset default_config
```

The default CMake configuration builds parameter set `3` (`N=512`, `q=256`),
paper-compatible CBD0, the CCA2 wrapper, fully masked decryption, and timing
target `5` (the selectable CPA/CCA2 round-trip benchmark). Select another
documented configuration during the configure step, for example:

```text
cmake --preset project -DRLWE_PARAMETER_SET=1 -DTIMING_TEST_TARGET=3
cmake --build --preset default_config
```

Build products are written under `cmake_gcc/build/base/`, including `.out`,
`.hex`, `.bin`, and `.s37` files. They are deliberately excluded from Git.

## Flash and run

With the board connected, flash the generated image using the project-recorded
Commander workflow (omit `--device` if Commander detects it unambiguously):

```text
commander flash cmake_gcc/build/base/RBLWE_on_EFM32_ShAd_CBD0.hex --halt --device EFM32PG28B310F1024IM68
```

The firmware communicates through the board's VCOM USART at 115200 baud. The
exact UART records depend on `TIMING_TEST_TARGET`; their formats and class
definitions are documented in `CONSTANT_TIME_ANALYSIS.md` and implemented in
`timing_test.c`.

For a complete build/flash/UART benchmark campaign, ensure CMake, Arm GDB,
`arm-none-eabi-size`, and J-Link GDB Server are on `PATH`, then run:

```text
python tools/benchmark_campaign.py --campaign combined --trials 10 --serial-port <PORT>
```

The script accepts `--cmake`, `--gdb`, `--size`, `--server`, and
`--probe-serial` when automatic discovery is unsuitable. Run any experiment
script with `--help` before collection to inspect its required hardware and
capture arguments.

For logical fault injection, configure with `-DFI_TEST_ENABLE=1`, rebuild the
exact ELF, select sites from that ELF's disassembly, and follow
`FAULT_INJECTION_TEST_PLAN.md`. The scripts deliberately do not embed a probe
serial number or claim that a source-level site maps to a stable address.

## Reproducibility artifacts

- `benchmark_results/20260824_223723/` contains the curated combined-campaign
  cycle summary and linked-memory footprint table.
- `fi_results/` contains compact baseline, instruction-skip, and randomized
  campaign summaries. Target manifests are the top-level `FI_*_TARGETS.csv`
  files.
- `tvla_results/` contains selected first- and second-order summary JSON files
  and plots, including both threshold-passing and threshold-exceeding results.
  Paths, device identifiers, raw traces, partial captures, and capture logs
  have been excluded from the public release.

Raw UART logs, oscilloscope arrays, debugger transcripts, ELF/map files, and
generated build trees are not versioned. The checked-in firmware and tools are
the inputs for regenerating them on the target setup.

## Citation

If you use this implementation or the accompanying experimental artifacts in academic work, please cite the associated paper:

```bibtex
@article{javaid2026efficient,
  author  = {Muhammad Asim Javaid and Muhammad Adeel Pasha and Muhammad Ali Siddiqi},
  title   = {Efficient and Implementation-Hardened RBLWE on Commodity Cortex-M Microcontrollers},
  journal = {arXiv preprint arXiv:2610.07820},
  year    = {2026},
  doi     = {10.48550/arXiv.2610.07820},
  url     = {https://arxiv.org/abs/2610.07820}
}
```

The corresponding implementation and reproducibility artifacts are available at:

```text
https://github.com/MuhammadAsimJavaid/rbinlwe-efm32pg28
```

For reproducibility, please also record the repository commit or release used in your experiments.

## License

Copyright (c) 2026 Muhammad Asim Javaid, Muhammad Adeel Pasha, and Muhammad Ali Siddiqi.

A software license for the authors' original source code will be specified separately.

This repository also contains generated configuration and support files associated with the Silicon Labs development environment. Such third-party or vendor-generated material remains subject to its respective copyright and licensing terms and is not relicensed by the authors of this repository.

The accompanying research paper is distributed separately through arXiv and is subject to the license stated on its arXiv record.

