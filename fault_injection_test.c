#include "fault_injection_test.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_compiler.h"
#include "rng_pool.h"
#include "ntt.h"

volatile fi_command_block_t fi_command = {
    FI_PROTOCOL_MAGIC, FI_PROTOCOL_VERSION, FI_COMMAND_IDLE,
    FI_OPERATION_DECRYPT_INVALID, 0u, 0u, 0u
};

volatile fi_result_block_t fi_result = {
    .magic = FI_PROTOCOL_MAGIC,
    .version = FI_PROTOCOL_VERSION,
    .state = FI_STATE_BOOT
};

rlwe_cca2_pke_pubkey_t fi_public_key;
rlwe_cca2_pke_seckey_t fi_secret_key;
rlwe_cca2_pke_ciphertext_t fi_valid_ciphertext;
rlwe_cca2_pke_ciphertext_t fi_invalid_ciphertext;
rlwe_cca2_pke_ciphertext_t fi_working_ciphertext;
uint8_t fi_plaintext[RLWE_MSG_BYTES];
uint8_t fi_correct_output[RLWE_MSG_BYTES];
uint8_t fi_expected_rejection[RLWE_MSG_BYTES];

static rlwe_cca2_pke_pubkey_t generated_public_key;
static rlwe_cca2_pke_seckey_t generated_secret_key;
static rlwe_cca2_pke_seckey_t baseline_secret_key;
static uint8_t encryption_output_message[RLWE_MSG_BYTES];
static uint8_t setup_complete;

__attribute__((noinline, used)) void fi_trial_ready(void)
{
    __COMPILER_BARRIER();
}

__attribute__((noinline, used)) void fi_operation_start(void)
{
    __COMPILER_BARRIER();
}

__attribute__((noinline, used)) void fi_operation_end(void)
{
    __COMPILER_BARRIER();
}

void fi_capture_decrypt_intermediates(const uint8_t candidate[RLWE_MSG_BYTES],
                                      const uint8_t rejection[RLWE_MSG_BYTES],
                                      uint8_t valid_mask)
{
    memcpy((void *)fi_result.candidate, candidate, RLWE_MSG_BYTES);
    memcpy((void *)fi_result.rejection, rejection, RLWE_MSG_BYTES);
    fi_result.valid_mask = valid_mask;
}

static uint32_t bytes_equal(const uint8_t *a, const uint8_t *b, size_t length)
{
    uint32_t diff = 0u;
    for (size_t i = 0u; i < length; ++i) {
        diff |= (uint32_t)(a[i] ^ b[i]);
    }
    return (diff == 0u) ? 1u : 0u;
}

static uint32_t secret_hamming_weight(const rlwe_seckey_t *sk)
{
    uint32_t weight = 0u;
    for (size_t i = 0u; i < RLWE_N; ++i) {
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
        weight += (uint32_t)(addmod(sk->share[0][i], sk->share[1][i]) & 1u);
#else
        weight += (uint32_t)(sk->s[i] & 1u);
#endif
    }
    return weight;
}

static uint32_t popcount_byte(uint8_t value)
{
    uint32_t count = 0u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        count += (uint32_t)((value >> bit) & 1u);
    }
    return count;
}

static void measure_candidate_release(void)
{
    uint32_t differing = 0u;
    uint32_t candidate_released = 0u;
    uint32_t rejection_retained = 0u;

    for (size_t i = 0u; i < RLWE_MSG_BYTES; ++i) {
        uint8_t output = fi_result.output[i];
        uint8_t candidate = fi_result.candidate[i];
        uint8_t rejection = fi_result.rejection[i];
        uint8_t difference = (uint8_t)(candidate ^ rejection);

        /*
         * Only differing candidate/rejection bits carry classification
         * information. Equal bits cannot reveal which source was selected.
         */
        uint8_t candidate_matches =
            (uint8_t)(~(uint8_t)(output ^ candidate) & difference);
        uint8_t rejection_matches =
            (uint8_t)(~(uint8_t)(output ^ rejection) & difference);

        differing += popcount_byte(difference);
        candidate_released += popcount_byte(candidate_matches);
        rejection_retained += popcount_byte(rejection_matches);
    }

    fi_result.candidate_differing_bits = differing;
    fi_result.candidate_bits_released = candidate_released;
    fi_result.rejection_bits_retained = rejection_retained;
    fi_result.partial_candidate_release =
        (candidate_released != 0u && candidate_released < differing) ? 1u : 0u;
}

static void clear_trial_result(void)
{
    fi_result.api_status = 0;
    fi_result.valid_mask = 0u;
    fi_result.output_is_correct = 0u;
    fi_result.output_is_rejection = 0u;
    fi_result.output_is_candidate = 0u;
    fi_result.key_is_all_zero = 0u;
    fi_result.secret_hamming_weight = 0u;
    fi_result.candidate_differing_bits = 0u;
    fi_result.candidate_bits_released = 0u;
    fi_result.rejection_bits_retained = 0u;
    fi_result.partial_candidate_release = 0u;
    memset((void *)fi_result.output, 0, RLWE_MSG_BYTES);
    memset((void *)fi_result.candidate, 0, RLWE_MSG_BYTES);
    memset((void *)fi_result.rejection, 0, RLWE_MSG_BYTES);
}

