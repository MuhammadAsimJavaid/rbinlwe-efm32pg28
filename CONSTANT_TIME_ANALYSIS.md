# Constant-Time Analysis of the RBLWE Firmware

## 1. Purpose and scope

This document records the constant-time design review and dudect-style timing
tests for the RBLWE firmware targeting the Silicon Labs EFM32PG28 (Arm
Cortex-M33). It is intended to support the implementation and evaluation
chapters of a thesis.

The security objective is to prevent secret values, decoded messages, and
ciphertext-validity decisions from changing:

- executed control flow;
- loop iteration counts;
- memory addresses;
- instruction sequences; or
- externally observable execution time.

A constant-time code review and a timing test provide complementary evidence:

1. Source and assembly review identify obvious secret-dependent branches and
   memory accesses.
2. Welch's t-test detects timing distributions that differ between two input
   classes on the physical target.

Passing a timing test does not prove that an implementation is constant-time.
It means that no timing difference was detected under the tested configuration,
inputs, compiler, device, and measurement conditions.

## 2. Evaluated configuration

| Item | Evaluated setting |
|---|---|
| Target | EFM32PG28 |
| Processor | Arm Cortex-M33 |
| Execution model | Bare metal |
| Project security attribution | TrustZone-unaware application |
| Timing source | `DWT->CYCCNT` |
| UART | USART0 VCOM, 115200 baud, 8-N-1 |
| Current parameter-set cache | `RLWE_PARAMETER_SET=1` |
| Current ring | `N=256`, `q=256` |
| Binary multiplication | `RLWE_BRANCH_FREE_MUL_BINARY=1` |
| Default trial count | 100,000 |
| Leakage reference | `|t| = 4.5` |

Results in this document apply to the compiled configuration above. Tests must
be repeated after changing the compiler, optimization flags, parameter set,
security attribution, or relevant source code.

## 3. Threat model and class definitions

Timing tests should isolate one security question at a time.

### 3.1 Secret-multiplier dependence

- Class 0: fixed binary multiplier.
- Class 1: freshly generated random binary multiplier.
- The other multiplicand remains fixed.

This class definition applies to the ephemeral binary polynomial used during
encryption and to secret-key multiplication during key generation or
decryption.

### 3.2 Attacker-controlled ciphertext dependence

- Class 0: fixed ciphertext polynomial or complete fixed ciphertext.
- Class 1: freshly generated varying ciphertexts.
- The secret key remains fixed.

For CCA-secure interfaces, class 1 should include invalid and malformed
ciphertexts. A separate, especially important comparison is:

- Class 0: valid ciphertexts.
- Class 1: invalid ciphertexts produced by changing one or more authenticated
  ciphertext bytes.

This tests for a ciphertext-validity timing oracle.

### 3.3 Message dependence

- Class 0: fixed message, normally all zero.
- Class 1: freshly generated random message.
- Key, coins, buffers, and execution history are kept identical.

## 4. Measurement method

The firmware harness is implemented in `timing_test.c`.

### 4.1 DWT setup

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0u;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
__DSB();
__ISB();
```

The counter is checked before a capture starts. DWT access must be verified in
the same Secure or Non-secure state in which the target routine executes.

### 4.2 Balanced class schedule

The harness creates 50,000 labels for each class and shuffles them using
Fisher-Yates with rejection sampling. Labels are stored as a packed bit array.
The schedule is completed before any timing records are emitted.

### 4.3 Controlled operand preparation

The first version of the harness generated random data only for class 1 and
used different input buffers for the two classes. This introduced a
class-dependent pre-measurement history and different addresses. Those early
measurements are not admissible as evidence about the multiplication itself.

The corrected harness:

- generates fresh random data before **every** trial;
- performs the same preparation loop for both classes;
- selects fixed or random values using a mask rather than a branch;
- copies both classes into common `working_a` and `working_b` buffers;
- invokes the target with identical input and output addresses; and
- performs an untimed warm-up before the start marker.

Corrected selection snapshot:

```c
rlwe_coeff_t mask =
    (rlwe_coeff_t)(0u - (uint32_t)(class_label & 1u));

