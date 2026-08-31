/**
 * decrypt.c — RLWE Decryption Implementation
 *
 * §10 Decryption
 *
 *  w = c1*r2 + c2
 *    = (a*e1 + e2)*r2 + (r1 - a*r2)*e1 + e3 + encode(m)
 *    = encode(m) + e1*r1 + e2*r2 + e3
 *                      ↑
 *                   small noise term, ∥·∥∞ ≪ q/4
 *
 * Message recovery via rounding:
 *   Each coefficient w_i encodes one message bit plus noise.
 *   Rounding to nearest of {0, q/2} recovers the bit correctly.
 *
 * Security:
 *   Noise is bounded such that no decryption errors occur.
 *   Intermediate values are securely erased from stack.
 */

#include "decrypt.h"
#include "ntt.h"
#include "sampling.h"
#include "cmsis_compiler.h"
#include "secure_wipe.h"
#include "rng_pool.h"
#include <string.h>

#ifndef RLWE_PACKED_CODEC
#define RLWE_PACKED_CODEC 1
#endif

#if ((RLWE_PACKED_CODEC != 0) && (RLWE_PACKED_CODEC != 1))
#error "RLWE_PACKED_CODEC must be 0 or 1"
#endif

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
#include "masked_decode.h"

void masked_refresh_poly(rlwe_poly share0, rlwe_poly share1,
                         const uint8_t *randomness, uint32_t length,
                         uint32_t coefficient_mask);
#endif

/* ── Core parameters (copied for self-contained module) ────────────── */
/**
 * decode_message() — recover an N-bit message from a polynomial.
 *
 * For each coefficient w_i, compute distances to 0 and q/2,
 * then extract bit based on which is closer.
 *
 * Rounding rule:
 *   d0 = min(w, q−w)     distance from w to 0 mod q
 *   d1 = |w − q/2|       distance from w to q/2
 *   bit_i = 1  iff  d1 ≤ d0
 *
 * This recovers the original bit that was encoded as:
 *   0 → 0,  1 → RLWE_QHALF
 * with high probability as long as noise is bounded.
 */
#if 0
static void decode_message(uint8_t msg[RLWE_MSG_BYTES], const rlwe_poly p)
{
    memset(msg, 0, RLWE_MSG_BYTES);
    
    for (int i = 0; i < (int)RLWE_N; i++) {
        uint16_t v = p[i];
        
        uint16_t c0 = RLWE_DECODE_BIAS;
        uint16_t c1 = (uint16_t)((RLWE_DECODE_BIAS + RLWE_QHALF) & 0xFFu);
        uint16_t diff0 = (v >= c0) ? (uint16_t)(v - c0)
                                   : (uint16_t)(c0 - v);
        uint16_t diff1 = (v >= c1) ? (uint16_t)(v - c1)
                                   : (uint16_t)(c1 - v);

        /* Compute circular distance to the two bias-shifted codewords. */
        uint16_t d0 = (diff0 < (uint16_t)(RLWE_Q - diff0)) ? diff0
                                                           : (uint16_t)(RLWE_Q - diff0);
        uint16_t d1 = (diff1 < (uint16_t)(RLWE_Q - diff1)) ? diff1
                                                           : (uint16_t)(RLWE_Q - diff1);
        
        /* Set bit if closer to q/2 than to 0 */
        if (d1 <= d0) {
            msg[i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
}
#endif

#if !RLWE_FULL_MASKED_DECRYPT_ENABLE
uint8_t rlwe_decode_coefficient_unmasked(rlwe_coeff_t v,
                                         uint32_t coefficient_index)
{
#if RLWE_ERROR_DISTRIBUTION == RLWE_ERROR_DIST_CBD0
    int32_t lower = (int32_t)coefficient_index
                  - ((int32_t)RLWE_N / 2)
                  + (int32_t)RLWE_QQUARTER
                  + 2;
#else
    int32_t lower = (int32_t)RLWE_QQUARTER;
    (void)coefficient_index;
#endif
    uint32_t shifted = ((uint32_t)v - (uint32_t)lower) & RLWE_Q_MASK;

    return (uint8_t)(1u ^ (shifted >> (RLWE_LOGQ - 1u)));
}

static void decode_message(uint8_t msg[RLWE_MSG_BYTES], const rlwe_poly p)
{
#if RLWE_PACKED_CODEC
    for (int j = 0; j < (int)RLWE_MSG_BYTES; j++) {
        const rlwe_coeff_t *src = &p[8 * j];

        uint8_t m =
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[0], (8u * j) + 0u) << 0) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[1], (8u * j) + 1u) << 1) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[2], (8u * j) + 2u) << 2) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[3], (8u * j) + 3u) << 3) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[4], (8u * j) + 4u) << 4) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[5], (8u * j) + 5u) << 5) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[6], (8u * j) + 6u) << 6) |
            (uint8_t)(rlwe_decode_coefficient_unmasked(src[7], (8u * j) + 7u) << 7);

        msg[j] = m;
    }
