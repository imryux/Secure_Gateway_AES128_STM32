/**
 * @file    net_service.c
 * @brief   Network initialization and socket state/interrupt test implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "net_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "w5500.h"

#include "app_config.h"
#include "bsp_w5500.h"
#include "log_service.h"
#include "protocol_config.h"

#define NET_SERVICE_LOCK_TIMEOUT_MS    (100U)
#define NET_SERVICE_TEST_BUF_SIZE      (128U)

/* Centralized static socket configuration table (Ref: Section III-C in Reference Paper) */
static const proto_socket_cfg_t s_socket_table[PROTO_SOCKETS_PER_CHIP] =
{
    {PROTO_SOCKET_TCP_MAIN,   PROTO_TYPE_TCP_SERVER, PROTO_TCP_RELAY_PORT, true},
    {PROTO_SOCKET_TCP_BACKUP, PROTO_TYPE_TCP_SERVER, PROTO_TCP_RELAY_PORT, true},
    {PROTO_SOCKET_UDP,        PROTO_TYPE_UDP_PEER,   PROTO_UDP_RELAY_PORT, true}
};

static volatile uint32_t s_exti_irq_count[BSP_W5500_COUNT] = {0U, 0U};
static uint8_t s_rx_test_buf[NET_SERVICE_TEST_BUF_SIZE];

static void net_service_on_w5500_exti_isr(bsp_w5500_id_t chip_id)
{
    if (chip_id < BSP_W5500_COUNT)
    {
        s_exti_irq_count[chip_id]++;
    }
}

static void net_service_ensure_socket_open(const proto_socket_cfg_t *p_cfg)
{
    const uint8_t sn = p_cfg->socket_num;
    const uint8_t sock_status = getSn_SR(sn);

    if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
    {
        if (sock_status == SOCK_CLOSED)
        {
            if (socket(sn, Sn_MR_TCP, p_cfg->local_port, SF_IO_NONBLOCK) == (int8_t)sn)
            {
                (void)listen(sn);
            }
        }
        else if (sock_status == SOCK_INIT)
        {
            (void)listen(sn);
        }
        else if (sock_status == SOCK_CLOSE_WAIT)
        {
            (void)close(sn);
        }
        else
        {
            /* SOCK_LISTEN or SOCK_ESTABLISHED: keep active */
        }
    }
    else
    {
        if (sock_status == SOCK_CLOSED)
        {
            (void)socket(sn, Sn_MR_UDP, p_cfg->local_port, SF_IO_NONBLOCK);
        }
    }
}

