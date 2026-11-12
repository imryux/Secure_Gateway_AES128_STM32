/**
 * @file    bsp_w5500.h
 * @brief   W5500 dual-chip hardware driver with mutex-protected interface.
 * @version 1.0
 * @date    2026-11-08
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

/** W5500 Common Register addresses */
#define W5500_REG_PHYCFGR   (0x002EU)
#define W5500_REG_VERSIONR  (0x0039U)

/** Expected version constant of W5500 silicon */
#define W5500_EXPECTED_VERSION  (0x04U)

/**
 * @brief  Initialize W5500 driver, create static mutex, and issue hardware reset.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_init(void);

/**
 * @brief  Lock the shared W5500 bus mutex and assert CS for selected chip.
 * @param[in] chip_id Target module.
 * @param[in] timeout_ms Maximum time to wait for mutex.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_acquire(
    bsp_w5500_id_t chip_id,
    uint32_t timeout_ms);

/**
 * @brief  Deassert CS and release the shared W5500 bus mutex.
 * @param[in] chip_id Target module.
 * @return APP_STATUS_OK on success.
 */
app_status_t bsp_w5500_release(bsp_w5500_id_t chip_id);

/**
 * @brief  Read a register from the Common Register Block (Block 0).
 * @param[in]  chip_id Target module.
 * @param[in]  reg_addr 16-bit register offset.
 * @param[out] p_value Pointer to store read byte.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_w5500_read_common_reg(
    bsp_w5500_id_t chip_id,
    uint16_t reg_addr,
    uint8_t *p_value);

/**
 * @brief  Read the hardware chip version (VERSIONR register).
 * @param[in]  chip_id Target module.
 * @param[out] p_version Pointer to store version (0x04 for authentic W5500).
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_w5500_read_version(
    bsp_w5500_id_t chip_id,
    uint8_t *p_version);

/**
 * @brief  Read Ethernet physical link status (from PHYCFGR register).
 * @param[in]  chip_id Target module.
 * @param[out] p_is_link_up true if Ethernet cable is connected with active link.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_w5500_read_phy_link(
    bsp_w5500_id_t chip_id,
    bool *p_is_link_up);

#ifdef __cplusplus
}
#endif

#endif /* BSP_W5500_H */
