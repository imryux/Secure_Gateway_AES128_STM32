/**
 * @file    bsp_spi.h
 * @brief   SPI1 and SPI2 hardware abstraction interface with DMA burst support.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Hardware SPI bus identifier.
 */
typedef enum
{
    BSP_SPI_BUS_1 = 0, /**< SPI1 peripheral (connected to W5500 #1) */
    BSP_SPI_BUS_2 = 1, /**< SPI2 peripheral (connected to W5500 #2) */
    BSP_SPI_BUS_COUNT = 2
} bsp_spi_bus_t;

/**
 * @brief  Initialize static RTOS synchronization objects for SPI DMA transfers.
 * @return APP_STATUS_OK on success, APP_STATUS_NO_MEMORY if semaphore creation fails.
 */
app_status_t bsp_spi_init(void);

/**
 * @brief  Transfer a single byte in full-duplex mode over the specified SPI bus.
 * @param[in]  bus_id  Target SPI bus (BSP_SPI_BUS_1 or BSP_SPI_BUS_2).
 * @param[in]  tx_byte Byte to transmit.
 * @param[out] p_rx_byte Pointer to store the received byte (must not be NULL).
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_spi_transfer_byte(
    bsp_spi_bus_t bus_id,
    uint8_t tx_byte,
    uint8_t *p_rx_byte);

/**
 * @brief  Write a buffer over the specified SPI bus (uses DMA when scheduler is running).
 * @param[in] bus_id Target SPI bus.
 * @param[in] p_data Pointer to transmit buffer.
 * @param[in] length Number of bytes to transmit.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_spi_write_burst(
    bsp_spi_bus_t bus_id,
    const uint8_t *p_data,
    size_t length);

/**
 * @brief  Read a buffer over the specified SPI bus (uses DMA when scheduler is running).
 * @param[in]  bus_id Target SPI bus.
 * @param[out] p_data Pointer to receive buffer.
 * @param[in]  length Number of bytes to receive.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t bsp_spi_read_burst(
    bsp_spi_bus_t bus_id,
    uint8_t *p_data,
    size_t length);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */
