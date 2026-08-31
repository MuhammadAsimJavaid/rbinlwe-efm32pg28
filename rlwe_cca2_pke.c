/**
 * rlwe_cca2_pke.c - FO-style CCA2 PKE wrapper for caller plaintexts.
 *
 * The construction follows the PKE-oriented idea used in CCA2 Ring-BinLWE:
 * hide a fresh random vector v inside the RLWE ciphertext, mask the caller
 * message with G(v), then re-encrypt during decryption before releasing the
 * recovered message.
 */

#include "rlwe_cca2_pke.h"
#include "keygen.h"
#include "decrypt.h"
#include "rng_pool.h"
#include "sl_se_manager_hash.h"
#include <string.h>

#if FI_TEST_ENABLE
#include "fault_injection_test.h"
#endif

#define PKE_HMAC_BLOCK_BYTES 64u
#define PKE_HMAC_MAX_DATA    128u

static sl_se_command_context_t *pke_se_context;

void rlwe_cca2_pke_init(sl_se_command_context_t *context)
{
    pke_se_context = context;
}

static int pke_sha256(const uint8_t *in, size_t in_len,
                      uint8_t out[RLWE_CCA2_PKE_HASH_BYTES])
{
#if RLWE_CCA2_PKE_SE_ORACLES_ENABLE
    if (pke_se_context == NULL || in_len > (unsigned int)in_len) {
        memset(out, 0, RLWE_CCA2_PKE_HASH_BYTES);
        return -1;
    }

    sl_status_t status = sl_se_hash(pke_se_context,
                                    SL_SE_HASH_SHA256,
                                    in,
                                    (unsigned int)in_len,
                                    out,
                                    RLWE_CCA2_PKE_HASH_BYTES);
    if (status != SL_STATUS_OK) {
        memset(out, 0, RLWE_CCA2_PKE_HASH_BYTES);
        return -1;
    }

    return 0;
#else
    (void)in;
    (void)in_len;
    memset(out, 0, RLWE_CCA2_PKE_HASH_BYTES);
    return -1;
#endif
}

