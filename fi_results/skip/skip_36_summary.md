# Fault-injection result summary

## Detailed results

| Model | Operation | Fault ID | Target | Size | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| skip | decrypt-invalid | 1 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-invalid | 2 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-invalid | 3 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-invalid | 4 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-invalid | 5 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-invalid | 6 |  | 1 | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | decrypt-valid | 7 |  | 1 | 3 | 0 | 0 | 0 | 0 | 3 | 0 | 0 | 0 | functional vulnerability |
| skip | decrypt-valid | 8 |  | 1 | 3 | 0 | 0 | 0 | 0 | 3 | 0 | 0 | 0 | functional vulnerability |
| skip | encrypt | 10 |  | 1 | 3 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | encrypt | 9 |  | 1 | 3 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | keypair | 11 |  | 1 | 3 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| skip | keypair | 12 |  | 1 | 3 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | protected in tested model |

## Aggregate results

| Fault type | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | 95% upper bound when no bypass/weak key |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| skip | 36 | 0 | 18 | 12 | 0 | 6 | 0 | 0 | 0 | 8.333% |
