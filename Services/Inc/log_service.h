/**
 * @file    log_service.h
 * @brief   Thread-safe debug logging and DWT cycle measurement service.
 * @version 1.0
 * @date    2026-11-07
 */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include <stdint.h>

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Initialize logging mutex and enable Cortex-M3 DWT cycle counter.
 * @return APP_STATUS_OK on success, APP_STATUS_NO_MEMORY if mutex creation fails.
 */
app_status_t log_service_init(void);

/**
 * @brief  Print a formatted string with "[RELAY DEBUG] " prefix over UART1.
 * @param[in] p_format Null-terminated printf-style format string.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t log_service_print(const char *p_format, ...);

/**
 * @brief  Read current value of the 32-bit DWT cycle counter (DWT->CYCCNT).
 * @return Current CPU clock cycle count.
 */
uint32_t log_service_dwt_get_cycles(void);

/**
 * @brief  Convert CPU clock cycles to microseconds based on SystemCoreClock.
 * @param[in] cycles Number of elapsed CPU clock cycles.
 * @return Elapsed time in microseconds (us).
 */
uint32_t log_service_cycles_to_us(uint32_t cycles);

#ifdef __cplusplus
}
#endif

#endif /* LOG_SERVICE_H */
