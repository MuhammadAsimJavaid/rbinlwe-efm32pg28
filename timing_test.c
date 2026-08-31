/**
 * Dudect-style constant-time measurement harness.
 *
 * UART output is deliberately restricted to:
 *   # start,<function_name>,<N>\r\n
 *   <class>,<cycle_delta>\r\n
 *   # end\r\n
 *
 * Class scheduling, random generation, operand construction, RNG-pool refill,
 * validation, and UART transmission are outside the DWT-timed interval.
 */

#include "timing_test.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__)
#define TIMING_NOINLINE __attribute__((noinline))
#else
#define TIMING_NOINLINE
#endif

#include "em_device.h"
#include "em_gpio.h"
#include "decrypt.h"
#include "encrypt.h"
#include "keygen.h"
#include "masked_decode.h"
#include "ntt.h"
#include "rlwe_cca2_pke.h"
#include "rng_pool.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"
#include "sl_iostream_init_instances.h"

#ifndef TIMING_TEST_TRIALS
#define TIMING_TEST_TRIALS 1000u
#endif

#ifndef TVLA_HOST_HANDSHAKE_ENABLE
#define TVLA_HOST_HANDSHAKE_ENABLE 1
#endif

#define TIMING_TEST_ENCRYPT_MUL  1u
#define TIMING_TEST_DECRYPT_MUL  2u
#define TIMING_TEST_CCA2_ENCRYPT 3u
#define TIMING_TEST_CCA2_DECRYPT 4u
#define TIMING_TEST_ROUNDTRIP    5u
#define TIMING_TEST_COMPONENT_CYCLES 6u
#define TIMING_TEST_TVLA_MUL_COEFF   7u
#define TIMING_TEST_TVLA_DECODE_COEFF 8u
#define TIMING_TEST_PD12_DIAGNOSTIC  9u
#define TIMING_TEST_TRNG_OPERATION_CYCLES 10u

/* Dedicated oscilloscope trigger on BRD2506A J102 pin 3 (PD12). */
#define TVLA_TRIGGER_PORT gpioPortD
#define TVLA_TRIGGER_PIN  12u

#ifndef TIMING_TEST_TARGET
#define TIMING_TEST_TARGET TIMING_TEST_CCA2_ENCRYPT
#endif

#if ((TIMING_TEST_TARGET >= TIMING_TEST_ENCRYPT_MUL) && \
     (TIMING_TEST_TARGET <= TIMING_TEST_CCA2_DECRYPT)) || \
    (TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF) || \
    (TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF)
#define TIMING_TEST_USES_CLASS_CAPTURE 1
#else
#define TIMING_TEST_USES_CLASS_CAPTURE 0
#endif

#if (TIMING_TEST_TARGET < TIMING_TEST_ENCRYPT_MUL) || \
    (TIMING_TEST_TARGET > TIMING_TEST_TRNG_OPERATION_CYCLES)
#error "TIMING_TEST_TARGET must be in the range 1..10"
#endif

#if TIMING_TEST_TRIALS < 2u
#error "TIMING_TEST_TRIALS must be at least 2"
#endif

#if (TIMING_TEST_TARGET == TIMING_TEST_COMPONENT_CYCLES) && \
    !RLWE_FULL_MASKED_DECRYPT_ENABLE
#error "Masked component cycle probe requires RLWE_FULL_MASKED_DECRYPT_ENABLE=1"
#endif

#if ((TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT) || \
     (TIMING_TEST_TARGET == TIMING_TEST_CCA2_DECRYPT)) && \
    !RLWE_CCA2_PKE_ENABLE
#error "CCA2 timing targets require RLWE_CCA2_PKE_ENABLE=1"
#endif

#if (TIMING_TEST_TARGET == TIMING_TEST_CCA2_DECRYPT) && \
    RLWE_FULL_MASKED_DECRYPT_ENABLE && \
    ((RLWE_MASKED_DECODE_RANDOM_BYTES + RLWE_N) > RNG_POOL_BYTES)
#error "One masked decryption must fit in RNG_POOL_BYTES without a timed refill"
#endif

#define CLASS_BYTES ((TIMING_TEST_TRIALS + 7u) / 8u)

/* One bit per trial keeps the 100,000-label schedule near 12.5 KiB. */
static volatile uint8_t result_sink;
#if TIMING_TEST_USES_CLASS_CAPTURE
static uint8_t class_bits[CLASS_BYTES];
static uint32_t schedule_prng_state;
#endif

#if TIMING_TEST_USES_CLASS_CAPTURE || \
    (TIMING_TEST_TARGET == TIMING_TEST_PD12_DIAGNOSTIC)
static void tvla_trigger_init(void)
{
    GPIO_PinModeSet(TVLA_TRIGGER_PORT,
                    TVLA_TRIGGER_PIN,
                    gpioModePushPull,
                    0u);
    GPIO_PinOutClear(TVLA_TRIGGER_PORT, TVLA_TRIGGER_PIN);
}
#endif

#if TIMING_TEST_TARGET == TIMING_TEST_COMPONENT_CYCLES
static rlwe_poly component_public;
static rlwe_poly component_share0;
static rlwe_poly component_share1;
static rlwe_poly component_result0;
static rlwe_poly component_result1;
#elif (TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF) || \
      (TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF)
static rlwe_poly component_public;
static rlwe_poly component_secret0;
static rlwe_poly component_secret1;
static rlwe_poly component_result0;
static rlwe_poly component_result1;
static uint8_t component_decode_bit0;
static uint8_t component_decode_bit1;
#elif TIMING_TEST_TARGET <= TIMING_TEST_DECRYPT_MUL
static rlwe_poly fixed_a;
static rlwe_poly fixed_binary_b;
static rlwe_poly random_operand;
static rlwe_poly working_a;
static rlwe_poly working_b;
static rlwe_poly poly_result;
#if TIMING_TEST_TARGET == TIMING_TEST_ENCRYPT_MUL
static uint8_t binary_coins[(RLWE_N + 7u) / 8u];
#endif
#elif TIMING_TEST_TARGET == TIMING_TEST_PD12_DIAGNOSTIC
/* The PD12 diagnostic needs no cryptographic state. */
#elif ((TIMING_TEST_TARGET == TIMING_TEST_ROUNDTRIP) || \
       (TIMING_TEST_TARGET == TIMING_TEST_TRNG_OPERATION_CYCLES)) && \
      !RLWE_CCA2_PKE_ENABLE
static rlwe_pubkey_t pke_public_key;
static rlwe_seckey_t pke_secret_key;
static rlwe_ciphertext_t working_ciphertext;
static uint8_t fixed_message[RLWE_MSG_BYTES];
static uint8_t recovered_message[RLWE_MSG_BYTES];
#else
static rlwe_cca2_pke_pubkey_t pke_public_key;
static rlwe_cca2_pke_seckey_t pke_secret_key;
static rlwe_cca2_pke_ciphertext_t working_ciphertext;
static uint8_t fixed_message[RLWE_MSG_BYTES];
#if TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
static uint8_t random_message[RLWE_MSG_BYTES];
static uint8_t working_message[RLWE_MSG_BYTES];
#elif TIMING_TEST_TARGET == TIMING_TEST_CCA2_DECRYPT
static rlwe_cca2_pke_ciphertext_t valid_ciphertext;
static uint8_t recovered_message[RLWE_MSG_BYTES];
#else
static uint8_t recovered_message[RLWE_MSG_BYTES];
#endif

