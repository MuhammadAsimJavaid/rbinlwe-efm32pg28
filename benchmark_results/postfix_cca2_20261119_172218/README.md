# Post-FI Set-1 CCA2 cycle/memory refresh (2026-11-19)

Authoritative **Set 1** (`N=256`, `q=256`) measurements after the hardened
selector update (duplicated output store, read-back, and completion checks).

| Stage folder | Paper role |
|---|---|
| `N256_Q256_cca2_unmasked/` | BF + CCA2/fault protection |
| `N256_Q256_cca2_masked/` | BF + CCA2/fault + shared CPA decryption |

Each folder contains `results.csv` (10 trials) and `memory.csv`.

Cold-start decrypt averages used in the revised manuscript:

| Stage | Decrypt cold avg (cycles) | Flash (B) |
|---|---:|---:|
| `cca2_unmasked` | 871329 | 29076 |
| `cca2_masked` | 3973512 | 30256 |

The earlier combined campaign under `../20260824_223723/` remains as the
full multi-set acceleration/security matrix captured before this selector
refresh. Prefer this folder for Set-1 CCA2 numbers that match the revised
paper tables.
