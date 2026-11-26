/***************************************************************************//**
 * @file
 * @brief PQClean randombytes adapter using PSA hardware TRNG.
 ******************************************************************************/

#include "randombytes.h"

#include "psa/crypto.h"

#include <stddef.h>
#include <stdint.h>

int randombytes(uint8_t *output, size_t n)
{
  psa_status_t status;

  if ((output == NULL) && (n > 0u)) {
    return -1;
  }

  status = psa_generate_random(output, n);
  if (status != PSA_SUCCESS) {
    /* PQClean callers ignore the return value; halt instead of continuing. */
    while (1) {
      __asm volatile ("nop");
    }
  }

  return 0;
}
