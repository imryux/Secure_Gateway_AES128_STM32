/**
 * @file    bsp_spi.h
 * @brief   Hardware abstraction interface for SPI1 and SPI2 peripherals.
 * @version 1.0
 * @date    2026-11-08
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "bsp_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BSP_SPI_DEFAULT_TIMEOUT_MS (50U)

/**
 * @brief  Transmit and receive a single byte over SPI associated with target W5500.
 * @param[in]  chip_id Target module (BSP_W5500_ID_A for SPI1, BSP_W5500_ID_B for SPI2).
 * @param[in]  tx_byte Byte to transmit.
 * @param[out] p_rx_byte Pointer to store received byte (can be NULL if ignored).
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_spi_transfer_byte(
    bsp_w5500_id_t chip_id,
    uint8_t tx_byte,
    uint8_t *p_rx_byte);

/**
 * @brief  Transmit and receive multiple bytes sequentially over SPI.
 * @param[in]  chip_id Target module.
 * @param[in]  p_tx_data Pointer to transmit buffer (or NULL if sending dummy bytes 0xFF).
 * @param[out] p_rx_data Pointer to receive buffer (or NULL if receiving is ignored).
 * @param[in]  length Number of bytes to transfer.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_spi_transfer_buffer(
    bsp_w5500_id_t chip_id,
    const uint8_t *p_tx_data,
    uint8_t *p_rx_data,
    size_t length);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */
