/**
 * @file    relay_service.c
 * @brief   Bidirectional encrypted relay service implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "relay_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "socket.h"
#include "w5500.h"

#include "bsp_w5500.h"
#include "crypto_service.h"
#include "log_service.h"
#include "protocol_config.h"

#define RELAY_POOL_LOCK_TIMEOUT_MS    (20U)
#define RELAY_WIZ_LOCK_TIMEOUT_MS     (100U)

typedef struct
{
    TickType_t last_rx_tick;
    bool has_pending_data;
} relay_sock_watch_t;

/* Static buffer pool and synchronization objects (Ref: RTOS-06, SAF-07) */
static relay_buffer_slot_t s_buffer_pool[APP_STATIC_BUFFER_COUNT];
static bool s_slot_in_use[APP_STATIC_BUFFER_COUNT];
static StaticSemaphore_t s_pool_mutex_buffer;
static SemaphoreHandle_t s_pool_mutex_handle = NULL;

/* Per-chip, per-socket supervision state for hung-frame timeout and PHY link tracking */
static relay_sock_watch_t s_sock_watch[BSP_W5500_COUNT][PROTO_SOCKETS_PER_CHIP];
static bool s_prev_link_up[BSP_W5500_COUNT] = {false, false};
static uint8_t s_udp_peer_ip[BSP_W5500_COUNT][4] =
{
    {PROTO_NET_A_PEER_IP_0, PROTO_NET_A_PEER_IP_1, PROTO_NET_A_PEER_IP_2, PROTO_NET_A_PEER_IP_3},
    {PROTO_NET_B_PEER_IP_0, PROTO_NET_B_PEER_IP_1, PROTO_NET_B_PEER_IP_2, PROTO_NET_B_PEER_IP_3}
};
static uint16_t s_udp_peer_port[BSP_W5500_COUNT] =
{
    PROTO_UDP_RELAY_PORT,
    PROTO_UDP_RELAY_PORT
};

static const proto_socket_cfg_t s_socket_cfg[PROTO_SOCKETS_PER_CHIP] =
{
    {PROTO_SOCKET_TCP_MAIN,   PROTO_TYPE_TCP_SERVER, PROTO_TCP_RELAY_PORT, true},
    {PROTO_SOCKET_TCP_BACKUP, PROTO_TYPE_TCP_SERVER, PROTO_TCP_RELAY_PORT, true},
    {PROTO_SOCKET_UDP,        PROTO_TYPE_UDP_PEER,   PROTO_UDP_RELAY_PORT, true}
};

static void relay_maintain_socket_state(const proto_socket_cfg_t *p_cfg)
{
    const uint8_t sn = p_cfg->socket_num;
    const uint8_t sr = getSn_SR(sn);

    if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
    {
        if (sr == SOCK_CLOSED)
        {
            if (socket(sn, Sn_MR_TCP, p_cfg->local_port, SF_IO_NONBLOCK) == (int8_t)sn)
            {
                (void)listen(sn);
            }
        }
        else if (sr == SOCK_INIT)
        {
            (void)listen(sn);
        }
        else if (sr == SOCK_CLOSE_WAIT)
        {
            /* Close half-closed socket so backup or reopened socket can accept new clients */
            (void)disconnect(sn);
            (void)close(sn);
            if (socket(sn, Sn_MR_TCP, p_cfg->local_port, SF_IO_NONBLOCK) == (int8_t)sn)
            {
                (void)listen(sn);
            }
        }
        else
        {
            /* SOCK_LISTEN or SOCK_ESTABLISHED: keep active */
        }
    }
    else
    {
        if (sr == SOCK_CLOSED)
        {
            (void)socket(sn, Sn_MR_UDP, p_cfg->local_port, SF_IO_NONBLOCK);
        }
    }
}

