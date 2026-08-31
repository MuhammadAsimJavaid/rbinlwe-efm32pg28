/**
 * keygen.c — RLWE Key Pair Generation
 *
 * §8  Key Generation
 *
 *  a  ←  Uniform(Zq^n)
 *  r1,r2 ← Binary({0,1}^n)
 *  p     = r1 - a*r2 mod (x^n+1, q)
 *  pk=(a,p), sk=r2
 */

#include "keygen.h"
#include "sampling.h"
#include "ntt.h"
#include "rng_pool.h"
#include "secure_wipe.h"
#include <string.h>
#include <stdio.h>

int rlwe_keypair(rlwe_pubkey_t *pk, rlwe_seckey_t *sk)
{
    rlwe_poly r1, ar2;
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    rlwe_poly ar0, ar1;
    uint8_t binary_coins[RLWE_BINARY_COIN_BYTES];
    uint8_t share_random[RLWE_N];
#endif

    sample_uniform(pk->a);      /* a  ← Uniform(Zq^n) */

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    if (rng_pool_get_bytes(binary_coins, sizeof(binary_coins)) != 0 ||
        rng_pool_get_bytes(share_random, sizeof(share_random)) != 0) {
        secure_wipe(pk, sizeof(*pk));
        secure_wipe(sk, sizeof(*sk));
        secure_wipe(binary_coins, sizeof(binary_coins));
        secure_wipe(share_random, sizeof(share_random));
        return -1;
    }
    for (size_t i = 0; i < RLWE_N; i++) {
        rlwe_coeff_t secret_bit =
            (rlwe_coeff_t)((binary_coins[i >> 3] >> (i & 7u)) & 1u);
        sk->share[0][i] = share_random[i] & (rlwe_coeff_t)RLWE_Q_MASK;
        sk->share[1][i] = submod(secret_bit, sk->share[0][i]);
    }
#else
    sample_binary(sk->s);
#endif

    sample_error(r1);           /* paper r1: CBD0 or optional CBD1 */

    /* Use optimized binary multiplication since s is binary {0,1}^n */
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    rlwe_poly_mul(ar0, pk->a, sk->share[0]);
    rlwe_poly_mul(ar1, pk->a, sk->share[1]);
    rlwe_poly_add(ar2, ar0, ar1);
#else
    rlwe_poly_mul_binary(ar2, pk->a, sk->s);
#endif
    rlwe_poly_sub(pk->p, r1, ar2);            /* p = r1 - a*r2 */

    /* Erase sensitive temporaries from the stack */
    secure_wipe(r1, sizeof(r1));
    secure_wipe(ar2, sizeof(ar2));
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    secure_wipe(ar0, sizeof(ar0));
    secure_wipe(ar1, sizeof(ar1));
    secure_wipe(binary_coins, sizeof(binary_coins));
    secure_wipe(share_random, sizeof(share_random));
#endif
    return 0;
}
