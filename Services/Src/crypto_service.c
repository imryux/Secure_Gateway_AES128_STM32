/**
 * @file    crypto_service.c
 * @brief   AES-128-CBC encryption service implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "crypto_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "aes.h"

#include "app_config.h"
#include "crypto_keys.h"
#include "log_service.h"

#define CRYPTO_PRNG_FALLBACK_SEED     (0x6D2B79F5UL)
#define CRYPTO_NIST_VECTOR_LEN        (64U)
#define CRYPTO_BENCH_COUNT            (4U)

/* NAM-04: File-scope static AES contexts and PRNG state (no dynamic allocation) */
static struct AES_ctx s_aes_ctx_a_to_b;
static struct AES_ctx s_aes_ctx_b_to_a;
static uint32_t s_prng_state = CRYPTO_PRNG_FALLBACK_SEED;
static bool s_is_initialized = false;

static uint32_t crypto_get_system_tick(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        return (uint32_t)xTaskGetTickCount();
    }

    return HAL_GetTick();
}

/* Lightweight xorshift32 PRNG (Ref: Section III-B & IV-C in Reference Paper) */
static uint32_t crypto_xorshift32_next(void)
{
    uint32_t state = s_prng_state;
    if (state == 0U)
    {
        state = CRYPTO_PRNG_FALLBACK_SEED;
    }

    state ^= (state << 13U);
    state ^= (state >> 17U);
    state ^= (state << 5U);
    s_prng_state = state;

    return state;
}

static void crypto_generate_iv(uint8_t *p_iv_out)
{
    /* Mix current tick into PRNG state before generating 16-byte per-frame IV */
    s_prng_state ^= crypto_get_system_tick();
    if (s_prng_state == 0U)
    {
        s_prng_state = CRYPTO_PRNG_FALLBACK_SEED;
    }

    for (uint32_t word_idx = 0U; word_idx < 4U; word_idx++)
    {
        const uint32_t rnd = crypto_xorshift32_next();
        const size_t offset = (size_t)(word_idx * 4U);
        p_iv_out[offset]      = (uint8_t)(rnd & 0xFFU);
        p_iv_out[offset + 1U] = (uint8_t)((rnd >> 8U) & 0xFFU);
        p_iv_out[offset + 2U] = (uint8_t)((rnd >> 16U) & 0xFFU);
        p_iv_out[offset + 3U] = (uint8_t)((rnd >> 24U) & 0xFFU);
    }
}

app_status_t crypto_service_init(void)
{
    const uint8_t key_a_to_b[APP_AES_BLOCK_SIZE] = CRYPTO_KEY_A_TO_B_INIT;
    const uint8_t key_b_to_a[APP_AES_BLOCK_SIZE] = CRYPTO_KEY_B_TO_A_INIT;

    /* Initialize both directional AES-128 contexts once at startup (Ref: Section III-B) */
    AES_init_ctx(&s_aes_ctx_a_to_b, key_a_to_b);
    AES_init_ctx(&s_aes_ctx_b_to_a, key_b_to_a);

    const uint32_t seed = crypto_get_system_tick() ^ log_service_dwt_get_cycles();
    s_prng_state = (seed != 0U) ? seed : CRYPTO_PRNG_FALLBACK_SEED;
    s_is_initialized = true;

    return APP_STATUS_OK;
}

