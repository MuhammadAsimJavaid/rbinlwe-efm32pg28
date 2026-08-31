/**
 * masked_decode.h - First-order Boolean-masked threshold decoding.
 */

#ifndef MASKED_DECODE_H
#define MASKED_DECODE_H

#include <stddef.h>

#include "decrypt.h"

#define RLWE_MASKED_DECODE_RANDOM_BYTES (5u * RLWE_MSG_BITS)

/* Decode one production coefficient, including buffered randomness
 * consumption. The returned bit remains Boolean-shared. */
int rlwe_decode_coefficient_masked(rlwe_coeff_t arithmetic_share0,
                                   rlwe_coeff_t arithmetic_share1,
                                   size_t coefficient_index,
                                   uint8_t *bit_share0,
                                   uint8_t *bit_share1);

int rlwe_decode_message_masked(uint8_t msg[RLWE_MSG_BYTES],
                               const rlwe_poly arithmetic_share0,
                               const rlwe_poly arithmetic_share1);

#endif /* MASKED_DECODE_H */