for (uint32_t i = 0u; i < RLWE_N; ++i) {
#if TIMING_TEST_TARGET == TIMING_TEST_ENCRYPT_MUL
    working_a[i] = fixed_a[i];
    working_b[i] =
        (rlwe_coeff_t)((fixed_binary_b[i] & (rlwe_coeff_t)~mask)
                     | (random_operand[i] & mask));
#else
    working_a[i] =
        (rlwe_coeff_t)((fixed_a[i] & (rlwe_coeff_t)~mask)
                     | (random_operand[i] & mask));
    working_b[i] = fixed_binary_b[i];
#endif
}
```

### 4.4 Timed interval

```c
uint32_t saved_primask = __get_PRIMASK();
__disable_irq();
__DSB();
__ISB();
__COMPILER_BARRIER();
uint32_t start = DWT->CYCCNT;
__COMPILER_BARRIER();

rlwe_poly_mul_binary(result, working_a, working_b);

__COMPILER_BARRIER();
uint32_t end = DWT->CYCCNT;
__COMPILER_BARRIER();
__set_PRIMASK(saved_primask);
```

Random generation, input selection, result formatting, and UART transmission
are outside the timed interval. Interrupts are disabled only around the target
call so that ISR latency does not contaminate an individual trial.

### 4.5 UART format

```text
# start,<function_name>,100000\r\n
<class>,<cycle_delta>\r\n
...
# end\r\n
```

Each CRLF-delimited line is independently parseable. The declared trial count
allows a missing record to be detected.

### 4.6 Offline analysis

`analyze.py`:

- parses ASCII, UTF-8, UTF-16, ordinary CRLF, and literal `<CR><LF>` logs;
- validates start/end framing and the expected record count;
- separates classes;
- optionally crops an equal percentage from both tails of each class;
- computes running Welch t-statistics; and
- plots `|t|` against samples per class.

Welch's statistic is:

```text
                  mean_0 - mean_1
t = ------------------------------------------------
    sqrt(variance_0 / n_0 + variance_1 / n_1)
```

The primary analysis should use uncropped data. Cropping 0.1% from each tail is
a robustness check, not a way to turn a failure into a pass.

## 5. Constant-time multiplication claim

### 5.1 Source snapshot

`rlwe_poly_mul_binary()` is used by encryption, decryption, and key generation.
Its binary multiplier is converted to an arithmetic mask:

```c
for (int i = 0; i < (int)RLWE_N; i++) {
    uint32_t mask = 0u - (uint32_t)(b[i] & 1u);
    rlwe_coeff_t mask8 = (rlwe_coeff_t)mask;

    int k = i;
    for (; k + 3 < (int)RLWE_N; k += 4) {
        uint32_t rv = poly_load4(r, k);
        uint32_t av = poly_load4(a, k - i) & mask;
        poly_store4(r, k, __UADD8(rv, av));
    }

    for (; k < (int)RLWE_N; k++) {
        r[k] = addmod(r[k],
                      (rlwe_coeff_t)(a[k - i] & mask8));
    }
    /* The fixed-index negative-wrap loops use the same masking method. */
}
```

The loop bounds and addresses depend on public indices, not coefficient values.
No work is skipped when a binary coefficient is zero.

### 5.2 Compiled assembly snapshot

The Cortex-M33 object code loads the binary coefficient and turns it into a
mask. It does not branch on that mask:

```asm
ldrb.w  r8, [r7, r3]       /* b[i] */
sbfx    r8, r8, #0, #1     /* 0 or 0xffffffff */
...
and.w   r5, r5, r8
uadd8   r0, r0, r5
...
and.w   r5, r5, r8
usub8   r2, r2, r5
```

Observed conditional branches compare public loop indices, pointers, and
fixed bounds. No branch condition or memory address is derived from `a[i]` or
secret `b[i]`.

### 5.3 Call sites covered

Encryption:

```c
rlwe_poly_mul_binary(ar, pk->a, r);
rlwe_poly_mul_binary(br, pk->p, r);
```

Decryption:

```c
rlwe_poly_mul_binary(us, ct->u, sk->s);
```

Key generation:

```c
rlwe_poly_mul_binary(ar2, pk->a, sk->s);
```

The same compiled primitive serves all four calls. Testing two operand roles is
still useful:

- the encryption test varies the binary multiplier;
- the decryption test fixes the binary secret and varies the general
  ciphertext polynomial.

Together they exercise value variation in both formal input positions.

## 6. Measurement status

| Target | Corrected class question | Status | Interpretation |
|---|---|---|---|
| Decryption multiplication | Fixed vs random `ct->u`, fixed `sk->s` | **PASS** | Final `|t|=0.49146`; maximum running `|t|=1.21239` |
| Encryption multiplication | Fixed vs random ephemeral binary multiplier, fixed multiplicand | **PASS** | Final and maximum running `|t|=0` |

### 6.1 Corrected final multiplication results

#### Decryption multiplication

Reported analyzer output:

```text
Function: dudect_decrypt_mul04_asc
Raw samples: class 0=50000, class 1=50000
Cropped samples: class 0=50000 [241233, 241237],
                 class 1=50000 [241233, 241237]
