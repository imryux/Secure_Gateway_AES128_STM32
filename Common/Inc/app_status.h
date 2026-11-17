/**
 * @file    app_status.h
 * @brief   Common application status codes.
 *          Provides a standardized set of return error codes across all BSP drivers,
 *          middleware abstractions, background services, and application tasks.
 * @version 1.1
 * @date    2026-11-17
 */

#ifndef APP_STATUS_H
#define APP_STATUS_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Universal application status and error enumeration.
 */
typedef enum
{
    APP_STATUS_OK = 0,       /**< Operation completed successfully without error */
    APP_STATUS_INVALID_ARG,  /**< Invalid argument provided (e.g. NULL pointer or out of range) */
    APP_STATUS_BUSY,         /**< Resource is currently locked or busy (e.g. Mutex timeout) */
    APP_STATUS_TIMEOUT,      /**< Operation timed out before completing (e.g. SPI/DMA timeout) */
    APP_STATUS_IO_ERROR,     /**< Hardware I/O transmission or bus transaction error */
    APP_STATUS_NO_MEMORY,    /**< Insufficient memory or all static buffer slots occupied */
    APP_STATUS_NOT_READY,    /**< Subsystem or peripheral has not been initialized yet */
    APP_STATUS_CRYPTO_ERROR  /**< Cryptographic error or self-test verification mismatch */
} app_status_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATUS_H */
