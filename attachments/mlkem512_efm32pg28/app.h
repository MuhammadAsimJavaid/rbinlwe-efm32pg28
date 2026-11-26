/***************************************************************************//**
 * @file
 * @brief Top level application function declarations
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/***************************************************************************//**
 * @brief Initialize application and hardware resources
 ******************************************************************************/
void app_init(void);

/***************************************************************************//**
 * @brief Main application process loop (called repeatedly)
 ******************************************************************************/
void app_process_action(void);

/***************************************************************************//**
 * @brief Get 32-bit random number from pre-buffered hardware TRNG pool.
 *
 * @return uint32_t random value
 ******************************************************************************/
uint32_t get_random_number(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