Malformed lines ignored: 1
Final checkpoint |t|: 0.49146
Maximum running |t|: 1.21239
Clear upward trend: no
Result: PASS
Plot: dudect_decrypt_mul04_asc_t_convergence.png
```

Both classes retained 50,000 samples and had the same reported cycle range.
The maximum running statistic remained well below the `|t|=4.5` reference and
showed no upward trend.

Metadata caveat: the analyzer displayed the filename-derived name
`dudect_decrypt_mul04_asc`, rather than the firmware marker
`decrypt_mul_binary`, and reported one malformed line. This indicates that the
start marker may have been the malformed line. The sample counts and end marker
support a complete 100,000-record capture, but the original log should be
retained and its first line checked before final thesis submission.

#### Encryption multiplication

Analysis used `--crop-tail-percent 0.01`.

```text
Function: encrypt_mul_binary
Raw samples: class 0=50000, class 1=50000
Cropped samples: class 0=50000 [241233, 241233],
                 class 1=49999 [241233, 241233]
Malformed lines ignored: 0
Capture framing/count: OK
Final checkpoint |t|: 0
Maximum running |t|: 0
Clear upward trend: no
Result: PASS
Plot: dudect_encrypt_mul04_asc_t_convergence.png
```

The retained samples were identical at 241,233 cycles. No class-dependent
timing difference was observed. Because a zero-variance distribution forces
Welch's statistic to zero when the means are also equal, the uncropped result
should also be preserved as a sensitivity check.

### 6.2 Superseded preliminary measurements

The original encryption run produced identical retained cycle values and
`|t| = 0`. The original decryption run produced very large values
(`|t| = 2852.02` after cropping and `|t| = 1556.16` without cropping).
These runs used different class buffers and class-dependent random-generation
history. They motivated the harness correction and must not be presented as
evidence about the final implementation.

### 6.3 Result fields to preserve for every final run

Record the following with each thesis result:

- firmware commit or archive identifier;
- parameter set and build options;
- function marker from the log;
- raw samples per class;
- cropping percentage;
- retained samples per class;
- final `|t|`;
- maximum running `|t|`;
- convergence-plot filename; and
- PASS/FAIL decision.

## 7. Selected remaining dudect tests

The scope of this work is timing-based constant-time evaluation of the CCA2 PKE
implementation. FO KEM is not part of the implemented or claimed design and is
excluded from this report.

To avoid overlapping evidence, internal helpers such as encoding, decoding,
comparison, selection, hashing, polynomial addition, and polynomial
multiplication will not receive separate dudect tests. They are exercised
through the externally visible complete operations. Isolated helper tests are
reserved only for diagnosing a failed complete-operation test.

Exactly two dudect tests remain.

The firmware harness implements them as compile-time targets:

| CMake value | Test | UART function marker |
|---:|---|---|
| `TIMING_TEST_TARGET=3` | CT-ENC | `cca2_pke_encrypt` |
| `TIMING_TEST_TARGET=4` | CT-DEC | `cca2_pke_decrypt` |

Both targets compile successfully for the evaluated `N=256`, masked-decoder
configuration. CT-ENC is the active build at the time of this report update.

### 7.1 Test CT-ENC: complete CCA2 PKE encryption

Target:

```c
int rlwe_cca2_pke_encrypt(
    rlwe_cca2_pke_ciphertext_t *ct,
    const uint8_t msg[RLWE_MSG_BYTES],
    const rlwe_cca2_pke_pubkey_t *pk);
