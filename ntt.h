/**
 * ntt.h — Modular arithmetic and polynomial operations
 * 
 * Shift-and-add polynomial multiplication for RLWE
 * using negacyclic convolution in Zq[x]/(x^256 + 1).
 *
 * ── Arithmetic design note ──────────────────────────────────────
 *   With q=256, all operations use bitwise masking with 0xFF.
 *   max(a·b) = (q−1)² = 65,025 < 2^17 < UINT32_MAX
 *   No overflow possible on 32-bit ARM targets.
 *
 * ── Ring parameters ──────────────────────────────────────────────
 *   q = 256  (modulus as power of 2)
 *   N = 256  (polynomial degree)
 *   Ring: Z_256[x] / (x^256 + 1)
 *   x^256 ≡ -1  (negacyclic)
 */

#ifndef NTT_H
#define NTT_H

#include "rlwe_core.h"

/* ── Modular arithmetic (inline, optimized for q=256) ────────────── */

/**
 * mulmod — multiply two Zq elements mod 256.
 * a, b in [0, 256) → a·b in [0, 256)
 * Uses bitwise AND since q is a power of 2.
 */
static inline rlwe_coeff_t mulmod(rlwe_coeff_t a, rlwe_coeff_t b)
{
    return (rlwe_coeff_t)(((uint32_t)a * (uint32_t)b) & RLWE_Q_MASK);
}

/**
 * addmod — add two Zq elements mod 256 (branchless).
 */
static inline rlwe_coeff_t addmod(rlwe_coeff_t a, rlwe_coeff_t b)
{
    return (rlwe_coeff_t)(((uint16_t)a + (uint16_t)b) & RLWE_Q_MASK);
}

/**
 * submod — subtract two Zq elements mod 256.
 * Handles wrapping for negative results.
 */
static inline rlwe_coeff_t submod(rlwe_coeff_t a, rlwe_coeff_t b)
{
    return (rlwe_coeff_t)(((uint16_t)a + RLWE_Q - (uint16_t)b) & RLWE_Q_MASK);
}

/* ── Polynomial arithmetic ──────────────────────────────────────– */

/**
 * rlwe_poly_add() — coefficient-wise addition mod q.
 */
void rlwe_poly_add(rlwe_poly r, const rlwe_poly a, const rlwe_poly b);

/**
 * rlwe_poly_sub() — coefficient-wise subtraction mod q.
 */
void rlwe_poly_sub(rlwe_poly r, const rlwe_poly a, const rlwe_poly b);

/**
 * rlwe_poly_mul() — Shift-and-add polynomial multiplication.
 * Negacyclic convolution: r = a * b  mod (x^256 + 1, 256)
 * Uses iterative shift and add for binary/small-coefficient optimization.
 */
void rlwe_poly_mul(rlwe_poly r, const rlwe_poly a, const rlwe_poly b);

/* Accumulate exactly one outer-loop coefficient of the general negacyclic
 * multiplication into an already initialized result polynomial. */
void rlwe_poly_mul_accumulate_coefficient(rlwe_poly r,
                                          const rlwe_poly a,
                                          const rlwe_poly b,
                                          size_t coefficient_index);

/**
 * rlwe_poly_mul_binary() — Optimized multiplication when b is binary {0,1}^n.
 * Shifts polynomial a by i positions (negacyclic) and adds only when b[i]=1.
 * Skips operations when b[i]=0, achieving ~50% speedup for binary operands.
 * 
 * Negacyclic shift by i positions:
 *   For position k in output, add a[(k-i) mod 512]:
 *   - If (k-i) mod 512 < 256: positive (k-i >= 0 or wraps within lower half)
 *   - If (k-i) mod 512 >= 256: negative (due to x^256 ≡ -1)
 */
void rlwe_poly_mul_binary(rlwe_poly r, const rlwe_poly a, const rlwe_poly b);

/* Accumulate one outer-loop coefficient of the production binary-secret
 * multiplier into an initialized result polynomial. */
void rlwe_poly_mul_binary_accumulate_coefficient(
    rlwe_poly r, const rlwe_poly a, const rlwe_poly b,
    size_t coefficient_index);

#endif /* NTT_H */
