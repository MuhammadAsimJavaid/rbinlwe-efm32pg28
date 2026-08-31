/**
 * rlwe_cca2_pke.h - CCA2-secure PKE wrapper for the RLWE CPA core.
 *
 * This mode encrypts caller-provided messages and protects plaintext release
 * with deterministic re-encryption, verification, and implicit rejection.
 */

#ifndef RLWE_CCA2_PKE_H
#define RLWE_CCA2_PKE_H

#include "rlwe_core.h"
#include "encrypt.h"
#include "sl_se_manager.h"
#include <stdint.h>

#ifndef RLWE_CCA2_PKE_ENABLE
#define RLWE_CCA2_PKE_ENABLE 1
#endif

#ifndef RLWE_CCA2_PKE_SE_ORACLES_ENABLE
#define RLWE_CCA2_PKE_SE_ORACLES_ENABLE 1
#endif

#define RLWE_CCA2_PKE_HASH_BYTES      32u
#define RLWE_CCA2_PKE_REJECT_BYTES    32u
#define RLWE_CCA2_PKE_COIN_BYTES      (RLWE_BINARY_COIN_BYTES + \
                                       (2u * RLWE_ERROR_COIN_BYTES))
#define RLWE_CCA2_PKE_CT_BYTES        ((2u * RLWE_N) + \
                                       RLWE_MSG_BYTES + \
                                       RLWE_CCA2_PKE_HASH_BYTES)

typedef struct {
    rlwe_ciphertext_t c12;                  /* deterministic Enc(pk, v) */
    uint8_t c3[RLWE_MSG_BYTES];             /* m XOR G(v, H(pk)) */
    uint8_t c4[RLWE_CCA2_PKE_HASH_BYTES];   /* verification tag */
} rlwe_cca2_pke_ciphertext_t;

typedef struct {
    rlwe_pubkey_t pk;
} rlwe_cca2_pke_pubkey_t;

typedef struct {
    rlwe_seckey_t cpa_sk;
    rlwe_pubkey_t pk;
    uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t z[RLWE_CCA2_PKE_REJECT_BYTES];
} rlwe_cca2_pke_seckey_t;

void rlwe_cca2_pke_init(sl_se_command_context_t *context);

int rlwe_cca2_pke_keypair(rlwe_cca2_pke_pubkey_t *pk,
                          rlwe_cca2_pke_seckey_t *sk);

int rlwe_cca2_pke_encrypt(rlwe_cca2_pke_ciphertext_t *ct,
                          const uint8_t msg[RLWE_MSG_BYTES],
                          const rlwe_cca2_pke_pubkey_t *pk);

int rlwe_cca2_pke_decrypt(uint8_t msg[RLWE_MSG_BYTES],
                          const rlwe_cca2_pke_ciphertext_t *ct,
                          rlwe_cca2_pke_seckey_t *sk);

#endif /* RLWE_CCA2_PKE_H */
