/**
 * @file    bsp_w5500.h
 * @brief   Dual W5500 Ethernet controller port abstraction (wiz_port) with mutex and EXTI support.
 * @version 1.1
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
 * @brief IPv4 static network configuration parameters for a W5500 chip.
 */
typedef struct
{
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t dns[4];
} bsp_w5500_net_cfg_t;

/**
 * @brief ISR callback signature invoked when W5500 asserts INTn falling edge.
 * @param[in] chip_id Chip that generated the interrupt (BSP_W5500_ID_A or BSP_W5500_ID_B).
 */
typedef void (*bsp_w5500_exti_cb_t)(bsp_w5500_id_t chip_id);

/**
 * @brief  Initialize W5500 port mutex, register ioLibrary callbacks, and reset both chips.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_init(void);

/**
 * @brief  Register an ISR-safe callback for W5500 external interrupts (PA2 / PB10).
 * @param[in] callback Function pointer executed inside EXTI ISR context.
 */
void bsp_w5500_register_exti_callback(bsp_w5500_exti_cb_t callback);

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

/**
 * @brief  Write and verify static IPv4 network configuration on a W5500 module.
 * @param[in] chip_id Target W5500 module.
 * @param[in] p_cfg   Pointer to network configuration struct.
 * @return APP_STATUS_OK on success, APP_STATUS_IO_ERROR if readback verification fails.
 */
app_status_t bsp_w5500_configure_network(
    bsp_w5500_id_t chip_id,
    const bsp_w5500_net_cfg_t *p_cfg);

/**
 * @brief  Enable hardware INTn assertion for sockets 0..2 on a W5500 module.
 * @param[in] chip_id Target W5500 module.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_w5500_enable_socket_interrupts(bsp_w5500_id_t chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BSP_W5500_H */