static int pke_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t out[RLWE_CCA2_PKE_HASH_BYTES])
{
    uint8_t k0[PKE_HMAC_BLOCK_BYTES];
    uint8_t inner_key[PKE_HMAC_BLOCK_BYTES];
    uint8_t outer_key[PKE_HMAC_BLOCK_BYTES];
    uint8_t inner_msg[PKE_HMAC_BLOCK_BYTES + PKE_HMAC_MAX_DATA];
    uint8_t outer_msg[PKE_HMAC_BLOCK_BYTES + RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t inner_hash[RLWE_CCA2_PKE_HASH_BYTES];
    int rc = 0;

    if (key_len > PKE_HMAC_BLOCK_BYTES || data_len > PKE_HMAC_MAX_DATA) {
        memset(out, 0, RLWE_CCA2_PKE_HASH_BYTES);
        return -1;
    }

    memset(k0, 0, sizeof(k0));
    memcpy(k0, key, key_len);

    for (size_t i = 0; i < PKE_HMAC_BLOCK_BYTES; i++) {
        inner_key[i] = (uint8_t)(k0[i] ^ 0x36u);
        outer_key[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    memcpy(inner_msg, inner_key, PKE_HMAC_BLOCK_BYTES);
    memcpy(&inner_msg[PKE_HMAC_BLOCK_BYTES], data, data_len);
    rc |= pke_sha256(inner_msg, PKE_HMAC_BLOCK_BYTES + data_len, inner_hash);

    memcpy(outer_msg, outer_key, PKE_HMAC_BLOCK_BYTES);
    memcpy(&outer_msg[PKE_HMAC_BLOCK_BYTES], inner_hash,
           RLWE_CCA2_PKE_HASH_BYTES);
    rc |= pke_sha256(outer_msg, sizeof(outer_msg), out);

    memset(k0, 0, sizeof(k0));
    memset(inner_key, 0, sizeof(inner_key));
    memset(outer_key, 0, sizeof(outer_key));
    memset(inner_msg, 0, sizeof(inner_msg));
    memset(outer_msg, 0, sizeof(outer_msg));
    memset(inner_hash, 0, sizeof(inner_hash));

    return rc;
}

static int pke_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                           const uint8_t *ikm, size_t ikm_len,
                           const uint8_t *info, size_t info_len,
                           uint8_t *out, size_t out_len)
{
    uint8_t prk[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t t[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t msg[RLWE_CCA2_PKE_HASH_BYTES + 32u + 1u];
    size_t produced = 0;
    size_t t_len = 0;
    uint8_t counter = 1u;
    int rc;

    if (info_len > 32u) {
        memset(out, 0, out_len);
        return -1;
    }

    rc = pke_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    while (produced < out_len && rc == 0) {
        size_t take;

        memcpy(msg, t, t_len);
        memcpy(&msg[t_len], info, info_len);
        msg[t_len + info_len] = counter;

        rc = pke_hmac_sha256(prk, sizeof(prk), msg,
                             t_len + info_len + 1u, t);
        if (rc != 0) {
            break;
        }

        take = out_len - produced;
        if (take > RLWE_CCA2_PKE_HASH_BYTES) {
            take = RLWE_CCA2_PKE_HASH_BYTES;
        }
        memcpy(&out[produced], t, take);
        produced += take;
        t_len = RLWE_CCA2_PKE_HASH_BYTES;
        counter++;
    }

    if (rc != 0) {
        memset(out, 0, out_len);
    }

    memset(prk, 0, sizeof(prk));
    memset(t, 0, sizeof(t));
    memset(msg, 0, sizeof(msg));

    return rc;
}

static int pke_hash_pk(const rlwe_pubkey_t *pk,
                       uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES])
{
    uint8_t enc[2u * RLWE_N];

    memcpy(enc, pk->a, RLWE_N);
    memcpy(&enc[RLWE_N], pk->p, RLWE_N);

    return pke_sha256(enc, sizeof(enc), hpk);
}

static int pke_hash_ct(const rlwe_cca2_pke_ciphertext_t *ct,
                       uint8_t hc[RLWE_CCA2_PKE_HASH_BYTES])
{
    static const uint8_t domain[] = "RLWE-PKE-HC-v1";
    uint8_t enc[sizeof(domain) - 1u + RLWE_CCA2_PKE_CT_BYTES];
    size_t off = 0;

    memcpy(&enc[off], domain, sizeof(domain) - 1u);
    off += sizeof(domain) - 1u;
    memcpy(&enc[off], ct->c12.u, RLWE_N);
    off += RLWE_N;
    memcpy(&enc[off], ct->c12.v, RLWE_N);
    off += RLWE_N;
    memcpy(&enc[off], ct->c3, RLWE_MSG_BYTES);
    off += RLWE_MSG_BYTES;
    memcpy(&enc[off], ct->c4, RLWE_CCA2_PKE_HASH_BYTES);

    return pke_sha256(enc, sizeof(enc), hc);
}

static int pke_seed_h(const uint8_t v[RLWE_MSG_BYTES],
                      const uint8_t m[RLWE_MSG_BYTES],
                      const uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES],
                      uint8_t seed[RLWE_CCA2_PKE_HASH_BYTES])
{
    static const uint8_t domain[] = "RLWE-PKE-H-v1";
    uint8_t enc[sizeof(domain) - 1u + RLWE_MSG_BYTES +
                RLWE_MSG_BYTES + RLWE_CCA2_PKE_HASH_BYTES];
    size_t off = 0;

    memcpy(&enc[off], domain, sizeof(domain) - 1u);
    off += sizeof(domain) - 1u;
    memcpy(&enc[off], v, RLWE_MSG_BYTES);
    off += RLWE_MSG_BYTES;
    memcpy(&enc[off], m, RLWE_MSG_BYTES);
    off += RLWE_MSG_BYTES;
    memcpy(&enc[off], hpk, RLWE_CCA2_PKE_HASH_BYTES);

    return pke_sha256(enc, sizeof(enc), seed);
}

static int pke_hh(const uint8_t *in, size_t in_len,
                  const uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES],
                  uint8_t out[RLWE_CCA2_PKE_HASH_BYTES])
{
    static const uint8_t domain[] = "RLWE-PKE-HH-v1";
    uint8_t enc[sizeof(domain) - 1u + RLWE_CCA2_PKE_HASH_BYTES +
                RLWE_CCA2_PKE_HASH_BYTES];
    size_t off = 0;

    if (in_len > RLWE_CCA2_PKE_HASH_BYTES) {
        memset(out, 0, RLWE_CCA2_PKE_HASH_BYTES);
        return -1;
    }

    memcpy(&enc[off], domain, sizeof(domain) - 1u);
    off += sizeof(domain) - 1u;
    memcpy(&enc[off], in, in_len);
    off += in_len;
    memcpy(&enc[off], hpk, RLWE_CCA2_PKE_HASH_BYTES);
    off += RLWE_CCA2_PKE_HASH_BYTES;

    return pke_sha256(enc, off, out);
}

static int pke_mask_g(const uint8_t v[RLWE_MSG_BYTES],
                      const uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES],
                      uint8_t mask[RLWE_MSG_BYTES])
{
    static const uint8_t salt[] = "RLWE-PKE-G-v1";
    static const uint8_t info[] = "mask";
    uint8_t ikm[RLWE_MSG_BYTES + RLWE_CCA2_PKE_HASH_BYTES];
    int rc;

    memcpy(ikm, v, RLWE_MSG_BYTES);
    memcpy(&ikm[RLWE_MSG_BYTES], hpk, RLWE_CCA2_PKE_HASH_BYTES);

    rc = pke_hkdf_sha256(salt, sizeof(salt) - 1u,
                         ikm, sizeof(ikm),
                         info, sizeof(info) - 1u,
                         mask, RLWE_MSG_BYTES);
    memset(ikm, 0, sizeof(ikm));

    return rc;
}

