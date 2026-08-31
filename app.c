/***************************************************************************//**
 * @file
 * @brief Constant-time capture application entry points.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/

#include <stdint.h>

#include "app.h"
#include "rng_pool.h"
#include "rlwe_cca2_pke.h"
#include "sl_board_control.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"
#include "sl_iostream_init_instances.h"
#include "sl_se_manager.h"
#include "timing_test.h"
#include "fault_injection_test.h"

static sl_se_command_context_t se_context;
static uint8_t capture_ready;
static uint8_t capture_done;

uint32_t get_random_number(void)
{
    return rng_pool_get_u32();
}

void app_init(void)
{
    /* This UART is capture-only: do not add printf/debug output here. */
    sl_board_enable_vcom();
    sl_iostream_set_default(sl_iostream_vcom_handle);

    /* The generated Silicon Labs component handler initializes SE Manager
     * before app_init(). Calling sl_se_init() again can return an
     * already-initialized status and previously caused this function to exit
     * before the TVLA harness ran. */
    sl_se_init_command_context(&se_context);
    rng_pool_init(&se_context);
    rlwe_cca2_pke_init(&se_context);
    if (rng_pool_fill_full() != 0) {
        static const char error[] = "# error,rng_pool_fill_full\r\n";
        (void)sl_iostream_write(sl_iostream_vcom_handle,
                                error,
                                sizeof(error) - 1u);
        return;
    }

    capture_ready = 1u;
}

void app_process_action(void)
{
    if (capture_ready == 0u || capture_done != 0u) {
        return;
    }

#if FI_TEST_ENABLE
    fault_injection_test_process();
#else
    capture_done = 1u;
    timing_test_run();
#endif
}
