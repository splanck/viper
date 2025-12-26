#pragma once

/**
 * @file ethernet.hpp
 * @brief Ethernet (Layer 2) framing and demultiplexing.
 *
 * @details
 * Provides a small Ethernet layer responsible for:
 * - Constructing and transmitting Ethernet II frames via the NIC driver.
 * - Validating received frames and dispatching payloads based on ethertype.
 *
 * The implementation currently supports IPv4 and ARP ethertypes and uses the
 * virtio-net driver for I/O.
 */

#include "../net.hpp"

namespace net
{
namespace eth
{

/**
 * @brief Ethernet II header (14 bytes).
 *
 * @details
 * This header precedes Ethernet payload data:
 * - Destination and source MAC addresses.
 * - A 16-bit ethertype field identifying the payload protocol.
 */
struct EthHeader
{
    MacAddr dst;
    MacAddr src;
    u16 ethertype; // Network byte order
} __attribute__((packed));

/** @name Ethernet framing constants */
///@{
constexpr usize ETH_HEADER_SIZE = 14;
constexpr usize ETH_MIN_PAYLOAD = 46;
constexpr usize ETH_MAX_PAYLOAD = 1500;
constexpr usize ETH_MIN_FRAME = ETH_HEADER_SIZE + ETH_MIN_PAYLOAD; // 60
constexpr usize ETH_MAX_FRAME = ETH_HEADER_SIZE + ETH_MAX_PAYLOAD; // 1514

///@}

/**
 * @brief Ethertype values for common payload protocols.
 *
 * @details
 * Ethertype constants are expressed as their canonical on-the-wire values
 * (big-endian). Callers should still pass values in host order to helpers such
 * as @ref tx_frame; the Ethernet layer converts as needed.
 */
namespace ethertype
{
constexpr u16 IPV4 = 0x0800;
constexpr u16 ARP = 0x0806;
constexpr u16 IPV6 = 0x86DD;
} // namespace ethertype

/**
 * @brief Transmit an Ethernet frame.
 *
 * @details
 * Builds an Ethernet II frame in an internal buffer and sends it via the
 * virtio-net driver. If the payload is smaller than the minimum Ethernet frame
 * payload size, the frame is padded with zeros to meet the minimum size.
 *
 * @param dst Destination MAC address.
 * @param ethertype Payload ethertype (e.g., @ref ethertype::IPV4).
 * @param payload Pointer to payload bytes.
 * @param len Payload length in bytes.
 * @return `true` if the frame was queued for transmit, otherwise `false`.
 */
bool tx_frame(const MacAddr &dst, u16 ethertype, const void *payload, usize len);

/**
 * @brief Process a received Ethernet frame.
 *
 * @details
 * Validates the frame length, filters by destination MAC address (our MAC,
 * broadcast, or multicast), and dispatches the payload to the appropriate
 * protocol handler based on ethertype.
 *
 * This function is typically called from @ref net::network_poll.
 *
 * @param frame Pointer to the full Ethernet frame.
 * @param len Length of the frame in bytes.
 */
void rx_frame(const void *frame, usize len);

/**
 * @brief Initialize the Ethernet layer.
 *
 * @details
 * Currently a lightweight bring-up hook that prints diagnostics. Higher-level
 * configuration is handled by @ref net::netif_init.
 */
void eth_init();

} // namespace eth
} // namespace net
