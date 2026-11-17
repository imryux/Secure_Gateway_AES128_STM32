/**
 * @file    bsp_spi.c
 * @brief   SPI1 and SPI2 hardware abstraction implementation with DMA support.
 * @version 1.0
 * @date    2026-11-12
 */

#include "bsp_spi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "spi.h"

/** @brief Timeout in milliseconds for single-byte blocking SPI transfers */
#define BSP_SPI_BYTE_TIMEOUT_MS     (10U)

/** @brief Timeout in milliseconds for multi-byte burst transfers (DMA or polling) */
#define BSP_SPI_BURST_TIMEOUT_MS    (50U)

/** @brief Minimum payload size in bytes required to trigger DMA instead of polling */
#define BSP_SPI_DMA_MIN_BYTES       (16U)

/* RTOS-06 & NAM-04: Static binary semaphores for SPI1 and SPI2 DMA completion */
/** @brief Static control blocks for SPI DMA completion semaphores */
static StaticSemaphore_t s_spi_dma_sem_buffer[BSP_SPI_BUS_COUNT];

/** @brief Semaphore handles used to synchronize task execution with DMA ISRs */
static SemaphoreHandle_t s_spi_dma_sem_handle[BSP_SPI_BUS_COUNT] = {NULL, NULL};

/** @brief Error flags set by ISR when a DMA/SPI hardware error occurs */
static volatile bool s_spi_dma_error[BSP_SPI_BUS_COUNT] = {false, false};

/**
 * @brief  Resolves the logical SPI bus ID to its corresponding STM32 HAL handle.
 * @param  bus_id Logical SPI bus identifier.
 * @return Pointer to the SPI_HandleTypeDef instance, or NULL if invalid.
 */
static SPI_HandleTypeDef *bsp_spi_get_handle(bsp_spi_bus_t bus_id)
{
    if (bus_id == BSP_SPI_BUS_1)
    {
        return &hspi1;
    }

    if (bus_id == BSP_SPI_BUS_2)
    {
        return &hspi2;
    }

    return NULL;
}

/**
 * @brief  Signals the waiting task from an ISR upon DMA completion or error.
 * @param  p_hspi    Pointer to the HAL SPI handle that triggered the interrupt.
 * @param  has_error True if the interrupt was caused by an error; false on normal completion.
 */
