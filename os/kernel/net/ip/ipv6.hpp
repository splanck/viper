#pragma once

/**
 * @file ipv6.hpp
 * @brief IPv6 (Layer 3) header definitions and send/receive helpers.
 *
 * @details
 * Provides basic IPv6 implementation sufficient to support ICMPv6 and
 * link-local communication:
 * - Parse and validate received IPv6 packets.
 * - Construct and transmit IPv6 packets.
 * - Handle extension headers (hop-by-hop, routing, fragment).
 *
 * The implementation assumes Ethernet as the underlying link layer and uses
 * ICMPv6 Neighbor Discovery for address resolution.
 */

#include "../net.hpp"

namespace net
{
namespace ipv6
{

/**
 * @brief IPv6 header (40 bytes, fixed size).
 */
struct Ipv6Header
{
    u32 version_tc_flow; // Version (4) + Traffic Class (8) + Flow Label (20)
    u16 payload_length;  // Length of payload (excludes this header)
    u8 next_header;      // Protocol number (ICMPv6=58, TCP=6, UDP=17)
    u8 hop_limit;        // TTL equivalent
    Ipv6Addr src;        // Source address
    Ipv6Addr dst;        // Destination address
} __attribute__((packed));

/** @brief IPv6 header size (always 40 bytes). */
constexpr usize IPV6_HEADER_SIZE = 40;

/** @brief Maximum transmission unit for IPv6 over Ethernet. */
constexpr usize IPV6_MTU = 1500;

/** @brief Minimum MTU required by IPv6. */
constexpr usize IPV6_MIN_MTU = 1280;

/** @brief Maximum IPv6 payload without fragmentation. */
constexpr usize IPV6_MAX_PAYLOAD = IPV6_MTU - IPV6_HEADER_SIZE;

/**
 * @brief IPv6 Next Header values (protocol numbers).
 */
namespace next_header
{
constexpr u8 HOP_BY_HOP = 0;   ///< Hop-by-Hop Options
constexpr u8 TCP = 6;          ///< TCP
constexpr u8 UDP = 17;         ///< UDP
constexpr u8 ROUTING = 43;     ///< Routing Header
constexpr u8 FRAGMENT = 44;    ///< Fragment Header
constexpr u8 ICMPV6 = 58;      ///< ICMPv6
constexpr u8 NO_NEXT = 59;     ///< No Next Header
constexpr u8 DEST_OPTIONS = 60; ///< Destination Options
} // namespace next_header

/**
 * @brief Fragment Header for IPv6.
 */
struct FragmentHeader
{
    u8 next_header;      // Next header after reassembly
    u8 reserved;         // Reserved (must be 0)
    u16 frag_offset_mf;  // Fragment offset (13 bits) + Reserved (2) + M flag (1)
    u32 identification;  // Identification
} __attribute__((packed));

constexpr usize FRAGMENT_HEADER_SIZE = 8;

/**
 * @brief Extract version from version_tc_flow field.
 */
inline u8 get_version(u32 vtf)
{
    return (ntohl(vtf) >> 28) & 0x0f;
}

/**
 * @brief Extract traffic class from version_tc_flow field.
 */
inline u8 get_traffic_class(u32 vtf)
{
    return (ntohl(vtf) >> 20) & 0xff;
}

/**
 * @brief Extract flow label from version_tc_flow field.
 */
inline u32 get_flow_label(u32 vtf)
{
    return ntohl(vtf) & 0x000fffff;
}

/**
 * @brief Build version_tc_flow field.
 */
inline u32 make_version_tc_flow(u8 version, u8 tc, u32 flow)
{
    u32 vtf = (static_cast<u32>(version) << 28) |
              (static_cast<u32>(tc) << 20) |
              (flow & 0x000fffff);
    return htonl(vtf);
}

/**
 * @brief Initialize the IPv6 layer.
 */
void ipv6_init();

/**
 * @brief Process a received IPv6 packet.
 *
 * @param data Pointer to the IPv6 packet (after Ethernet header).
 * @param len Total packet length in bytes.
 */
void rx_packet(const void *data, usize len);

/**
 * @brief Transmit an IPv6 packet.
 *
 * @param dst Destination IPv6 address.
 * @param next_header Next header value (protocol).
 * @param payload Pointer to payload data.
 * @param len Payload length in bytes.
 * @return true if packet was transmitted, false otherwise.
 */
bool tx_packet(const Ipv6Addr &dst, u8 next_header, const void *payload, usize len);

/**
 * @brief Check if IPv6 is enabled on the interface.
 */
bool is_enabled();

/**
 * @brief Get the link-local IPv6 address.
 */
Ipv6Addr get_link_local();

/**
 * @brief Get the global IPv6 address (if configured).
 */
Ipv6Addr get_global();

/**
 * @brief Set the global IPv6 address.
 */
void set_global(const Ipv6Addr &addr);

} // namespace ipv6
} // namespace net