static int pke_expand_coins(const uint8_t seed1[RLWE_CCA2_PKE_HASH_BYTES],
                            const uint8_t seed2[RLWE_CCA2_PKE_HASH_BYTES],
                            const uint8_t seed3[RLWE_CCA2_PKE_HASH_BYTES],
                            uint8_t coins[RLWE_CCA2_PKE_COIN_BYTES])
{
    static const uint8_t salt[] = "RLWE-PKE-COINS-v1";
    static const uint8_t info[] = "coins";
    uint8_t ikm[3u * RLWE_CCA2_PKE_HASH_BYTES];
    int rc;

    memcpy(ikm, seed1, RLWE_CCA2_PKE_HASH_BYTES);
    memcpy(&ikm[RLWE_CCA2_PKE_HASH_BYTES], seed2,
           RLWE_CCA2_PKE_HASH_BYTES);
    memcpy(&ikm[2u * RLWE_CCA2_PKE_HASH_BYTES], seed3,
           RLWE_CCA2_PKE_HASH_BYTES);

    rc = pke_hkdf_sha256(salt, sizeof(salt) - 1u,
                         ikm, sizeof(ikm),
                         info, sizeof(info) - 1u,
                         coins, RLWE_CCA2_PKE_COIN_BYTES);
    memset(ikm, 0, sizeof(ikm));

    return rc;
}

static int pke_tag_c4(const uint8_t v[RLWE_MSG_BYTES],
                      const rlwe_ciphertext_t *c12,
                      const uint8_t c3[RLWE_MSG_BYTES],
                      const uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES],
                      uint8_t c4[RLWE_CCA2_PKE_HASH_BYTES])
{
    static const uint8_t domain[] = "RLWE-PKE-TAG-v1";
    uint8_t enc[sizeof(domain) - 1u + RLWE_MSG_BYTES + (2u * RLWE_N) +
                RLWE_MSG_BYTES + RLWE_CCA2_PKE_HASH_BYTES];
    size_t off = 0;

    memcpy(&enc[off], domain, sizeof(domain) - 1u);
    off += sizeof(domain) - 1u;
    memcpy(&enc[off], v, RLWE_MSG_BYTES);
    off += RLWE_MSG_BYTES;
    memcpy(&enc[off], c12->u, RLWE_N);
    off += RLWE_N;
    memcpy(&enc[off], c12->v, RLWE_N);
    off += RLWE_N;
    memcpy(&enc[off], c3, RLWE_MSG_BYTES);
    off += RLWE_MSG_BYTES;
    memcpy(&enc[off], hpk, RLWE_CCA2_PKE_HASH_BYTES);

    return pke_sha256(enc, sizeof(enc), c4);
}

