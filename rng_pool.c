/**
 * rng_pool.c - Pre-buffered hardware TRNG random pool.
 *
 * Random bytes are produced by the Silicon Labs Secure Element TRNG, stored in
 * a small ring buffer, consumed once, and erased from the pool as they are read.
 */

#include "rng_pool.h"
#include "sl_se_manager_entropy.h"
#include <string.h>

#if RLWE_BUFFERED_TRNG
typedef struct {
    uint8_t buf[RNG_POOL_BYTES];
    size_t head;
    size_t tail;
    size_t count;
    sl_se_command_context_t *context;
} rng_pool_t;

static rng_pool_t rng_pool;

static void rng_pool_push_byte(uint8_t v)
{
    rng_pool.buf[rng_pool.tail] = v;
    rng_pool.tail++;
    if (rng_pool.tail == RNG_POOL_BYTES) {
        rng_pool.tail = 0u;
    }
    rng_pool.count++;
}

static uint8_t rng_pool_pop_byte(void)
{
    uint8_t v = rng_pool.buf[rng_pool.head];
    rng_pool.buf[rng_pool.head] = 0u;
    rng_pool.head++;
    if (rng_pool.head == RNG_POOL_BYTES) {
        rng_pool.head = 0u;
    }
    rng_pool.count--;
    return v;
}
#else
static sl_se_command_context_t *rng_context;
#endif

void rng_pool_init(sl_se_command_context_t *context)
{
#if RLWE_BUFFERED_TRNG
    memset(&rng_pool, 0, sizeof(rng_pool));
    rng_pool.context = context;
#else
    rng_context = context;
#endif
}

size_t rng_pool_available(void)
{
#if RLWE_BUFFERED_TRNG
    return rng_pool.count;
#else
    return 0u;
#endif
}

void rng_pool_discard_all(void)
{
#if RLWE_BUFFERED_TRNG
    memset(rng_pool.buf, 0, sizeof(rng_pool.buf));
    rng_pool.head = 0u;
    rng_pool.tail = 0u;
    rng_pool.count = 0u;
#endif
}

int rng_pool_refill_step(void)
{
#if RLWE_BUFFERED_TRNG
    uint8_t rnd[RNG_POOL_REFILL_CHUNK_BYTES];
    size_t free_bytes = RNG_POOL_BYTES - rng_pool.count;
    size_t request = free_bytes;

    if (request > RNG_POOL_REFILL_CHUNK_BYTES) {
        request = RNG_POOL_REFILL_CHUNK_BYTES;
    }
    if (request == 0u) {
        return 0;
    }
    if (rng_pool.context == NULL) {
        return -1;
    }

    sl_status_t status = sl_se_get_random(rng_pool.context, rnd, request);
    if (status != SL_STATUS_OK) {
        memset(rnd, 0, sizeof(rnd));
        return -1;
    }

    for (size_t i = 0; i < request; i++) {
        rng_pool_push_byte(rnd[i]);
    }
    memset(rnd, 0, sizeof(rnd));

    return 0;
#else
    return rng_context == NULL ? -1 : 0;
#endif
}

int rng_pool_fill_to(size_t target_bytes)
{
#if RLWE_BUFFERED_TRNG
    if (target_bytes > RNG_POOL_BYTES
        || (target_bytes % RNG_POOL_REFILL_CHUNK_BYTES) != 0u) {
        return -1;
    }

    while (rng_pool.count < target_bytes) {
        if (rng_pool_refill_step() != 0) {
            return -1;
        }
    }

    return 0;
#else
    (void)target_bytes;
    return rng_context == NULL ? -1 : 0;
#endif
}

int rng_pool_fill_full(void)
{
    return rng_pool_fill_to(RNG_POOL_BYTES);
}

int rng_pool_get_bytes(uint8_t *out, size_t len)
{
    if (out == NULL) {
        return -1;
    }

#if RLWE_BUFFERED_TRNG
    size_t produced = 0u;
    while (produced < len) {
        while (rng_pool.count == 0u) {
            if (rng_pool_refill_step() != 0) {
                memset(out, 0, len);
                return -1;
            }
        }

        size_t take = len - produced;
        if (take > rng_pool.count) {
            take = rng_pool.count;
        }
        for (size_t i = 0; i < take; i++) {
            out[produced + i] = rng_pool_pop_byte();
        }
        produced += take;
    }

    return 0;
#else
    if (rng_context == NULL) {
        memset(out, 0, len);
        return -1;
    }
    sl_status_t status = sl_se_get_random(rng_context, out, len);
    if (status != SL_STATUS_OK) {
        memset(out, 0, len);
        return -1;
    }
    return 0;
#endif
}

uint32_t rng_pool_get_u32(void)
{
    uint8_t bytes[4];

    if (rng_pool_get_bytes(bytes, sizeof(bytes)) != 0) {
        return 0u;
    }

    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}
