# Selectable RLWE parameter sets

The implementation supports these compile-time parameter-set identifiers:

| `RLWE_PARAMETER_SET` | Polynomial degree | Coefficient modulus |
|---:|---:|---:|
| `1` | 256 | 256 |
| `2` | 256 | 128 |
| `3` | 512 | 256 |

The generated CMake project defaults to parameter set `3` (`N=512`, `q=256`).
The header-only fallback used outside CMake defaults to parameter set `1`.
With the generated CMake project, select a set while configuring:

```text
cmake --preset project -DRLWE_PARAMETER_SET=2
cmake --build --preset default_config
```

Select the error distribution independently:

```text
cmake --preset project -DRLWE_PARAMETER_SET=2 -DRLWE_ERROR_DISTRIBUTION=0
```

| `RLWE_ERROR_DISTRIBUTION` | Sampling | Decoder center |
|---:|---|---|
| `0` | Paper Bernoulli `{0,1}` (CBD0) | `k - N/2 + 3/2` |
| `1` | Centered `{-1,0,1}` (CBD1) | zero |

CBD0 is the default required to reproduce the paper. CBD1 retains the same
Figure-2 signs but uses centered `r1`, `e2`, and `e3`; the secret `r2` and
ephemeral multiplier `e1` remain binary so optimized binary multiplication is
still applicable.

For a non-CMake build, define `RLWE_PARAMETER_SET` to `1`, `2`, or `3` in the
compiler settings. The numeric identifiers and derived constants are declared
in `rlwe_core.h`; do not separately define `RLWE_N` or `RLWE_Q`.

The plaintext follows the paper and contains `N` bits: 32 bytes for `N=256`
and 64 bytes for `N=512`. Keys, polynomial ciphertext components,
deterministic coins, serialized frames, hashes, and comparisons automatically
use the selected polynomial size.

The CPA core implements Figure 2 of Buchmann et al.:

```text
p  = r1 - a*r2
c1 = a*e1 + e2
c2 = p*e1 + e3 + encode(m)
w  = c1*r2 + c2
```

All `r1`, `r2`, `e1`, `e2`, and `e3` coefficients are uniform Bernoulli
values in `{0,1}`. Decoding uses the coefficient-dependent expected noise
`k - N/2 + 3/2` from Equation (2). The `a` polynomial is stored explicitly in
the public-key object rather than being supplied as a separate global system
parameter; this does not change the equations, but it increases stored and
transmitted public-key size relative to Table 1.

The optional CCA2 wrapper is a project extension around this CPA core and is
not part of the cited 2016 paper.

## Independent countermeasure switches

The parameter and error-distribution selectors do not disable the existing
countermeasures. These remain independent compile-time definitions:

| Definition | Default | Purpose |
|---|---:|---|
| `RLWE_BRANCH_FREE_MUL_BINARY` | `1` | Branch-free secret binary multiplication |
| `RLWE_FULL_MASKED_DECRYPT_ENABLE` | `1` | Shared key, polynomial decryption, conversion, and decoding |
| `RLWE_MASKING_ORDER` | `1` | Supported masking order |
| `RLWE_CCA2_PKE_ENABLE` | `1` | CCA2 PKE wrapper |
| `RLWE_CCA2_PKE_SE_ORACLES_ENABLE` | `1` | Secure-Element SHA-256 oracles |

They can be overridden with compiler definitions without changing `N`, `q`,
or the error distribution. Both decoder variants use fixed loop bounds and the
branch-free multiplier remains enabled for CBD0 and CBD1.

## Correctness and security qualification

Table 1 reports *bit* failure probabilities of `2^-32`, `2^-10`, and `2^-18`
for sets 1, 2, and 3 respectively. These are not whole-message failure
probabilities and apply to paper-compatible CBD0. CBD1 is an experimental
alternative and requires a separate DFR and security qualification. The
implementation still requires device-level statistical testing before use.

Wire formats differ between `N=256` and `N=512`. Two communicating endpoints
must use the same parameter set. The parameter-set identifier is printed at
startup, but it is not embedded in the ciphertext structure itself.
