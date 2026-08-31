# Fault-injection result summary

## Detailed results

| Model | Operation | Fault ID | Target | Size | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| none | decrypt-invalid | 0 |  | 1 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | protected in tested model |

## Aggregate results

| Fault type | Trials | No effect | Safe rejection | Explicit error | Crash/hang | Silent | Bypass | Weak key | Missed | 95% upper bound when no bypass/weak key |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| none | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 60.000% |
