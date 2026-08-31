/**
 * masked_decode.c - First-order Boolean-masked threshold decoder.
 *
 * Each coefficient is Boolean-shared before evaluating the decoding interval.
 * Nonlinear AND/OR operations use fresh randomness in a two-share ISW gadget.
 * The resulting message remains in two Boolean shares until final packing.
 */

#include "masked_decode.h"
#include "rng_pool.h"
#include "sampling.h"
#include "secure_wipe.h"
#include <string.h>

#if defined(__GNUC__)
#define MASKED_NOINLINE __attribute__((noinline))
#else
#define MASKED_NOINLINE
#endif

/*
 * Implemented in masked_gadgets.S. Keeping the nonlinear ISW gadget outside
 * C prevents GCC from factoring the cross-products into b0 ^ b1 (or a0 ^ a1)
 * and temporarily reconstructing an unmasked value.
 */
void masked_and_bit(uint8_t a0, uint8_t a1,
                    uint8_t b0, uint8_t b1,
                    uint8_t random_bit,
                    uint8_t *out0, uint8_t *out1);

static uint8_t random_bit(uint16_t randomness, unsigned int index)
{
    return (uint8_t)((randomness >> index) & 1u);
}

static MASKED_NOINLINE void masked_add(uint8_t a0, uint8_t a1,
                                      uint8_t b0, uint8_t b1,
                                      uint16_t randomness,
                                      uint8_t *sum0, uint8_t *sum1)
{
    uint8_t carry0 = 0u;
    uint8_t carry1 = 0u;
    uint8_t result0 = 0u;
    uint8_t result1 = 0u;

    for (unsigned int bit = 0; bit < RLWE_LOGQ; bit++) {
        uint8_t ab0 = (uint8_t)(((a0 ^ b0) >> bit) & 1u);
        uint8_t ab1 = (uint8_t)(((a1 ^ b1) >> bit) & 1u);
        uint8_t ai0 = (uint8_t)((a0 >> bit) & 1u);
        uint8_t ai1 = (uint8_t)((a1 >> bit) & 1u);
        uint8_t bi0 = (uint8_t)((b0 >> bit) & 1u);
        uint8_t bi1 = (uint8_t)((b1 >> bit) & 1u);
        uint8_t generate0, generate1, propagate0, propagate1;

        result0 |= (uint8_t)((ab0 ^ carry0) << bit);
        result1 |= (uint8_t)((ab1 ^ carry1) << bit);

        masked_and_bit(ai0, ai1, bi0, bi1,
                       random_bit(randomness, 2u * bit),
                       &generate0, &generate1);
        masked_and_bit(ab0, ab1, carry0, carry1,
                       random_bit(randomness, (2u * bit) + 1u),
                       &propagate0, &propagate1);
        carry0 = (uint8_t)(generate0 ^ propagate0);
        carry1 = (uint8_t)(generate1 ^ propagate1);
    }

    *sum0 = result0;
    *sum1 = result1;
}

static MASKED_NOINLINE void masked_add_public_constant(uint8_t x0, uint8_t x1,
                                                  uint8_t constant,
                                                  uint16_t randomness,
                                                  uint8_t *sum0,
                                                  uint8_t *sum1)
{
    uint8_t carry0 = 0u;
    uint8_t carry1 = 0u;
    uint8_t result0 = 0u;
    uint8_t result1 = 0u;

    for (unsigned int bit = 0; bit < RLWE_LOGQ; bit++) {
        uint8_t xb0 = (uint8_t)((x0 >> bit) & 1u);
        uint8_t xb1 = (uint8_t)((x1 >> bit) & 1u);
        uint8_t constant_bit = (uint8_t)((constant >> bit) & 1u);
        uint8_t and0;
        uint8_t and1;
        uint8_t or0;
        uint8_t or1;
        uint8_t select_mask = (uint8_t)(0u - constant_bit);
        uint8_t sb0 = (uint8_t)(xb0 ^ carry0 ^ constant_bit);
        uint8_t sb1 = (uint8_t)(xb1 ^ carry1);

        result0 |= (uint8_t)(sb0 << bit);
        result1 |= (uint8_t)(sb1 << bit);

        masked_and_bit(xb0, xb1, carry0, carry1,
                       random_bit(randomness, bit), &and0, &and1);
        or0 = (uint8_t)(xb0 ^ carry0 ^ and0);
        or1 = (uint8_t)(xb1 ^ carry1 ^ and1);
        carry0 = (uint8_t)((and0 & (uint8_t)~select_mask) |
                           (or0 & select_mask));
        carry1 = (uint8_t)((and1 & (uint8_t)~select_mask) |
                           (or1 & select_mask));
    }

    *sum0 = result0;
    *sum1 = result1;
}

