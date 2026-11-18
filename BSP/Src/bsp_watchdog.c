/**
 * @file    bsp_watchdog.c
 * @brief   Independent Watchdog (IWDG) hardware abstraction implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "bsp_watchdog.h"

#include "iwdg.h"

/**
 * @brief  Refreshes (kicks) the Independent Watchdog (IWDG) hardware counter.
 *         Must be invoked periodically before the counter reaches zero to avoid MCU reset.
 * @return APP_STATUS_OK on success, APP_STATUS_IO_ERROR if HAL refresh fails.
 */
app_status_t bsp_watchdog_refresh(void)
{
    if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}