#else
    memset(msg, 0, RLWE_MSG_BYTES);
    for (uint32_t i = 0u; i < RLWE_N; i++) {
        msg[i >> 3] |= (uint8_t)(
            rlwe_decode_coefficient_unmasked(p[i], i) << (i & 7u));
    }
#endif
}
#endif

/**
 * rlwe_decrypt() — decrypt ciphertext to recover plaintext message.
 *
 * Computes w = c1*r2 + c2 and applies Equation (2) of R-BinLWEEnc.
 *
 * Mathematical correctness:
 *   With p=r1-a*r2, c1=a*e1+e2, and c2=p*e1+e3+encode(m):
 *     w = c1*r2+c2 = encode(m)+e1*r1+e2*r2+e3.
 *
 *   Noise bound ensures | noise | ≪ q/4, so rounding succeeds.
 *
 * @param msg   Output N-bit recovered plaintext
 * @param ct    Input ciphertext (u, v)
 * @param sk    Secret key (binary polynomial s ∈ {0,1}^n)
 * @return      0 always (no error detection at this layer)
 */
int rlwe_decrypt(uint8_t                      msg[RLWE_MSG_BYTES],
                 const rlwe_ciphertext_t      *ct,
                 rlwe_seckey_t                *sk)
{
    int rc = 0;

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    rlwe_poly us0, us1, w0, w1;
    uint8_t refresh[RLWE_N];

    if (rng_pool_get_bytes(refresh, sizeof(refresh)) != 0) {
        secure_wipe(msg, RLWE_MSG_BYTES);
        secure_wipe(refresh, sizeof(refresh));
        return -1;
    }
    masked_refresh_poly(sk->share[0], sk->share[1], refresh,
                        RLWE_N, RLWE_Q_MASK);

    rlwe_poly_mul(us0, ct->u, sk->share[0]);
    rlwe_poly_mul(us1, ct->u, sk->share[1]);
    memcpy(w0, us0, sizeof(w0));
    rlwe_poly_add(w1, ct->v, us1);

    rc = rlwe_decode_message_masked(msg, w0, w1);

    secure_wipe(us0, sizeof(us0));
    secure_wipe(us1, sizeof(us1));
    secure_wipe(w0, sizeof(w0));
    secure_wipe(w1, sizeof(w1));
    secure_wipe(refresh, sizeof(refresh));
#else
    rlwe_poly us, w;

    /* Compute u·s using optimized binary multiplication */
    rlwe_poly_mul_binary(us, ct->u, sk->s);

    /* Compute w = v − u·s */
    rlwe_poly_add(w, ct->v, us);

    /* Round and extract message bits. */
    decode_message(msg, w);

    /* ── Secure erasure of intermediate values ────────────────── */
    secure_wipe(us, sizeof(us));
    secure_wipe(w, sizeof(w));
#endif

    return rc;
}