```

#### Security question

Does complete encryption execution time depend on the plaintext message?

#### Classes

| Item | Class 0 | Class 1 |
|---|---|---|
| Message | Fixed all-zero message | Fresh random message |
| Public key | Same fixed key | Same fixed key |
| Output address | Common ciphertext buffer | Same common buffer |
| Trial count | 50,000 | 50,000 |

The public key must be generated once before capture and remain unchanged. A
fresh random candidate message must be generated before every trial, including
class 0, and fixed/random selection must use the same common message buffer and
the same amount of preparation work.

#### Randomness control

`rlwe_cca2_pke_encrypt()` obtains `hidden_v` from `rng_pool`. Before every
timed call:

1. fill the random pool completely outside the timed interval;
2. ensure that one encryption consumes less than the available pool;
3. verify that no Secure Element refill can occur inside the timed call; and
4. perform the same refill/preparation sequence for both classes.

Reading already-buffered random bytes is part of the complete function. A
hardware TRNG refill is not allowed inside the measured interval.

#### Timed interval

The interval must contain exactly one call to `rlwe_cca2_pke_encrypt()`.
Interrupts are disabled only for that interval. Message generation, selection,
pool refill, output validation, and UART transmission remain outside it.

#### Internal components covered

This single test covers:

- plaintext handling;
- `pke_hash_pk()`;
- seed derivation and HKDF coin expansion;
- deterministic RBLWE encryption;
- both encryption polynomial multiplications;
- branch-free message encoding;
- polynomial addition;
- message masking;
- ciphertext-tag generation; and
- secure clearing executed by the complete function.

No separate dudect test is required for these components if CT-ENC passes.

#### Acceptance criteria

- Capture framing and all 100,000 records are valid.
- Maximum running `|t| < 4.5`.
- Final `|t| < 4.5`.
- No clear sustained upward trend is present.
- The uncropped result is the primary result.
- A 0.1%-per-tail result may be reported only as a sensitivity check.

### 7.2 Test CT-DEC: complete CCA2 PKE decryption

Target:

```c
int rlwe_cca2_pke_decrypt(
    uint8_t msg[RLWE_MSG_BYTES],
    const rlwe_cca2_pke_ciphertext_t *ct,
    const rlwe_cca2_pke_seckey_t *sk);
```

#### Security question

Does complete decryption execution time reveal whether a ciphertext is valid?

This is the most security-critical remaining timing test. A distinction
between valid and invalid ciphertexts can provide a chosen-ciphertext oracle
even when the polynomial multiplication itself is constant-time.

#### Classes

| Item | Class 0 | Class 1 |
|---|---|---|
| Ciphertext | Valid ciphertext | Invalid ciphertext derived from a valid one |
| Secret key | Same fixed key | Same fixed key |
| Input address | Common ciphertext buffer | Same common buffer |
| Output address | Common message buffer | Same common buffer |
| Trial count | 50,000 | 50,000 |

Generate a valid base ciphertext before the measurement loop. For every trial,
perform identical copying into the common ciphertext buffer. For class 1,
apply a nonzero corruption mask without a class-dependent branch. Rotate the
corruption location across `c12.u`, `c12.v`, `c3`, and `c4` so the invalid
class does not test only one byte position. Class 0 executes the same mutation
loop with a zero mask.

The malformed input must remain memory-safe and have the normal fixed
ciphertext length. Truncated buffers, null pointers, and public-length errors
are excluded because they exercise public argument validation rather than the
CCA validity decision.

#### Randomness control

The masked decoder consumes randomness from `rng_pool`. Before every timed
call:

1. refill the pool completely outside the interval;
2. confirm that one decryption consumes less than the pool capacity;
3. ensure no Secure Element refill occurs during decryption; and
4. give both classes the same initial pool state and preparation history.

#### Timed interval

The interval contains exactly one call to `rlwe_cca2_pke_decrypt()`.
Ciphertext construction/corruption, pool refill, result checking, and UART
transmission are outside the interval.

Relevant validity code covered by this test:

```c
valid_mask = pke_ct_equal_mask(ct, &check_c12, check_c4);
if (rc != 0) {
    valid_mask = 0u;
}
pke_select_msg(msg, recovered, reject, valid_mask);
```

#### Internal components covered

This one complete-operation test covers:

- ciphertext hashing;
- secret-key polynomial multiplication;
- polynomial addition;
- masked threshold decoding;
- plaintext unmasking;
- seed and coin regeneration;
- deterministic re-encryption;
- tag recomputation;
- constant-time ciphertext comparison;
- rejection-message derivation;
- recovered/reject message selection; and
- secure clearing executed by the complete function.

No separate dudect tests for the decoder, comparator, selector, or
re-encryption path are required if CT-DEC passes.

#### Acceptance criteria

The same criteria as CT-ENC apply: complete framing, exactly 100,000 records,
maximum and final `|t| < 4.5`, no sustained upward trend, and uncropped primary
analysis.

### 7.3 Non-overlap rule

The final essential campaign is:

| Test | Status |
|---|---|
| Binary multiplication, varying general/ciphertext operand | PASS |
| Binary multiplication, varying binary multiplier | PASS |
| CT-ENC: complete CCA2 PKE encryption | **PASS** |
| CT-DEC: complete CCA2 PKE decryption, valid versus invalid | **PASS** |

If CT-ENC or CT-DEC fails, isolate the internal component responsible in a
separate diagnostic phase. Such diagnostic tests do not form part of the
minimal thesis claim unless they lead to a code correction and repeated
complete-operation test.

### 7.4 CT-ENC result

The complete CCA2 PKE encryption capture was analyzed with
`--crop-tail-percent 0.01`.

```text
Function: cca2_pke_encrypt
Raw samples: class 0=50000, class 1=50000
Cropped samples: class 0=49990 [633762, 637505],
                 class 1=49990 [633651, 637673]
