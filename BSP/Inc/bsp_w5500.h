/**
 * @file    bsp_w5500.h
 * @brief   Dual W5500 Ethernet controller port abstraction (wiz_port) with mutex protection.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef BSP_W5500_H
#define BSP_W5500_H

#include <stdbool.h>
#include <stdint.h>

#include "app_status.h"
#include "bsp_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Expected value of W5500 VERSIONR register (0x0039). */
#define BSP_W5500_EXPECTED_VERSION    (0x04U)

/**
 * @brief  Initialize W5500 port mutex, register ioLibrary callbacks, and reset both chips.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_init(void);

/**
 * @brief  Acquire the global ioLibrary mutex and select the active W5500 chip.
 * @param[in] chip_id    Target W5500 module (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param[in] timeout_ms Maximum wait time in milliseconds to acquire mutex.
 * @return APP_STATUS_OK on success, APP_STATUS_BUSY on timeout.
 */
app_status_t bsp_w5500_lock(
    bsp_w5500_id_t chip_id,
    uint32_t timeout_ms);

/**
 * @brief  Release the global ioLibrary mutex.
 */
void bsp_w5500_unlock(void);

/**
 * @brief  Perform hardware reset sequence on a specific W5500 module.
 * @param[in] chip_id Target W5500 module.
 * @return APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if chip_id is invalid.
 */
app_status_t bsp_w5500_hw_reset(bsp_w5500_id_t chip_id);

/**
 * @brief  Read the chip version register (VERSIONR) of a W5500 module.
 * @param[in]  chip_id   Target W5500 module.
 * @param[out] p_version Pointer to store read version byte (expected 0x04).
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_read_version(
    bsp_w5500_id_t chip_id,
    uint8_t *p_version);

/**
 * @brief  Check physical Ethernet link status (PHYCFGR LNK bit) of a W5500 module.
 * @param[in]  chip_id    Target W5500 module.
 * @param[out] p_link_up  Pointer to store link state (true = UP, false = DOWN).
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_is_link_up(
    bsp_w5500_id_t chip_id,
    bool *p_link_up);

#ifdef __cplusplus
}
#endif

#endif /* BSP_W5500_H */
