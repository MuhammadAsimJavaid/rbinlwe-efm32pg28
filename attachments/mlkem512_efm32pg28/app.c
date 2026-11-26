/***************************************************************************//**
 * @file
 * @brief ML-KEM-512 bare-metal benchmark application entry points.
 ******************************************************************************/

#include "app.h"
#include "mlkem_bench.h"
#include "sl_board_control.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"

static uint8_t bench_done;

void app_init(void)
{
  sl_board_enable_vcom();
  sl_iostream_set_default(sl_iostream_vcom_handle);
  bench_done = 0u;
}

void app_process_action(void)
{
  if (bench_done != 0u) {
    return;
  }
  bench_done = 1u;
  mlkem_bench_run();
}

uint32_t get_random_number(void)
{
  /* Retained for SDK compatibility; ML-KEM uses PSA via randombytes(). */
  return 0u;
}
