/**
 * @file    app_main.h
 * @brief   Application initialization and startup interface.
 * @version 1.0
 * @date    2026-11-06
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Initialize application BSP, services, and static RTOS tasks.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t app_main_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
