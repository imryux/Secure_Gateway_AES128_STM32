/**
 * @file    net_service.h
 * @brief   Network initialization and socket state/interrupt test service.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef NET_SERVICE_H
#define NET_SERVICE_H

#include <stdint.h>

#include "app_status.h"
#include "bsp_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Configure static IPv4 settings and open 3 sockets on each W5500 module.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t net_service_init(void);

/**
 * @brief  Poll and service active sockets on a W5500 module (for Step 4 verification).
 * @param[in] chip_id Target W5500 module (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t net_service_service_chip(bsp_w5500_id_t chip_id);

/**
 * @brief  Read and reset the hardware EXTI interrupt counter for a W5500 chip.
 * @param[in] chip_id Target W5500 module.
 * @return Number of EXTI triggers recorded since last query.
 */
uint32_t net_service_fetch_exti_count(bsp_w5500_id_t chip_id);

#ifdef __cplusplus
}
#endif

#endif /* NET_SERVICE_H */
