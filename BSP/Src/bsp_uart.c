/**
 * @file    bsp_uart.c
 * @brief   UART1 hardware abstraction implementation.
 * @version 1.0
 * @date    2026-11-07
 */

#include "bsp_uart.h"

#include <stddef.h>
#include <stdint.h>

#include "usart.h"

/**
 * @brief  Transmits data synchronously via UART1 with a specified timeout.
 * @param  p_data     Pointer to the data buffer to be transmitted.
 * @param  length     Number of bytes to transmit.
 * @param  timeout_ms Timeout duration in milliseconds.
 * @return app_status_t Status of the operation:
 *         - APP_STATUS_OK: Transmit completed successfully.
 *         - APP_STATUS_INVALID_ARG: Null pointer, zero length, or length exceeding UINT16_MAX.
 *         - APP_STATUS_TIMEOUT: HAL transmit operation timed out.
 *         - APP_STATUS_IO_ERROR: Hardware or HAL transmission error.
 */
app_status_t bsp_uart_send(
    const uint8_t *p_data,
    size_t length,
    uint32_t timeout_ms)
{
	/* SAF-03: Validate input pointer and data length to prevent NULL dereference or invalid calls */
    if ((p_data == NULL) || (length == 0U))
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* SAF-10: Check narrowing conversion before casting size_t to uint16_t */
    if (length > (size_t)UINT16_MAX)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* HAL-05: Always check return status of critical HAL I/O API */
    const HAL_StatusTypeDef hal_status = HAL_UART_Transmit(
        &huart1,
        p_data,
        (uint16_t)length,
        timeout_ms);
    /* Map HAL status codes to application-level status values */
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
