# TVLA result provenance

Curated first- and second-order TVLA summaries and plots. Raw oscilloscope
arrays and capture logs are omitted from this public release.

## Authoritative masked-decoding result (paper Table IV)

| Field | Value |
|---|---|
| Campaign directory | `masked-decode_20260830_121458/` |
| Valid traces | 5,000 |
| Reported max first-order \|t\| | 3.320 |
| Threshold | 4.5 (not exceeded) |
| Paper use | Table IV, masked decoding |

Published files for that campaign: `tvla_summary.json`, first-/second-order
plots, analysis `.npz` intermediates, and `metadata.json`.

## Archived exploratory masked-decoding result (not used in the paper)

| Field | Value |
|---|---|
| Campaign directory | `masked-decode_combined_10k_20260829/` |
| Valid traces | 10,000 (concatenation of two earlier 5,000-trace sessions; not one continuous paper capture) |
| Source sessions | `masked-decode_20260829_005337/`, `masked-decode_20260829_023714/` |
| Reported max first-order \|t\| | 8.845 |
| Status | Superseded / non-authoritative; retained as evidence of an earlier campaign |

Do not treat the combined 10k summary as the manuscript number. The paper
reports the later 5,000-trace campaign above. The larger combined peak arises
from late activity **outside** the manuscript post-trigger ROI used for Table
IV; it is not a different masked decoder and is not a recalculation of the
paper's 3.320 result.

## Other curated campaigns

- `comparison_5k/` and `*-mul_*`: multiplication TVLA used for the masked vs
  unmasked figure.
- `masked-cca2-decrypt_*`, `cca2-encrypt_*`: protected-operation summaries
  matching the remaining Table IV entries.
- `unmasked-*`: supporting unmasked campaigns.
