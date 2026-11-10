/**
 * @file    bsp_uart.h
 * @brief   UART1 hardware abstraction interface for debug logging.
 * @version 1.0
 * @date    2026-11-07
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Transmit a byte buffer over UART1 with bounded timeout.
 * @param[in] p_data     Pointer to transmit buffer.
 * @param[in] length     Number of bytes to transmit.
 * @param[in] timeout_ms Maximum blocking time in milliseconds.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
app_status_t bsp_uart_send(
    const uint8_t *p_data,
    size_t length,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
