/**
 * encrypt.h — RLWE Encryption Function
 * 
 * §9  Encryption
 *
 * Implements IND-CPA encryption for Ring Learning With Errors.
 * Produces ciphertext (u, v) from plaintext message and recipient's public key.
 *
 * Algorithm:
 *   r  ←  Binary({0,1}^n)     [fresh ephemeral randomness]
 *   e1 ←  configured error distribution
 *   e2 ←  configured error distribution
 *   u   = a·r + e1            [ciphertext component 1]
 *   v   = b·r + e2 + encode(m) [ciphertext component 2]
 */

#ifndef ENCRYPT_H
#define ENCRYPT_H

#include "rlwe_core.h"
#include "sampling.h"
#include <stdint.h>

/* ── Message and ciphertext sizes ────────────────────────────────── */
#define RLWE_SEED_BYTES     32u        /* entropy for encryption      */

/**
 * rlwe_encrypt() — encrypt an N-bit message.
 *
 *   r  ←  Binary({0,1}^n)              (ephemeral randomness)
 *   e1 ←  configured error distribution
 *   e2 ←  configured error distribution
 *   u   = a·r + e1
 *   v   = b·r + e2 + encode(msg)
 *
 * @param ct    Output ciphertext (u, v)
 * @param msg   Input N-bit plaintext message
 * @param pk    Recipient's public key (a, b)
 *
 * IMPORTANT: Each encryption samples fresh ephemeral randomness internally
 *            through get_random_number() from app.h.
 */
void rlwe_encrypt(rlwe_ciphertext_t       *ct,
                  const uint8_t            msg[RLWE_MSG_BYTES],
                  const rlwe_pubkey_t     *pk);

void rlwe_encrypt_deterministic(rlwe_ciphertext_t       *ct,
                                const uint8_t            msg[RLWE_MSG_BYTES],
                                const rlwe_pubkey_t     *pk,
                                const uint8_t            r_coins[RLWE_BINARY_COIN_BYTES],
                                const uint8_t            e1_coins[RLWE_ERROR_COIN_BYTES],
                                const uint8_t            e2_coins[RLWE_ERROR_COIN_BYTES]);

#endif /* ENCRYPT_H */
