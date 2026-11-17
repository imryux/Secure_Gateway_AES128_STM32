/**
 * @file    protocol_config.h
 * @brief   Static network and socket configuration table for dual W5500 ports.
 *          Defines MAC addresses, static IPv4 addresses, subnet masks, gateways,
 *          and port numbers for both Network A (W5500 #1) and Network B (W5500 #2).
 * @version 1.1
 * @date    2026-11-17
 */

#ifndef PROTOCOL_CONFIG_H
#define PROTOCOL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/* Hardware Socket Allocations per W5500 chip                                 */
/* Each W5500 supports 8 hardware sockets; this gateway activates sockets 0..2 */
/* -------------------------------------------------------------------------- */
#define PROTO_SOCKETS_PER_CHIP        (3U) /**< Total active hardware sockets per chip */
#define PROTO_SOCKET_TCP_MAIN         (0U) /**< Primary TCP server listening socket */
#define PROTO_SOCKET_TCP_BACKUP       (1U) /**< Secondary/backup TCP server socket (same port) */
#define PROTO_SOCKET_UDP              (2U) /**< Dedicated UDP relay socket */

/* -------------------------------------------------------------------------- */
/* Service Port Numbers                                                       */
/* -------------------------------------------------------------------------- */
#define PROTO_TCP_RELAY_PORT          (5000U) /**< Listening port for TCP relay traffic */
#define PROTO_UDP_RELAY_PORT          (5001U) /**< Listening port for UDP relay traffic */

/* -------------------------------------------------------------------------- */
/* Network A Configuration: W5500 #1 connected via SPI1                       */
/* Default IP: 192.168.10.2 / 24, Gateway: 192.168.10.1                       */
/* -------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------- */
/* Network B Configuration: W5500 #2 connected via SPI2                       */
/* Default IP: 192.168.20.2 / 24, Gateway: 192.168.20.1                       */
/* -------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------- */
/* Default Fallback Destination Peer IPs for UDP Outbound Forwarding          */
/* Used before an inbound packet is received to dynamically learn peer IP     */
/* -------------------------------------------------------------------------- */
#define PROTO_NET_A_PEER_IP_0         (192U)
#define PROTO_NET_A_PEER_IP_1         (168U)
#define PROTO_NET_A_PEER_IP_2         (10U)
#define PROTO_NET_A_PEER_IP_3         (10U)

#define PROTO_NET_B_PEER_IP_0         (192U)
#define PROTO_NET_B_PEER_IP_1         (168U)
#define PROTO_NET_B_PEER_IP_2         (20U)
#define PROTO_NET_B_PEER_IP_3         (10U)

/**
 * @brief Supported transport protocols for W5500 hardware sockets.
 */
typedef enum
{
    PROTO_TYPE_TCP_SERVER = 0, /**< Socket operates as a listening TCP server */
    PROTO_TYPE_UDP_PEER   = 1  /**< Socket operates as a bidirectional UDP peer */
} proto_socket_type_t;

/**
 * @brief Static configuration record for one hardware socket.
 */
typedef struct
{
    uint8_t socket_num;          /**< Hardware socket index (0..7 on W5500) */
    proto_socket_type_t protocol;/**< Transport protocol: TCP Server or UDP */
    uint16_t local_port;         /**< Local port bound on this socket */
    bool reconnect_enable;       /**< True to automatically reopen if disconnected */
} proto_socket_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_CONFIG_H */
