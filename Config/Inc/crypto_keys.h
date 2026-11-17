/**
 * @file    crypto_keys.h
 * @brief   Pre-shared 128-bit AES keys for bidirectional encryption (A->B and B->A).
 *          Separate symmetric keys are used for each direction to provide domain
 *          separation and prevent replay reflection attacks between the two network segments.
 * @note    Security warning: These keys are pre-shared demonstration keys.
 *          Do not commit production secrets to public repositories.
 * @version 1.1
 * @date    2026-11-17
 */

#ifndef CRYPTO_KEYS_H
#define CRYPTO_KEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 16-byte (128-bit) symmetric AES key used to encrypt traffic
 *        flowing from Network A to Network B.
 */
#define CRYPTO_KEY_A_TO_B_INIT \
    { \
        0x2BU, 0x7EU, 0x15U, 0x16U, \
        0x28U, 0xAEU, 0xD2U, 0xA6U, \
        0xABU, 0xF7U, 0x15U, 0x88U, \
        0x09U, 0xCFU, 0x4FU, 0x3CU  \
    }

/**
 * @brief 16-byte (128-bit) symmetric AES key used to encrypt traffic
 *        flowing from Network B to Network A.
 */
#define CRYPTO_KEY_B_TO_A_INIT \
    { \
        0x60U, 0x3DU, 0xEBU, 0x10U, \
        0x15U, 0xCAU, 0x71U, 0xBEU, \
        0x2BU, 0x73U, 0xAEU, 0xF0U, \
        0x85U, 0x7DU, 0x77U, 0x81U  \
    }

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_KEYS_H */
