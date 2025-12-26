#pragma once

/**
 * @file ipv4.hpp
 * @brief IPv4 (Layer 3) header definitions and send/receive helpers.
 *
 * @details
 * Provides a minimal IPv4 implementation sufficient to support ICMP, UDP and
 * TCP on a single interface:
 * - Parse and validate received IPv4 packets and dispatch by protocol.
 * - Construct and transmit IPv4 packets (no fragmentation, no options).
 *
 * The implementation assumes Ethernet as the underlying link layer and relies
 * on ARP for next-hop address resolution.
 */

#include "../net.hpp"

namespace net
{
namespace ip
{

/**
 * @brief IPv4 header (without options).
 *
 * @details
 * The header is packed to match the on-the-wire layout. The implementation
 * supports IHL values >= 5 (20 bytes) but ignores options beyond skipping over
 * them for payload extraction.
 */
struct Ipv4Header
{
    u8 version_ihl;     // Version (4 bits) + IHL (4 bits)
    u8 dscp_ecn;        // DSCP + ECN
    u16 total_length;   // Total length
    u16 identification; // Identification
    u16 flags_fragment; // Flags (3 bits) + Fragment offset (13 bits)
    u8 ttl;             // Time to live
    u8 protocol;        // Protocol (1=ICMP, 6=TCP, 17=UDP)
    u16 checksum;       // Header checksum
    Ipv4Addr src;       // Source address
    Ipv4Addr dst;       // Destination address
    // Options may follow
} __attribute__((packed));

/** @brief Minimum IPv4 header size in bytes (IHL = 5). */
constexpr usize IPV4_HEADER_MIN = 20;

/**
 * @brief IPv4 protocol numbers for the payload.
 *
 * @details
 * Values used in the IPv4 header `protocol` field.
 */
namespace protocol
{
constexpr u8 ICMP = 1;
constexpr u8 TCP = 6;
constexpr u8 UDP = 17;
} // namespace protocol

/**
 * @brief Initialize the IPv4 layer.
 *
 * @details
 * Currently a lightweight bring-up hook that prints diagnostics.
 */
void ip_init();

/**
 * @brief Process a received IPv4 packet.
 *
 * @details
 * Validates basic header fields, filters packets not destined for the local
 * interface (except broadcast), then dispatches payload to ICMP/UDP/TCP handlers
 * based on the `protocol` field.
 *
 * @param data Pointer to the IPv4 packet (after Ethernet header).
 * @param len Total packet length in bytes.
 */
void rx_packet(const void *data, usize len);

/**
 * @brief Transmit an IPv4 packet.
 *
 * @details
 * Builds an IPv4 header, computes the header checksum, copies the payload, and
 * transmits the packet over Ethernet. The function resolves the next-hop MAC
 * address via ARP:
 * - If ARP cache lookup succeeds, the packet is sent immediately.
 * - If ARP resolution is pending, an ARP request is sent and this function
 *   returns `false` so the caller can retry later.
 *
 * Fragmentation is not implemented; packets larger than the local buffer may be
 * rejected by the caller layers.
 *
 * @param dst Destination IPv4 address.
 * @param protocol Protocol number from @ref protocol.
 * @param payload Pointer to payload data (transport header + data).
 * @param len Payload length in bytes.
 * @return `true` if the packet was transmitted, otherwise `false`.
 */
bool tx_packet(const Ipv4Addr &dst, u8 protocol, const void *payload, usize len);

} // namespace ip
} // namespace net
