#pragma once

/**
 * @file icmpv6.hpp
 * @brief ICMPv6 implementation for IPv6 control messages.
 *
 * @details
 * Implements ICMPv6 (RFC 4443) including:
 * - Echo Request/Reply (ping6)
 * - Neighbor Discovery Protocol (RFC 4861):
 *   - Neighbor Solicitation/Advertisement
 *   - Router Solicitation/Advertisement
 */

#include "../net.hpp"

namespace net
{
namespace icmpv6
{

/**
 * @brief ICMPv6 message header.
 */
struct Icmpv6Header
{
    u8 type;
    u8 code;
    u16 checksum;
    // Message-specific data follows
} __attribute__((packed));

constexpr usize ICMPV6_HEADER_SIZE = 4;

/**
 * @brief ICMPv6 message types.
 */
namespace type
{
// Error messages (0-127)
constexpr u8 DEST_UNREACHABLE = 1;
constexpr u8 PACKET_TOO_BIG = 2;
constexpr u8 TIME_EXCEEDED = 3;
constexpr u8 PARAM_PROBLEM = 4;

// Informational messages (128-255)
constexpr u8 ECHO_REQUEST = 128;
constexpr u8 ECHO_REPLY = 129;

// Neighbor Discovery messages
constexpr u8 ROUTER_SOLICITATION = 133;
constexpr u8 ROUTER_ADVERTISEMENT = 134;
constexpr u8 NEIGHBOR_SOLICITATION = 135;
constexpr u8 NEIGHBOR_ADVERTISEMENT = 136;
constexpr u8 REDIRECT = 137;
} // namespace type

/**
 * @brief Echo Request/Reply message.
 */
struct EchoMessage
{
    Icmpv6Header header;
    u16 identifier;
    u16 sequence;
    // Data follows
} __attribute__((packed));

/**
 * @brief Neighbor Solicitation message.
 */
struct NeighborSolicitation
{
    Icmpv6Header header;
    u32 reserved;
    Ipv6Addr target; // Target address being queried
    // Options follow
} __attribute__((packed));

constexpr usize NS_SIZE = ICMPV6_HEADER_SIZE + 4 + 16;

/**
 * @brief Neighbor Advertisement message.
 */
struct NeighborAdvertisement
{
    Icmpv6Header header;
    u8 flags; // R|S|O flags in high bits
    u8 reserved[3];
    Ipv6Addr target; // Target address
    // Options follow
} __attribute__((packed));

constexpr usize NA_SIZE = ICMPV6_HEADER_SIZE + 4 + 16;

// NA flags
constexpr u8 NA_FLAG_ROUTER = 0x80;
constexpr u8 NA_FLAG_SOLICITED = 0x40;
constexpr u8 NA_FLAG_OVERRIDE = 0x20;

/**
 * @brief Router Solicitation message.
 */
struct RouterSolicitation
{
    Icmpv6Header header;
    u32 reserved;
    // Options follow
} __attribute__((packed));

/**
 * @brief Router Advertisement message.
 */
struct RouterAdvertisement
{
    Icmpv6Header header;
    u8 cur_hop_limit;
    u8 flags; // M|O flags
    u16 router_lifetime;
    u32 reachable_time;
    u32 retrans_timer;
    // Options follow
} __attribute__((packed));

// RA flags
constexpr u8 RA_FLAG_MANAGED = 0x80;
constexpr u8 RA_FLAG_OTHER = 0x40;

/**
 * @brief NDP option header.
 */
struct NdpOption
{
    u8 type;
    u8 length; // In units of 8 bytes
    // Data follows
} __attribute__((packed));

/**
 * @brief NDP option types.
 */
namespace ndp_option
{
constexpr u8 SOURCE_LINK_ADDR = 1;
constexpr u8 TARGET_LINK_ADDR = 2;
constexpr u8 PREFIX_INFO = 3;
constexpr u8 REDIRECTED_HEADER = 4;
constexpr u8 MTU = 5;
} // namespace ndp_option

/**
 * @brief Source/Target Link-Layer Address option.
 */
struct LinkLayerAddrOption
{
    u8 type;
    u8 length;
    MacAddr addr;
} __attribute__((packed));

constexpr usize LLA_OPTION_SIZE = 8;

/**
 * @brief Neighbor cache entry.
 */
struct NeighborEntry
{
    Ipv6Addr ip;
    MacAddr mac;
    u64 timestamp;
    bool valid;
    bool router;
};

/** @brief Maximum neighbor cache entries. */
constexpr usize MAX_NEIGHBORS = 32;

/**
 * @brief Initialize ICMPv6 layer.
 */
void icmpv6_init();

/**
 * @brief Process a received ICMPv6 message.
 *
 * @param src Source IPv6 address.
 * @param data Pointer to ICMPv6 header.
 * @param len Length in bytes.
 */
void rx_packet(const Ipv6Addr &src, const void *data, usize len);

/**
 * @brief Send an ICMPv6 Echo Request (ping6).
 *
 * @param dst Destination IPv6 address.
 * @param seq Sequence number.
 * @return true if sent successfully.
 */
bool send_echo_request(const Ipv6Addr &dst, u16 seq);

/**
 * @brief Send a Neighbor Solicitation.
 *
 * @param target Target IPv6 address to query.
 * @return true if sent successfully.
 */
bool send_neighbor_solicitation(const Ipv6Addr &target);

/**
 * @brief Send a Router Solicitation.
 *
 * @return true if sent successfully.
 */
bool send_router_solicitation();

/**
 * @brief Look up a neighbor's MAC address.
 *
 * @param ip IPv6 address to look up.
 * @param mac Output: MAC address if found.
 * @return true if found in cache, false otherwise.
 */
bool lookup_neighbor(const Ipv6Addr &ip, MacAddr *mac);

/**
 * @brief Resolve an IPv6 address to MAC.
 *
 * @details
 * If the address is in the cache, returns immediately.
 * Otherwise sends a Neighbor Solicitation and returns false.
 *
 * @param ip IPv6 address to resolve.
 * @param mac Output: MAC address if resolved.
 * @return true if resolved, false if resolution pending.
 */
bool resolve_neighbor(const Ipv6Addr &ip, MacAddr *mac);

/**
 * @brief Compute ICMPv6 checksum (with pseudo-header).
 *
 * @param src Source IPv6 address.
 * @param dst Destination IPv6 address.
 * @param data ICMPv6 message.
 * @param len Message length.
 * @return Checksum value.
 */
u16 compute_checksum(const Ipv6Addr &src, const Ipv6Addr &dst, const void *data, usize len);

/**
 * @brief Get count of entries in neighbor cache.
 */
u32 get_neighbor_count();

} // namespace icmpv6
} // namespace net