static void pke_encrypt_from_coins(rlwe_ciphertext_t *ct,
                                   const uint8_t v[RLWE_MSG_BYTES],
                                   const rlwe_pubkey_t *pk,
                                   const uint8_t coins[RLWE_CCA2_PKE_COIN_BYTES])
{
    const uint8_t *r = coins;
    const uint8_t *e1 = &coins[RLWE_BINARY_COIN_BYTES];
    const uint8_t *e2 = &coins[RLWE_BINARY_COIN_BYTES + RLWE_ERROR_COIN_BYTES];

    rlwe_encrypt_deterministic(ct, v, pk, r, e1, e2);
}

__attribute__((noinline))
static uint32_t pke_ct_diff_forward(
    const rlwe_cca2_pke_ciphertext_t *a,
    const rlwe_ciphertext_t *b_c12,
    const uint8_t b_c4[RLWE_CCA2_PKE_HASH_BYTES])
{
    uint32_t diff = 0u;

    for (size_t i = 0; i < RLWE_N; i++) {
        diff |= (uint32_t)(a->c12.u[i] ^ b_c12->u[i]);
        diff |= (uint32_t)(a->c12.v[i] ^ b_c12->v[i]);
    }
    for (size_t i = 0; i < RLWE_CCA2_PKE_HASH_BYTES; i++) {
        diff |= (uint32_t)(a->c4[i] ^ b_c4[i]);
    }
    return diff;
}

__attribute__((noinline))
static uint32_t pke_ct_diff_reverse(
    const rlwe_cca2_pke_ciphertext_t *a,
    const rlwe_ciphertext_t *b_c12,
    const uint8_t b_c4[RLWE_CCA2_PKE_HASH_BYTES])
{
    uint32_t diff = 0u;

    for (size_t i = RLWE_N; i-- > 0u;) {
        diff |= (uint32_t)(a->c12.v[i] ^ b_c12->v[i]);
        diff |= (uint32_t)(a->c12.u[i] ^ b_c12->u[i]);
    }
    for (size_t i = RLWE_CCA2_PKE_HASH_BYTES; i-- > 0u;) {
        diff |= (uint32_t)(a->c4[i] ^ b_c4[i]);
    }
    return diff;
}

static uint8_t pke_diff_equal_mask(uint32_t diff)
{
    uint32_t equal_bit = ((diff | (0u - diff)) >> 31) ^ 1u;
    return (uint8_t)(0u - equal_bit);
}

static void pke_ct_equal_masks(
    const rlwe_cca2_pke_ciphertext_t *a,
    const rlwe_ciphertext_t *b_c12,
    const uint8_t b_c4[RLWE_CCA2_PKE_HASH_BYTES],
    uint8_t *mask_forward,
    uint8_t *mask_reverse)
{
    uint32_t diff_forward = pke_ct_diff_forward(a, b_c12, b_c4);
    uint32_t diff_reverse = pke_ct_diff_reverse(a, b_c12, b_c4);

    *mask_forward = pke_diff_equal_mask(diff_forward);
    *mask_reverse = pke_diff_equal_mask(diff_reverse);
}

