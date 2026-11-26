/***************************************************************************//**
 * @file
 * @brief Bare-metal FIPS 203 ML-KEM-512 (PQClean clean) cycle benchmark.
 ******************************************************************************/

#include "mlkem_bench.h"

#include "api.h"
#include "psa/crypto.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"

#include <stdio.h>
#include <string.h>

#define MLKEM_BENCH_TRIALS 100u

#ifndef DWT
#include "core_cm33.h"
#endif

/* Large KEM buffers are static to keep stack usage safe on bare metal. */
static uint8_t s_pk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t s_sk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t s_ct[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t s_ss_enc[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint8_t s_ss_dec[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint8_t s_ct_bad[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t s_ss_bad[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint32_t s_keygen_cycles[MLKEM_BENCH_TRIALS];
static uint32_t s_encap_cycles[MLKEM_BENCH_TRIALS];
static uint32_t s_decap_cycles[MLKEM_BENCH_TRIALS];

static void uart_print(const char *text)
{
  (void)sl_iostream_write(sl_iostream_vcom_handle, text, strlen(text));
}

static int dwt_init_and_check(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  __DSB();
  __ISB();

  uint32_t before = DWT->CYCCNT;
  for (volatile uint32_t i = 0u; i < 32u; ++i) {
    __NOP();
  }
  return (DWT->CYCCNT != before) ? 0 : -1;
}

static void summarize(const uint32_t *samples,
                      uint32_t count,
                      uint32_t *min_out,
                      uint32_t *avg_out)
{
  uint32_t min_v = samples[0];
  uint64_t sum = 0u;
  for (uint32_t i = 0u; i < count; ++i) {
    if (samples[i] < min_v) {
      min_v = samples[i];
    }
    sum += samples[i];
  }
  *min_out = min_v;
  *avg_out = (uint32_t)(sum / (uint64_t)count);
}

static int secrets_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
  uint8_t diff = 0u;
  for (size_t i = 0u; i < n; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0u;
}

void mlkem_bench_run(void)
{
  char line[160];
  psa_status_t psa_status;

  uart_print("MLKEM512,PQClean,FIPS203,clean\r\n");
  uart_print("ALGNAME," PQCLEAN_MLKEM512_CLEAN_CRYPTO_ALGNAME "\r\n");

  uart_print("STEP,psa_crypto_init\r\n");
  psa_status = psa_crypto_init();
  if (psa_status != PSA_SUCCESS) {
    snprintf(line, sizeof(line), "ERROR,psa_crypto_init,%ld\r\n", (long)psa_status);
    uart_print(line);
    return;
  }
  uart_print("STEP,psa_ok\r\n");

  uart_print("STEP,dwt_init\r\n");
  if (dwt_init_and_check() != 0) {
    uart_print("ERROR,dwt_init\r\n");
    return;
  }
  uart_print("STEP,dwt_ok\r\n");

  /* Correctness: one round-trip plus altered-ciphertext rejection check. */
  uart_print("STEP,selftest_keypair\r\n");
  if (PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(s_pk, s_sk) != 0) {
    uart_print("ERROR,keypair\r\n");
    return;
  }
  uart_print("STEP,selftest_enc\r\n");
  if (PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(s_ct, s_ss_enc, s_pk) != 0) {
    uart_print("ERROR,enc\r\n");
    return;
  }
  uart_print("STEP,selftest_dec\r\n");
  if (PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(s_ss_dec, s_ct, s_sk) != 0) {
    uart_print("ERROR,dec\r\n");
    return;
  }
  if (!secrets_equal(s_ss_enc, s_ss_dec, sizeof(s_ss_enc))) {
    uart_print("ERROR,ss_mismatch\r\n");
    return;
  }

  memcpy(s_ct_bad, s_ct, sizeof(s_ct_bad));
  s_ct_bad[0] ^= 0x01u;
  if (PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(s_ss_bad, s_ct_bad, s_sk) != 0) {
    uart_print("ERROR,dec_bad\r\n");
    return;
  }
  if (secrets_equal(s_ss_enc, s_ss_bad, sizeof(s_ss_enc))) {
    uart_print("ERROR,implicit_rejection\r\n");
    return;
  }
  uart_print("SELFTEST,PASS\r\n");

  for (uint32_t trial = 0u; trial < MLKEM_BENCH_TRIALS; ++trial) {
    uint32_t start;
    uint32_t end;

    /* Do not mask interrupts across PSA TRNG-backed calls; SE entropy may
     * need the normal bare-metal service path. */
    start = DWT->CYCCNT;
    (void)PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(s_pk, s_sk);
    end = DWT->CYCCNT;
    s_keygen_cycles[trial] = end - start;

    start = DWT->CYCCNT;
    (void)PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(s_ct, s_ss_enc, s_pk);
    end = DWT->CYCCNT;
    s_encap_cycles[trial] = end - start;

    start = DWT->CYCCNT;
    (void)PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(s_ss_dec, s_ct, s_sk);
    end = DWT->CYCCNT;
    s_decap_cycles[trial] = end - start;

    if (!secrets_equal(s_ss_enc, s_ss_dec, sizeof(s_ss_enc))) {
      uart_print("ERROR,trial_ss_mismatch\r\n");
      return;
    }

    snprintf(line,
             sizeof(line),
             "TRIAL,%lu,keygen,%lu,encap,%lu,decap,%lu\r\n",
             (unsigned long)(trial + 1u),
             (unsigned long)s_keygen_cycles[trial],
             (unsigned long)s_encap_cycles[trial],
             (unsigned long)s_decap_cycles[trial]);
    uart_print(line);
  }

  {
    uint32_t min_v;
    uint32_t avg_v;
    summarize(s_keygen_cycles, MLKEM_BENCH_TRIALS, &min_v, &avg_v);
    snprintf(line,
             sizeof(line),
             "SUMMARY,keygen,min,%lu,avg,%lu,trials,%lu\r\n",
             (unsigned long)min_v,
             (unsigned long)avg_v,
             (unsigned long)MLKEM_BENCH_TRIALS);
    uart_print(line);

    summarize(s_encap_cycles, MLKEM_BENCH_TRIALS, &min_v, &avg_v);
    snprintf(line,
             sizeof(line),
             "SUMMARY,encap,min,%lu,avg,%lu,trials,%lu\r\n",
             (unsigned long)min_v,
             (unsigned long)avg_v,
             (unsigned long)MLKEM_BENCH_TRIALS);
    uart_print(line);

    summarize(s_decap_cycles, MLKEM_BENCH_TRIALS, &min_v, &avg_v);
    snprintf(line,
             sizeof(line),
             "SUMMARY,decap,min,%lu,avg,%lu,trials,%lu\r\n",
             (unsigned long)min_v,
             (unsigned long)avg_v,
             (unsigned long)MLKEM_BENCH_TRIALS);
    uart_print(line);
  }

  uart_print("DONE\r\n");
}
