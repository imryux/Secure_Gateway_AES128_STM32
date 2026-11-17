/**
 * @file    app_config.h
 * @brief   System-wide compile-time configuration parameters.
 *          Defines buffer sizes, cryptographic block dimensions, frame layout constants,
 *          and operational reliability timeouts for the Secure Gateway.
 * @version 1.1
 * @date    2026-11-17
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/* Frame and Buffer Pool Parameters (Zero-malloc static memory architecture)  */
/* -------------------------------------------------------------------------- */

/** @brief Maximum allowed size in bytes for an incoming unencrypted payload (plaintext). */
#define APP_MAX_PLAINTEXT_SIZE       (512U)

/** @brief AES cipher block size in bytes (128 bits = 16 bytes). */
#define APP_AES_BLOCK_SIZE           (16U)

/** @brief Length in bytes of the per-frame random Initialization Vector (IV). */
#define APP_AES_IV_SIZE              (16U)

/** @brief Size in bytes of the big-endian frame length header (2 bytes = uint16_t). */
#define APP_FRAME_LEN_HEADER_SIZE    (2U)

/**
 * @brief Total size in bytes of each static frame buffer slot in the pool.
 * Calculated as:
 * Header (2B) + IV (16B) + Max Plaintext (512B) + PKCS#7 Max Padding (16B) = 546 bytes.
 * Rounded up to 550 bytes for safety margin.
 */
#define APP_STATIC_BUFFER_SIZE       (550U)

/** @brief Total number of pre-allocated slots in the static frame buffer pool. */
#define APP_STATIC_BUFFER_COUNT      (10U)

/* -------------------------------------------------------------------------- */
/* Operational Reliability Timeouts (in milliseconds)                         */
/* -------------------------------------------------------------------------- */

/** @brief Timeout threshold to detect and recover a hung/stalled TCP socket (5000 ms). */
#define APP_FRAME_HANG_TIMEOUT_MS    (5000U)

/** @brief Maximum blocking time allowed when transmitting a frame to Ethernet (100 ms). */
#define APP_TX_TIMEOUT_MS            (100U)

/** @brief Hardware Independent Watchdog (IWDG) timeout period (500 ms). */
#define APP_IWDG_TIMEOUT_MS          (500U)

/** @brief Periodic interval for the heartbeat task (LED toggle & IWDG kick) (200 ms). */
#define APP_HEARTBEAT_PERIOD_MS      (200U)

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
