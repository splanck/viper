//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperos/net_stats.hpp
// Purpose: Network statistics structure for SYS_NET_STATS syscall.
// Key invariants: ABI-stable; counters cumulative since boot.
// Ownership/Lifetime: Shared; included by kernel and user-space.
// Links: kernel/net/netstack.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file net_stats.hpp
 * @brief Network statistics structure for SYS_NET_STATS syscall.
 *
 * @details
 * This header defines the structure returned by the network statistics syscall.
 * It provides counters for packets and bytes at each protocol layer, as well as
 * error counts and active connection information.
 */

/**
 * @brief Network stack statistics.
 *
 * @details
 * Contains counters for network activity at various protocol layers. All counters
 * are cumulative since boot (or network init). User-space can periodically query
 * these to compute rates.
 */
struct NetStats
{
    /* Ethernet layer */
    unsigned long long eth_rx_packets;  /**< Ethernet frames received. */
    unsigned long long eth_tx_packets;  /**< Ethernet frames transmitted. */
    unsigned long long eth_rx_bytes;    /**< Ethernet bytes received. */
    unsigned long long eth_tx_bytes;    /**< Ethernet bytes transmitted. */
    unsigned long long eth_rx_errors;   /**< Receive errors (CRC, too short, etc). */
    unsigned long long eth_tx_errors;   /**< Transmit errors. */
    unsigned long long eth_rx_dropped;  /**< Dropped due to no buffer space. */

    /* ARP layer */
    unsigned long long arp_requests;    /**< ARP requests sent. */
    unsigned long long arp_replies;     /**< ARP replies received. */

    /* IPv4 layer */
    unsigned long long ip_rx_packets;   /**< IP packets received. */
    unsigned long long ip_tx_packets;   /**< IP packets transmitted. */
    unsigned long long ip_rx_bytes;     /**< IP payload bytes received. */
    unsigned long long ip_tx_bytes;     /**< IP payload bytes transmitted. */

    /* ICMP layer */
    unsigned long long icmp_rx;         /**< ICMP messages received. */
    unsigned long long icmp_tx;         /**< ICMP messages transmitted. */

    /* UDP layer */
    unsigned long long udp_rx_packets;  /**< UDP datagrams received. */
    unsigned long long udp_tx_packets;  /**< UDP datagrams transmitted. */

    /* TCP layer */
    unsigned long long tcp_rx_segments; /**< TCP segments received. */
    unsigned long long tcp_tx_segments; /**< TCP segments transmitted. */
    unsigned long long tcp_retransmits; /**< TCP retransmissions. */
    unsigned int tcp_active_conns;      /**< Currently active TCP connections. */
    unsigned int tcp_listen_sockets;    /**< TCP sockets in LISTEN state. */

    /* DNS layer */
    unsigned long long dns_queries;     /**< DNS queries sent. */
    unsigned long long dns_responses;   /**< DNS responses received. */

    /* TLS layer */
    unsigned long long tls_handshakes;  /**< TLS handshakes completed. */
    unsigned long long tls_rx_records;  /**< TLS records received. */
    unsigned long long tls_tx_records;  /**< TLS records transmitted. */

    unsigned int _reserved[8];          /**< Reserved for future use. */
};
