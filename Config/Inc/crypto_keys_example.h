/**
 * @file    crypto_keys_example.h
 * @brief   Template for pre-shared 128-bit AES keys.
 * @note    Copy this file to "crypto_keys.h" and replace with secret keys before building.
 * @version 1.0
 * @date    2026-11-18
 */

#ifndef CRYPTO_KEYS_H
#define CRYPTO_KEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Example 128-bit AES key for Network A to Network B traffic (REPLACE IN PRODUCTION). */
#define CRYPTO_KEY_A_TO_B_INIT \
    { \
        0x00U, 0x01U, 0x02U, 0x03U, \
        0x04U, 0x05U, 0x06U, 0x07U, \
        0x08U, 0x09U, 0x0AU, 0x0BU, \
        0x0CU, 0x0DU, 0x0EU, 0x0FU  \
    }

/** @brief Example 128-bit AES key for Network B to Network A traffic (REPLACE IN PRODUCTION). */
#define CRYPTO_KEY_B_TO_A_INIT \
    { \
        0x10U, 0x11U, 0x12U, 0x13U, \
        0x14U, 0x15U, 0x16U, 0x17U, \
        0x18U, 0x19U, 0x1AU, 0x1BU, \
        0x1CU, 0x1DU, 0x1EU, 0x1FU  \
    }

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_KEYS_H */