app_status_t relay_service_init(void)
{
    s_pool_mutex_handle = xSemaphoreCreateMutexStatic(&s_pool_mutex_buffer);
    if (s_pool_mutex_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    (void)memset(s_buffer_pool, 0, sizeof(s_buffer_pool));
    (void)memset(s_slot_in_use, 0, sizeof(s_slot_in_use));
    (void)memset(s_sock_watch, 0, sizeof(s_sock_watch));

    return APP_STATUS_OK;
}

app_status_t relay_service_alloc_slot(uint8_t *p_slot_idx)
{
    if (p_slot_idx == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (s_pool_mutex_handle == NULL)
    {
        return APP_STATUS_NOT_READY;
    }

    if (xSemaphoreTake(s_pool_mutex_handle, pdMS_TO_TICKS(RELAY_POOL_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_STATUS_BUSY;
    }

    app_status_t status = APP_STATUS_NO_MEMORY;
    for (uint8_t idx = 0U; idx < APP_STATIC_BUFFER_COUNT; idx++)
    {
        if (s_slot_in_use[idx] == false)
        {
            s_slot_in_use[idx] = true;
            s_buffer_pool[idx].length = 0U;
            *p_slot_idx = idx;
            status = APP_STATUS_OK;
            break;
        }
    }

    (void)xSemaphoreGive(s_pool_mutex_handle);
    return status;
}

app_status_t relay_service_free_slot(uint8_t slot_idx)
{
    if (slot_idx >= APP_STATIC_BUFFER_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (s_pool_mutex_handle == NULL)
    {
        return APP_STATUS_NOT_READY;
    }

    if (xSemaphoreTake(s_pool_mutex_handle, pdMS_TO_TICKS(RELAY_POOL_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_STATUS_BUSY;
    }

    s_slot_in_use[slot_idx] = false;
    s_buffer_pool[slot_idx].length = 0U;

    (void)xSemaphoreGive(s_pool_mutex_handle);
    return APP_STATUS_OK;
}

relay_buffer_slot_t *relay_service_get_slot(uint8_t slot_idx)
{
    if (slot_idx >= APP_STATIC_BUFFER_COUNT)
    {
        return NULL;
    }

    return &s_buffer_pool[slot_idx];
}

app_status_t relay_service_poll_rx(
    bsp_w5500_id_t chip_id,
    app_status_t (*on_rx_ready)(uint8_t slot_idx))
{
    if ((chip_id >= BSP_W5500_COUNT) || (on_rx_ready == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    app_status_t status = bsp_w5500_lock(chip_id, RELAY_WIZ_LOCK_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    for (uint32_t idx = 0U; idx < PROTO_SOCKETS_PER_CHIP; idx++)
    {
        const proto_socket_cfg_t *p_cfg = &s_socket_cfg[idx];
        const uint8_t sn = p_cfg->socket_num;

        relay_maintain_socket_state(p_cfg);

        /* Clear ONLY receive and connection interrupt flags.
         * NEVER clear Sn_IR_SENDOK or Sn_IR_TIMEOUT here, because send() and sendto()
         * in ioLibrary rely on those flags to detect transmission completion and timeout! */
        const uint8_t ir_flags = getSn_IR(sn);
        const uint8_t rx_ir_flags = ir_flags & (uint8_t)(Sn_IR_RECV | Sn_IR_CON | Sn_IR_DISCON);
        if (rx_ir_flags != 0U)
        {
            setSn_IR(sn, rx_ir_flags);
        }

        uint16_t rx_avail = getSn_RX_RSR(sn);
        if (rx_avail == 0U)
        {
            s_sock_watch[chip_id][idx].has_pending_data = false;
            continue;
        }

        s_sock_watch[chip_id][idx].last_rx_tick = xTaskGetTickCount();
        s_sock_watch[chip_id][idx].has_pending_data = true;

        while (rx_avail > 0U)
        {
            uint8_t slot_idx = 0U;
            if (relay_service_alloc_slot(&slot_idx) != APP_STATUS_OK)
            {
                /* Static buffer pool full: drop overflow frame cleanly (Ref: Section V-B) */
                uint8_t discard_buf[64];
                const uint16_t drop_len = (rx_avail > (uint16_t)sizeof(discard_buf))
                                              ? (uint16_t)sizeof(discard_buf)
                                              : rx_avail;
                if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
                {
                    (void)recv(sn, discard_buf, drop_len);
                }
                else
                {
                    uint8_t dummy_ip[4] = {0U, 0U, 0U, 0U};
                    uint16_t dummy_port = 0U;
                    (void)recvfrom(sn, discard_buf, drop_len, dummy_ip, &dummy_port);
                }
                break;
            }

            relay_buffer_slot_t *p_slot = relay_service_get_slot(slot_idx);
            uint16_t chunk_len = rx_avail;
            if (chunk_len > (uint16_t)APP_MAX_PLAINTEXT_SIZE)
            {
                chunk_len = (uint16_t)APP_MAX_PLAINTEXT_SIZE;
            }

            int32_t recvd = 0;
            if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
            {
                recvd = recv(sn, p_slot->data, chunk_len);
                p_slot->transport = RELAY_TRANSPORT_TCP;
            }
            else
            {
                uint8_t sender_ip[4] = {0U, 0U, 0U, 0U};
                uint16_t sender_port = 0U;
                recvd = recvfrom(sn, p_slot->data, chunk_len, sender_ip, &sender_port);
                p_slot->transport = RELAY_TRANSPORT_UDP;
                if (recvd > 0)
                {
                    (void)memcpy(s_udp_peer_ip[chip_id], sender_ip, sizeof(sender_ip));
                    s_udp_peer_port[chip_id] = sender_port;
                }
            }

            if (recvd <= 0)
            {
                (void)relay_service_free_slot(slot_idx);
                break;
            }

            p_slot->length = (size_t)recvd;
            p_slot->src_chip = chip_id;

            /* Release W5500 mutex before pushing to queue to avoid blocking the other chip */
            bsp_w5500_unlock();

            if (on_rx_ready(slot_idx) != APP_STATUS_OK)
            {
                (void)relay_service_free_slot(slot_idx);
            }

            status = bsp_w5500_lock(chip_id, RELAY_WIZ_LOCK_TIMEOUT_MS);
            if (status != APP_STATUS_OK)
            {
                return status;
            }

            rx_avail = getSn_RX_RSR(sn);
        }
    }

    bsp_w5500_unlock();
    return APP_STATUS_OK;
}

app_status_t relay_service_encrypt_slot(uint8_t slot_idx)
{
    relay_buffer_slot_t *p_slot = relay_service_get_slot(slot_idx);
    if (p_slot == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if ((p_slot->length == 0U) || (p_slot->length > APP_MAX_PLAINTEXT_SIZE))
    {
        return APP_STATUS_INVALID_ARG;
    }

    uint8_t plain_copy[APP_MAX_PLAINTEXT_SIZE];
    (void)memcpy(plain_copy, p_slot->data, p_slot->length);

    const crypto_direction_t dir = (p_slot->src_chip == BSP_W5500_ID_A)
                                       ? CRYPTO_DIR_A_TO_B
                                       : CRYPTO_DIR_B_TO_A;

    size_t encrypted_frame_len = 0U;
    const app_status_t status = crypto_service_encrypt_frame(
        dir,
        plain_copy,
        p_slot->length,
        p_slot->data,
        sizeof(p_slot->data),
        &encrypted_frame_len);

    if (status != APP_STATUS_OK)
    {
        return status;
    }

    p_slot->length = encrypted_frame_len;
    return APP_STATUS_OK;
}

app_status_t relay_service_transmit_slot(uint8_t slot_idx)
{
    relay_buffer_slot_t *p_slot = relay_service_get_slot(slot_idx);
    if (p_slot == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if ((p_slot->length == 0U) || (p_slot->length > APP_STATIC_BUFFER_SIZE))
    {
        return APP_STATUS_INVALID_ARG;
    }

    const bsp_w5500_id_t dst_chip = (p_slot->src_chip == BSP_W5500_ID_A)
                                        ? BSP_W5500_ID_B
                                        : BSP_W5500_ID_A;

    app_status_t status = bsp_w5500_lock(dst_chip, RELAY_WIZ_LOCK_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    int32_t sent_bytes = -1;
    if (p_slot->transport == RELAY_TRANSPORT_TCP)
    {
        /* Select active established TCP socket (Main Socket 0 or Backup Socket 1) */
        uint8_t target_sn = PROTO_SOCKETS_PER_CHIP;
        if (getSn_SR(PROTO_SOCKET_TCP_MAIN) == SOCK_ESTABLISHED)
        {
            target_sn = PROTO_SOCKET_TCP_MAIN;
        }
        else if (getSn_SR(PROTO_SOCKET_TCP_BACKUP) == SOCK_ESTABLISHED)
        {
            target_sn = PROTO_SOCKET_TCP_BACKUP;
        }
        else
        {
            /* No TCP receiver connected on destination network */
        }

        if (target_sn < PROTO_SOCKETS_PER_CHIP)
        {
            /* If socket is temporarily busy completing previous transmission, retry with brief delays */
            const TickType_t start_tick = xTaskGetTickCount();
            const TickType_t max_wait_ticks = pdMS_TO_TICKS(100U);
            while ((xTaskGetTickCount() - start_tick) <= max_wait_ticks)
            {
                sent_bytes = send(target_sn, p_slot->data, (uint16_t)p_slot->length);
                if (sent_bytes != SOCK_BUSY)
                {
                    break;
                }
                bsp_w5500_unlock();
                vTaskDelay(pdMS_TO_TICKS(1));
                if (bsp_w5500_lock(dst_chip, RELAY_WIZ_LOCK_TIMEOUT_MS) != APP_STATUS_OK)
                {
                    return APP_STATUS_BUSY;
                }
            }
        }
    }
    else
    {
        if (getSn_SR(PROTO_SOCKET_UDP) == SOCK_UDP)
        {
            const TickType_t start_tick = xTaskGetTickCount();
            const TickType_t max_wait_ticks = pdMS_TO_TICKS(100U);
            while ((xTaskGetTickCount() - start_tick) <= max_wait_ticks)
            {
                sent_bytes = sendto(
                    PROTO_SOCKET_UDP,
                    p_slot->data,
                    (uint16_t)p_slot->length,
                    s_udp_peer_ip[dst_chip],
                    s_udp_peer_port[dst_chip]);
                if (sent_bytes != SOCK_BUSY)
                {
                    break;
                }
                bsp_w5500_unlock();
                vTaskDelay(pdMS_TO_TICKS(1));
                if (bsp_w5500_lock(dst_chip, RELAY_WIZ_LOCK_TIMEOUT_MS) != APP_STATUS_OK)
                {
                    return APP_STATUS_BUSY;
                }
            }
        }
    }

    bsp_w5500_unlock();

    if (sent_bytes <= 0)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

void relay_service_supervise_health(void)
{
    const TickType_t now_tick = xTaskGetTickCount();
    const TickType_t hang_timeout_ticks = pdMS_TO_TICKS(APP_FRAME_HANG_TIMEOUT_MS);

    for (uint32_t id = 0U; id < (uint32_t)BSP_W5500_COUNT; id++)
    {
        const bsp_w5500_id_t chip_id = (bsp_w5500_id_t)id;
        bool link_up = false;

        if (bsp_w5500_is_link_up(chip_id, &link_up) != APP_STATUS_OK)
        {
            continue;
        }

        if (bsp_w5500_lock(chip_id, RELAY_WIZ_LOCK_TIMEOUT_MS) != APP_STATUS_OK)
        {
            continue;
        }

        /* Close all sockets immediately if physical Ethernet link drops (Ref: Section IV-B) */
        if ((link_up == false) && (s_prev_link_up[id] == true))
        {
            for (uint8_t sn = 0U; sn < PROTO_SOCKETS_PER_CHIP; sn++)
            {
                (void)close(sn);
            }
        }
        else if (link_up == true)
        {
            for (uint32_t idx = 0U; idx < PROTO_SOCKETS_PER_CHIP; idx++)
            {
                const proto_socket_cfg_t *p_cfg = &s_socket_cfg[idx];
                const uint8_t sn = p_cfg->socket_num;

                /* Recover hung TCP socket if partial frame stalls > 5000 ms (Ref: Section IV-D) */
                if ((p_cfg->protocol == PROTO_TYPE_TCP_SERVER) &&
                    (s_sock_watch[id][idx].has_pending_data == true) &&
                    ((now_tick - s_sock_watch[id][idx].last_rx_tick) >= hang_timeout_ticks))
                {
                    (void)disconnect(sn);
                    (void)close(sn);
                    s_sock_watch[id][idx].has_pending_data = false;
                }

                relay_maintain_socket_state(p_cfg);
            }
        }
        else
        {
            /* Link remains DOWN */
        }

        s_prev_link_up[id] = link_up;
        bsp_w5500_unlock();
    }
}
