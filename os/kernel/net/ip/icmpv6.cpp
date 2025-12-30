/**
 * @file icmpv6.cpp
 * @brief ICMPv6 implementation including Neighbor Discovery Protocol.
 *
 * @details
 * Implements ICMPv6 (RFC 4443) and NDP (RFC 4861):
 * - Echo Request/Reply for ping6
 * - Neighbor Solicitation/Advertisement for address resolution
 * - Router Solicitation/Advertisement for router discovery
 */

#include "icmpv6.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../eth/ethernet.hpp"
#include "../netif.hpp"
#include "ipv6.hpp"

namespace net
{
namespace icmpv6
{

namespace
{

// Neighbor cache
NeighborEntry neighbor_cache[MAX_NEIGHBORS];

// Statistics
u32 g_echo_requests = 0;
u32 g_echo_replies = 0;
u32 g_ns_sent = 0;
u32 g_na_received = 0;

// Neighbor cache timeout (5 minutes)
constexpr u64 NEIGHBOR_TIMEOUT_MS = 300000;

/**
 * @brief Add or update a neighbor cache entry.
 */
void update_neighbor(const Ipv6Addr &ip, const MacAddr &mac, bool is_router)
{
    // Look for existing entry
    for (usize i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (neighbor_cache[i].valid && neighbor_cache[i].ip == ip)
        {
            copy_mac(neighbor_cache[i].mac, mac);
            neighbor_cache[i].timestamp = timer::get_ms();
            neighbor_cache[i].router = is_router;
            return;
        }
    }

    // Find empty slot or oldest entry
    usize oldest = 0;
    u64 oldest_time = 0xFFFFFFFFFFFFFFFF;

    for (usize i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (!neighbor_cache[i].valid)
        {
            oldest = i;
            break;
        }
        if (neighbor_cache[i].timestamp < oldest_time)
        {
            oldest_time = neighbor_cache[i].timestamp;
            oldest = i;
        }
    }

    // Add entry
    copy_ipv6(neighbor_cache[oldest].ip, ip);
    copy_mac(neighbor_cache[oldest].mac, mac);
    neighbor_cache[oldest].timestamp = timer::get_ms();
    neighbor_cache[oldest].valid = true;
    neighbor_cache[oldest].router = is_router;
}

/**
 * @brief Handle Echo Request (respond with Echo Reply).
 */
void handle_echo_request(const Ipv6Addr &src, const EchoMessage *msg, usize len)
{
    g_echo_requests++;

    // Build echo reply
    static u8 reply_buf[1280];
    EchoMessage *reply = reinterpret_cast<EchoMessage *>(reply_buf);

    // Copy the request, change type to reply
    usize data_len = len - sizeof(EchoMessage);
    if (data_len > sizeof(reply_buf) - sizeof(EchoMessage))
    {
        data_len = sizeof(reply_buf) - sizeof(EchoMessage);
    }

    reply->header.type = type::ECHO_REPLY;
    reply->header.code = 0;
    reply->header.checksum = 0;
    reply->identifier = msg->identifier;
    reply->sequence = msg->sequence;

    // Copy data
    const u8 *src_data = reinterpret_cast<const u8 *>(msg) + sizeof(EchoMessage);
    u8 *dst_data = reply_buf + sizeof(EchoMessage);
    for (usize i = 0; i < data_len; i++)
    {
        dst_data[i] = src_data[i];
    }

    usize reply_len = sizeof(EchoMessage) + data_len;

    // Compute checksum
    reply->header.checksum = compute_checksum(ipv6::get_link_local(), src, reply_buf, reply_len);

    // Send reply
    ipv6::tx_packet(src, ipv6::next_header::ICMPV6, reply_buf, reply_len);
}

/**
 * @brief Handle Echo Reply.
 */
void handle_echo_reply(const Ipv6Addr &src, const EchoMessage *msg)
{
    g_echo_replies++;

    serial::puts("[icmpv6] Echo reply from ");
    // Print source address (abbreviated)
    for (int i = 0; i < 16; i += 2)
    {
        if (i > 0)
            serial::puts(":");
        u16 word = (static_cast<u16>(src.bytes[i]) << 8) | src.bytes[i + 1];
        serial::put_hex(word);
    }
    serial::puts(" seq=");
    serial::put_dec(ntohs(msg->sequence));
    serial::puts("\n");
}

/**
 * @brief Handle Neighbor Solicitation.
 */
void handle_neighbor_solicitation(const Ipv6Addr &src, const NeighborSolicitation *ns, usize len)
{
    // Check if target is one of our addresses
    Ipv6Addr target;
    copy_ipv6(target, ns->target);

    Ipv6Addr our_ll = ipv6::get_link_local();
    Ipv6Addr our_global = ipv6::get_global();

    bool is_ours = (target == our_ll);
    if (!is_ours && ipv6::get_global().is_unspecified() == false)
    {
        is_ours = (target == our_global);
    }

    if (!is_ours)
    {
        return;
    }

    // Parse source link-layer address option
    MacAddr src_mac = MacAddr::zero();
    const u8 *options = reinterpret_cast<const u8 *>(ns) + NS_SIZE;
    usize opt_len = len - NS_SIZE;

    while (opt_len >= 2)
    {
        u8 opt_type = options[0];
        u8 opt_size = options[1] * 8; // Length in 8-byte units

        if (opt_size == 0 || opt_size > opt_len)
            break;

        if (opt_type == ndp_option::SOURCE_LINK_ADDR && opt_size >= LLA_OPTION_SIZE)
        {
            const LinkLayerAddrOption *lla =
                reinterpret_cast<const LinkLayerAddrOption *>(options);
            copy_mac(src_mac, lla->addr);
        }

        options += opt_size;
        opt_len -= opt_size;
    }

    // Update neighbor cache if we got a source MAC
    if (!src_mac.is_broadcast() && src_mac != MacAddr::zero())
    {
        update_neighbor(src, src_mac, false);
    }

    // Send Neighbor Advertisement
    static u8 na_buf[64];
    NeighborAdvertisement *na = reinterpret_cast<NeighborAdvertisement *>(na_buf);

    na->header.type = type::NEIGHBOR_ADVERTISEMENT;
    na->header.code = 0;
    na->header.checksum = 0;
    na->flags = NA_FLAG_SOLICITED | NA_FLAG_OVERRIDE;
    na->reserved[0] = 0;
    na->reserved[1] = 0;
    na->reserved[2] = 0;
    copy_ipv6(na->target, target);

    // Add Target Link-Layer Address option
    LinkLayerAddrOption *tlla = reinterpret_cast<LinkLayerAddrOption *>(na_buf + NA_SIZE);
    tlla->type = ndp_option::TARGET_LINK_ADDR;
    tlla->length = 1; // 8 bytes
    copy_mac(tlla->addr, netif().mac());

    usize na_len = NA_SIZE + LLA_OPTION_SIZE;

    // Compute checksum
    Ipv6Addr reply_src;
    copy_ipv6(reply_src, target);
    na->header.checksum = compute_checksum(reply_src, src, na_buf, na_len);

    // Send NA
    ipv6::tx_packet(src, ipv6::next_header::ICMPV6, na_buf, na_len);
}

/**
 * @brief Handle Neighbor Advertisement.
 */
void handle_neighbor_advertisement(const Ipv6Addr & /*src*/, const NeighborAdvertisement *na, usize len)
{
    g_na_received++;

    // Extract target link-layer address
    const u8 *options = reinterpret_cast<const u8 *>(na) + NA_SIZE;
    usize opt_len = len - NA_SIZE;

    MacAddr target_mac = MacAddr::zero();

    while (opt_len >= 2)
    {
        u8 opt_type = options[0];
        u8 opt_size = options[1] * 8;

        if (opt_size == 0 || opt_size > opt_len)
            break;

        if (opt_type == ndp_option::TARGET_LINK_ADDR && opt_size >= LLA_OPTION_SIZE)
        {
            const LinkLayerAddrOption *lla =
                reinterpret_cast<const LinkLayerAddrOption *>(options);
            copy_mac(target_mac, lla->addr);
        }

        options += opt_size;
        opt_len -= opt_size;
    }

    if (target_mac != MacAddr::zero())
    {
        Ipv6Addr target;
        copy_ipv6(target, na->target);
        bool is_router = (na->flags & NA_FLAG_ROUTER) != 0;
        update_neighbor(target, target_mac, is_router);
    }
}

/**
 * @brief Handle Router Advertisement.
 */
void handle_router_advertisement(const Ipv6Addr &src,
                                 const RouterAdvertisement *ra,
                                 usize len)
{
    serial::puts("[icmpv6] Router Advertisement from ");
    // Print source (link-local)
    serial::puts("fe80::");
    serial::put_hex(src.bytes[8]);
    serial::put_hex(src.bytes[9]);
    serial::puts("...\n");

    // Parse options for prefix information
    const u8 *options = reinterpret_cast<const u8 *>(ra) + sizeof(RouterAdvertisement);
    usize opt_len = len - sizeof(RouterAdvertisement);

    // Extract source link-layer address if present
    while (opt_len >= 2)
    {
        u8 opt_type = options[0];
        u8 opt_size = options[1] * 8;

        if (opt_size == 0 || opt_size > opt_len)
            break;

        if (opt_type == ndp_option::SOURCE_LINK_ADDR && opt_size >= LLA_OPTION_SIZE)
        {
            const LinkLayerAddrOption *lla =
                reinterpret_cast<const LinkLayerAddrOption *>(options);
            update_neighbor(src, lla->addr, true);
        }

        if (opt_type == ndp_option::PREFIX_INFO && opt_size >= 32)
        {
            // Prefix Information option
            // u8 prefix_len = options[2];
            // u8 flags = options[3];
            // Could implement SLAAC here
        }

        options += opt_size;
        opt_len -= opt_size;
    }
}

} // namespace

/** @copydoc net::icmpv6::icmpv6_init */
void icmpv6_init()
{
    // Clear neighbor cache
    for (usize i = 0; i < MAX_NEIGHBORS; i++)
    {
        neighbor_cache[i].valid = false;
    }

    serial::puts("[icmpv6] ICMPv6 layer initialized\n");
}

/** @copydoc net::icmpv6::rx_packet */
void rx_packet(const Ipv6Addr &src, const void *data, usize len)
{
    if (len < ICMPV6_HEADER_SIZE)
    {
        return;
    }

    const Icmpv6Header *hdr = static_cast<const Icmpv6Header *>(data);

    // TODO: Verify checksum

    switch (hdr->type)
    {
        case type::ECHO_REQUEST:
            if (len >= sizeof(EchoMessage))
            {
                handle_echo_request(src, static_cast<const EchoMessage *>(data), len);
            }
            break;

        case type::ECHO_REPLY:
            if (len >= sizeof(EchoMessage))
            {
                handle_echo_reply(src, static_cast<const EchoMessage *>(data));
            }
            break;

        case type::NEIGHBOR_SOLICITATION:
            if (len >= NS_SIZE)
            {
                handle_neighbor_solicitation(
                    src, static_cast<const NeighborSolicitation *>(data), len);
            }
            break;

        case type::NEIGHBOR_ADVERTISEMENT:
            if (len >= NA_SIZE)
            {
                handle_neighbor_advertisement(
                    src, static_cast<const NeighborAdvertisement *>(data), len);
            }
            break;

        case type::ROUTER_ADVERTISEMENT:
            if (len >= sizeof(RouterAdvertisement))
            {
                handle_router_advertisement(
                    src, static_cast<const RouterAdvertisement *>(data), len);
            }
            break;

        case type::ROUTER_SOLICITATION:
            // We're not a router, ignore
            break;

        default:
            // Unknown type
            break;
    }
}

/** @copydoc net::icmpv6::send_echo_request */
bool send_echo_request(const Ipv6Addr &dst, u16 seq)
{
    static u8 msg_buf[64];
    EchoMessage *msg = reinterpret_cast<EchoMessage *>(msg_buf);

    msg->header.type = type::ECHO_REQUEST;
    msg->header.code = 0;
    msg->header.checksum = 0;
    msg->identifier = htons(0x1234);
    msg->sequence = htons(seq);

    // Add some data
    u8 *data = msg_buf + sizeof(EchoMessage);
    for (int i = 0; i < 8; i++)
    {
        data[i] = static_cast<u8>(i);
    }

    usize msg_len = sizeof(EchoMessage) + 8;

    // Compute checksum
    msg->header.checksum = compute_checksum(ipv6::get_link_local(), dst, msg_buf, msg_len);

    return ipv6::tx_packet(dst, ipv6::next_header::ICMPV6, msg_buf, msg_len);
}

/** @copydoc net::icmpv6::send_neighbor_solicitation */
bool send_neighbor_solicitation(const Ipv6Addr &target)
{
    g_ns_sent++;

    static u8 ns_buf[64];
    NeighborSolicitation *ns = reinterpret_cast<NeighborSolicitation *>(ns_buf);

    ns->header.type = type::NEIGHBOR_SOLICITATION;
    ns->header.code = 0;
    ns->header.checksum = 0;
    ns->reserved = 0;
    copy_ipv6(ns->target, target);

    // Add Source Link-Layer Address option
    LinkLayerAddrOption *slla = reinterpret_cast<LinkLayerAddrOption *>(ns_buf + NS_SIZE);
    slla->type = ndp_option::SOURCE_LINK_ADDR;
    slla->length = 1; // 8 bytes
    copy_mac(slla->addr, netif().mac());

    usize ns_len = NS_SIZE + LLA_OPTION_SIZE;

    // Destination is solicited-node multicast
    Ipv6Addr dst = target.solicited_node_multicast();

    // Compute checksum
    ns->header.checksum = compute_checksum(ipv6::get_link_local(), dst, ns_buf, ns_len);

    return ipv6::tx_packet(dst, ipv6::next_header::ICMPV6, ns_buf, ns_len);
}

/** @copydoc net::icmpv6::send_router_solicitation */
bool send_router_solicitation()
{
    static u8 rs_buf[16];
    RouterSolicitation *rs = reinterpret_cast<RouterSolicitation *>(rs_buf);

    rs->header.type = type::ROUTER_SOLICITATION;
    rs->header.code = 0;
    rs->header.checksum = 0;
    rs->reserved = 0;

    // Add Source Link-Layer Address option
    LinkLayerAddrOption *slla = reinterpret_cast<LinkLayerAddrOption *>(rs_buf + 8);
    slla->type = ndp_option::SOURCE_LINK_ADDR;
    slla->length = 1;
    copy_mac(slla->addr, netif().mac());

    usize rs_len = 8 + LLA_OPTION_SIZE;

    // Destination is all-routers multicast (ff02::2)
    Ipv6Addr dst = Ipv6Addr::unspecified();
    dst.bytes[0] = 0xff;
    dst.bytes[1] = 0x02;
    dst.bytes[15] = 0x02;

    // Compute checksum
    rs->header.checksum = compute_checksum(ipv6::get_link_local(), dst, rs_buf, rs_len);

    return ipv6::tx_packet(dst, ipv6::next_header::ICMPV6, rs_buf, rs_len);
}

/** @copydoc net::icmpv6::lookup_neighbor */
bool lookup_neighbor(const Ipv6Addr &ip, MacAddr *mac)
{
    u64 now = timer::get_ms();

    for (usize i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (neighbor_cache[i].valid && neighbor_cache[i].ip == ip)
        {
            // Check timeout
            if (now - neighbor_cache[i].timestamp > NEIGHBOR_TIMEOUT_MS)
            {
                neighbor_cache[i].valid = false;
                return false;
            }

            if (mac)
            {
                copy_mac(*mac, neighbor_cache[i].mac);
            }
            return true;
        }
    }

    return false;
}

/** @copydoc net::icmpv6::resolve_neighbor */
bool resolve_neighbor(const Ipv6Addr &ip, MacAddr *mac)
{
    if (lookup_neighbor(ip, mac))
    {
        return true;
    }

    // Send neighbor solicitation
    send_neighbor_solicitation(ip);
    return false;
}

/** @copydoc net::icmpv6::compute_checksum */
u16 compute_checksum(const Ipv6Addr &src, const Ipv6Addr &dst, const void *data, usize len)
{
    u32 sum = 0;

    // Pseudo-header: src + dst + length + next_header
    const u16 *src_words = reinterpret_cast<const u16 *>(src.bytes);
    const u16 *dst_words = reinterpret_cast<const u16 *>(dst.bytes);

    for (int i = 0; i < 8; i++)
    {
        sum += src_words[i];
        sum += dst_words[i];
    }

    // Upper-layer length (32-bit, but we only support up to 64K)
    sum += htons(static_cast<u16>(len));

    // Next header (ICMPv6 = 58)
    sum += htons(ipv6::next_header::ICMPV6);

    // ICMPv6 message
    const u16 *msg = static_cast<const u16 *>(data);
    usize msg_len = len;

    while (msg_len > 1)
    {
        sum += *msg++;
        msg_len -= 2;
    }

    // Odd byte
    if (msg_len > 0)
    {
        sum += *reinterpret_cast<const u8 *>(msg);
    }

    // Fold into 16 bits
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~static_cast<u16>(sum);
}

/** @copydoc net::icmpv6::get_neighbor_count */
u32 get_neighbor_count()
{
    u32 count = 0;
    u64 now = timer::get_ms();

    for (usize i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (neighbor_cache[i].valid && (now - neighbor_cache[i].timestamp) <= NEIGHBOR_TIMEOUT_MS)
        {
            count++;
        }
    }

    return count;
}

} // namespace icmpv6
} // namespace net
