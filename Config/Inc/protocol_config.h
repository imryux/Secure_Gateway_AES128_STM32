/**
 * @file    protocol_config.h
 * @brief   Static network and socket configuration table for dual W5500 ports.
 * @version 1.0
 * @date    2026-11-12
 */

#ifndef PROTOCOL_CONFIG_H
#define PROTOCOL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Number of active sockets per W5500 chip (Ref: Table II in Reference Paper) */
#define PROTO_SOCKETS_PER_CHIP        (3U)
#define PROTO_SOCKET_TCP_MAIN         (0U)
#define PROTO_SOCKET_TCP_BACKUP       (1U)
#define PROTO_SOCKET_UDP              (2U)

/* Service Ports */
#define PROTO_TCP_RELAY_PORT          (5000U)
#define PROTO_UDP_RELAY_PORT          (5001U)

/* Network A (W5500 #1 on SPI1) Static IPv4 Configuration */
#define PROTO_NET_A_MAC_0             (0x00U)
#define PROTO_NET_A_MAC_1             (0x08U)
#define PROTO_NET_A_MAC_2             (0xDCU)
#define PROTO_NET_A_MAC_3             (0x10U)
#define PROTO_NET_A_MAC_4             (0x10U)
#define PROTO_NET_A_MAC_5             (0x01U)

#define PROTO_NET_A_IP_0              (192U)
#define PROTO_NET_A_IP_1              (168U)
#define PROTO_NET_A_IP_2              (10U)
#define PROTO_NET_A_IP_3              (2U)

#define PROTO_NET_A_SN_0              (255U)
#define PROTO_NET_A_SN_1              (255U)
#define PROTO_NET_A_SN_2              (255U)
#define PROTO_NET_A_SN_3              (0U)

#define PROTO_NET_A_GW_0              (192U)
#define PROTO_NET_A_GW_1              (168U)
#define PROTO_NET_A_GW_2              (10U)
#define PROTO_NET_A_GW_3              (1U)

/* Network B (W5500 #2 on SPI2) Static IPv4 Configuration */
#define PROTO_NET_B_MAC_0             (0x00U)
#define PROTO_NET_B_MAC_1             (0x08U)
#define PROTO_NET_B_MAC_2             (0xDCU)
#define PROTO_NET_B_MAC_3             (0x20U)
#define PROTO_NET_B_MAC_4             (0x20U)
#define PROTO_NET_B_MAC_5             (0x02U)

#define PROTO_NET_B_IP_0              (192U)
#define PROTO_NET_B_IP_1              (168U)
#define PROTO_NET_B_IP_2              (20U)
#define PROTO_NET_B_IP_3              (2U)

#define PROTO_NET_B_SN_0              (255U)
#define PROTO_NET_B_SN_1              (255U)
#define PROTO_NET_B_SN_2              (255U)
#define PROTO_NET_B_SN_3              (0U)

#define PROTO_NET_B_GW_0              (192U)
#define PROTO_NET_B_GW_1              (168U)
#define PROTO_NET_B_GW_2              (20U)
#define PROTO_NET_B_GW_3              (1U)

/**
 * @brief Supported transport protocol for each configured W5500 socket.
 */
typedef enum
{
    PROTO_TYPE_TCP_SERVER = 0,
    PROTO_TYPE_UDP_PEER   = 1
} proto_socket_type_t;

/**
 * @brief Static configuration entry for one hardware socket (Ref: Section III-C).
 */
typedef struct
{
    uint8_t socket_num;
    proto_socket_type_t protocol;
    uint16_t local_port;
    bool reconnect_enable;
} proto_socket_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_CONFIG_H */
