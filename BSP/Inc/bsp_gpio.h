/**
 * @file    bsp_gpio.h
 * @brief   GPIO hardware abstraction interface for LED and W5500 control pins.
 * @version 1.0
 * @date    2026-11-07
 */

#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Identifier for the two physical W5500 Ethernet modules.
 */
typedef enum
{
    BSP_W5500_ID_A = 0, /**< W5500 #1 on SPI1 (Network A) */
    BSP_W5500_ID_B = 1, /**< W5500 #2 on SPI2 (Network B) */
    BSP_W5500_COUNT = 2
} bsp_w5500_id_t;

/**
 * @brief  Set the onboard heartbeat LED (PC13) state.
 * @param[in] is_on true to turn LED ON (active-low on Blue Pill), false to turn OFF.
 */
void bsp_gpio_led_write(bool is_on);

/**
 * @brief  Toggle the onboard heartbeat LED (PC13).
 */
void bsp_gpio_led_toggle(void);

/**
 * @brief  Control the Chip Select (SCSn) pin of a W5500 module.
 * @param[in] chip_id     Target W5500 module (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param[in] is_selected true to assert CS (drive LOW), false to deassert CS (drive HIGH).
 * @return APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if chip_id is invalid.
 */
app_status_t bsp_gpio_w5500_cs_write(
    bsp_w5500_id_t chip_id,
    bool is_selected);

/**
 * @brief  Control the Hardware Reset (RSTn) pin of a W5500 module.
 * @param[in] chip_id         Target W5500 module (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param[in] is_reset_active true to assert reset (drive LOW), false to release (drive HIGH).
 * @return APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if chip_id is invalid.
 */
app_status_t bsp_gpio_w5500_rst_write(
    bsp_w5500_id_t chip_id,
    bool is_reset_active);

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