static MASKED_NOINLINE void masked_decode_coeff(rlwe_coeff_t arithmetic0,
                                           rlwe_coeff_t arithmetic1,
                                           size_t coefficient_index,
                                           const uint8_t random_bytes[5],
                                           uint8_t *bit0,
                                           uint8_t *bit1)
{
    uint8_t a_mask = random_bytes[0] & (uint8_t)RLWE_Q_MASK;
    uint8_t b_mask = random_bytes[1] & (uint8_t)RLWE_Q_MASK;
    uint8_t a0 = a_mask;
    uint8_t a1 = (uint8_t)(arithmetic0 ^ a_mask);
    uint8_t b0 = b_mask;
    uint8_t b1 = (uint8_t)(arithmetic1 ^ b_mask);
    uint16_t conversion_randomness = (uint16_t)random_bytes[2] |
                                     ((uint16_t)random_bytes[3] << 8);
    uint8_t value0;
    uint8_t value1;
    uint8_t threshold0;
    uint8_t threshold1;
#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
    int32_t lower = (int32_t)coefficient_index
                  - ((int32_t)RLWE_N / 2)
                  + (int32_t)RLWE_QQUARTER
                  + 2;
#else
    int32_t lower = (int32_t)RLWE_QQUARTER;
    (void)coefficient_index;
#endif
    uint8_t add_constant =
        (uint8_t)((0u - (uint32_t)lower) & RLWE_Q_MASK);

    /*
     * Equation (2): subtract the coefficient-dependent lower edge of the
     * bit-one interval. Since q is a power of two, the shifted value is below
     * q/2 exactly when its most-significant coefficient bit is zero.
     */
    /* Convert arithmetic shares to Boolean shares without reconstructing:
     * arithmetic0 + arithmetic1 = (a0^a1) + (b0^b1) mod 2^logq. */
    masked_add(a0, a1, b0, b1, conversion_randomness,
               &value0, &value1);

    masked_add_public_constant(value0, value1, add_constant,
                               random_bytes[4], &threshold0, &threshold1);

    *bit0 = (uint8_t)(((threshold0 >> (RLWE_LOGQ - 1u)) & 1u) ^ 1u);
    *bit1 = (uint8_t)((threshold1 >> (RLWE_LOGQ - 1u)) & 1u);
}

int rlwe_decode_coefficient_masked(rlwe_coeff_t arithmetic_share0,
                                   rlwe_coeff_t arithmetic_share1,
                                   size_t coefficient_index,
                                   uint8_t *bit_share0,
                                   uint8_t *bit_share1)
{
    uint8_t random_bytes[5];

    if (rng_pool_get_bytes(random_bytes, sizeof(random_bytes)) != 0) {
        *bit_share0 = 0u;
        *bit_share1 = 0u;
        secure_wipe(random_bytes, sizeof(random_bytes));
        return -1;
    }

    masked_decode_coeff(arithmetic_share0, arithmetic_share1,
                        coefficient_index, random_bytes,
                        bit_share0, bit_share1);
    secure_wipe(random_bytes, sizeof(random_bytes));
    return 0;
}

int rlwe_decode_message_masked(uint8_t msg[RLWE_MSG_BYTES],
                               const rlwe_poly arithmetic_share0,
                               const rlwe_poly arithmetic_share1)
{
    uint8_t msg0[RLWE_MSG_BYTES];
    uint8_t msg1[RLWE_MSG_BYTES];

    memset(msg0, 0, sizeof(msg0));
    memset(msg1, 0, sizeof(msg1));

    for (size_t i = 0; i < RLWE_MSG_BITS; i++) {
        uint8_t bit0;
        uint8_t bit1;

        if (rlwe_decode_coefficient_masked(arithmetic_share0[i],
                                           arithmetic_share1[i], i,
                                           &bit0, &bit1) != 0) {
            secure_wipe(msg, RLWE_MSG_BYTES);
            secure_wipe(msg0, sizeof(msg0));
            secure_wipe(msg1, sizeof(msg1));
            return -1;
        }
        msg0[i / 8u] |= (uint8_t)(bit0 << (i % 8u));
        msg1[i / 8u] |= (uint8_t)(bit1 << (i % 8u));
    }

    for (size_t i = 0; i < RLWE_MSG_BYTES; i++) {
        msg[i] = (uint8_t)(msg0[i] ^ msg1[i]);
    }

    secure_wipe(msg0, sizeof(msg0));
    secure_wipe(msg1, sizeof(msg1));

    return 0;
}