_Static_assert(sizeof(rlwe_cca2_pke_ciphertext_t) == RLWE_CCA2_PKE_CT_BYTES,
               "CCA2 ciphertext must have no padding for test preparation");
#endif

#if TIMING_TEST_USES_CLASS_CAPTURE
static uint8_t class_get(uint32_t index)
{
    return (uint8_t)((class_bits[index >> 3] >> (index & 7u)) & 1u);
}

static void class_set(uint32_t index, uint8_t value)
{
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    uint8_t *slot = &class_bits[index >> 3];
    *slot = (uint8_t)((*slot & (uint8_t)~mask)
                      | ((uint8_t)(0u - (uint32_t)(value & 1u)) & mask));
}

/* Rejection sampling avoids modulo bias in the Fisher-Yates shuffle. */
static uint32_t schedule_prng_word(void)
{
    uint32_t x = schedule_prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    schedule_prng_state = x;
    return x;
}

static void random_below(uint32_t bound, uint32_t *value)
{
    uint32_t threshold = (uint32_t)(0u - bound) % bound;
    uint32_t random_value;

    do {
        random_value = schedule_prng_word();
    } while (random_value < threshold);

    *value = random_value % bound;
}

static int build_class_schedule(void)
{
    if (rng_pool_get_bytes((uint8_t *)&schedule_prng_state,
                           sizeof(schedule_prng_state)) != 0) {
        return -1;
    }
    if (schedule_prng_state == 0u) {
        schedule_prng_state = 0x6d2b79f5u;
    }

    memset(class_bits, 0, sizeof(class_bits));

    /* Exactly balanced for the default even trial count. */
    for (uint32_t i = TIMING_TEST_TRIALS / 2u;
         i < TIMING_TEST_TRIALS;
         ++i) {
        class_set(i, 1u);
    }

    for (uint32_t i = TIMING_TEST_TRIALS - 1u; i > 0u; --i) {
        uint32_t j;
        random_below(i + 1u, &j);
        uint8_t ci = class_get(i);
        uint8_t cj = class_get(j);
        class_set(i, cj);
        class_set(j, ci);
    }

    return 0;
}
#endif

static void uart_write_all(const char *data, size_t length)
{
    /* One blocking write per complete line prevents firmware-side
     * interleaving. CRLF permits recovery after a damaged fragment. */
    while (length != 0u) {
        sl_status_t status =
            sl_iostream_write(sl_iostream_vcom_handle, data, length);
        if (status == SL_STATUS_OK) {
            data += length;
            length = 0u;
        }
    }
}

