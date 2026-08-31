#ifndef SECURE_WIPE_H
#define SECURE_WIPE_H

#include <stddef.h>
#include <stdint.h>

static inline void secure_wipe(void *buffer, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)buffer;
    while (length-- != 0u) {
        *p++ = 0u;
    }
}

#endif
