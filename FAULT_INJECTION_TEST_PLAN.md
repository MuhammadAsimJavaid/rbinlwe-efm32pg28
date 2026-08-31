# CCA2-PKE Logical Fault-Injection Test Plan

## 1. Scope and claim

This campaign evaluates the active RBLWE CCA2-PKE firmware on the EFM32PG28
using debugger-assisted, on-target logical fault injection. It covers three
single-fault models:

1. instruction skipping;
2. zeroing a selected data object;
3. replacing a selected data object with a pseudorandom value.

The campaign does not emulate voltage, clock, electromagnetic, optical, or
other physical glitch effects. Results therefore support claims about the
tested logical fault models and tested binary only, not general physical
fault resistance.

The security property under test is fail-closed plaintext release:

> A fault affecting decryption of an invalid or corrupted ciphertext must not
> cause the unauthenticated candidate plaintext to be released.

Encryption and key-generation faults are also measured, but are reported
separately because the CCA2 decryption check cannot retroactively protect a
faulty key pair or a faulty ciphertext produced by the legitimate encryptor.

## 2. Tested configuration

Freeze and report the following before collecting results:

- board and MCU revision;
- compiler and linker versions;
- CMake cache values, including parameter set, error distribution, masked
  decoding, branch-free multiplication, and Secure Element oracle selection;
- optimization and debug flags;
- ELF and flash-image SHA-256 hashes;
- source revision;
- linked symbol addresses and disassembly used to select injection sites;
- debugger, probe, and debug-server versions.

Do not identify injection sites only by source line. The final campaign
manifest must identify each site by function, address, original instruction,
instruction width, and expected next address.

## 3. Dedicated firmware harness

Add a separate build-time fault-test mode rather than modifying the timing
capture loop. The harness should expose volatile global command and result
objects with stable ELF symbols.

Each trial follows this state machine:

1. debugger resets the target and halts at `fi_trial_ready`;
2. debugger writes a trial identifier, operation, vector identifier, and fault
   seed into the command object;
3. firmware prepares deterministic test inputs;
4. firmware reaches `fi_operation_start`;
5. the debugger arms one selected breakpoint;
6. the fault is injected exactly once;
7. firmware reaches `fi_operation_end`, HardFault, reset, or timeout;
8. debugger reads the result object and relevant output buffers;
9. the host classifier writes one result row.

Required result fields:

- magic value and harness version;
- trial and vector identifiers;
- operation and injection-site identifiers;
- start/end markers;
- API return value;
- output buffer;
- reset cause;
- HardFault registers when applicable;
- control-flow completion markers;
- compact checksums of relevant inputs and outputs.

Use deterministic vectors for reproducibility. Hardware randomness may be
tested in a separate campaign, but it must not prevent replay of a reported
fault.

## 4. Baseline and oracles

For every test vector, first collect a fault-free baseline.

### 4.1 Valid-ciphertext oracle

For a valid ciphertext:

- decryption returns the original message;
- the test records the fault-free output and return value.

### 4.2 Invalid-ciphertext oracle

Create invalid ciphertexts by controlled corruption of each component:
`c12.u`, `c12.v`, `c3`, and `c4`.

For each invalid ciphertext, independently compute or record:

- the unauthenticated candidate plaintext produced before final selection;
- the expected implicit-rejection message derived from `z` and `H(C)`;
- the final fault-free output.

The candidate plaintext must be available only in the test build. It must not
be added to the production API.

### 4.3 Key-generation checks

Because key generation is randomized, equality with a reference key is not a
valid oracle. Record:

- secret-key Hamming weight;
- all-zero and unusually sparse-secret indicators;
- consistency of `p = r1 - a*s` when deterministic sampling is enabled;
- encryption/decryption success over a fixed message set;
- whether the generated public and secret keys remain mutually consistent.

## 5. Fault model A: instruction skipping

At a hardware breakpoint immediately before the target instruction, set the
program counter to the decoded next-instruction address. Do not use `stepi` or
`nexti`, because they execute the instruction. Thumb instruction widths must
come from the tested ELF disassembly; never assume that the next address is
`pc + 2`.

Test one skipped instruction per reset/trial. Initial target groups are:

| Group | Active code region | Representative targets | Security question |
|---|---|---|---|
| SK-A | `rlwe_keypair` | sampling call, multiplication call, final subtraction/store | Can a structurally weak or inconsistent key be produced silently? |
| ENC-A | `encrypt_with_polys` | two multiplication calls and final additions | Can a faulty ciphertext be emitted without an encryption error? |
| MUL-A | `rlwe_poly_mul_binary` | mask creation, load, `UADD8`, `USUB8`, store, final reduction | Is the arithmetic fault caught by later CCA2 verification? |
| DEC-A | `rlwe_decrypt` and masked decoder | multiplication/addition calls and decoded-byte store | Does corrupted hidden `v` lead to rejection? |
| DERIVE-A | CCA2 seed/hash/coin derivation | calls and result stores | Does a derivation fault cause re-encryption mismatch? |
| REENC-A | deterministic re-encryption | call and output stores | Does corruption of `check_c12` cause rejection? |
| CMP-A | `pke_ct_equal_mask` | difference accumulation and zero-to-mask conversion | Can equality be forced for an invalid ciphertext? |
| RC-A | return-status handling | status accumulation and `rc != 0` handling | Can an oracle failure be treated as success? |
| SEL-A | `pke_select_msg` | valid-mask use and final output store | Can candidate plaintext bypass implicit rejection? |