static void bsp_spi_notify_dma_complete_from_isr(
    const SPI_HandleTypeDef *p_hspi,
    bool has_error)
{
    bsp_spi_bus_t bus_id = BSP_SPI_BUS_COUNT;

    /* Map peripheral instance back to internal bus index */
    if (p_hspi->Instance == SPI1)
    {
        bus_id = BSP_SPI_BUS_1;
    }
    else if (p_hspi->Instance == SPI2)
    {
        bus_id = BSP_SPI_BUS_2;
    }
    else
    {
        return;
    }

    /* Record error status before releasing the semaphore */
    s_spi_dma_error[bus_id] = has_error;

    if (s_spi_dma_sem_handle[bus_id] != NULL)
    {
        BaseType_t higher_priority_task_woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(
            s_spi_dma_sem_handle[bus_id],
            &higher_priority_task_woken);

        /* Perform context switch if a higher-priority task was unblocked */
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
 * @brief  Initializes synchronization primitives for all configured SPI buses.
 * @return APP_STATUS_OK on success, or APP_STATUS_NO_MEMORY if semaphore creation fails.
 */
app_status_t bsp_spi_init(void)
{
    for (uint32_t idx = 0U; idx < (uint32_t)BSP_SPI_BUS_COUNT; idx++)
    {
        s_spi_dma_error[idx] = false;
        s_spi_dma_sem_handle[idx] = xSemaphoreCreateBinaryStatic(&s_spi_dma_sem_buffer[idx]);
        if (s_spi_dma_sem_handle[idx] == NULL)
        {
            return APP_STATUS_NO_MEMORY;
        }
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Performs a full-duplex blocking transfer of a single byte.
 * @param  bus_id    Logical SPI bus identifier.
 * @param  tx_byte   Byte to transmit.
 * @param  p_rx_byte Pointer to store the received byte.
 * @return APP_STATUS_OK on success, or an appropriate error status.
 */
app_status_t bsp_spi_transfer_byte(
    bsp_spi_bus_t bus_id,
    uint8_t tx_byte,
    uint8_t *p_rx_byte)
{
    if (p_rx_byte == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    SPI_HandleTypeDef *p_hspi = bsp_spi_get_handle(bus_id);
    if (p_hspi == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Execute single-byte polling transfer */
    const HAL_StatusTypeDef hal_status = HAL_SPI_TransmitReceive(
        p_hspi,
        &tx_byte,
        p_rx_byte,
        1U,
        BSP_SPI_BYTE_TIMEOUT_MS);

    if (hal_status == HAL_TIMEOUT)
    {
        return APP_STATUS_TIMEOUT;
    }

    if (hal_status != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Transmits a buffer of bytes over SPI, automatically selecting DMA or polling.
 * @note   Uses DMA when the FreeRTOS scheduler is running and length >= BSP_SPI_DMA_MIN_BYTES.
 * @param  bus_id Logical SPI bus identifier.
 * @param  p_data Pointer to the transmit data buffer.
 * @param  length Number of bytes to transmit (1 to UINT16_MAX).
 * @return APP_STATUS_OK on success, or an appropriate error status.
 */
app_status_t bsp_spi_write_burst(
    bsp_spi_bus_t bus_id,
    const uint8_t *p_data,
    size_t length)
{
    if ((p_data == NULL) || (length == 0U) || (length > (size_t)UINT16_MAX))
    {
        return APP_STATUS_INVALID_ARG;
    }

    SPI_HandleTypeDef *p_hspi = bsp_spi_get_handle(bus_id);
    if (p_hspi == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Use DMA only if RTOS scheduler is active and payload meets threshold */
    const bool use_dma = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) &&
                         (s_spi_dma_sem_handle[bus_id] != NULL) &&
                         (length >= BSP_SPI_DMA_MIN_BYTES);

    if (use_dma == true)
    {
        /* Clear any stale semaphore token before starting DMA */
        (void)xSemaphoreTake(s_spi_dma_sem_handle[bus_id], 0U);
        s_spi_dma_error[bus_id] = false;

        /* HAL_SPI_Transmit_DMA accepts non-const pointer in STM32F1 HAL signature */
        const HAL_StatusTypeDef hal_status = HAL_SPI_Transmit_DMA(
            p_hspi,
            (uint8_t *)(uintptr_t)p_data,
            (uint16_t)length);

        if (hal_status != HAL_OK)
        {
            return APP_STATUS_IO_ERROR;
        }

        /* Block current task until DMA ISR signals completion or timeout expires */
        if (xSemaphoreTake(
                s_spi_dma_sem_handle[bus_id],
                pdMS_TO_TICKS(BSP_SPI_BURST_TIMEOUT_MS)) != pdTRUE)
        {
            /* Abort ongoing transfer to reset peripheral state on timeout */
            (void)HAL_SPI_Abort(p_hspi);
            return APP_STATUS_TIMEOUT;
        }

        if (s_spi_dma_error[bus_id] == true)
        {
            return APP_STATUS_IO_ERROR;
        }

        return APP_STATUS_OK;
    }

    /* Fallback to blocking polling transfer for small payloads or pre-scheduler calls */
    const HAL_StatusTypeDef hal_status = HAL_SPI_Transmit(
        p_hspi,
        (uint8_t *)(uintptr_t)p_data,
        (uint16_t)length,
        BSP_SPI_BURST_TIMEOUT_MS);

    if (hal_status == HAL_TIMEOUT)
    {
        return APP_STATUS_TIMEOUT;
    }

    if (hal_status != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Receives a buffer of bytes over SPI, automatically selecting DMA or polling.
 * @note   Uses DMA when the FreeRTOS scheduler is running and length >= BSP_SPI_DMA_MIN_BYTES.
 * @param  bus_id Logical SPI bus identifier.
 * @param  p_data Pointer to the receive data buffer.
 * @param  length Number of bytes to receive (1 to UINT16_MAX).
 * @return APP_STATUS_OK on success, or an appropriate error status.
 */
app_status_t bsp_spi_read_burst(
    bsp_spi_bus_t bus_id,
    uint8_t *p_data,
    size_t length)
{
    if ((p_data == NULL) || (length == 0U) || (length > (size_t)UINT16_MAX))
    {
        return APP_STATUS_INVALID_ARG;
    }

    SPI_HandleTypeDef *p_hspi = bsp_spi_get_handle(bus_id);
    if (p_hspi == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Use DMA only if RTOS scheduler is active and payload meets threshold */
    const bool use_dma = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) &&
                         (s_spi_dma_sem_handle[bus_id] != NULL) &&
                         (length >= BSP_SPI_DMA_MIN_BYTES);

    if (use_dma == true)
    {
        /* Ensure semaphore is empty before initiating transfer */
        (void)xSemaphoreTake(s_spi_dma_sem_handle[bus_id], 0U);
        s_spi_dma_error[bus_id] = false;

        /* On STM32F1 full-duplex master, HAL_SPI_Receive_DMA clocks TX+RX DMA channels */
        const HAL_StatusTypeDef hal_status = HAL_SPI_Receive_DMA(
            p_hspi,
            p_data,
            (uint16_t)length);

        if (hal_status != HAL_OK)
        {
            return APP_STATUS_IO_ERROR;
        }

        /* Wait for DMA completion notification from ISR */
        if (xSemaphoreTake(
                s_spi_dma_sem_handle[bus_id],
                pdMS_TO_TICKS(BSP_SPI_BURST_TIMEOUT_MS)) != pdTRUE)
        {
            (void)HAL_SPI_Abort(p_hspi);
            return APP_STATUS_TIMEOUT;
        }

        if (s_spi_dma_error[bus_id] == true)
        {
            return APP_STATUS_IO_ERROR;
        }

        return APP_STATUS_OK;
    }

    /* Fallback to blocking polling receive */
    const HAL_StatusTypeDef hal_status = HAL_SPI_Receive(
        p_hspi,
        p_data,
        (uint16_t)length,
        BSP_SPI_BURST_TIMEOUT_MS);

    if (hal_status == HAL_TIMEOUT)
    {
        return APP_STATUS_TIMEOUT;
    }

    if (hal_status != HAL_OK)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

/* HAL-06 & NAM-09: Vendor weak callbacks owned centrally by BSP SPI module */

/**
 * @brief  HAL SPI Transmit Complete callback (invoked from DMA/SPI ISR).
 * @param  hspi Pointer to the HAL SPI handle.
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi != NULL)
    {
        bsp_spi_notify_dma_complete_from_isr(hspi, false);
    }
}

/**
 * @brief  HAL SPI Transmit/Receive Complete callback (invoked during full-duplex DMA RX).
 * @param  hspi Pointer to the HAL SPI handle.
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi != NULL)
    {
        bsp_spi_notify_dma_complete_from_isr(hspi, false);
    }
}

/**
 * @brief  HAL SPI Receive Complete callback (invoked if HAL triggers RX DMA completion).
 * @param  hspi Pointer to the HAL SPI handle.
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi != NULL)
    {
        bsp_spi_notify_dma_complete_from_isr(hspi, false);
    }
}

/**
 * @brief  HAL SPI Error callback (invoked on overrun, mode fault, or DMA transfer error).
 * @param  hspi Pointer to the HAL SPI handle.
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi != NULL)
    {
        bsp_spi_notify_dma_complete_from_isr(hspi, true);
    }
}