Malformed lines ignored: 0
Capture framing/count: OK
Final checkpoint |t|: 0.137741
Maximum running |t|: 1.01779
Clear upward trend: no
Result: PASS
Plot: dudect_CCA2_ENC01_asc_t_convergence.png
```

The class populations remained balanced after cropping. The final and maximum
running statistics stayed well below `|t|=4.5`, and no upward convergence trend
was observed. Under this capture configuration, no plaintext-class-dependent
timing leakage was detected in complete CCA2 PKE encryption.

For the primary thesis result, preserve an additional uncropped analysis of the
same log. The 0.01%-per-tail result is retained as a sensitivity analysis.

### 7.5 CT-DEC result

The complete CCA2 PKE decryption capture produced:

```text
Function: cca2_pke_decrypt
Raw samples: class 0=50000, class 1=50000
Cropped samples: class 0=50000 [1.10496e+06, 1.10972e+06],
                 class 1=50000 [1.10489e+06, 1.11033e+06]
Malformed lines ignored: 0
Capture framing/count: OK
Final checkpoint |t|: 0.550685
Maximum running |t|: 1.24302
Clear upward trend: no
Result: PASS
```

All 100,000 records were retained, the capture framing was valid, and neither
the final nor maximum running statistic approached `|t|=4.5`. No upward trend
was observed. Under the evaluated configuration, no timing distinction was
detected between valid and branchlessly corrupted invalid ciphertexts.

## 10. Current conclusion

Source and generated-assembly review support the constant-time claim for
`rlwe_poly_mul_binary()` in the current `N=256, q=256` Cortex-M33 build. Both
corrected multiplication experiments remained below the dudect-style
`|t|=4.5` reference: decryption reached a maximum running `|t|` of 1.21239,
while encryption reached 0.

Complete CCA2 PKE encryption passed its fixed-versus-random-message test with a
maximum running `|t|` of 1.01779. Complete CCA2 PKE decryption passed its
valid-versus-invalid-ciphertext test with a maximum running `|t|` of 1.24302.
Neither result showed an upward trend. Together with the two corrected
multiplication tests, all essential dudect tests selected for this CCA2 PKE
implementation passed under the evaluated configuration.

## 11. Reproducibility artifacts

- Firmware harness: `timing_test.c`
- Firmware configuration: `cmake_gcc/CMakeLists.txt`
- Offline analysis: `analyze.py`
- Multiplication implementation: `ntt.c`
- CPA encryption/decryption: `encrypt.c`, `decrypt.c`
- Masked decoder: `masked_decode.c`, `masked_gadgets.S`
- CCA2 PKE wrapper: `rlwe_cca2_pke.c`
