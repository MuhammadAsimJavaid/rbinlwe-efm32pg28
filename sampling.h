/**
 * sampling.h — Sampling utilities for RLWE key generation
 * 
 * §5  PRNG (removed - using hardware TRNG directly)
 * §6  Sampling
 *
 * Helper functions for sampling from distributions used in
 * key generation.
 */

#ifndef SAMPLING_H
#define SAMPLING_H

#include "rlwe_core.h"

#define RLWE_ERROR_DIST_CBD0  0
#define RLWE_ERROR_DIST_CBD1  1

#ifndef RLWE_ERROR_DISTRIBUTION
#define RLWE_ERROR_DISTRIBUTION RLWE_ERROR_DIST_CBD0
#endif

#if ((RLWE_ERROR_DISTRIBUTION != RLWE_ERROR_DIST_CBD0) && (RLWE_ERROR_DISTRIBUTION != RLWE_ERROR_DIST_CBD1))
#error "RLWE_ERROR_DISTRIBUTION must be RLWE_ERROR_DIST_CBD0 or RLWE_ERROR_DIST_CBD1"
#endif

/**
 * sample_uniform() — sample each coefficient uniformly from Zq.
 * Uses rejection sampling with get_random_number() from app.h.
 * Expected rejection rate ≈ 6.2%.
 */
void sample_uniform(rlwe_poly out);

/**
 * sample_binary() — sample coefficients uniformly from {0,1}^n.
 * Each 32-bit random value gives 32 independent coin flips.
 * Uses get_random_number() from app.h.
 */
void sample_binary(rlwe_poly out);

/**
 * sample_error_binary() — non-centered binary error sampler.
 * Each coefficient e_i is sampled directly from one random bit; bits are
 * independent Bernoulli(1/2) bits.
 * Support: e_i in {0, 1}.
 * Uses 32 independent 1-bit samples from each get_random_number() call.
 */
/* The paper-compatible configuration uses uniform Bernoulli {0,1}. */
void sample_error(rlwe_poly out);
void sample_error_binary(rlwe_poly out);

#define RLWE_BINARY_COIN_BYTES  (RLWE_N / 8u)

#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
#define RLWE_ERROR_COIN_BYTES   (RLWE_N / 8u)
#else
#define RLWE_ERROR_COIN_BYTES   (RLWE_N / 4u)
#endif

void sample_binary_from_bytes(rlwe_poly out,
                              const uint8_t coins[RLWE_BINARY_COIN_BYTES]);
void sample_error_from_bytes(rlwe_poly out,
                             const uint8_t coins[RLWE_ERROR_COIN_BYTES]);

#endif /* SAMPLING_H */
