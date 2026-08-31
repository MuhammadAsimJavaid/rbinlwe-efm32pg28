/**
 * encrypt.c — RLWE Encryption Implementation
 *
 * §9  Encryption
 *
 *  e1,e2,e3 ← Binary({0,1}^n)
 *  u = c1 = a*e1 + e2
 *  v = c2 = p*e1 + e3 + encode(m)
 *
 * Security:
 *   Each encryption MUST use fresh randomness.
 *   Randomness is sourced from Silicon Labs hardware TRNG via sampling.c.
 *   Reusing ephemeral randomness leaks the secret key.
 *   All ephemeral secrets are securely erased from stack.
 */

#include "encrypt.h"
#include "sampling.h"
#include "ntt.h"
#include "cmsis_compiler.h"
#include <string.h>

#ifndef RLWE_PACKED_CODEC
#define RLWE_PACKED_CODEC 1
#endif

#if ((RLWE_PACKED_CODEC != 0) && (RLWE_PACKED_CODEC != 1))
#error "RLWE_PACKED_CODEC must be 0 or 1"
#endif

/* ── Core parameters (copied for self-contained module) ────────────── */
/**
 * encode_message() — encode an N-bit message into a polynomial.
 *
 * Maps each message bit to a polynomial coefficient:
 *   bit 0 → 0
 *   bit 1 → RLWE_QHALF (128)
 *
 * This spreads the message across all 256 polynomial positions,
 * providing diffusion before adding noise.
 */
#if !RLWE_PACKED_CODEC
static void encode_message(rlwe_poly p, const uint8_t msg[RLWE_MSG_BYTES])
{
    for (int i = 0; i < (int)RLWE_N; i++) {
        uint8_t bit = (msg[i / 8] >> (i % 8)) & 1u;
        p[i] = bit ? (rlwe_coeff_t)RLWE_QHALF : 0u;
    }
}
#else
static void encode_message(rlwe_poly p, const uint8_t msg[RLWE_MSG_BYTES])
{
    memset(p, 0, sizeof(rlwe_poly));
    for (int j = 0; j < (int)RLWE_MSG_BYTES; j++) {
        uint32_t b = msg[j];

        uint32_t w0 =
            ((-((b >> 0) & 1u) & RLWE_QHALF)      ) |
            ((-((b >> 1) & 1u) & RLWE_QHALF) << 8 ) |
            ((-((b >> 2) & 1u) & RLWE_QHALF) << 16) |
            ((-((b >> 3) & 1u) & RLWE_QHALF) << 24);

        uint32_t w1 =
            ((-((b >> 4) & 1u) & RLWE_QHALF)      ) |
            ((-((b >> 5) & 1u) & RLWE_QHALF) << 8 ) |
            ((-((b >> 6) & 1u) & RLWE_QHALF) << 16) |
            ((-((b >> 7) & 1u) & RLWE_QHALF) << 24);

        __UNALIGNED_UINT32_WRITE(&p[8 * j],     w0);
        __UNALIGNED_UINT32_WRITE(&p[8 * j + 4], w1);
    }
}
#endif

static void encrypt_with_polys(rlwe_ciphertext_t   *ct,
                               const uint8_t        msg[RLWE_MSG_BYTES],
                               const rlwe_pubkey_t *pk,
                               const rlwe_poly      r,
                               const rlwe_poly      e1,
                               const rlwe_poly      e2)
{
    rlwe_poly m_poly, ar, br;

    rlwe_poly_mul_binary(ar, pk->a, r);
    rlwe_poly_add(ct->u, ar, e1);

    rlwe_poly_mul_binary(br, pk->p, r);
    encode_message(m_poly, msg);
    rlwe_poly_add(ct->v, br, e2);
    rlwe_poly_add(ct->v, ct->v, m_poly);

    memset(m_poly, 0, sizeof(m_poly));
    memset(ar, 0, sizeof(ar));
    memset(br, 0, sizeof(br));
}

/**
 * rlwe_encrypt() — encrypt an N-bit message.
 *
 *   r  ←  Binary({0,1}^n)              (ephemeral randomness)
 *   e1 ←  Binary({0,1}^n)                  (error polynomial 1)
 *   e2 ←  Binary({0,1}^n)                  (error polynomial 2)
 *   u   = a·r + e1                     (ciphertext component 1)
 *   v   = b·r + e2 + encode(msg)       (ciphertext component 2)
 *
 * Security properties:
 *   - IND-CPA secure (semantic security) under RLWE assumption
 *   - Each encryption uses fresh hardware-TRNG-generated randomness
 *   - Ephemeral secrets (r, e1, e2) securely erased from stack
 *
 * Performance (EFM32):
 *   ~2–3 polynomial multiplications → ~10–15 ms on Cortex-M4
 *   Stack usage: ~3.6 KB of temporary polynomials
 *
 * @param ct    Output ciphertext (u, v)
 * @param msg   Input N-bit plaintext message
 * @param pk    Recipient's public key (a, b)
 */
void rlwe_encrypt(rlwe_ciphertext_t       *ct,
                  const uint8_t            msg[RLWE_MSG_BYTES],
                  const rlwe_pubkey_t     *pk)
{
    rlwe_poly   r, e1, e2;

    /* ── Sample ephemeral secrets using hardware TRNG ───────────── */
    sample_binary(r);           /* paper e1: ephemeral binary polynomial */
    sample_error(e1);           /* paper e2: selected error distribution */
    sample_error(e2);           /* paper e3: selected error distribution */

    encrypt_with_polys(ct, msg, pk, r, e1, e2);

    /* ── Secure erasure of ephemeral secrets ──────────────────────– */
    memset(r,    0, sizeof(r));         /* erase ephemeral r    */
    memset(e1,   0, sizeof(e1));        /* erase ephemeral e1   */
    memset(e2,   0, sizeof(e2));        /* erase ephemeral e2   */
}

void rlwe_encrypt_deterministic(rlwe_ciphertext_t       *ct,
                                const uint8_t            msg[RLWE_MSG_BYTES],
                                const rlwe_pubkey_t     *pk,
                                const uint8_t            r_coins[RLWE_BINARY_COIN_BYTES],
                                const uint8_t            e1_coins[RLWE_ERROR_COIN_BYTES],
                                const uint8_t            e2_coins[RLWE_ERROR_COIN_BYTES])
{
    rlwe_poly r, e1, e2;

    sample_binary_from_bytes(r, r_coins);
    sample_error_from_bytes(e1, e1_coins);
    sample_error_from_bytes(e2, e2_coins);

    encrypt_with_polys(ct, msg, pk, r, e1, e2);

    memset(r, 0, sizeof(r));
    memset(e1, 0, sizeof(e1));
    memset(e2, 0, sizeof(e2));
}
