/**
 * @file network.cpp
 * @brief High-level network stack initialization and polling implementation.
 *
 * @details
 * Wires the virtio-net driver to the protocol stack:
 * - @ref net::network_init initializes protocol layers when a NIC is present.
 * - @ref net::network_poll drains the receive queue and forwards frames to the
 *   Ethernet demultiplexer.
 *
 * The current stack uses a simple polling model and a single static receive
 * buffer to avoid dynamic allocation during early boot.
 */

#include "network.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/net.hpp"
#include "dns/dns.hpp"
#include "eth/arp.hpp"
#include "eth/ethernet.hpp"
#include "http/http.hpp"
#include "ip/icmp.hpp"
#include "ip/ipv4.hpp"
#include "ip/ipv6.hpp"
#include "ip/tcp.hpp"
#include "ip/udp.hpp"
#include "netif.hpp"

namespace net
{

// Receive buffer (aligned for struct access)
static u8 rx_buffer[2048] __attribute__((aligned(4)));

// Global network statistics
NetStats g_stats = {};

/** @copydoc net::network_init */
void network_init()
{
    if (!virtio::net_device())
    {
        serial::puts("[net] No network device, skipping network init\n");
        return;
    }

    serial::puts("[net] Initializing network stack\n");

    // Initialize layers (order matters)
    netif_init();
    eth::eth_init();
    arp::arp_init();
    ip::ip_init();
    ipv6::ipv6_init();
    icmp::icmp_init();
    udp::udp_init();
    tcp::tcp_init();
    dns::dns_init();
    http::http_init();

    serial::puts("[net] Network stack initialized\n");
}

/** @copydoc net::network_poll */
void network_poll()
{
    if (!virtio::net_device())
    {
        return;
    }

    // Poll for received packets
    while (true)
    {
        i32 len = virtio::net_device()->receive(rx_buffer, sizeof(rx_buffer));
        if (len <= 0)
        {
            break;
        }

        // Process Ethernet frame
        eth::rx_frame(rx_buffer, len);
    }

    // Check for TCP retransmissions
    tcp::check_retransmit();
}

/** @copydoc net::get_stats */
void get_stats(NetStats *stats)
{
    if (!stats)
        return;

    // Copy the global stats
    *stats = g_stats;

    // Get TCP connection counts
    stats->tcp_active_conns = tcp::get_active_count();
    stats->tcp_listen_sockets = tcp::get_listen_count();
}

} // namespace net
