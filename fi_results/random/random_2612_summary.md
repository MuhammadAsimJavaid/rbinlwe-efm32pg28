# Fault-injection result summary

## Detailed results

| Model | Operation | Fault ID | Target | Size | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| random | decrypt-invalid | 1 | r3 | 1 | 256 | 0 | 256 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| random | decrypt-invalid | 2 | $sp | 1 | 256 | 0 | 256 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| random | decrypt-invalid | 3 | r9 | 4 | 1000 | 0 | 1000 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| random | decrypt-invalid | 4 | r0 | 4 | 1000 | 0 | 1000 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |
| random | decrypt-valid | 5 | $sp+16 | 4 | 100 | 0 | 100 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |

## Aggregate results

| Fault type | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | 95% upper bound when no bypass/weak key |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| random | 2612 | 0 | 2612 | 0 | 0 | 0 | 0 | 0 | 0 | 0.115% |
