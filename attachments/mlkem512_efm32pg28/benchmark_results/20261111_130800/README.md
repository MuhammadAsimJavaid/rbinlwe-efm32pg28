# ML-KEM-512 100-trial cycle campaign (2026-11-11)

Platform: EFM32PG28 (BRD2506A), PQClean clean FIPS 203 ML-KEM-512
Method: DWT cycle counts, PSA hardware TRNG
Trials: 100

## Cycles

| Operation | min | avg |
|---|---:|---:|
| keygen | 836361 | 837415 |
| encap | 959860 | 960935 |
| decap | 1168378 | 1169437 |

## Linked memory (current `.out`)

| Metric | bytes |
|---|---:|
| flash (text+data) | 37604 |
| stack | 16384 |
| .bss | 6840 |
| RAM without heap | 23384 |

Source UART: `uart_capture.txt`; footprint: `memory_footprint.csv`
