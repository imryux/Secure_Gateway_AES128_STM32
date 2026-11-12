/**
 * @file    crypto_service.h
 * @brief   AES-128-CBC encryption service with per-frame random IV and PKCS#7 padding.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef CRYPTO_SERVICE_H
#define CRYPTO_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Traffic forwarding direction selecting the corresponding AES-128 context and key.
 */
typedef enum
{
    CRYPTO_DIR_A_TO_B = 0, /**< Plaintext from Network A encrypted toward Network B */
    CRYPTO_DIR_B_TO_A = 1  /**< Plaintext from Network B encrypted toward Network A */
} crypto_direction_t;

/**
 * @brief  Initialize bidirectional AES-128 contexts and seed the xorshift32 PRNG.
 * @return APP_STATUS_OK on success.
 */
app_status_t crypto_service_init(void);

/**
 * @brief  Pad plaintext with PKCS#7, generate a 16-byte per-frame IV, encrypt using
 *         AES-128-CBC, and pack into [len_hi][len_lo][IV (16B)][ciphertext].
 * @param[in]  direction      Traffic direction (CRYPTO_DIR_A_TO_B or CRYPTO_DIR_B_TO_A).
 * @param[in]  p_plaintext    Pointer to input plaintext buffer (1..512 bytes).
 * @param[in]  plaintext_len  Length of plaintext in bytes.
 * @param[out] p_out_frame    Pointer to destination frame buffer (must hold up to 546 bytes).
 * @param[in]  out_capacity   Capacity of p_out_frame in bytes.
 * @param[out] p_out_len      Pointer to store total packed frame length in bytes.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t crypto_service_encrypt_frame(
    crypto_direction_t direction,
    const uint8_t *p_plaintext,
    size_t plaintext_len,
    uint8_t *p_out_frame,
    size_t out_capacity,
    size_t *p_out_len);

/**
 * @brief  Verify AES-128-CBC implementation against NIST SP 800-38A F.2.1 test vector.
 * @return APP_STATUS_OK if ciphertext matches NIST reference, APP_STATUS_CRYPTO_ERROR otherwise.
 */
app_status_t crypto_service_run_self_test(void);

/**
 * @brief  Measure and log AES-128-CBC frame encryption latency for 64B, 128B, 256B, 512B
 *         using the Cortex-M3 DWT cycle counter.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t crypto_service_run_benchmark(void);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_SERVICE_H */
