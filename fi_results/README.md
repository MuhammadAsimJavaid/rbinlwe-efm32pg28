# Fault-injection results

Curated debugger-assisted logical-fault summaries for the manuscript.

## Authoritative instruction-skip campaign (post-selector fix)

| Field | Value |
|---|---|
| Summary | `skip/skip_36_summary.md` |
| Sites | 12 × 3 = 36 trials |
| Selector sites S07 / S08 | No silent corruption after duplicated store / completion checks |
| Paper use | Fault-campaign table and revised skip narrative |

Target addresses for the skip suite are in the top-level
`FI_SKIP_TARGETS.csv` (regenerated for the post-fix ELF).

## Pre-fix skip archive (not the manuscript endpoint)

| Field | Value |
|---|---|
| Summary | `skip_pre_fix_archive/skip_36_summary.md` |
| Status | Historical; documents the residual S07/S08 silent-corruption gap that motivated the selector hardening |

## Other curated campaigns

- `baseline_*_summary.md`: valid/invalid baselines
- `random/random_2612_summary.md`: randomized substitution campaign

Raw GDB transcripts and ELF hashes used during live campaigns are omitted
from the public release; regenerate them from the checked-in firmware and
`FAULT_INJECTION_TEST_PLAN.md`.
