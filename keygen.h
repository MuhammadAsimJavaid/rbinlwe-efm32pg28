/**
 * keygen.h — RLWE Key Pair Generation
 * 
 * §8  Key Generation
 *
 * No seed parameter; uses hardware TRNG via get_random_number().
 */

#ifndef KEYGEN_H
#define KEYGEN_H

#include "rlwe_core.h"

/**
 * rlwe_keypair() — generate a public/secret key pair.
 *
 *   a  ← Uniform(Zq^n)
 *   s  ← Binary({0,1}^n)              [secret key]
 *   e  ← configured error distribution
 *   b   = a·s + e   mod (x^n+1, q)    [public key component]
 *
 * No seed parameter; uses get_random_number() internally via
 * sampling functions.
 *
 * @param pk    Output public key (a, b)
 * @param sk    Output secret key (shared when full masking is enabled)
 * @return      0 on success, -1 if masked key generation lacks randomness
 */
int rlwe_keypair(rlwe_pubkey_t *pk, rlwe_seckey_t *sk);

#endif /* KEYGEN_H */
