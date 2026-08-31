#ifndef FAULT_INJECTION_TEST_H
#define FAULT_INJECTION_TEST_H

#include <stdint.h>

#include "rlwe_cca2_pke.h"

#ifndef FI_TEST_ENABLE
#define FI_TEST_ENABLE 0
#endif

#define FI_PROTOCOL_MAGIC   0x46495431u
#define FI_PROTOCOL_VERSION 1u

typedef enum {
    FI_COMMAND_IDLE = 0,
    FI_COMMAND_RUN = 1
} fi_command_code_t;

typedef enum {
    FI_OPERATION_DECRYPT_VALID = 1,
    FI_OPERATION_DECRYPT_INVALID = 2,
    FI_OPERATION_ENCRYPT = 3,
    FI_OPERATION_KEYPAIR = 4
} fi_operation_t;

typedef enum {
    FI_STATE_BOOT = 0,
    FI_STATE_READY = 1,
    FI_STATE_RUNNING = 2,
    FI_STATE_DONE = 3,
    FI_STATE_SETUP_ERROR = 4
} fi_state_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t command;
    volatile uint32_t operation;
    volatile uint32_t trial_id;
    volatile uint32_t fault_id;
    volatile uint32_t fault_seed;
} fi_command_block_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t state;
    volatile uint32_t completed_trial_id;
    volatile int32_t api_status;
    volatile uint32_t valid_mask;
    volatile uint32_t output_is_correct;
    volatile uint32_t output_is_rejection;
    volatile uint32_t output_is_candidate;
    volatile uint32_t key_is_all_zero;
    volatile uint32_t secret_hamming_weight;
    volatile uint32_t candidate_differing_bits;
    volatile uint32_t candidate_bits_released;
    volatile uint32_t rejection_bits_retained;
    volatile uint32_t partial_candidate_release;
    uint8_t output[RLWE_MSG_BYTES];
    uint8_t candidate[RLWE_MSG_BYTES];
    uint8_t rejection[RLWE_MSG_BYTES];
} fi_result_block_t;

extern volatile fi_command_block_t fi_command;
extern volatile fi_result_block_t fi_result;

extern rlwe_cca2_pke_pubkey_t fi_public_key;
extern rlwe_cca2_pke_seckey_t fi_secret_key;
extern rlwe_cca2_pke_ciphertext_t fi_valid_ciphertext;
extern rlwe_cca2_pke_ciphertext_t fi_invalid_ciphertext;
extern rlwe_cca2_pke_ciphertext_t fi_working_ciphertext;
extern uint8_t fi_plaintext[RLWE_MSG_BYTES];
extern uint8_t fi_correct_output[RLWE_MSG_BYTES];
extern uint8_t fi_expected_rejection[RLWE_MSG_BYTES];

void fault_injection_test_process(void);

void fi_capture_decrypt_intermediates(const uint8_t candidate[RLWE_MSG_BYTES],
                                      const uint8_t rejection[RLWE_MSG_BYTES],
                                      uint8_t valid_mask);

void fi_trial_ready(void);
void fi_operation_start(void);
void fi_operation_end(void);

#endif /* FAULT_INJECTION_TEST_H */
