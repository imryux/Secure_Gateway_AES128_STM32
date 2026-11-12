/**
 * @file    bsp_spi.c
 * @brief   Hardware abstraction implementation for SPI1 and SPI2 interfacing with W5500 modules.
 * @details Provides board-support hardware abstraction APIs to transfer data over SPI
 *          supporting dual W5500 chip configurations (ID A/B) using blocking (polling) mode.
 * @version 1.0
 * @date    2026-11-08
 */

/* ========================================================================== */
/*                             INCLUDE DIRECTORIES                            */
/* ========================================================================== */

#include "bsp_spi.h"
#include "spi.h"

/* ========================================================================== */
/*                             INTERNAL FUNCTIONS                             */
/* ========================================================================== */

/**
 * @brief  Map a given W5500 chip ID to its corresponding HAL SPI handle.
 * @param  chip_id Target W5500 device identifier (@ref bsp_w5500_id_t).
 * @return Pointer to SPI_HandleTypeDef if valid; NULL otherwise.
 */
static SPI_HandleTypeDef *bsp_spi_get_handle(bsp_w5500_id_t chip_id)
{
    if (chip_id == BSP_W5500_ID_A)
    {
        return &hspi1;  /**< W5500 instance A routed via SPI1 */
    }
    else if (chip_id == BSP_W5500_ID_B)
    {
        return &hspi2;  /**< W5500 instance B routed via SPI2 */
    }

    return NULL;        /**< Unrecognized chip identifier */
}

/* ========================================================================== */
/*                              PUBLIC FUNCTIONS                              */
/* ========================================================================== */

/**
 * @brief  Transmit and receive a single byte over SPI (Full-Duplex 1-byte).
 * @param  chip_id  Target W5500 device identifier.
 * @param  tx_byte  Data byte to transmit.
 * @param  p_rx_byte Pointer to store the received byte (optional, can be NULL).
 * @retval APP_STATUS_OK          Byte transferred successfully.
 * @retval APP_STATUS_INVALID_ARG Invalid chip ID (no SPI handle mapped).
 * @retval APP_STATUS_IO_ERROR     Peripheral hardware error or timeout occurred.
 */
app_status_t bsp_spi_transfer_byte(
    bsp_w5500_id_t chip_id,
    uint8_t tx_byte,
    uint8_t *p_rx_byte)
{
    /* Retrieve the associated SPI instance */
    SPI_HandleTypeDef *hspi = bsp_spi_get_handle(chip_id);
    if (hspi == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    uint8_t rx_data = 0U;

    /* Execute single-byte full-duplex transfer in blocking mode */
    const HAL_StatusTypeDef hal_status = HAL_SPI_TransmitReceive(
        hspi,
        &tx_byte,
        &rx_data,
        1U,
        BSP_SPI_DEFAULT_TIMEOUT_MS);

    if (hal_status != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    /* Output received byte if destination pointer is provided */
    if (p_rx_byte != NULL)
    {
        *p_rx_byte = rx_data;
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Transfer a buffer over SPI with adaptive operation modes:
 *         Full-Duplex (TX & RX), Transmit-only (TX), or Receive-only (RX).
 * @param  chip_id   Target W5500 device identifier.
 * @param  p_tx_data Pointer to transmission buffer (NULL for RX-only mode).
 * @param  p_rx_data Pointer to reception buffer (NULL for TX-only mode).
 * @param  length    Total number of bytes to transfer.
 * @retval APP_STATUS_OK          Buffer transferred successfully.
 * @retval APP_STATUS_INVALID_ARG Zero length, length exceeding UINT16_MAX,
 *                                invalid chip ID, or both buffers are NULL.
 * @retval APP_STATUS_IO_ERROR     Peripheral hardware error or timeout occurred.
 */
app_status_t bsp_spi_transfer_buffer(
    bsp_w5500_id_t chip_id,
    const uint8_t *p_tx_data,
    uint8_t *p_rx_data,
    size_t length)
{
    /* Validate transfer length */
    if (length == 0U)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* SAF-10: Guard against integer truncation before casting to uint16_t */
    if (length > (size_t)UINT16_MAX)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Validate target peripheral */
    SPI_HandleTypeDef *hspi = bsp_spi_get_handle(chip_id);
    if (hspi == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    HAL_StatusTypeDef hal_status;
    const uint16_t transfer_len = (uint16_t)length;

    /* Mode 1: Full-Duplex (Simultaneous Transmit and Receive) */
    if ((p_tx_data != NULL) && (p_rx_data != NULL))
    {
        hal_status = HAL_SPI_TransmitReceive(
            hspi,
            (uint8_t *)p_tx_data,
            p_rx_data,
            transfer_len,
            BSP_SPI_DEFAULT_TIMEOUT_MS);
    }
    /* Mode 2: Transmit-only (MISO data discarded) */
    else if (p_tx_data != NULL)
    {
        hal_status = HAL_SPI_Transmit(
            hspi,
            (uint8_t *)p_tx_data,
            transfer_len,
            BSP_SPI_DEFAULT_TIMEOUT_MS);
    }
    /* Mode 3: Receive-only (generates clock pulses with dummy TX data) */
    else if (p_rx_data != NULL)
    {
        hal_status = HAL_SPI_Receive(
            hspi,
            p_rx_data,
            transfer_len,
            BSP_SPI_DEFAULT_TIMEOUT_MS);
    }
    /* Mode 4: Invalid parameters (both buffers are NULL) */
    else
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Validate transfer result from HAL driver */
    if (hal_status != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}
