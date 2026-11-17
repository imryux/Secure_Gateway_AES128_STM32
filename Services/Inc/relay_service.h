/**
 * @file    relay_service.h
 * @brief   Bidirectional encrypted relay service with static buffer pool and socket recovery.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef RELAY_SERVICE_H
#define RELAY_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "app_status.h"
#include "bsp_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Transport type of an incoming frame inside the static buffer pool.
 */
typedef enum
{
    RELAY_TRANSPORT_TCP = 0,
    RELAY_TRANSPORT_UDP = 1
} relay_transport_t;

/**
 * @brief Single slot inside the static frame buffer pool (Ref: Section III-C & IV-D).
 */
typedef struct
{
    uint8_t data[APP_STATIC_BUFFER_SIZE];
    size_t length;
    bsp_w5500_id_t src_chip;
    relay_transport_t transport;
} relay_buffer_slot_t;

/**
 * @brief  Initialize static buffer pool mutex and socket tracking tables.
 * @return APP_STATUS_OK on success, APP_STATUS_NO_MEMORY if mutex creation fails.
 */
app_status_t relay_service_init(void);

/**
 * @brief  Allocate a free buffer slot index from the static pool.
 * @param[out] p_slot_idx Pointer to store allocated slot index (0..APP_STATIC_BUFFER_COUNT-1).
 * @return APP_STATUS_OK on success, APP_STATUS_NO_MEMORY if all slots are busy.
 */
app_status_t relay_service_alloc_slot(uint8_t *p_slot_idx);

/**
 * @brief  Release a previously allocated buffer slot back to the static pool.
 * @param[in] slot_idx Slot index to release.
 * @return APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if slot_idx is out of range.
 */
app_status_t relay_service_free_slot(uint8_t slot_idx);

/**
 * @brief  Get mutable pointer to a buffer slot by index.
 * @param[in] slot_idx Slot index (0..APP_STATIC_BUFFER_COUNT-1).
 * @return Pointer to slot struct, or NULL if slot_idx is invalid.
 */
relay_buffer_slot_t *relay_service_get_slot(uint8_t slot_idx);

/**
 * @brief  Poll sockets on one W5500 chip, read incoming plaintext into static slots,
 *         and invoke the provided enqueue callback for each received frame.
 * @param[in] chip_id       Source W5500 module (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param[in] on_rx_ready   Callback invoked with allocated slot_idx when a frame is ready.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t relay_service_poll_rx(
    bsp_w5500_id_t chip_id,
    app_status_t (*on_rx_ready)(uint8_t slot_idx));

/**
 * @brief  Encrypt the plaintext stored inside the given buffer slot using AES-128-CBC.
 * @param[in] slot_idx Buffer slot index containing raw plaintext.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t relay_service_encrypt_slot(uint8_t slot_idx);

/**
 * @brief  Transmit the encrypted frame stored in slot_idx out through the opposite W5500 chip.
 * @param[in] slot_idx Buffer slot index containing packed encrypted frame.
 * @return APP_STATUS_OK on success, error status otherwise.
 */
app_status_t relay_service_transmit_slot(uint8_t slot_idx);

/**
 * @brief  Periodic reliability supervision: check PHY link state and recover hung TCP sockets.
 */
void relay_service_supervise_health(void);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_SERVICE_H */