app_status_t crypto_service_encrypt_frame(
    crypto_direction_t direction,
    const uint8_t *p_plaintext,
    size_t plaintext_len,
    uint8_t *p_out_frame,
    size_t out_capacity,
    size_t *p_out_len)
{
    if (s_is_initialized == false)
    {
        return APP_STATUS_NOT_READY;
    }

    /* SAF-02 & SAF-03: Strict pointer and input length bounds validation */
    if ((p_plaintext == NULL) || (p_out_frame == NULL) || (p_out_len == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    if ((plaintext_len == 0U) || (plaintext_len > APP_MAX_PLAINTEXT_SIZE))
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Step 1: Compute PKCS#7 padded ciphertext length (always a multiple of 16 bytes) */
    const size_t pad_len = APP_AES_BLOCK_SIZE - (plaintext_len % APP_AES_BLOCK_SIZE);
    const size_t padded_len = plaintext_len + pad_len;
    const size_t payload_after_len = APP_AES_IV_SIZE + padded_len;
    const size_t total_frame_len = APP_FRAME_LEN_HEADER_SIZE + payload_after_len;

    if ((out_capacity < total_frame_len) || (payload_after_len > (size_t)UINT16_MAX))
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Frame layout: [len_hi (1B)] [len_lo (1B)] [IV (16B)] [ciphertext (padded_len B)] */
    uint8_t *p_iv_field = &p_out_frame[APP_FRAME_LEN_HEADER_SIZE];
    uint8_t *p_cipher_field = &p_out_frame[APP_FRAME_LEN_HEADER_SIZE + APP_AES_IV_SIZE];

    /* Copy plaintext and append PKCS#7 padding bytes */
    (void)memcpy(p_cipher_field, p_plaintext, plaintext_len);
    (void)memset(&p_cipher_field[plaintext_len], (int)pad_len, pad_len);

    /* Step 2: Generate a fresh 16-byte per-frame IV using xorshift32 */
    crypto_generate_iv(p_iv_field);

    /* Step 3: Load per-frame IV into directional AES context and encrypt in CBC mode */
    struct AES_ctx *p_ctx = (direction == CRYPTO_DIR_B_TO_A)
                                ? &s_aes_ctx_b_to_a
                                : &s_aes_ctx_a_to_b;

    AES_ctx_set_iv(p_ctx, p_iv_field);
    AES_CBC_encrypt_buffer(p_ctx, p_cipher_field, padded_len);

    /* Step 4: Write 2-byte big-endian frame length header */
    p_out_frame[0] = (uint8_t)((payload_after_len >> 8U) & 0xFFU);
    p_out_frame[1] = (uint8_t)(payload_after_len & 0xFFU);
    *p_out_len = total_frame_len;

    return APP_STATUS_OK;
}

app_status_t crypto_service_run_self_test(void)
{
    /* Official NIST SP 800-38A Section F.2.1 (CBC-AES128.Encrypt) Test Vector */
    static const uint8_t s_nist_key[APP_AES_BLOCK_SIZE] =
    {
        0x2BU, 0x7EU, 0x15U, 0x16U, 0x28U, 0xAEU, 0xD2U, 0xA6U,
        0xABU, 0xF7U, 0x15U, 0x88U, 0x09U, 0xCFU, 0x4FU, 0x3CU
    };

    static const uint8_t s_nist_iv[APP_AES_IV_SIZE] =
    {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU
    };

    static const uint8_t s_nist_plaintext[CRYPTO_NIST_VECTOR_LEN] =
    {
        0x6BU, 0xC1U, 0xBEU, 0xE2U, 0x2EU, 0x40U, 0x9FU, 0x96U,
        0xE9U, 0x3DU, 0x7EU, 0x11U, 0x73U, 0x93U, 0x17U, 0x2AU,
        0xAEU, 0x2DU, 0x8AU, 0x57U, 0x1EU, 0x03U, 0xACU, 0x9CU,
        0x9EU, 0xB7U, 0x6FU, 0xACU, 0x45U, 0xAFU, 0x8EU, 0x51U,
        0x30U, 0xC8U, 0x1CU, 0x46U, 0xA3U, 0x5CU, 0xE4U, 0x11U,
        0xE5U, 0xFBU, 0xC1U, 0x19U, 0x1AU, 0x0AU, 0x52U, 0xEFU,
        0xF6U, 0x9FU, 0x24U, 0x45U, 0xDFU, 0x4FU, 0x9BU, 0x17U,
        0xADU, 0x2BU, 0x41U, 0x7BU, 0xE6U, 0x6CU, 0x37U, 0x10U
    };

    static const uint8_t s_nist_expected_cipher[CRYPTO_NIST_VECTOR_LEN] =
    {
        0x76U, 0x49U, 0xABU, 0xACU, 0x81U, 0x19U, 0xB2U, 0x46U,
        0xCEU, 0xE9U, 0x8EU, 0x9BU, 0x12U, 0xE9U, 0x19U, 0x7DU,
        0x50U, 0x86U, 0xCBU, 0x9BU, 0x50U, 0x72U, 0x19U, 0xEEU,
        0x95U, 0xDBU, 0x11U, 0x3AU, 0x91U, 0x76U, 0x78U, 0xB2U,
        0x73U, 0xBEU, 0xD6U, 0xB8U, 0xE3U, 0xC1U, 0x74U, 0x3BU,
        0x71U, 0x16U, 0xE6U, 0x9EU, 0x22U, 0x22U, 0x95U, 0x16U,
        0x3FU, 0xF1U, 0xCAU, 0xA1U, 0x68U, 0x1FU, 0xACU, 0x09U,
        0x12U, 0x0EU, 0xCAU, 0x30U, 0x75U, 0x86U, 0xE1U, 0xA7U
    };

    uint8_t work_buf[CRYPTO_NIST_VECTOR_LEN];
    (void)memcpy(work_buf, s_nist_plaintext, sizeof(work_buf));

    struct AES_ctx test_ctx;
    AES_init_ctx_iv(&test_ctx, s_nist_key, s_nist_iv);
    AES_CBC_encrypt_buffer(&test_ctx, work_buf, sizeof(work_buf));

    if (memcmp(work_buf, s_nist_expected_cipher, sizeof(work_buf)) != 0)
    {
        (void)log_service_print("[TEST FAIL] NIST SP 800-38A AES-128-CBC mismatch!\r\n");
        return APP_STATUS_CRYPTO_ERROR;
    }

    (void)log_service_print("[TEST PASS] NIST SP 800-38A AES-128-CBC Vector Verified!\r\n");
    return APP_STATUS_OK;
}

app_status_t crypto_service_run_benchmark(void)
{
    static const size_t s_bench_sizes[CRYPTO_BENCH_COUNT] = {64U, 128U, 256U, 512U};
    static uint8_t s_bench_plain[APP_MAX_PLAINTEXT_SIZE];
    static uint8_t s_bench_frame[APP_STATIC_BUFFER_SIZE];

    (void)memset(s_bench_plain, 0x5AU, sizeof(s_bench_plain));

    for (uint32_t idx = 0U; idx < CRYPTO_BENCH_COUNT; idx++)
    {
        const size_t payload_len = s_bench_sizes[idx];
        size_t out_frame_len = 0U;

        const uint32_t start_cycles = log_service_dwt_get_cycles();
        const app_status_t status = crypto_service_encrypt_frame(
            CRYPTO_DIR_A_TO_B,
            s_bench_plain,
            payload_len,
            s_bench_frame,
            sizeof(s_bench_frame),
            &out_frame_len);
        const uint32_t elapsed_cycles = log_service_dwt_get_cycles() - start_cycles;

        if (status != APP_STATUS_OK)
        {
            return status;
        }

        const uint32_t elapsed_us = log_service_cycles_to_us(elapsed_cycles);
        (void)log_service_print(
            "AES Benchmark | Payload=%u B -> Frame=%u B | Time=%lu us\r\n",
            (unsigned int)payload_len,
            (unsigned int)out_frame_len,
            (unsigned long)elapsed_us);
    }

    return APP_STATUS_OK;
}
