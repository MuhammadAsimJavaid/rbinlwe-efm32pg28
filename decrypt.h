/**
 * decrypt.h — RLWE Decryption Function
 * 
 * §10 Decryption
 *
 * Implements message recovery from RLWE ciphertext using secret key.
 * Reverses the encryption process via polynomial subtraction and rounding.
 *
 * Algorithm:
 *   w = v − u·s                        [subtract scaled ciphertext]
 *   msg_i = round(w_i → nearest of {0, q/2})  [extract message bits]
 */

#ifndef DECRYPT_H
#define DECRYPT_H

#include "rlwe_core.h"
#include <stdint.h>

/* ── Message size ────────────────────────────────────────────────── */
#ifndef RLWE_MASKING_ORDER
#define RLWE_MASKING_ORDER 1
#endif

#if RLWE_FULL_MASKED_DECRYPT_ENABLE && (RLWE_MASKING_ORDER != 1)
#error "The masked decryption implementation supports first order only"
#endif

/**
 * rlwe_decrypt() — decrypt a ciphertext to recover plaintext message.
 *
 *   w = v − u·s  =  encode(m) + small_noise
 *   round w → bits  [extract message via nearest-distance rounding]
 *
 * Correctness:
 *   Noise term ∥·∥∞ ≪ q/4 (where q = 256) ensures perfect rounding.
 *   Expected error rate: 0% for well-formed ciphertexts.
 *
 * @param msg   Output N-bit recovered plaintext
 * @param ct    Input ciphertext (u, v)
 * @param sk    Secret key (binary polynomial s ∈ {0,1}^n)
 * @return      0 on success, -1 if masked decoding cannot obtain randomness
 */
int rlwe_decrypt(uint8_t                      msg[RLWE_MSG_BYTES],
                 const rlwe_ciphertext_t      *ct,
                 rlwe_seckey_t                *sk);

#if !RLWE_FULL_MASKED_DECRYPT_ENABLE
uint8_t rlwe_decode_coefficient_unmasked(rlwe_coeff_t value,
                                         uint32_t coefficient_index);
#endif

#endif /* DECRYPT_H */