__attribute__((noinline))
static int pke_secret_key_is_structurally_valid(const rlwe_seckey_t *sk)
{
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    (void)sk;
    /* The masked representation intentionally has no individually meaningful
     * binary coefficients. Key generation constructs both shares canonically. */
    return 1;
#else
    uint32_t coefficient_or = 0u;
    uint32_t nonbinary_or = 0u;

    for (size_t i = 0u; i < RLWE_N; ++i) {
        uint32_t coefficient = sk->s[i];
        coefficient_or |= coefficient;
        nonbinary_or |= coefficient & ~1u;
    }

    return coefficient_or != 0u && nonbinary_or == 0u;
#endif
}

void pke_select_msg_hardened(uint8_t *out,
                             const uint8_t *valid,
                             const uint8_t *reject,
                             uint32_t mask_forward,
                             uint32_t mask_reverse,
                             size_t length);

static int pke_reject_msg(const uint8_t z[RLWE_CCA2_PKE_REJECT_BYTES],
                          const uint8_t hc[RLWE_CCA2_PKE_HASH_BYTES],
                          uint8_t reject[RLWE_MSG_BYTES])
{
    static const uint8_t salt[] = "RLWE-PKE-REJECT-v1";
    static const uint8_t info[] = "msg";
    uint8_t ikm[RLWE_CCA2_PKE_REJECT_BYTES + RLWE_CCA2_PKE_HASH_BYTES];
    int rc;

    memcpy(ikm, z, RLWE_CCA2_PKE_REJECT_BYTES);
    memcpy(&ikm[RLWE_CCA2_PKE_REJECT_BYTES], hc, RLWE_CCA2_PKE_HASH_BYTES);

    rc = pke_hkdf_sha256(salt, sizeof(salt) - 1u,
                         ikm, sizeof(ikm),
                         info, sizeof(info) - 1u,
                         reject, RLWE_MSG_BYTES);
    memset(ikm, 0, sizeof(ikm));

    return rc;
}

int rlwe_cca2_pke_keypair(rlwe_cca2_pke_pubkey_t *pk,
                          rlwe_cca2_pke_seckey_t *sk)
{
    if (pk == NULL || sk == NULL) {
        return -1;
    }

    if (rlwe_keypair(&pk->pk, &sk->cpa_sk) != 0) {
        memset(pk, 0, sizeof(*pk));
        memset(sk, 0, sizeof(*sk));
        return -1;
    }
    if (!pke_secret_key_is_structurally_valid(&sk->cpa_sk)) {
        memset(pk, 0, sizeof(*pk));
        memset(sk, 0, sizeof(*sk));
        return -1;
    }
    memcpy(&sk->pk, &pk->pk, sizeof(sk->pk));

    if (pke_hash_pk(&sk->pk, sk->hpk) != 0) {
        memset(pk, 0, sizeof(*pk));
        memset(sk, 0, sizeof(*sk));
        return -1;
    }
    if (rng_pool_get_bytes(sk->z, RLWE_CCA2_PKE_REJECT_BYTES) != 0) {
        memset(pk, 0, sizeof(*pk));
        memset(sk, 0, sizeof(*sk));
        return -1;
    }

    return 0;
}

