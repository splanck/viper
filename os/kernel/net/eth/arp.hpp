#pragma once

/**
 * @file arp.hpp
 * @brief ARP (Address Resolution Protocol) for IPv4 over Ethernet.
 *
 * @details
 * The ARP layer maps IPv4 addresses to Ethernet MAC addresses. ViperOS uses ARP
 * to resolve the next-hop MAC address before transmitting IPv4 packets.
 *
 * This implementation provides:
 * - A small in-memory ARP cache with time-based expiration.
 * - Parsing of incoming ARP requests/replies to populate the cache.
 * - Transmission of broadcast ARP requests when a mapping is missing.
 *
 * The current implementation is designed for QEMU user-mode networking and is
 * intentionally minimal (no ARP probe/announcement logic beyond caching).
 */

#include "../net.hpp"

namespace net
{
namespace arp
{

/**
 * @brief ARP packet header for Ethernet/IPv4.
 *
 * @details
 * This header format matches ARP as used on Ethernet for IPv4. Fields are
 * encoded in network byte order where applicable. The structure is packed to
 * match on-the-wire layout.
 */
struct ArpHeader
{
    u16 htype;    // Hardware type (1 = Ethernet)
    u16 ptype;    // Protocol type (0x0800 = IPv4)
    u8 hlen;      // Hardware address length (6 for Ethernet)
    u8 plen;      // Protocol address length (4 for IPv4)
    u16 oper;     // Operation (1=request, 2=reply)
    MacAddr sha;  // Sender hardware address
    Ipv4Addr spa; // Sender protocol address
    MacAddr tha;  // Target hardware address
    Ipv4Addr tpa; // Target protocol address
} __attribute__((packed));

/** @brief ARP operation codes. */
namespace oper
{
constexpr u16 REQUEST = 1;
constexpr u16 REPLY = 2;
} // namespace oper

/** @brief ARP hardware type codes. */
namespace htype
{
constexpr u16 ETHERNET = 1;
}

/**
 * @brief Initialize the ARP layer and clear the cache.
 *
 * @details
 * Resets the ARP cache to an empty state. Should be called once during network
 * stack initialization.
 */
void arp_init();

/**
 * @brief Process a received ARP packet.
 *
 * @details
 * Validates the ARP header for Ethernet/IPv4, updates the ARP cache with the
 * sender's mapping, and responds to ARP requests directed at our IPv4 address.
 *
 * @param data Pointer to the ARP packet payload (after Ethernet header).
 * @param len Length of the payload in bytes.
 */
void rx_packet(const void *data, usize len);

/**
 * @brief Resolve an IPv4 address to a MAC address using the cache.
 *
 * @details
 * Checks the local ARP cache for a valid mapping. If the mapping is missing or
 * expired, an ARP request is transmitted and the function returns `false`.
 * Callers should retry later after the reply has been received and processed.
 *
 * @param ip IPv4 address to resolve (typically next hop).
 * @param mac_out Output MAC address on cache hit.
 * @return `true` if a valid cache entry was found, otherwise `false`.
 */
bool resolve(const Ipv4Addr &ip, MacAddr *mac_out);

/**
 * @brief Transmit an ARP request for a target IPv4 address.
 *
 * @details
 * Broadcasts an ARP request on the local network asking for the MAC address
 * associated with `target_ip`.
 *
 * @param target_ip IPv4 address to query.
 */
void send_request(const Ipv4Addr &target_ip);

/**
 * @brief Add or update an entry in the ARP cache.
 *
 * @details
 * Inserts a mapping from `ip` to `mac`, updating timestamp and replacing an
 * existing entry if present. If the cache is full, the implementation replaces
 * the oldest entry.
 *
 * @param ip IPv4 address.
 * @param mac Corresponding MAC address.
 */
void cache_add(const Ipv4Addr &ip, const MacAddr &mac);

/**
 * @brief Print the current ARP cache to the serial console.
 *
 * @details
 * Prints non-expired cache entries along with their approximate age.
 */
void print_cache();

} // namespace arp
} // namespace net
