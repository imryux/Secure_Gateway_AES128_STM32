/**
 * @file    gateway_tasks.h
 * @brief   Static FreeRTOS tasks and IPC queues for the Secure Gateway pipeline.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef GATEWAY_TASKS_H
#define GATEWAY_TASKS_H

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Create all static queues and FreeRTOS tasks, and bind W5500 EXTI notifications.
 * @return APP_STATUS_OK on success, APP_STATUS_NO_MEMORY if any static creation fails.
 */
app_status_t gateway_tasks_start_all(void);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_TASKS_H */
