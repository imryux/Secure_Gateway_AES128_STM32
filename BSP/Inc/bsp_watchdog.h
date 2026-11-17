/**
 * @file    bsp_watchdog.h
 * @brief   Independent Watchdog (IWDG) hardware abstraction interface.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Refresh (kick) the Independent Watchdog hardware counter.
 * @return APP_STATUS_OK on success, APP_STATUS_IO_ERROR if HAL refresh fails.
 */
app_status_t bsp_watchdog_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_WATCHDOG_H */