app_status_t net_service_init(void)
{
    const bsp_w5500_net_cfg_t net_cfg_a =
    {
        .mac     = {PROTO_NET_A_MAC_0, PROTO_NET_A_MAC_1, PROTO_NET_A_MAC_2,
                    PROTO_NET_A_MAC_3, PROTO_NET_A_MAC_4, PROTO_NET_A_MAC_5},
        .ip      = {PROTO_NET_A_IP_0, PROTO_NET_A_IP_1, PROTO_NET_A_IP_2, PROTO_NET_A_IP_3},
        .subnet  = {PROTO_NET_A_SN_0, PROTO_NET_A_SN_1, PROTO_NET_A_SN_2, PROTO_NET_A_SN_3},
        .gateway = {PROTO_NET_A_GW_0, PROTO_NET_A_GW_1, PROTO_NET_A_GW_2, PROTO_NET_A_GW_3},
        .dns     = {0U, 0U, 0U, 0U}
    };

    const bsp_w5500_net_cfg_t net_cfg_b =
    {
        .mac     = {PROTO_NET_B_MAC_0, PROTO_NET_B_MAC_1, PROTO_NET_B_MAC_2,
                    PROTO_NET_B_MAC_3, PROTO_NET_B_MAC_4, PROTO_NET_B_MAC_5},
        .ip      = {PROTO_NET_B_IP_0, PROTO_NET_B_IP_1, PROTO_NET_B_IP_2, PROTO_NET_B_IP_3},
        .subnet  = {PROTO_NET_B_SN_0, PROTO_NET_B_SN_1, PROTO_NET_B_SN_2, PROTO_NET_B_SN_3},
        .gateway = {PROTO_NET_B_GW_0, PROTO_NET_B_GW_1, PROTO_NET_B_GW_2, PROTO_NET_B_GW_3},
        .dns     = {0U, 0U, 0U, 0U}
    };

    app_status_t status = bsp_w5500_configure_network(BSP_W5500_ID_A, &net_cfg_a);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = bsp_w5500_configure_network(BSP_W5500_ID_B, &net_cfg_b);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    bsp_w5500_register_exti_callback(net_service_on_w5500_exti_isr);

    /* Open all 3 configured sockets on each W5500 chip */
    for (uint32_t id = 0U; id < (uint32_t)BSP_W5500_COUNT; id++)
    {
        const bsp_w5500_id_t chip_id = (bsp_w5500_id_t)id;

        status = bsp_w5500_lock(chip_id, NET_SERVICE_LOCK_TIMEOUT_MS);
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        for (uint32_t idx = 0U; idx < PROTO_SOCKETS_PER_CHIP; idx++)
        {
            net_service_ensure_socket_open(&s_socket_table[idx]);
        }

        bsp_w5500_unlock();

        status = bsp_w5500_enable_socket_interrupts(chip_id);
        if (status != APP_STATUS_OK)
        {
            return status;
        }
    }

    (void)log_service_print(
        "Net Init OK | W5500_A: %u.%u.%u.%u | W5500_B: %u.%u.%u.%u\r\n",
        PROTO_NET_A_IP_0, PROTO_NET_A_IP_1, PROTO_NET_A_IP_2, PROTO_NET_A_IP_3,
        PROTO_NET_B_IP_0, PROTO_NET_B_IP_1, PROTO_NET_B_IP_2, PROTO_NET_B_IP_3);

    return APP_STATUS_OK;
}

app_status_t net_service_service_chip(bsp_w5500_id_t chip_id)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    app_status_t status = bsp_w5500_lock(chip_id, NET_SERVICE_LOCK_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    for (uint32_t idx = 0U; idx < PROTO_SOCKETS_PER_CHIP; idx++)
    {
        const proto_socket_cfg_t *p_cfg = &s_socket_table[idx];
        const uint8_t sn = p_cfg->socket_num;

        net_service_ensure_socket_open(p_cfg);

        /* Clear pending socket interrupt flags so W5500 INTn can trigger again */
        const uint8_t ir_flags = getSn_IR(sn);
        if (ir_flags != 0U)
        {
            setSn_IR(sn, ir_flags);
        }

        const uint16_t rx_avail = getSn_RX_RSR(sn);
        if (rx_avail > 0U)
        {
            uint16_t read_len = rx_avail;
            if (read_len > (uint16_t)(NET_SERVICE_TEST_BUF_SIZE - 1U))
            {
                read_len = (uint16_t)(NET_SERVICE_TEST_BUF_SIZE - 1U);
            }

            int32_t recvd = 0;
            if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
            {
                recvd = recv(sn, s_rx_test_buf, read_len);
            }
            else
            {
                uint8_t peer_ip[4] = {0U, 0U, 0U, 0U};
                uint16_t peer_port = 0U;
                recvd = recvfrom(sn, s_rx_test_buf, read_len, peer_ip, &peer_port);
            }

            if (recvd > 0)
            {
                s_rx_test_buf[recvd] = (uint8_t)'\0';
                bsp_w5500_unlock();

                (void)log_service_print(
                    "RX Test | Chip=%c | Sock=%u | Len=%ld | Data=%s\r\n",
                    (chip_id == BSP_W5500_ID_A) ? 'A' : 'B',
                    (unsigned int)sn,
                    (long)recvd,
                    (const char *)s_rx_test_buf);

                status = bsp_w5500_lock(chip_id, NET_SERVICE_LOCK_TIMEOUT_MS);
                if (status != APP_STATUS_OK)
                {
                    return status;
                }
            }
        }
    }

    bsp_w5500_unlock();
    return APP_STATUS_OK;
}

uint32_t net_service_fetch_exti_count(bsp_w5500_id_t chip_id)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    const uint32_t count = s_exti_irq_count[chip_id];
    s_exti_irq_count[chip_id] = 0U;
    taskEXIT_CRITICAL();

    return count;
}
