#pragma once

#include "../../include/viperos/net_stats.hpp"

namespace net
{

/**
 * @file network.hpp
 * @brief High-level network stack entry points.
 *
 * @details
 * The ViperOS network stack is structured in protocol layers (Ethernet, ARP,
 * IPv4, ICMP, UDP, TCP, DNS, HTTP). This header exposes the top-level
 * initialization and polling functions that tie those layers together and
 * integrate with the virtio-net device driver.
 *
 * The current design is polled rather than interrupt-driven: callers are
 * expected to periodically call @ref network_poll from a timer interrupt or the
 * main loop to drain received frames and drive protocol timeouts.
 */

/**
 * @brief Initialize the network stack.
 *
 * @details
 * Initializes the network interface configuration and each protocol layer in
 * dependency order. If no virtio-net device is present, the function returns
 * without enabling networking.
 */
void network_init();

/**
 * @brief Poll for network activity and process received frames.
 *
 * @details
 * Drains the virtio-net receive queue into an internal buffer and dispatches
 * each received Ethernet frame to the Ethernet layer for parsing and further
 * protocol demultiplexing.
 *
 * This function is safe to call frequently; if no data is available it returns
 * quickly.
 */
void network_poll();

/**
 * @brief Get current network statistics.
 *
 * @details
 * Fills the provided NetStats structure with cumulative counters from all
 * network protocol layers.
 *
 * @param stats Output structure to fill.
 */
void get_stats(NetStats *stats);

} // namespace net
