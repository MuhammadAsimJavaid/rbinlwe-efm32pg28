# ML-KEM-512 on EFM32PG28 (FIPS 203 / PQClean clean)

Same-platform companion benchmark used for the manuscript comparison against
RBLWE on Silicon Labs **EFM32PG28** (BRD2506A). Crypto core: PQClean
`crypto_kem/ml-kem-512/clean` (`CRYPTO_ALGNAME "ML-KEM-512"`).

This attachment is research benchmark firmware, not a production library.

## What it measures

- Correctness self-test: KeyGen → Encap → Decaps shared-secret match, plus
  altered-ciphertext implicit rejection check
- DWT cycle counts for KeyGen / Encap / Decaps
- Linked memory footprint via `tools/memory_profile.py`

Randomness uses PSA `psa_generate_random` (hardware TRNG). Timing regions run
with interrupts masked.

## Authoritative results (manuscript)

| Field | Value |
|---|---|
| Campaign | `benchmark_results/20261111_130800/` |
| Trials | 100 |
| KeyGen / Encap / Decap averages | 837415 / 960935 / 1169437 cycles |
| Flash (`text+data`) | 37604 B |

Do not substitute an older 10-trial campaign for the paper table.

## Build

```powershell
$env:SIMPLICITY_SDK_DIR = 'C:\path\to\simplicity_sdk'
$env:ARM_GCC_DIR = 'C:\path\to\gcc-arm-none-eabi'
$env:NINJA_EXE_PATH = 'C:\path\to\ninja.exe'
Set-Location cmake_gcc
cmake --preset project
cmake --build --preset default_config
```

Artifact: `cmake_gcc/build/base/MLKEM512_on_EFM32PG28.hex`

## Flash and run

```text
commander flash cmake_gcc/build/base/MLKEM512_on_EFM32PG28.hex --halt --device EFM32PG28B310F1024IM68
```

Open VCOM at 115200 8N1. Reset the board and capture UART until `DONE`.