int rlwe_cca2_pke_encrypt(rlwe_cca2_pke_ciphertext_t *ct,
                          const uint8_t msg[RLWE_MSG_BYTES],
                          const rlwe_cca2_pke_pubkey_t *pk)
{
    uint8_t hpk[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t hidden_v[RLWE_MSG_BYTES];
    uint8_t mask[RLWE_MSG_BYTES];
    uint8_t seed1[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t seed2[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t seed3[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t coins[RLWE_CCA2_PKE_COIN_BYTES];
    int rc = 0;

    if (ct == NULL || msg == NULL || pk == NULL) {
        return -1;
    }

    rc |= rng_pool_get_bytes(hidden_v, sizeof(hidden_v));
    rc |= pke_hash_pk(&pk->pk, hpk);
    rc |= pke_seed_h(hidden_v, msg, hpk, seed1);
    rc |= pke_hh(seed1, sizeof(seed1), hpk, seed2);
    rc |= pke_hh(seed2, sizeof(seed2), hpk, seed3);
    rc |= pke_expand_coins(seed1, seed2, seed3, coins);

    pke_encrypt_from_coins(&ct->c12, hidden_v, &pk->pk, coins);

    rc |= pke_mask_g(hidden_v, hpk, mask);
    for (size_t i = 0; i < RLWE_MSG_BYTES; i++) {
        ct->c3[i] = (uint8_t)(msg[i] ^ mask[i]);
    }
    rc |= pke_tag_c4(hidden_v, &ct->c12, ct->c3, hpk, ct->c4);

    memset(hpk, 0, sizeof(hpk));
    memset(hidden_v, 0, sizeof(hidden_v));
    memset(mask, 0, sizeof(mask));
    memset(seed1, 0, sizeof(seed1));
    memset(seed2, 0, sizeof(seed2));
    memset(seed3, 0, sizeof(seed3));
    memset(coins, 0, sizeof(coins));

    if (rc != 0) {
        memset(ct, 0, sizeof(*ct));
        return -1;
    }

    return 0;
}

int rlwe_cca2_pke_decrypt(uint8_t msg[RLWE_MSG_BYTES],
                          const rlwe_cca2_pke_ciphertext_t *ct,
                          rlwe_cca2_pke_seckey_t *sk)
{
    rlwe_ciphertext_t check_c12;
    uint8_t hidden_v[RLWE_MSG_BYTES];
    uint8_t mask[RLWE_MSG_BYTES];
    uint8_t recovered[RLWE_MSG_BYTES];
    uint8_t seed1[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t seed2[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t seed3[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t coins[RLWE_CCA2_PKE_COIN_BYTES];
    uint8_t check_c4[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t hc[RLWE_CCA2_PKE_HASH_BYTES];
    uint8_t reject[RLWE_MSG_BYTES];
    uint8_t valid_mask_forward = 0u;
    uint8_t valid_mask_reverse = 0u;
    int rc = 0;

    if (msg == NULL || ct == NULL || sk == NULL) {
        return -1;
    }

    rc |= pke_hash_ct(ct, hc);
    rc |= rlwe_decrypt(hidden_v, &ct->c12, &sk->cpa_sk);
    rc |= pke_mask_g(hidden_v, sk->hpk, mask);
    for (size_t i = 0; i < RLWE_MSG_BYTES; i++) {
        recovered[i] = (uint8_t)(ct->c3[i] ^ mask[i]);
    }

    rc |= pke_seed_h(hidden_v, recovered, sk->hpk, seed1);
    rc |= pke_hh(seed1, sizeof(seed1), sk->hpk, seed2);
    rc |= pke_hh(seed2, sizeof(seed2), sk->hpk, seed3);
    rc |= pke_expand_coins(seed1, seed2, seed3, coins);
    pke_encrypt_from_coins(&check_c12, hidden_v, &sk->pk, coins);
    rc |= pke_tag_c4(hidden_v, &check_c12, ct->c3, sk->hpk, check_c4);
    rc |= pke_reject_msg(sk->z, hc, reject);

    pke_ct_equal_masks(ct, &check_c12, check_c4,
                       &valid_mask_forward, &valid_mask_reverse);
    if (rc != 0) {
        valid_mask_forward = 0u;
        valid_mask_reverse = 0u;
    }
#if FI_TEST_ENABLE
    fi_capture_decrypt_intermediates(
        recovered, reject,
        (uint8_t)(valid_mask_forward & valid_mask_reverse));
#endif
    pke_select_msg_hardened(msg, recovered, reject,
                            valid_mask_forward, valid_mask_reverse,
                            RLWE_MSG_BYTES);

    memset(&check_c12, 0, sizeof(check_c12));
    memset(hidden_v, 0, sizeof(hidden_v));
    memset(mask, 0, sizeof(mask));
    memset(recovered, 0, sizeof(recovered));
    memset(seed1, 0, sizeof(seed1));
    memset(seed2, 0, sizeof(seed2));
    memset(seed3, 0, sizeof(seed3));
    memset(coins, 0, sizeof(coins));
    memset(check_c4, 0, sizeof(check_c4));
    memset(hc, 0, sizeof(hc));
    memset(reject, 0, sizeof(reject));

    return rc;
}