CMP-A and SEL-A are the primary countermeasure-bypass campaigns. Arithmetic
campaigns alone are insufficient to evaluate fail-closed release.

## 6. Fault model B: zeroing

At a precise point of use, overwrite the complete selected object, one selected
word, or one selected byte with zero. These are separate submodels and must not
be pooled in one statistic.

Initial targets:

| Operation | Objects |
|---|---|
| Key generation | `sk->s`, selected secret coefficient, `r1`, multiplication result, `pk->p` |
| Encryption | ephemeral `r`, `e1`, `e2`, `hidden_v`, seeds, coins, `c3`, `c4` |
| Decryption | `hidden_v`, decoded byte, mask, recovered message, seeds, coins |
| Verification | `check_c12`, `check_c4`, comparator accumulator, `valid_mask`, `rc` |
| Rejection/selection | `z`, `hc`, rejection message, selected output byte |

For arrays, test at least the first, middle, and last byte or coefficient,
followed by positions sampled uniformly from the complete object.

## 7. Fault model C: randomization

Use the same injection points as zeroing, replacing the target with values from
a reproducible host-side cryptographic pseudorandom generator. Save the master
seed and per-trial generated value.

Exclude the original value when practical so that a trial always injects an
actual change. For masks with a small valid domain, include directed values:

- `0x00`;
- `0xFF`;
- one-hot values;
- one-cold values;
- uniformly random byte or word.

Randomization campaigns must report the number of trials per site and per
object width. Do not combine byte, word, register, and full-array corruption
into a single fault rate.

## 8. Outcome classification

Classify each trial into exactly one primary outcome:

1. `NO_EFFECT`: output and status equal the fault-free baseline.
2. `SAFE_REJECTION`: output equals the expected implicit-rejection message.
3. `EXPLICIT_ERROR`: an explicit nonzero status or test-only fault flag occurs.
4. `CRASH_RESET`: HardFault or unexpected reset.
5. `HANG_TIMEOUT`: the completion breakpoint is not reached in time.
6. `SILENT_CORRUPTION`: output differs from both correct and rejection outputs.
7. `AUTH_BYPASS`: an invalid ciphertext releases its unauthenticated candidate
   plaintext, in whole or in a defined significant part.
8. `WEAK_KEY`: a key-generation fault meets a predeclared weak-key criterion.

`CRASH_RESET` and `HANG_TIMEOUT` are denial-of-service outcomes, not successful
detection, unless an intentional fail-stop mechanism is implemented and
identified.

## 9. Campaign order

Run the campaign in stages:

1. build and validate the deterministic harness;
2. collect baselines for all valid and invalid vectors;
3. run directed zeroing at verification and selection sites;
4. run directed instruction skips at verification and selection sites;
5. run arithmetic and decoder instruction skips;
6. run zeroing across key generation, encryption, and decryption;
7. run seeded randomization campaigns;
8. reproduce every `SILENT_CORRUPTION`, `AUTH_BYPASS`, and `WEAK_KEY` result
   at least three times;
9. inspect the exact disassembly and debugger transcript for every dangerous
   result.

This order tests the highest-risk bypasses before spending time on broad
statistical campaigns.

## 10. Trial counts and reporting

For directed skips, execute every selected instruction site for every selected
vector at least once, then repeat dangerous and unstable outcomes.

For zeroing, cover every declared directed target and width. For randomized
replacement, begin with 1,000 trials per critical comparator/selection site and
100 trials per exploratory arithmetic site. Increase counts after measuring
runtime and observed outcome rates.

When zero dangerous outcomes are observed in `N` independent trials, report the
rough 95% upper confidence bound `3/N`; do not report the implementation as
proven fault-resistant.

The final results table should contain:

| Field | Meaning |
|---|---|
| build hash | exact tested binary |
| operation/vector | test context |
| fault model | skip, zero, or randomize |
| site/address | exact binary location |
| target/value | corrupted object and injected value |
| trials | number attempted |
| outcomes | count in every classification bucket |
| bypass rate | `AUTH_BYPASS / completed trials` |
| notes | resets, timeouts, reproductions, limitations |

## 11. Acceptance criteria

The primary CCA2 decryption campaign passes when:

- no tested invalid ciphertext produces `AUTH_BYPASS`;
- verification-intermediate faults lead to safe rejection, explicit fail-stop,
  or a separately reported denial of service;
- all dangerous anomalies are reproducible and explained or fixed;
- the result is stated only for the tested single-fault models and binary.

Key-generation and encryption results are reported independently. Any
repeatable weak-key generation or silently emitted malformed ciphertext is a
finding even if later CCA2 decryption rejects some consequences.
