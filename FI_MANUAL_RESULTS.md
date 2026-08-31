# Manual Logical Fault-Injection Results

These results are debugger-assisted, on-target logical fault injections on the
EFM32PG28. Addresses apply only to the corresponding evaluated ELF and must be
accompanied by its SHA-256 hash in the final thesis evidence package.

## M-ZERO-01: zero final validity mask

| Field | Value |
|---|---|
| Operation | Valid CCA2-PKE decryption |
| Fault model | Data zeroing |
| Function | `rlwe_cca2_pke_decrypt` |
| Injection address | `0x0800435c` |
| Target | `r4`, live final-selection validity mask |
| Original value | `0xFF` |
| Injected value | `0x00` |
| API status | `0` |
| Output | Same-trial implicit-rejection message |
| Candidate release | None observed |
| Classification | `SAFE_REJECTION` |
| Verdict | Fail-closed/protected under this tested fault |

The test-only `fi_result.valid_mask` snapshot remained `0xFF` because it was
captured immediately before injection. The live `r4` value used by selection
was changed to zero.

## M-RAND-01: randomize final validity mask

| Field | Value |
|---|---|
| Operation | Invalid CCA2-PKE decryption |
| Fault model | Data randomization |
| Function | `rlwe_cca2_pke_decrypt` |
| Injection address | `0x0800435c` |
| Target | `r4`, live final-selection validity mask |
| Original value | `0x00` |
| Injected value | `0xA7` (`10100111` binary) |
| API status | `0` |
| Candidate/rejection differing bits | `135` |
| Candidate bits released | `83` |
| Rejection bits retained | `52` |
| Accounting check | `83 + 52 = 135` |
| Expected proportions | approximately `5/8` candidate and `3/8` rejection |
| Observed proportions | `61.48%` candidate and `38.52%` rejection |
| Classification | `AUTH_BYPASS` (partial unauthenticated release) |
| Verdict | Vulnerable to this tested precise data-corruption fault |

`0xA7` contains five one-bits and three zero-bits. At positions where candidate
and rejection differ, the mask selects candidate bits in the five enabled bit
planes and rejection bits in the other three. Exact counts need not equal
`5/8` and `3/8` because the 135 differing bits are not guaranteed to be evenly
distributed among bit positions.

## M-FORCE-01: force invalid ciphertext onto valid-output path

| Field | Value |
|---|---|
| Operation | Invalid CCA2-PKE decryption |
| Fault model | Directed data corruption |
| Function | `rlwe_cca2_pke_decrypt` |
| Injection address | `0x0800435c` |
| Target | `r4`, live final-selection validity mask |
| Original value | `0x00` |
| Injected value | `0xFF` |
| API status | `0` |
| Output equals candidate | Yes |
| Output equals original plaintext | Yes |
| Output equals rejection | No |
| Classification | `AUTH_BYPASS` (full unauthenticated release) |
| Verdict | Vulnerable to this tested precise data-corruption fault |

The invalid test vector differs from its valid counterpart only in `c4`.
Consequently, its unauthenticated candidate still equals the original
plaintext. Both `output_is_candidate=1` and `output_is_correct=1` are therefore
expected; the bypass is established by releasing that candidate despite the
invalid authentication tag.

## M-SKIP-01: skip final-selection mask instruction

| Field | Value |
|---|---|
| Operation | Invalid CCA2-PKE decryption |
| Fault model | Single instruction skip |
| Function | `rlwe_cca2_pke_decrypt` |
| Injection address | `0x0800436e` |
| Skipped instruction | `ands r3, r4` |
| Resume address | `0x08004370` |
| Validity mask | `r4 = 0x00` |
| API status | `0` |
| Candidate/rejection differing bits | `124` |
| Candidate bits released | `6` |
| Rejection bits retained | `118` |
| Accounting check | `6 + 118 = 124` |
| Classification | `AUTH_BYPASS` (partial unauthenticated release) |
| Verdict | Vulnerable to this tested single-instruction-skip fault |

The skipped instruction occurred only in the first iteration of the byte-wise
selection loop. Before the skip, `r3` contained
`candidate_byte XOR rejection_byte`; the skipped `ANDS` would normally clear
that difference because `r4=0`. The following XOR and store therefore emitted
the first candidate byte. Six bits of that byte differed from rejection and
were demonstrably released. The remaining loop iterations executed normally,
retaining 118 rejection-selected differing bits.
