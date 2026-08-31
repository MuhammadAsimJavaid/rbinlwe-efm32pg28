/**
 * ntt.c — Polynomial arithmetic using shift-and-add
 * 
 * Negacyclic convolution for ring Z_256[x]/(x^256 + 1)
 * No Number Theoretic Transform; direct coefficient computation.
 *
 * §1  Polynomial arithmetic (shift-and-add multiplication)
 */

#include "ntt.h"
#include "cmsis_compiler.h"
#include <stddef.h>
#include <string.h>

#ifndef RLWE_BRANCH_FREE_MUL_BINARY
#define RLWE_BRANCH_FREE_MUL_BINARY 1
#endif

#ifndef RLWE_PACKED_ARITHMETIC
#define RLWE_PACKED_ARITHMETIC 1
#endif

#if ((RLWE_PACKED_ARITHMETIC != 0) && (RLWE_PACKED_ARITHMETIC != 1))
#error "RLWE_PACKED_ARITHMETIC must be 0 or 1"
#endif

#if RLWE_PACKED_ARITHMETIC
__STATIC_FORCEINLINE uint32_t poly_load4(const rlwe_poly p, int idx)
{
    return __UNALIGNED_UINT32_READ(&p[idx]);
}

__STATIC_FORCEINLINE void poly_store4(rlwe_poly p, int idx, uint32_t v)
{
    __UNALIGNED_UINT32_WRITE(&p[idx], v);
}
#endif

/* ════════════════════════════════════════════════════════════════
 * §1  Polynomial arithmetic (public helpers)
 * ════════════════════════════════════════════════════════════════
 */

/**
 * rlwe_poly_add() — coefficient-wise addition mod q.
 */
void rlwe_poly_add(rlwe_poly r, const rlwe_poly a, const rlwe_poly b)
{
    for (int i = 0; i < (int)RLWE_N; i++) {
        r[i] = addmod(a[i], b[i]);
    }
}

/**
 * rlwe_poly_sub() — coefficient-wise subtraction mod q.
 */
void rlwe_poly_sub(rlwe_poly r, const rlwe_poly a, const rlwe_poly b)
{
    for (int i = 0; i < (int)RLWE_N; i++) {
        r[i] = submod(a[i], b[i]);
    }
}

/**
 * rlwe_poly_mul() — Shift-and-add polynomial multiplication.
 * 
 * Computes r = a * b in Z_256[x]/(x^256 + 1)
 * using direct coefficient computation with negacyclic wrapping.
 *
 * Algorithm:
 *   For each i in [0, 256):
 *     For each j in [0, 256):
 *       pos = (i + j) mod 512
 *       if pos < 256:
 *         r[pos] += a[i] * b[j]   (positive)
 *       else:
 *         r[pos - 256] -= a[i] * b[j]   (negation due to x^256 = -1)
 *
 * Complexity: O(n²) — suitable for n=256 with binary/small coefficients.
 * Performance: ~2-5ms on Cortex-M4 (depending on coefficient sparsity).
 */
void rlwe_poly_mul_accumulate_coefficient(rlwe_poly r,
                                          const rlwe_poly a,
                                          const rlwe_poly b,
                                          size_t coefficient_index)
{
    int i = (int)coefficient_index;

    for (int j = 0; j < (int)RLWE_N; j++) {
        rlwe_coeff_t prod = mulmod(a[i], b[j]);
        int pos = i + j;

        if (pos < (int)RLWE_N) {
            r[pos] = addmod(r[pos], prod);
        } else {
            r[pos - (int)RLWE_N] = submod(r[pos - (int)RLWE_N], prod);
        }
    }
}

void rlwe_poly_mul(rlwe_poly r, const rlwe_poly a, const rlwe_poly b)
{
    /* Initialize result to zero */
    memset(r, 0, sizeof(rlwe_poly));
    
    /* Negacyclic convolution */
    for (size_t i = 0; i < RLWE_N; i++) {
        rlwe_poly_mul_accumulate_coefficient(r, a, b, i);
    }
}

/**
 * rlwe_poly_mul_binary() — Optimized shift-and-add for binary polynomials.
 *
 * When b is binary {0,1}^n, this avoids multiplications by zero:
 * For each i where b[i] = 1, shift a by i positions and add to result.
 * Expected speedup: ~50% vs standard multiplication (half the coefficients are zero).
 *
 * Negacyclic shift by i positions in Z_256[x]/(x^256 + 1):
 *   Shifted polynomial at position k:
 *   - If k >= i:  a[k-i]          (positive)
 *   - If k < i:   -a[k-i+256]     (negative, due to x^256 ≡ -1)
 *
 * @param r     Output polynomial
 * @param a     Multiplicand (any polynomial)
 * @param b     Multiplier (binary polynomial {0,1}^n)
 */
void rlwe_poly_mul_binary_accumulate_coefficient(
    rlwe_poly r, const rlwe_poly a, const rlwe_poly b,
    size_t coefficient_index)
{
    int i = (int)coefficient_index;
#if RLWE_BRANCH_FREE_MUL_BINARY
    uint32_t mask = 0u - (uint32_t)(b[i] & 1u);
    rlwe_coeff_t mask8 = (rlwe_coeff_t)mask;
#else
    if (b[i] == 0) {
        return;
    }
#endif

    int k = i;
#if RLWE_PACKED_ARITHMETIC
    for (; k + 3 < (int)RLWE_N; k += 4) {
        uint32_t rv = poly_load4(r, k);
#if RLWE_BRANCH_FREE_MUL_BINARY
        uint32_t av = poly_load4(a, k - i) & mask;
#else
        uint32_t av = poly_load4(a, k - i);
#endif
        poly_store4(r, k, __UADD8(rv, av));
    }
#endif
    for (; k < (int)RLWE_N; k++) {
#if RLWE_BRANCH_FREE_MUL_BINARY
        r[k] = addmod(r[k], (rlwe_coeff_t)(a[k - i] & mask8));
#else
        r[k] = addmod(r[k], a[k - i]);
#endif
    }

    k = 0;
#if RLWE_PACKED_ARITHMETIC
    for (; k + 3 < i; k += 4) {
        uint32_t rv = poly_load4(r, k);
#if RLWE_BRANCH_FREE_MUL_BINARY
        uint32_t av = poly_load4(a, k - i + (int)RLWE_N) & mask;
#else
        uint32_t av = poly_load4(a, k - i + (int)RLWE_N);
#endif
        poly_store4(r, k, __USUB8(rv, av));
    }
#endif
    for (; k < i; k++) {
#if RLWE_BRANCH_FREE_MUL_BINARY
        r[k] = submod(r[k],
                      (rlwe_coeff_t)(a[k - i + (int)RLWE_N] & mask8));
#else
        r[k] = submod(r[k], a[k - i + (int)RLWE_N]);
#endif
    }
}

void rlwe_poly_mul_binary(rlwe_poly r, const rlwe_poly a, const rlwe_poly b)
{
    memset(r, 0, sizeof(rlwe_poly));

    for (size_t i = 0u; i < RLWE_N; i++) {
        rlwe_poly_mul_binary_accumulate_coefficient(r, a, b, i);
    }

#if RLWE_PACKED_ARITHMETIC && (RLWE_Q < 256u)
    /* Packed byte operations wrap modulo 256. Since q divides 256, reducing
     * once at the end preserves the result and restores canonical values. */
    for (int i = 0; i < (int)RLWE_N; i++) {
        r[i] &= (rlwe_coeff_t)RLWE_Q_MASK;
    }
#endif
}

