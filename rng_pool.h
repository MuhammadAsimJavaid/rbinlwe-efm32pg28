/**
 * rng_pool.h - Pre-buffered hardware TRNG random pool.
 */

#ifndef RNG_POOL_H
#define RNG_POOL_H

#include <stddef.h>
#include <stdint.h>
#include "sl_se_manager.h"

#include "rlwe_core.h"

#ifndef RLWE_BUFFERED_TRNG
#define RLWE_BUFFERED_TRNG 1
#endif

#if ((RLWE_BUFFERED_TRNG != 0) && (RLWE_BUFFERED_TRNG != 1))
#error "RLWE_BUFFERED_TRNG must be 0 or 1"
#endif

#if RLWE_FULL_MASKED_DECRYPT_ENABLE
#define RNG_POOL_BYTES (6u * RLWE_MSG_BITS)
#else
#define RNG_POOL_BYTES 1024u
#endif
#define RNG_POOL_REFILL_CHUNK_BYTES 32u

void rng_pool_init(sl_se_command_context_t *context);
size_t rng_pool_available(void);
void rng_pool_discard_all(void);
int rng_pool_fill_to(size_t target_bytes);
int rng_pool_fill_full(void);
int rng_pool_refill_step(void);
int rng_pool_get_bytes(uint8_t *out, size_t len);
uint32_t rng_pool_get_u32(void);

#endif /* RNG_POOL_H */
