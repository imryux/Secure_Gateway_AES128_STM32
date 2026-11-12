/**
 * @file    log_service.c
 * @brief   Thread-safe debug logging and DWT cycle measurement implementation.
 * @version 1.0
 * @date    2026-11-07
 */

#include "log_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "stm32f1xx_hal.h"

#include "bsp_uart.h"

#define LOG_BUFFER_MAX_SIZE      (192U)
#define LOG_MUTEX_TIMEOUT_MS     (50U)
#define LOG_UART_TIMEOUT_MS      (50U)
#define LOG_US_PER_SECOND        (1000000UL)

/* RTOS-06 & NAM-04: Static allocation for mutex and formatting buffer */
static StaticSemaphore_t s_log_mutex_buffer;
static SemaphoreHandle_t s_log_mutex_handle = NULL;
static char s_log_format_buffer[LOG_BUFFER_MAX_SIZE];

/**
 * @brief  Initializes the log service including DWT cycle counter and static Mutex.
 * @return app_status_t Status of initialization:
 *         - APP_STATUS_OK: Initialization successful.
 *         - APP_STATUS_NO_MEMORY: Failed to create static mutex.
 * @note   Enables ARM Cortex-M DWT (Data Watchpoint and Trace) cycle counter for high-resolution profiling.
 */
app_status_t log_service_init(void)
{
    /* Enable ARM Cortex-M3 DWT Cycle Counter for microsecond profiling */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* RTOS-06 & RTOS-09: Use static mutex with priority inheritance */
    s_log_mutex_handle = xSemaphoreCreateMutexStatic(&s_log_mutex_buffer);
    if (s_log_mutex_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Prints a formatted debug log message via UART in a thread-safe manner.
 * @param  p_format Format string (printf style).
 * @param  ...      Variable argument list matching format specifiers.
 * @return app_status_t Status of the log operation:
 *         - APP_STATUS_OK: Log printed successfully.
 *         - APP_STATUS_INVALID_ARG: NULL format string provided.
 *         - APP_STATUS_BUSY: Failed to acquire log mutex within timeout.
 *         - APP_STATUS_IO_ERROR: Hardware error during UART transmission.
 */
app_status_t log_service_print(const char *p_format, ...)
{
	/* SAF-03: Validate input pointer */
    if (p_format == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Acquire log mutex only if FreeRTOS scheduler is actively running */
    const BaseType_t scheduler_state = xTaskGetSchedulerState();
    if ((scheduler_state == taskSCHEDULER_RUNNING) && (s_log_mutex_handle != NULL))
    {
        if (xSemaphoreTake(
                s_log_mutex_handle,
                pdMS_TO_TICKS(LOG_MUTEX_TIMEOUT_MS)) != pdTRUE)
        {
            return APP_STATUS_BUSY;
        }
    }

    /* Format standard log prefix */
    int32_t prefix_len = snprintf(
        s_log_format_buffer,
        sizeof(s_log_format_buffer),
        "[RELAY DEBUG] ");

    /* Handle prefix formatting errors or buffer overflow truncation */
    if ((prefix_len < 0) || ((size_t)prefix_len >= sizeof(s_log_format_buffer)))
    {
        prefix_len = 0;
    }

    /* Format user message payload into the remaining buffer space */
    va_list args;
    va_start(args, p_format);
    const int32_t body_len = vsnprintf(
        &s_log_format_buffer[prefix_len],
        sizeof(s_log_format_buffer) - (size_t)prefix_len,
        p_format,
        args);
    va_end(args);

    app_status_t status = APP_STATUS_OK;
    if (body_len > 0)
    {
    	/* Calculate total length and safeguard against buffer boundary overflow */
        size_t total_len = (size_t)prefix_len + (size_t)body_len;
        if (total_len >= sizeof(s_log_format_buffer))
        {
            total_len = sizeof(s_log_format_buffer) - 1U;
        }

        /* Transmit formatted string via BSP UART layer */
        status = bsp_uart_send(
            (const uint8_t *)s_log_format_buffer,
            total_len,
            LOG_UART_TIMEOUT_MS);
    }

    /* Release log mutex after transmission if acquired */
    if ((scheduler_state == taskSCHEDULER_RUNNING) && (s_log_mutex_handle != NULL))
    {
        (void)xSemaphoreGive(s_log_mutex_handle);
    }

    return status;
}

/**
 * @brief  Gets the current DWT cycle counter value.
 * @return uint32_t Current CPU cycle count value from DWT->CYCCNT.
 */
uint32_t log_service_dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief  Converts CPU cycle counts to microseconds based on SystemCoreClock.
 * @param  cycles CPU cycles to convert.
 * @return uint32_t Equivalent time duration in microseconds (us).
 */
uint32_t log_service_cycles_to_us(uint32_t cycles)
{
	/* Calculate clock frequency in MHz (e.g., 72 MHz -> 72 cycles per us) */
    const uint32_t clk_mhz = SystemCoreClock / LOG_US_PER_SECOND;
    if (clk_mhz == 0U)
    {
        return 0U;
    }

    return (cycles / clk_mhz);
}