#if TIMING_TEST_TARGET != TIMING_TEST_PD12_DIAGNOSTIC
static char *append_u32(char *dst, uint32_t value)
{
    char reverse[10];
    unsigned int count = 0u;

    do {
        reverse[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (count != 0u) {
        *dst++ = reverse[--count];
    }
    return dst;
}
#endif

#if TIMING_TEST_USES_CLASS_CAPTURE
static void uart_write_start(void)
{
    char line[64];
    char *p = line;
#if TIMING_TEST_TARGET == TIMING_TEST_ENCRYPT_MUL
    static const char prefix[] = "# start,encrypt_mul_binary,";
#elif TIMING_TEST_TARGET == TIMING_TEST_DECRYPT_MUL
    static const char prefix[] = "# start,decrypt_mul_binary,";
#elif TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
    static const char prefix[] = "# start,cca2_pke_encrypt,";
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    static const char prefix[] = "# start,tvla_mul_one_coeff_masked,";
#else
    static const char prefix[] = "# start,tvla_mul_one_coeff_unmasked,";
#endif
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    static const char prefix[] = "# start,tvla_decode_one_coeff_masked,";
#else
    static const char prefix[] = "# start,tvla_decode_one_coeff_unmasked,";
#endif
#else
    static const char prefix[] = "# start,cca2_pke_decrypt,";
#endif
    memcpy(p, prefix, sizeof(prefix) - 1u);
    p += sizeof(prefix) - 1u;
    p = append_u32(p, TIMING_TEST_TRIALS);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static int uart_wait_for_host(void)
{
#if TVLA_HOST_HANDSHAKE_ENABLE
    uint8_t token;
    size_t received = 0u;

    while (received == 0u) {
        sl_status_t status =
            sl_iostream_read(sl_iostream_vcom_handle,
                             &token,
                             sizeof(token),
                             &received);
        if (status == SL_STATUS_EMPTY) {
            continue;
        }
        if (status != SL_STATUS_OK) {
            return -1;
        }
    }
#endif
    return 0;
}

static void uart_write_result(uint8_t class_label, uint32_t cycles)
{
    char line[20];
    char *p = line;
    *p++ = (char)('0' + class_label);
    *p++ = ',';
    p = append_u32(p, cycles);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}
#endif

static int dwt_init_and_check(void)
{
    /* Verify DWT access in the same Secure/Non-secure state in which the
     * selected target executes. This project is currently TrustZone-unaware. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    uint32_t before = DWT->CYCCNT;
    for (volatile uint32_t i = 0u; i < 32u; ++i) {
        __NOP();
    }
    return DWT->CYCCNT != before ? 0 : -1;
}

#if TIMING_TEST_TARGET == TIMING_TEST_PD12_DIAGNOSTIC
static void dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}

static void run_pd12_diagnostic(void)
{
    static const char header[] =
        "# start,pd12_diagnostic,1000Hz,25percent\r\n";
    uint32_t high_cycles;
    uint32_t low_cycles;

    tvla_trigger_init();
    if (dwt_init_and_check() != 0) {
        static const char error[] = "# error,dwt_unavailable\r\n";
        uart_write_all(error, sizeof(error) - 1u);
        return;
    }

    SystemCoreClockUpdate();
    high_cycles = SystemCoreClock / 4000u; /* 250 us at 8 MHz. */
    low_cycles = (SystemCoreClock / 1000u) - high_cycles;
    uart_write_all(header, sizeof(header) - 1u);

    for (;;) {
        GPIO_PinOutSet(TVLA_TRIGGER_PORT, TVLA_TRIGGER_PIN);
        dwt_delay_cycles(high_cycles);
        GPIO_PinOutClear(TVLA_TRIGGER_PORT, TVLA_TRIGGER_PIN);
        dwt_delay_cycles(low_cycles);
    }
}
#endif

#if TIMING_TEST_TARGET == TIMING_TEST_TRNG_OPERATION_CYCLES

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
#define BENCH_KEYGEN_RANDOM_BYTES \
    (RLWE_N + RLWE_BINARY_COIN_BYTES + RLWE_N + RLWE_ERROR_COIN_BYTES \
     + (RLWE_CCA2_PKE_ENABLE ? RLWE_CCA2_PKE_REJECT_BYTES : 0u))
#define BENCH_DECRYPT_RANDOM_BYTES (RLWE_N + RLWE_MASKED_DECODE_RANDOM_BYTES)
#else
#define BENCH_KEYGEN_RANDOM_BYTES \
    (RLWE_N + RLWE_BINARY_COIN_BYTES + RLWE_ERROR_COIN_BYTES \
     + (RLWE_CCA2_PKE_ENABLE ? RLWE_CCA2_PKE_REJECT_BYTES : 0u))
#define BENCH_DECRYPT_RANDOM_BYTES 0u
#endif

#if RLWE_CCA2_PKE_ENABLE
#define BENCH_ENCRYPT_RANDOM_BYTES RLWE_MSG_BYTES
#else
#define BENCH_ENCRYPT_RANDOM_BYTES \
    (RLWE_BINARY_COIN_BYTES + (2u * RLWE_ERROR_COIN_BYTES))
#endif

_Static_assert((BENCH_KEYGEN_RANDOM_BYTES % RNG_POOL_REFILL_CHUNK_BYTES) == 0u,
               "key-generation randomness must use complete refill chunks");
_Static_assert((BENCH_ENCRYPT_RANDOM_BYTES % RNG_POOL_REFILL_CHUNK_BYTES) == 0u,
               "encryption randomness must use complete refill chunks");
_Static_assert((BENCH_DECRYPT_RANDOM_BYTES % RNG_POOL_REFILL_CHUNK_BYTES) == 0u,
               "decryption randomness must use complete refill chunks");
_Static_assert(BENCH_KEYGEN_RANDOM_BYTES <= RNG_POOL_BYTES,
               "key-generation randomness must fit in the pool");
_Static_assert(BENCH_ENCRYPT_RANDOM_BYTES <= RNG_POOL_BYTES,
               "encryption randomness must fit in the pool");
_Static_assert(BENCH_DECRYPT_RANDOM_BYTES <= RNG_POOL_BYTES,
               "decryption randomness must fit in the pool");

typedef enum {
    BENCH_OPERATION_KEYGEN = 0,
    BENCH_OPERATION_ENCRYPT = 1,
    BENCH_OPERATION_DECRYPT = 2
} bench_operation_t;

typedef enum {
    BENCH_MODE_BUFFERED = 0,
    BENCH_MODE_TRNG_ONLY = 1,
    BENCH_MODE_COLD = 2,
    BENCH_MODE_NATIVE = 3
} bench_mode_t;

static const char *benchmark_stage_name(void)
{
#if RLWE_COMBINED_BENCHMARK
#if !RLWE_PACKED_ARITHMETIC && !RLWE_PACKED_CODEC && !RLWE_BUFFERED_TRNG
    return "scalar_baseline";
#elif RLWE_PACKED_ARITHMETIC && !RLWE_PACKED_CODEC && !RLWE_BUFFERED_TRNG
    return "packed_arithmetic";
#elif RLWE_PACKED_ARITHMETIC && RLWE_PACKED_CODEC && !RLWE_BUFFERED_TRNG
    return "packed_codec";
#elif !RLWE_BRANCH_FREE_MUL_BINARY && !RLWE_CCA2_PKE_ENABLE && \
      !RLWE_FULL_MASKED_DECRYPT_ENABLE
    return "cpa_baseline";
#elif RLWE_BRANCH_FREE_MUL_BINARY && !RLWE_CCA2_PKE_ENABLE && \
      !RLWE_FULL_MASKED_DECRYPT_ENABLE
    return "cpa_branch_free";
#elif RLWE_BRANCH_FREE_MUL_BINARY && RLWE_CCA2_PKE_ENABLE && \
      !RLWE_FULL_MASKED_DECRYPT_ENABLE
    return "cca2_unmasked";
#elif RLWE_BRANCH_FREE_MUL_BINARY && RLWE_CCA2_PKE_ENABLE && \
      RLWE_FULL_MASKED_DECRYPT_ENABLE
    return "cca2_masked";
#else
    return "unsupported_combined_stage";
#endif
#elif RLWE_CCA2_PKE_ENABLE && RLWE_FULL_MASKED_DECRYPT_ENABLE
    return "cca2_masked";
#elif RLWE_CCA2_PKE_ENABLE
    return "cca2_unmasked";
#elif RLWE_BRANCH_FREE_MUL_BINARY
    return "cpa_branch_free";
#else
    return "cpa_baseline";
#endif
}

static const char *benchmark_operation_name(bench_operation_t operation)
{
    static const char *const names[] = { "keygen", "encrypt", "decrypt" };
    return names[(unsigned int)operation];
}

static const char *benchmark_mode_name(bench_mode_t mode)
{
    switch (mode) {
        case BENCH_MODE_BUFFERED:
            return "buffered";
        case BENCH_MODE_TRNG_ONLY:
            return "trng_only";
        case BENCH_MODE_COLD:
#if RLWE_COMBINED_BENCHMARK
            return "end_to_end";
#else
            return "cold";
#endif
        case BENCH_MODE_NATIVE:
            return "native";
        default:
            return "invalid";
    }
}

static uint32_t benchmark_se_calls(size_t random_bytes, bench_mode_t mode)
{
#if RLWE_BUFFERED_TRNG
    (void)mode;
    return (uint32_t)(random_bytes / RNG_POOL_REFILL_CHUNK_BYTES);
#else
    (void)mode;
    return (uint32_t)(random_bytes / sizeof(uint32_t));
#endif
}

static size_t benchmark_random_bytes(bench_operation_t operation)
{
    static const size_t bytes[] = {
        BENCH_KEYGEN_RANDOM_BYTES,
        BENCH_ENCRYPT_RANDOM_BYTES,
        BENCH_DECRYPT_RANDOM_BYTES
    };
    return bytes[(unsigned int)operation];
}

static char *append_text(char *out, const char *text)
{
    size_t length = strlen(text);
    memcpy(out, text, length);
    return out + length;
}

static int benchmark_keypair(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_keypair(&pke_public_key, &pke_secret_key);
#else
    return rlwe_keypair(&pke_public_key, &pke_secret_key);
#endif
}

static int benchmark_encrypt(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_encrypt(&working_ciphertext, fixed_message,
                                 &pke_public_key);
#else
    rlwe_encrypt(&working_ciphertext, fixed_message, &pke_public_key);
    return 0;
#endif
}

static int benchmark_decrypt(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_decrypt(recovered_message, &working_ciphertext,
                                 &pke_secret_key);
#else
    return rlwe_decrypt(recovered_message, &working_ciphertext,
                        &pke_secret_key);
#endif
}

static int benchmark_prepare_operation(bench_operation_t operation)
{
    if (operation == BENCH_OPERATION_KEYGEN) {
        return 0;
    }

    rng_pool_discard_all();
    if (rng_pool_fill_full() != 0 || benchmark_keypair() != 0) {
        return -1;
    }

    if (operation == BENCH_OPERATION_DECRYPT) {
        if (rng_pool_fill_full() != 0 || benchmark_encrypt() != 0) {
            return -1;
        }
    }
    return 0;
}

static int benchmark_validate_encryption(void)
{
    memset(recovered_message, 0, sizeof(recovered_message));
    if (rng_pool_fill_full() != 0 || benchmark_decrypt() != 0) {
        return -1;
    }
    return memcmp(fixed_message, recovered_message, RLWE_MSG_BYTES) == 0
           ? 0 : -1;
}

static void uart_write_benchmark_start(void)
{
    static const char columns[] =
        "# parameter_set,stage,operation,mode,trial,random_bytes,se_calls,cycles,status\r\n";
    char line[128];
    char *p = line;
    p = append_text(p, "# start,trng_operation_cycles,");
    p = append_text(p, RLWE_PARAMETER_SET_NAME);
    *p++ = ',';
    p = append_text(p, benchmark_stage_name());
    *p++ = ',';
    p = append_u32(p, TIMING_TEST_TRIALS);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
    uart_write_all(columns, sizeof(columns) - 1u);
}

static void uart_write_benchmark_row(bench_operation_t operation,
                                     bench_mode_t mode,
                                     uint32_t trial,
                                     size_t random_bytes,
                                     uint32_t cycles,
                                     uint32_t status)
{
    char line[160];
    char *p = line;
    p = append_text(p, RLWE_PARAMETER_SET_NAME);
    *p++ = ',';
    p = append_text(p, benchmark_stage_name());
    *p++ = ',';
    p = append_text(p, benchmark_operation_name(operation));
    *p++ = ',';
    p = append_text(p, benchmark_mode_name(mode));
    *p++ = ',';
    p = append_u32(p, trial);
    *p++ = ',';
    p = append_u32(p, (uint32_t)random_bytes);
    *p++ = ',';
    p = append_u32(p, benchmark_se_calls(random_bytes, mode));
    *p++ = ',';
    p = append_u32(p, cycles);
    *p++ = ',';
    p = append_u32(p, status);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void uart_write_benchmark_summary(bench_operation_t operation,
                                         bench_mode_t mode,
                                         uint32_t minimum,
                                         uint32_t maximum,
                                         uint32_t average)
{
    char line[128];
    char *p = line;
    p = append_text(p, "# summary,");
    p = append_text(p, benchmark_operation_name(operation));
    *p++ = ',';
    p = append_text(p, benchmark_mode_name(mode));
    p = append_text(p, ",min,");
    p = append_u32(p, minimum);
    p = append_text(p, ",max,");
    p = append_u32(p, maximum);
    p = append_text(p, ",average,");
    p = append_u32(p, average);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static int benchmark_execute_operation(bench_operation_t operation)
{
    switch (operation) {
        case BENCH_OPERATION_KEYGEN:
            return benchmark_keypair();
        case BENCH_OPERATION_ENCRYPT:
            return benchmark_encrypt();
        case BENCH_OPERATION_DECRYPT:
            memset(recovered_message, 0, sizeof(recovered_message));
            return benchmark_decrypt();
        default:
            return -1;
    }
}

static int benchmark_generate_randomness(size_t random_bytes)
{
#if RLWE_BUFFERED_TRNG
    return random_bytes == 0u ? 0 : rng_pool_fill_to(random_bytes);
#else
    uint8_t word[sizeof(uint32_t)];
    for (size_t offset = 0u; offset < random_bytes; offset += sizeof(word)) {
        if (rng_pool_get_bytes(word, sizeof(word)) != 0) {
            return -1;
        }
        result_sink ^= word[0];
    }
    return 0;
#endif
}

static void run_trng_operation_series(bench_operation_t operation,
                                      bench_mode_t mode)
{
    const size_t random_bytes = benchmark_random_bytes(operation);
    uint64_t sum = 0u;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0u;
    uint32_t completed = 0u;

    for (uint32_t trial = 0u; trial < TIMING_TEST_TRIALS; trial++) {
        uint32_t status = 0u;
        uint32_t cycles = 0u;

        if (mode != BENCH_MODE_TRNG_ONLY
            && benchmark_prepare_operation(operation) != 0) {
            status |= 1u;
        }

        rng_pool_discard_all();
        if (status == 0u && mode == BENCH_MODE_BUFFERED
            && rng_pool_fill_to(random_bytes) != 0) {
            status |= 2u;
        }
#if RLWE_BUFFERED_TRNG
        if (status == 0u && mode == BENCH_MODE_NATIVE
            && random_bytes != 0u
            && rng_pool_fill_to(random_bytes) != 0) {
            status |= 2u;
        }
#endif

        if (status == 0u) {
            uint32_t saved_primask = __get_PRIMASK();
            __disable_irq();
            __DSB();
            __ISB();
            uint32_t start = DWT->CYCCNT;

            int rc;
            if (mode == BENCH_MODE_TRNG_ONLY) {
                rc = benchmark_generate_randomness(random_bytes);
            } else {
                rc = benchmark_execute_operation(operation);
            }

            uint32_t end = DWT->CYCCNT;
            __set_PRIMASK(saved_primask);
            cycles = (random_bytes == 0u && mode == BENCH_MODE_TRNG_ONLY)
                     ? 0u : end - start;
            if (rc != 0) {
                status |= 4u;
            }
        }

        if (status == 0u && mode != BENCH_MODE_TRNG_ONLY) {
            if ((mode == BENCH_MODE_BUFFERED
                 || (mode == BENCH_MODE_NATIVE && RLWE_BUFFERED_TRNG))
                && rng_pool_available() != 0u) {
                status |= 8u;
            }
            if (operation == BENCH_OPERATION_ENCRYPT
                && benchmark_validate_encryption() != 0) {
                status |= 16u;
            } else if (operation == BENCH_OPERATION_DECRYPT
                       && memcmp(fixed_message, recovered_message,
                                 RLWE_MSG_BYTES) != 0) {
                status |= 16u;
            }
        }

        uart_write_benchmark_row(operation, mode, trial, random_bytes,
                                 cycles, status);
        rng_pool_discard_all();
        /* A message mismatch is a measured correctness event, not a timing
         * infrastructure failure.  Keep its cycle sample in the summary. */
        if ((status & ~16u) != 0u) {
            continue;
        }
        if (cycles < minimum) {
            minimum = cycles;
        }
        if (cycles > maximum) {
            maximum = cycles;
        }
        sum += cycles;
        completed++;
    }

    if (completed == 0u) {
        uart_write_benchmark_summary(operation, mode, 0u, 0u, 0u);
    } else {
        uart_write_benchmark_summary(operation, mode, minimum, maximum,
                                     (uint32_t)(sum / completed));
    }
}

static void run_trng_operation_cycle_benchmark(void)
{
    for (size_t i = 0u; i < RLWE_MSG_BYTES; i++) {
        fixed_message[i] = (uint8_t)(0xA5u ^ (uint8_t)(29u * i));
    }
    if (dwt_init_and_check() != 0) {
        uart_write_all("# error,dwt_unavailable\r\n", 25u);
        return;
    }

    uart_write_benchmark_start();
    for (unsigned int operation = BENCH_OPERATION_KEYGEN;
         operation <= BENCH_OPERATION_DECRYPT;
         operation++) {
#if RLWE_COMBINED_BENCHMARK
        run_trng_operation_series((bench_operation_t)operation,
                                  BENCH_MODE_NATIVE);
        run_trng_operation_series((bench_operation_t)operation,
                                  BENCH_MODE_TRNG_ONLY);
        run_trng_operation_series((bench_operation_t)operation,
                                  BENCH_MODE_COLD);
#else
        for (unsigned int mode = BENCH_MODE_BUFFERED;
             mode <= BENCH_MODE_COLD;
             mode++) {
            run_trng_operation_series((bench_operation_t)operation,
                                      (bench_mode_t)mode);
        }
#endif
    }
    uart_write_all("# end\r\n", 7u);
}

#endif /* TIMING_TEST_TRNG_OPERATION_CYCLES */

#if TIMING_TEST_TARGET == TIMING_TEST_COMPONENT_CYCLES
static void uart_write_component_row(uint32_t trial,
                                     uint32_t multiply_cycles,
                                     uint32_t decode_cycles,
                                     uint32_t status)
{
    char line[64];
    char *p = line;
    p = append_u32(p, trial);
    *p++ = ',';
    p = append_u32(p, multiply_cycles);
    *p++ = ',';
    p = append_u32(p, decode_cycles);
    *p++ = ',';
    p = append_u32(p, status);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void uart_write_component_summary(const char *name,
                                         uint32_t minimum,
                                         uint32_t maximum,
                                         uint32_t average)
{
    char line[96];
    char *p = line;
    static const char prefix[] = "# summary,";
    static const char minimum_label[] = ",min,";
    static const char maximum_label[] = ",max,";
    static const char average_label[] = ",average,";
    size_t name_length = strlen(name);

    memcpy(p, prefix, sizeof(prefix) - 1u);
    p += sizeof(prefix) - 1u;
    memcpy(p, name, name_length);
    p += name_length;
    memcpy(p, minimum_label, sizeof(minimum_label) - 1u);
    p += sizeof(minimum_label) - 1u;
    p = append_u32(p, minimum);
    memcpy(p, maximum_label, sizeof(maximum_label) - 1u);
    p += sizeof(maximum_label) - 1u;
    p = append_u32(p, maximum);
    memcpy(p, average_label, sizeof(average_label) - 1u);
    p += sizeof(average_label) - 1u;
    p = append_u32(p, average);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void uart_write_cpu_frequency(void)
{
    char line[32];
    char *p = line;
    static const char prefix[] = "# cpu_hz,";

    SystemCoreClockUpdate();
    memcpy(p, prefix, sizeof(prefix) - 1u);
    p += sizeof(prefix) - 1u;
    p = append_u32(p, SystemCoreClock);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void run_component_cycle_benchmark(void)
{
    static const char setup_error[] =
        "# error,component_setup_failed\r\n";
    static const char start_line[] =
        "# start,masked_component_cycles\r\n";
    static const char columns[] =
        "# trial,mul_one_coefficient_both_shares_cycles,"
        "decode_one_coefficient_cycles,status\r\n";
    uint64_t multiply_sum = 0u;
    uint64_t decode_sum = 0u;
    uint32_t multiply_min = UINT32_MAX;
    uint32_t multiply_max = 0u;
    uint32_t decode_min = UINT32_MAX;
    uint32_t decode_max = 0u;
    uint32_t completed = 0u;

    for (size_t i = 0u; i < RLWE_N; i++) {
        component_public[i] =
            (rlwe_coeff_t)((37u * (uint32_t)i + 19u) & RLWE_Q_MASK);
        component_share0[i] =
            (rlwe_coeff_t)((91u * (uint32_t)i + 73u) & RLWE_Q_MASK);
        component_share1[i] =
            (rlwe_coeff_t)((1u - component_share0[i]) & RLWE_Q_MASK);
    }

    if (dwt_init_and_check() != 0 || rng_pool_fill_full() != 0) {
        uart_write_all(setup_error, sizeof(setup_error) - 1u);
        return;
    }

    uart_write_all(start_line, sizeof(start_line) - 1u);
    uart_write_cpu_frequency();
    uart_write_all(columns, sizeof(columns) - 1u);

    for (uint32_t trial = 0u; trial < TIMING_TEST_TRIALS; trial++) {
        uint32_t status = 0u;
        uint8_t bit0 = 0u;
        uint8_t bit1 = 0u;
        uint32_t saved_primask;
        uint32_t start;
        uint32_t end;
        uint32_t multiply_cycles;
        uint32_t decode_cycles;
        size_t coefficient_index = trial % RLWE_N;

        memset(component_result0, 0, sizeof(component_result0));
        memset(component_result1, 0, sizeof(component_result1));

        saved_primask = __get_PRIMASK();
        __disable_irq();
        __DSB();
        __ISB();
        start = DWT->CYCCNT;
        rlwe_poly_mul_accumulate_coefficient(component_result0,
                                              component_public,
                                              component_share0,
                                              coefficient_index);
        rlwe_poly_mul_accumulate_coefficient(component_result1,
                                              component_public,
                                              component_share1,
                                              coefficient_index);
        end = DWT->CYCCNT;
        __set_PRIMASK(saved_primask);
        multiply_cycles = end - start;

        saved_primask = __get_PRIMASK();
        __disable_irq();
        __DSB();
        __ISB();
        start = DWT->CYCCNT;
        if (rlwe_decode_coefficient_masked(component_result0[0],
                                           component_result1[0], 0u,
                                           &bit0, &bit1) != 0) {
            status = 1u;
        }
        end = DWT->CYCCNT;
        __set_PRIMASK(saved_primask);
        decode_cycles = end - start;
        result_sink ^= (uint8_t)(bit0 ^ bit1);

        if (status != 0u) {
            uart_write_component_row(trial, multiply_cycles,
                                     decode_cycles, status);
            break;
        }

        multiply_sum += multiply_cycles;
        decode_sum += decode_cycles;
        if (multiply_cycles < multiply_min) multiply_min = multiply_cycles;
        if (multiply_cycles > multiply_max) multiply_max = multiply_cycles;
        if (decode_cycles < decode_min) decode_min = decode_cycles;
        if (decode_cycles > decode_max) decode_max = decode_cycles;
        completed++;
        uart_write_component_row(trial, multiply_cycles,
                                 decode_cycles, status);
    }

    if (completed != 0u) {
        uart_write_component_summary("mul_one_coefficient_both_shares",
                                     multiply_min, multiply_max,
                                     (uint32_t)(multiply_sum / completed));
        uart_write_component_summary("decode_one_coefficient",
                                     decode_min, decode_max,
                                     (uint32_t)(decode_sum / completed));
    }
    uart_write_all("# end\r\n", 7u);
}
#endif

#if TIMING_TEST_TARGET == TIMING_TEST_ROUNDTRIP
static void uart_write_roundtrip_row(uint32_t trial,
                                     uint32_t keygen_cycles,
                                     uint32_t encrypt_cycles,
                                     uint32_t decrypt_cycles,
                                     uint32_t status,
                                     uint32_t mismatch_bytes)
{
    char line[96];
    char *p = line;
    p = append_u32(p, trial);
    *p++ = ',';
    p = append_u32(p, keygen_cycles);
    *p++ = ',';
    p = append_u32(p, encrypt_cycles);
    *p++ = ',';
    p = append_u32(p, decrypt_cycles);
    *p++ = ',';
    p = append_u32(p, status);
    *p++ = ',';
    p = append_u32(p, mismatch_bytes);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void uart_write_roundtrip_summary(uint32_t passed, uint32_t failed)
{
    char line[80];
    char *p = line;
    static const char prefix[] = "# summary,tests,";
    static const char passed_label[] = ",passed,";
    static const char failed_label[] = ",failed,";

    memcpy(p, prefix, sizeof(prefix) - 1u);
    p += sizeof(prefix) - 1u;
    p = append_u32(p, TIMING_TEST_TRIALS);
    memcpy(p, passed_label, sizeof(passed_label) - 1u);
    p += sizeof(passed_label) - 1u;
    p = append_u32(p, passed);
    memcpy(p, failed_label, sizeof(failed_label) - 1u);
    p += sizeof(failed_label) - 1u;
    p = append_u32(p, failed);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static void uart_write_roundtrip_averages(uint32_t keygen_average,
                                          uint32_t encrypt_average,
                                          uint32_t decrypt_average)
{
    char line[112];
    char *p = line;
    static const char prefix[] = "# averages,keygen_cycles,";
    static const char encrypt_label[] = ",encrypt_cycles,";
    static const char decrypt_label[] = ",decrypt_cycles,";

    memcpy(p, prefix, sizeof(prefix) - 1u);
    p += sizeof(prefix) - 1u;
    p = append_u32(p, keygen_average);
    memcpy(p, encrypt_label, sizeof(encrypt_label) - 1u);
    p += sizeof(encrypt_label) - 1u;
    p = append_u32(p, encrypt_average);
    memcpy(p, decrypt_label, sizeof(decrypt_label) - 1u);
    p += sizeof(decrypt_label) - 1u;
    p = append_u32(p, decrypt_average);
    *p++ = '\r';
    *p++ = '\n';
    uart_write_all(line, (size_t)(p - line));
}

static uint32_t measure_mismatch_bytes(const uint8_t *a, const uint8_t *b)
{
    uint32_t mismatches = 0u;
    for (size_t i = 0u; i < RLWE_MSG_BYTES; i++) {
        mismatches += (uint32_t)(a[i] != b[i]);
    }
    return mismatches;
}

static int roundtrip_keypair(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_keypair(&pke_public_key, &pke_secret_key);
#else
    return rlwe_keypair(&pke_public_key, &pke_secret_key);
#endif
}

static int roundtrip_encrypt(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_encrypt(&working_ciphertext, fixed_message,
                                 &pke_public_key);
#else
    rlwe_encrypt(&working_ciphertext, fixed_message, &pke_public_key);
    return 0;
#endif
}

static int roundtrip_decrypt(void)
{
#if RLWE_CCA2_PKE_ENABLE
    return rlwe_cca2_pke_decrypt(recovered_message, &working_ciphertext,
                                 &pke_secret_key);
#else
    return rlwe_decrypt(recovered_message, &working_ciphertext,
                        &pke_secret_key);
#endif
}

static void run_roundtrip_benchmark(void)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;
    uint64_t keygen_cycle_sum = 0u;
    uint64_t encrypt_cycle_sum = 0u;
    uint64_t decrypt_cycle_sum = 0u;
#if RLWE_CCA2_PKE_ENABLE && RLWE_FULL_MASKED_DECRYPT_ENABLE
    static const char start_prefix[] = "# start,cca2_masked_roundtrip,";
#elif RLWE_CCA2_PKE_ENABLE
    static const char start_prefix[] = "# start,cca2_unmasked_roundtrip,";
#elif RLWE_BRANCH_FREE_MUL_BINARY
    static const char start_prefix[] = "# start,cpa_branch_free_roundtrip,";
#else
    static const char start_prefix[] = "# start,cpa_baseline_roundtrip,";
#endif
    static const char columns[] =
        "# trial,keygen_cycles,encrypt_cycles,decrypt_cycles,status,mismatch_bytes\r\n";
    static const char plaintext_description[] =
        "# plaintext,byte_i=0xA5_xor_(29*i)\r\n";
    char start_line[48];
    char *start_p = start_line;

    for (size_t i = 0u; i < RLWE_MSG_BYTES; i++) {
        fixed_message[i] = (uint8_t)(0xA5u ^ (uint8_t)(29u * i));
    }

    if (dwt_init_and_check() != 0) {
        static const char error[] = "# error,dwt_unavailable\r\n";
        uart_write_all(error, sizeof(error) - 1u);
        return;
    }

    memcpy(start_p, start_prefix, sizeof(start_prefix) - 1u);
    start_p += sizeof(start_prefix) - 1u;
    start_p = append_u32(start_p, TIMING_TEST_TRIALS);
    *start_p++ = '\r';
    *start_p++ = '\n';
    uart_write_all(start_line, (size_t)(start_p - start_line));
    uart_write_all(plaintext_description,
                   sizeof(plaintext_description) - 1u);
    uart_write_all(columns, sizeof(columns) - 1u);

    for (uint32_t trial = 0u; trial < TIMING_TEST_TRIALS; trial++) {
        uint32_t keygen_cycles = 0u;
        uint32_t encrypt_cycles = 0u;
        uint32_t decrypt_cycles = 0u;
        uint32_t status = 0u;
        uint32_t mismatch_bytes;
        uint32_t start;
        uint32_t end;
        uint32_t saved_primask;

        if (rng_pool_fill_full() != 0) {
            status |= 8u;
        } else {
            saved_primask = __get_PRIMASK();
            __disable_irq();
            __DSB();
            __ISB();
            start = DWT->CYCCNT;
            if (roundtrip_keypair() != 0) {
                status |= 1u;
            }
            end = DWT->CYCCNT;
            __set_PRIMASK(saved_primask);
            keygen_cycles = end - start;
        }

        if (status == 0u && rng_pool_fill_full() == 0) {
            saved_primask = __get_PRIMASK();
            __disable_irq();
            __DSB();
            __ISB();
            start = DWT->CYCCNT;
            if (roundtrip_encrypt() != 0) {
                status |= 2u;
            }
            end = DWT->CYCCNT;
            __set_PRIMASK(saved_primask);
            encrypt_cycles = end - start;
        } else if (status == 0u) {
            status |= 8u;
        }

        memset(recovered_message, 0, sizeof(recovered_message));
        if (status == 0u && rng_pool_fill_full() == 0) {
            saved_primask = __get_PRIMASK();
            __disable_irq();
            __DSB();
            __ISB();
            start = DWT->CYCCNT;
            if (roundtrip_decrypt() != 0) {
                status |= 4u;
            }
            end = DWT->CYCCNT;
            __set_PRIMASK(saved_primask);
            decrypt_cycles = end - start;
        } else if (status == 0u) {
            status |= 8u;
        }

        mismatch_bytes = measure_mismatch_bytes(fixed_message,
                                                 recovered_message);
        keygen_cycle_sum += keygen_cycles;
        encrypt_cycle_sum += encrypt_cycles;
        decrypt_cycle_sum += decrypt_cycles;
        if (status == 0u && mismatch_bytes == 0u) {
            passed++;
        } else {
            failed++;
        }
        uart_write_roundtrip_row(trial, keygen_cycles, encrypt_cycles,
                                 decrypt_cycles, status, mismatch_bytes);
    }

    uart_write_roundtrip_summary(passed, failed);
    uart_write_roundtrip_averages(
        (uint32_t)(keygen_cycle_sum / TIMING_TEST_TRIALS),
        (uint32_t)(encrypt_cycle_sum / TIMING_TEST_TRIALS),
        (uint32_t)(decrypt_cycle_sum / TIMING_TEST_TRIALS));
    uart_write_all("# end\r\n", 7u);
}
#endif

#if TIMING_TEST_TARGET == TIMING_TEST_COMPONENT_CYCLES

/* The component-cycle probe prepares and measures its operands directly in
 * run_component_cycle_benchmark(). */

#elif (TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF) || \
      (TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF)

static void prepare_component_public(void)
{
    for (size_t i = 0u; i < RLWE_N; i++) {
        component_public[i] =
            (rlwe_coeff_t)((37u * (uint32_t)i + 19u) & RLWE_Q_MASK);
    }
}

static int prepare_target_state(void)
{
    prepare_component_public();
    memset(component_secret0, 0, sizeof(component_secret0));
    memset(component_secret1, 0, sizeof(component_secret1));
    memset(component_result0, 0, sizeof(component_result0));
    memset(component_result1, 0, sizeof(component_result1));
    return rng_pool_fill_full();
}

static int prepare_trial(uint32_t trial, uint8_t class_label)
{
    uint8_t random_bytes[3];
    uint8_t class_mask = (uint8_t)(0u - (uint32_t)(class_label & 1u));
    uint8_t fixed_value;
    uint8_t random_value;
    uint8_t selected_value;
    (void)trial;

    /* Leave enough buffered randomness for the five-byte masked decoder
     * call so that hardware refilling can never occur while PD12 is high. */
    if (rng_pool_available() < 8u && rng_pool_fill_full() != 0) {
        return -1;
    }
    if (rng_pool_get_bytes(random_bytes, sizeof(random_bytes)) != 0) {
        return -1;
    }

#if TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF
    fixed_value = 1u;
    random_value = (uint8_t)(random_bytes[0] & 1u);
#else
    fixed_value = (uint8_t)RLWE_QHALF;
    random_value = (uint8_t)(random_bytes[0] & RLWE_Q_MASK);
#endif
    selected_value =
        (uint8_t)((fixed_value & (uint8_t)~class_mask) |
                  (random_value & class_mask));

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    component_secret0[0] =
        (rlwe_coeff_t)(random_bytes[1] & RLWE_Q_MASK);
    component_secret1[0] =
        (rlwe_coeff_t)(((uint32_t)selected_value -
                        component_secret0[0]) & RLWE_Q_MASK);
#else
    component_secret0[0] = (rlwe_coeff_t)selected_value;
#endif
    memset(component_result0, 0, sizeof(component_result0));
    memset(component_result1, 0, sizeof(component_result1));
    return 0;
}

static int warm_target(void)
{
    if (prepare_trial(0u, 0u) != 0) {
        return -1;
    }
#if TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    rlwe_poly_mul_accumulate_coefficient(component_result0,
                                          component_public,
                                          component_secret0, 0u);
    rlwe_poly_mul_accumulate_coefficient(component_result1,
                                          component_public,
                                          component_secret1, 0u);
#else
    rlwe_poly_mul_binary_accumulate_coefficient(component_result0,
                                                 component_public,
                                                 component_secret0, 0u);
#endif
#else
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    if (rlwe_decode_coefficient_masked(component_secret0[0],
                                       component_secret1[0], 0u,
                                       &component_decode_bit0,
                                       &component_decode_bit1) != 0) {
        return -1;
    }
#else
    component_decode_bit0 = rlwe_decode_coefficient_unmasked(
        component_secret0[0], 0u);
    component_decode_bit1 = 0u;
#endif
#endif
    return 0;
}

#elif TIMING_TEST_TARGET <= TIMING_TEST_DECRYPT_MUL

static void prepare_fixed_polynomials(void)
{
    for (uint32_t i = 0u; i < RLWE_N; ++i) {
        fixed_a[i] = (rlwe_coeff_t)((37u * i + 19u) & RLWE_Q_MASK);
        fixed_binary_b[i] = (rlwe_coeff_t)((i * 13u + 5u) & 1u);
    }
}

static int prepare_random_polynomial(void)
{
#if TIMING_TEST_TARGET == TIMING_TEST_ENCRYPT_MUL
    if (rng_pool_get_bytes(binary_coins, sizeof(binary_coins)) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < RLWE_N; ++i) {
        random_operand[i] =
            (rlwe_coeff_t)((binary_coins[i >> 3] >> (i & 7u)) & 1u);
    }
#else
    if (rng_pool_get_bytes(random_operand, sizeof(random_operand)) != 0) {
        return -1;
    }
#if RLWE_Q < 256u
    for (uint32_t i = 0u; i < RLWE_N; ++i) {
        random_operand[i] &= (rlwe_coeff_t)RLWE_Q_MASK;
    }
#endif
#endif
    return 0;
}

static void prepare_working_polynomials(uint8_t class_label)
{
    rlwe_coeff_t mask =
        (rlwe_coeff_t)(0u - (uint32_t)(class_label & 1u));

    for (uint32_t i = 0u; i < RLWE_N; ++i) {
#if TIMING_TEST_TARGET == TIMING_TEST_ENCRYPT_MUL
        working_a[i] = fixed_a[i];
        working_b[i] =
            (rlwe_coeff_t)((fixed_binary_b[i] & (rlwe_coeff_t)~mask)
                           | (random_operand[i] & mask));
#else
        working_a[i] =
            (rlwe_coeff_t)((fixed_a[i] & (rlwe_coeff_t)~mask)
                           | (random_operand[i] & mask));
        working_b[i] = fixed_binary_b[i];
#endif
    }
}

static int prepare_target_state(void)
{
    prepare_fixed_polynomials();
    if (prepare_random_polynomial() != 0) {
        return -1;
    }
    prepare_working_polynomials(0u);
    return 0;
}

static int prepare_trial(uint32_t trial, uint8_t class_label)
{
    (void)trial;
    if (prepare_random_polynomial() != 0) {
        return -1;
    }
    prepare_working_polynomials(class_label);
    return 0;
}

static int warm_target(void)
{
    rlwe_poly_mul_binary(poly_result, working_a, working_b);
    result_sink ^= poly_result[0];
    return 0;
}

#elif (TIMING_TEST_TARGET == TIMING_TEST_ROUNDTRIP) || \
      (TIMING_TEST_TARGET == TIMING_TEST_PD12_DIAGNOSTIC) || \
      (TIMING_TEST_TARGET == TIMING_TEST_TRNG_OPERATION_CYCLES)

/* The round-trip target performs all preparation and measurement directly in
 * run_roundtrip_benchmark(). Keeping it out of the complete CCA2 target block
 * allows target 5 to select the CPA implementation at compile time. */

#else /* complete CCA2 PKE targets */

static int prepare_target_state(void)
{
    memset(fixed_message, 0, sizeof(fixed_message));

    if (rng_pool_fill_full() != 0
        || rlwe_cca2_pke_keypair(&pke_public_key, &pke_secret_key) != 0) {
        return -1;
    }

#if TIMING_TEST_TARGET == TIMING_TEST_CCA2_DECRYPT
    /* The fixed valid ciphertext is constructed before capture. */
    if (rng_pool_fill_full() != 0
        || rlwe_cca2_pke_encrypt(&valid_ciphertext,
                                 fixed_message,
                                 &pke_public_key) != 0) {
        return -1;
    }
#endif

    return 0;
}

#if TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
static int prepare_trial(uint32_t trial, uint8_t class_label)
{
    (void)trial;
    uint8_t mask = (uint8_t)(0u - (uint32_t)(class_label & 1u));

    /* Generate a random candidate for every class, then select into the same
     * input buffer with identical work and no class-dependent branch. */
    if (rng_pool_get_bytes(random_message, sizeof(random_message)) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < RLWE_MSG_BYTES; ++i) {
        working_message[i] =
            (uint8_t)((fixed_message[i] & (uint8_t)~mask)
                      | (random_message[i] & mask));
    }

    /* Refill synchronously before timing. The complete encryption consumes
     * buffered hidden_v bytes but cannot trigger a hardware refill. */
    return rng_pool_fill_full();
}
#else
static int prepare_trial(uint32_t trial, uint8_t class_label)
{
    uint8_t random_byte;
    uint8_t class_mask = (uint8_t)(0u - (uint32_t)(class_label & 1u));
    size_t corruption_position =
        (size_t)(trial % (uint32_t)RLWE_CCA2_PKE_CT_BYTES);
    const uint8_t *valid = (const uint8_t *)&valid_ciphertext;
    uint8_t *working = (uint8_t *)&working_ciphertext;

    /* Draw a candidate byte for both classes. OR with one ensures that the
     * class-1 mutation is nonzero. */
    if (rng_pool_get_bytes(&random_byte, sizeof(random_byte)) != 0) {
        return -1;
    }
    random_byte |= 1u;

    /* Copy and scan the complete ciphertext for both classes. Only the public
     * trial index chooses a byte position; the class is applied as a mask. */
    for (size_t i = 0u; i < RLWE_CCA2_PKE_CT_BYTES; ++i) {
        uint8_t position_mask =
            (uint8_t)(0u - (uint32_t)(i == corruption_position));
        uint8_t mutation =
            (uint8_t)(random_byte & position_mask & class_mask);
        working[i] = (uint8_t)(valid[i] ^ mutation);
    }

    /* Full masking consumes 6*N buffered bytes: N for key refresh and 5*N
     * for arithmetic-to-Boolean conversion and threshold gadgets. */
    return rng_pool_fill_full();
}
#endif

static int warm_target(void)
{
    if (prepare_trial(0u, 0u) != 0) {
        return -1;
    }
#if TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
    int rc = rlwe_cca2_pke_encrypt(&working_ciphertext,
                                   working_message,
                                   &pke_public_key);
    result_sink ^= working_ciphertext.c4[0];
#else
    int rc = rlwe_cca2_pke_decrypt(recovered_message,
                                   &working_ciphertext,
                                   &pke_secret_key);
    result_sink ^= recovered_message[0];
    if (rc == 0
        && memcmp(recovered_message, fixed_message, RLWE_MSG_BYTES) != 0) {
        rc = -1;
    }
#endif
    return rc;
}

#endif

/* Keep the fixed/random class value out of the caller's live registers while
 * PD12 is high.  This wrapper must be available for every class-capture
 * target, not only the component TVLA targets. */
#if TIMING_TEST_USES_CLASS_CAPTURE
static TIMING_NOINLINE int prepare_scheduled_trial(uint32_t trial)
{
    return prepare_trial(trial, class_get(trial));
}
#endif

void timing_test_run(void)
{
#if TIMING_TEST_TARGET == TIMING_TEST_PD12_DIAGNOSTIC
    run_pd12_diagnostic();
#elif TIMING_TEST_TARGET == TIMING_TEST_TRNG_OPERATION_CYCLES
    run_trng_operation_cycle_benchmark();
#elif TIMING_TEST_TARGET == TIMING_TEST_COMPONENT_CYCLES
    run_component_cycle_benchmark();
#elif TIMING_TEST_TARGET == TIMING_TEST_ROUNDTRIP
    run_roundtrip_benchmark();
#else
    tvla_trigger_init();

    if (build_class_schedule() != 0) {
        return;
    }

    if (prepare_target_state() != 0) {
        return;
    }

    if (dwt_init_and_check() != 0) {
        return;
    }

    if (warm_target() != 0) {
        return;
    }

    uart_write_start();

    for (uint32_t trial = 0u; trial < TIMING_TEST_TRIALS; ++trial) {
        int target_status = 0;

        if (uart_wait_for_host() != 0) {
            break;
        }

        if (prepare_scheduled_trial(trial) != 0) {
            break;
        }

        /* Preserve the caller's interrupt state. ISR latency cannot
         * contaminate the selected complete target call. */
        uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();
        __DSB();
        __ISB();
        __COMPILER_BARRIER();

        /* The rising edge arms the oscilloscope. Only the selected target
         * executes while PD12 is high; all TVLA input preparation and UART
         * output remain outside this window. */
        GPIO_PinOutSet(TVLA_TRIGGER_PORT, TVLA_TRIGGER_PIN);
        __DSB();
        __ISB();
        __COMPILER_BARRIER();
        uint32_t start = DWT->CYCCNT;
        __COMPILER_BARRIER();

#if TIMING_TEST_TARGET <= TIMING_TEST_DECRYPT_MUL
        rlwe_poly_mul_binary(poly_result, working_a, working_b);
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
        rlwe_poly_mul_accumulate_coefficient(component_result0,
                                              component_public,
                                              component_secret0, 0u);
        rlwe_poly_mul_accumulate_coefficient(component_result1,
                                              component_public,
                                              component_secret1, 0u);
#else
        rlwe_poly_mul_binary_accumulate_coefficient(component_result0,
                                                     component_public,
                                                     component_secret0, 0u);
#endif
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
        target_status = rlwe_decode_coefficient_masked(
            component_secret0[0], component_secret1[0], 0u,
            &component_decode_bit0, &component_decode_bit1);
#else
        component_decode_bit0 = rlwe_decode_coefficient_unmasked(
            component_secret0[0], 0u);
        component_decode_bit1 = 0u;
#endif
#elif TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
        target_status =
            rlwe_cca2_pke_encrypt(&working_ciphertext,
                                  working_message,
                                  &pke_public_key);
#else
        target_status =
            rlwe_cca2_pke_decrypt(recovered_message,
                                  &working_ciphertext,
                                  &pke_secret_key);
#endif

        __COMPILER_BARRIER();
        uint32_t end = DWT->CYCCNT;
        __COMPILER_BARRIER();
        GPIO_PinOutClear(TVLA_TRIGGER_PORT, TVLA_TRIGGER_PIN);
        __set_PRIMASK(saved_primask);

        /* The GDS record extends beyond the falling trigger edge.  Keep the
         * class-dependent UART result outside that record so a TVLA cannot
         * detect the deliberately transmitted ASCII class label. */
        uint32_t guard_start = DWT->CYCCNT;
        uint32_t guard_cycles = SystemCoreClock / 1000u;
        while ((uint32_t)(DWT->CYCCNT - guard_start) < guard_cycles) {
            __NOP();
        }
        __COMPILER_BARRIER();

        if (target_status != 0) {
            break;
        }

#if TIMING_TEST_TARGET <= TIMING_TEST_DECRYPT_MUL
        result_sink ^= poly_result[trial % RLWE_N];
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_MUL_COEFF
        /* Never reconstruct arithmetic shares in the acquisition tail. */
        result_sink = component_result0[trial % RLWE_N];
#elif TIMING_TEST_TARGET == TIMING_TEST_TVLA_DECODE_COEFF
        /* The production full decoder reconstructs only after packing. This
         * component test deliberately retains the Boolean sharing. */
        result_sink = component_decode_bit0;
#elif TIMING_TEST_TARGET == TIMING_TEST_CCA2_ENCRYPT
        result_sink ^=
            working_ciphertext.c4[trial % RLWE_CCA2_PKE_HASH_BYTES];
#else
        result_sink ^= recovered_message[trial % RLWE_MSG_BYTES];
#endif
        uart_write_result(class_get(trial), end - start);
    }

    uart_write_all("# end\r\n", 7u);
#endif
}