static int prepare_baseline(void)
{
    for (size_t i = 0u; i < RLWE_MSG_BYTES; ++i) {
        fi_plaintext[i] = (uint8_t)(0xA5u ^ (uint8_t)(29u * i));
    }

    if (rng_pool_fill_full() != 0
        || rlwe_cca2_pke_keypair(&fi_public_key, &fi_secret_key) != 0
        || rng_pool_fill_full() != 0
        || rlwe_cca2_pke_encrypt(&fi_valid_ciphertext,
                                 fi_plaintext,
                                 &fi_public_key) != 0) {
        return -1;
    }
    memcpy(&baseline_secret_key, &fi_secret_key, sizeof(baseline_secret_key));

    memcpy(&fi_invalid_ciphertext,
           &fi_valid_ciphertext,
           sizeof(fi_invalid_ciphertext));
    fi_invalid_ciphertext.c4[0] ^= 1u;

    if (rng_pool_fill_full() != 0
        || rlwe_cca2_pke_decrypt(fi_correct_output,
                                 &fi_valid_ciphertext,
                                 &fi_secret_key) != 0
        || !bytes_equal(fi_correct_output, fi_plaintext, RLWE_MSG_BYTES)
        || rng_pool_fill_full() != 0
        || rlwe_cca2_pke_decrypt(fi_expected_rejection,
                                 &fi_invalid_ciphertext,
                                 &fi_secret_key) != 0) {
        return -1;
    }
    return 0;
}

static int run_selected_operation(uint32_t operation)
{
    int status = -1;

    switch (operation) {
    case FI_OPERATION_DECRYPT_VALID:
        memcpy(&fi_secret_key, &baseline_secret_key, sizeof(fi_secret_key));
        memcpy(&fi_working_ciphertext, &fi_valid_ciphertext,
               sizeof(fi_working_ciphertext));
        if (rng_pool_fill_full() == 0) {
            status = rlwe_cca2_pke_decrypt((uint8_t *)fi_result.output,
                                           &fi_working_ciphertext,
                                           &fi_secret_key);
        }
        break;

    case FI_OPERATION_DECRYPT_INVALID:
        memcpy(&fi_secret_key, &baseline_secret_key, sizeof(fi_secret_key));
        memcpy(&fi_working_ciphertext, &fi_invalid_ciphertext,
               sizeof(fi_working_ciphertext));
        if (rng_pool_fill_full() == 0) {
            status = rlwe_cca2_pke_decrypt((uint8_t *)fi_result.output,
                                           &fi_working_ciphertext,
                                           &fi_secret_key);
        }
        break;

    case FI_OPERATION_ENCRYPT:
        memset(&fi_working_ciphertext, 0, sizeof(fi_working_ciphertext));
        if (rng_pool_fill_full() == 0) {
            status = rlwe_cca2_pke_encrypt(&fi_working_ciphertext,
                                           fi_plaintext, &fi_public_key);
        }
        if (status == 0 && rng_pool_fill_full() == 0) {
            status = rlwe_cca2_pke_decrypt(encryption_output_message,
                                           &fi_working_ciphertext,
                                           &fi_secret_key);
            memcpy((void *)fi_result.output, encryption_output_message,
                   RLWE_MSG_BYTES);
        }
        break;

    case FI_OPERATION_KEYPAIR:
        memset(&generated_public_key, 0, sizeof(generated_public_key));
        memset(&generated_secret_key, 0, sizeof(generated_secret_key));
        if (rng_pool_fill_full() == 0) {
            status = rlwe_cca2_pke_keypair(&generated_public_key,
                                           &generated_secret_key);
        }
        fi_result.secret_hamming_weight =
            secret_hamming_weight(&generated_secret_key.cpa_sk);
        fi_result.key_is_all_zero =
            (fi_result.secret_hamming_weight == 0u) ? 1u : 0u;
        break;

    default:
        break;
    }
    return status;
}

void fault_injection_test_process(void)
{
#if FI_TEST_ENABLE
    if (setup_complete == 0u) {
        if (prepare_baseline() != 0) {
            fi_result.state = FI_STATE_SETUP_ERROR;
            return;
        }
        setup_complete = 1u;
        fi_result.state = FI_STATE_READY;
        fi_trial_ready();
    }

    if (fi_command.command != FI_COMMAND_RUN) {
        return;
    }

    clear_trial_result();
    fi_result.state = FI_STATE_RUNNING;
    fi_operation_start();
    fi_result.api_status = run_selected_operation(fi_command.operation);
    fi_operation_end();

    if (fi_command.operation != FI_OPERATION_KEYPAIR) {
        fi_result.output_is_correct =
            bytes_equal((const uint8_t *)fi_result.output,
                        fi_correct_output, RLWE_MSG_BYTES);
        fi_result.output_is_rejection =
            bytes_equal((const uint8_t *)fi_result.output,
                        (const uint8_t *)fi_result.rejection,
                        RLWE_MSG_BYTES);
        fi_result.output_is_candidate =
            bytes_equal((const uint8_t *)fi_result.output,
                        (const uint8_t *)fi_result.candidate, RLWE_MSG_BYTES);
        measure_candidate_release();
    }

    fi_result.completed_trial_id = fi_command.trial_id;
    fi_result.state = FI_STATE_DONE;
    fi_command.command = FI_COMMAND_IDLE;
    fi_trial_ready();
#endif
}
