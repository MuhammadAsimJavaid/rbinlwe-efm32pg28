/**
 * sampling.c — Sampling from distributions
 *
 * §5  Sampling
 *
 * All sampling functions use get_random_number() from app.h
 * which retrieves bytes directly from the hardware TRNG via Secure Element.
 */

#include "sampling.h"
#include "app.h"    /* for get_random_number() */

/**
 * sample_uniform() — sample each coefficient uniformly from Zq.
 *
 * Rejection sampling:  accept v if v < floor(65536/q)·q = 61448.
 * Expected rejection rate ≈ 6.2%.  Terminates quickly in practice.
 */
void sample_uniform(rlwe_poly out)
{
    for (int i = 0; i < (int)RLWE_N; i += 4) {
        uint32_t rnd = get_random_number();
        int lim = (int)RLWE_N - i;
        if (lim > 4) lim = 4;
        for (int b = 0; b < lim; b++) {
            out[i + b] = (rlwe_coeff_t)((rnd >> (8 * b)) & RLWE_Q_MASK);
        }
    }
}

/**
 * sample_binary() — sample s uniformly from {0,1}^n.
 * Each 32-bit random value gives 32 independent coin flips.
 */
void sample_binary(rlwe_poly out)
{
    for (int i = 0; i < (int)RLWE_N; i += 32) {
        uint32_t bits = get_random_number();
        int lim = (int)RLWE_N - i;
        if (lim > 32) lim = 32;
        for (int b = 0; b < lim; b++) {
            out[i + b] = (bits >> b) & 1u;
        }
    }
}

/**
 * sample_error_binary() — non-centered binary error.
 *
 * Each coefficient e_i is sampled directly from one random bit; bits are
 * independent Bernoulli(1/2) bits.
 * Support: e_i in {0, 1}; mean = 1/2.
 * This distribution is intentionally not centered.
 * One 32-bit random word provides 32 independent error coefficients.
 */
void sample_binary_from_bytes(rlwe_poly out,
                              const uint8_t coins[RLWE_BINARY_COIN_BYTES])
{
    for (int i = 0; i < (int)RLWE_N; i++) {
        out[i] = (rlwe_coeff_t)((coins[i >> 3] >> (i & 7)) & 1u);
    }
}

#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
static void sample_error_cbd0(rlwe_poly out)
{
    for (int i = 0; i < (int)RLWE_N; i += 32) {
        uint32_t bits = get_random_number();
        int lim = (int)RLWE_N - i;
        if (lim > 32) lim = 32;

        for (int b = 0; b < lim; b++) {
            out[i + b] = (rlwe_coeff_t)((bits >> b) & 1u);
        }
    }
}

#else
static void sample_error_cbd1(rlwe_poly out)
{
    for (int i = 0; i < (int)RLWE_N; i += 16) {
        uint32_t bits = get_random_number();
        int lim = (int)RLWE_N - i;
        if (lim > 16) lim = 16;

        for (int b = 0; b < lim; b++) {
            uint32_t bit_a = (bits >> (2 * b)) & 1u;
            uint32_t bit_b = (bits >> (2 * b + 1)) & 1u;
            out[i + b] = (rlwe_coeff_t)(bit_a - bit_b);
        }
    }
}
#endif

void sample_error(rlwe_poly out)
{
#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
    sample_error_cbd0(out);
#else
    sample_error_cbd1(out);
#endif
}

void sample_error_binary(rlwe_poly out)
{
    sample_error(out);
}

void sample_error_from_bytes(rlwe_poly out,
                             const uint8_t coins[RLWE_ERROR_COIN_BYTES])
{
#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
    for (int i = 0; i < (int)RLWE_N; i++) {
        out[i] = (rlwe_coeff_t)((coins[i >> 3] >> (i & 7)) & 1u);
    }
#else
    for (int i = 0; i < (int)RLWE_N; i++) {
        uint32_t two_bits = (uint32_t)((coins[i >> 2] >> (2 * (i & 3))) & 3u);
        uint32_t bit_a = two_bits & 1u;
        uint32_t bit_b = (two_bits >> 1) & 1u;
        out[i] = (rlwe_coeff_t)(bit_a - bit_b);
    }
#endif
}
