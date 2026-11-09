/**
 * @file    app_config.h
 * @brief   System-wide compile-time configuration parameters.
 * @version 1.0
 * @date    2026-11-06
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Frame and Buffer Pool Parameters (Ref: Table II in Reference Paper) */
#define APP_MAX_PLAINTEXT_SIZE       (512U)
#define APP_AES_BLOCK_SIZE           (16U)
#define APP_AES_IV_SIZE              (16U)
#define APP_FRAME_LEN_HEADER_SIZE    (2U)
#define APP_STATIC_BUFFER_SIZE       (550U)
#define APP_STATIC_BUFFER_COUNT      (10U)

/* Reliability Timeouts (ms) */
#define APP_FRAME_HANG_TIMEOUT_MS    (5000U)
#define APP_TX_TIMEOUT_MS            (100U)
#define APP_IWDG_TIMEOUT_MS          (500U)
#define APP_HEARTBEAT_PERIOD_MS      (200U)

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
