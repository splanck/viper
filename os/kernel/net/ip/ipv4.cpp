/**
 * @file ipv4.cpp
 * @brief IPv4 implementation (receive demux + transmit construction).
 *
 * @details
 * Implements minimal IPv4 processing for ViperOS:
 * - Inbound packets are validated and dispatched to ICMP/UDP/TCP handlers.
 * - Outbound packets are constructed and transmitted over Ethernet, using ARP
 *   to resolve the next-hop MAC address.
 * - Fragmented packets are reassembled before delivery to upper layers.
 * - Large outbound packets are fragmented when they exceed MTU.
 */

#include "ipv4.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../eth/arp.hpp"
#include "../eth/ethernet.hpp"
#include "../netif.hpp"
#include "icmp.hpp"
#include "tcp.hpp"
#include "udp.hpp"

namespace net
{
namespace ip
{

// IP identification counter for outgoing packets
static u16 g_ip_identification = 0;

//=============================================================================
// IP Fragment Reassembly
//=============================================================================

/** @brief Maximum number of concurrent reassembly buffers. */
constexpr usize MAX_REASSEMBLY_ENTRIES = 8;

/** @brief Reassembly buffer size - supports typical large datagrams (8KB). */
constexpr usize REASSEMBLY_BUFFER_SIZE = 8192;

/** @brief Reassembly timeout in milliseconds (30 seconds per RFC 791). */
constexpr u64 REASSEMBLY_TIMEOUT_MS = 30000;

/**
 * @brief Entry in the IP fragment reassembly queue.
 */
struct ReassemblyEntry
{
    bool in_use;                               ///< Entry is active
    Ipv4Addr src;                              ///< Source IP
    Ipv4Addr dst;                              ///< Destination IP
    u16 id;                                    ///< IP identification
    u8 protocol;                               ///< IP protocol
    u64 timestamp;                             ///< When first fragment arrived (ms)
    u8 buffer[REASSEMBLY_BUFFER_SIZE];         ///< Reassembly buffer
    bool received[REASSEMBLY_BUFFER_SIZE / 8]; ///< Bitmap of received bytes (1 bit per 8 bytes)
    usize total_len;      ///< Total datagram length (0 until last fragment received)
    usize bytes_received; ///< Total bytes received so far
    bool last_received;   ///< Last fragment has been received
};

static ReassemblyEntry reassembly_queue[MAX_REASSEMBLY_ENTRIES];

/**
 * @brief Find or create a reassembly entry for a fragment.
 */
static ReassemblyEntry *find_reassembly_entry(const Ipv4Addr &src,
                                              const Ipv4Addr &dst,
                                              u16 id,
                                              u8 protocol)
{
    // Look for existing entry
    for (usize i = 0; i < MAX_REASSEMBLY_ENTRIES; i++)
    {
        ReassemblyEntry &e = reassembly_queue[i];
        if (e.in_use && e.id == id && e.protocol == protocol && e.src == src && e.dst == dst)
        {
            return &e;
        }
    }

    // Allocate new entry
    for (usize i = 0; i < MAX_REASSEMBLY_ENTRIES; i++)
    {
        ReassemblyEntry &e = reassembly_queue[i];
        if (!e.in_use)
        {
            e.in_use = true;
            copy_ip(e.src, src);
            copy_ip(e.dst, dst);
            e.id = id;
            e.protocol = protocol;
            e.timestamp = timer::get_ms();
            e.total_len = 0;
            e.bytes_received = 0;
            e.last_received = false;
            // Clear received bitmap
            for (usize j = 0; j < REASSEMBLY_BUFFER_SIZE / 8; j++)
            {
                e.received[j] = false;
            }
            return &e;
        }
    }

    return nullptr; // Queue full
}

/**
 * @brief Add a fragment to a reassembly entry.
 *
 * @return true if the datagram is now complete.
 */
static bool add_fragment(
    ReassemblyEntry *entry, usize offset, const u8 *data, usize len, bool more_fragments)
{
    // Check bounds
    if (offset + len > REASSEMBLY_BUFFER_SIZE)
    {
        return false; // Fragment too large
    }

    // Copy fragment data
    for (usize i = 0; i < len; i++)
    {
        entry->buffer[offset + i] = data[i];
    }

    // Mark bytes as received (in 8-byte units since fragment offset is in 8-byte units)
    usize start_block = offset / 8;
    usize end_block = (offset + len + 7) / 8;
    for (usize i = start_block; i < end_block && i < REASSEMBLY_BUFFER_SIZE / 8; i++)
    {
        if (!entry->received[i])
        {
            entry->received[i] = true;
        }
    }
    entry->bytes_received = 0;
    for (usize i = 0; i < REASSEMBLY_BUFFER_SIZE / 8; i++)
    {
        if (entry->received[i])
        {
            entry->bytes_received += 8;
        }
    }

    // If this is the last fragment, we now know the total length
    if (!more_fragments)
    {
        entry->last_received = true;
        entry->total_len = offset + len;
    }

    // Check if complete
    if (entry->last_received && entry->total_len > 0)
    {
        // Verify all bytes up to total_len are received
        usize needed_blocks = (entry->total_len + 7) / 8;
        for (usize i = 0; i < needed_blocks; i++)
        {
            if (!entry->received[i])
            {
                return false;
            }
        }
        return true; // Complete!
    }

    return false;
}

/** @copydoc net::ip::ip_init */
void ip_init()
{
    serial::puts("[ip] IPv4 layer initialized\n");
}

/**
 * @brief Dispatch a complete IP payload to the appropriate protocol handler.
 */
static void dispatch_payload(const Ipv4Addr &src_ip,
                             u8 protocol,
                             const u8 *payload,
                             usize payload_len)
{
    switch (protocol)
    {
        case protocol::ICMP:
            icmp::rx_packet(src_ip, payload, payload_len);
            break;
        case protocol::UDP:
            udp::rx_packet(src_ip, payload, payload_len);
            break;
        case protocol::TCP:
            tcp::rx_segment(src_ip, payload, payload_len);
            break;
        default:
            // Unknown protocol, ignore
            break;
    }
}

/** @copydoc net::ip::rx_packet */
void rx_packet(const void *data, usize len)
{
    if (len < IPV4_HEADER_MIN)
    {
        return;
    }

    const Ipv4Header *hdr = static_cast<const Ipv4Header *>(data);

    // Check version
    u8 version = (hdr->version_ihl >> 4) & 0x0f;
    if (version != 4)
    {
        return;
    }

    // Get header length
    u8 ihl = (hdr->version_ihl & 0x0f) * 4;
    if (ihl < IPV4_HEADER_MIN || len < ihl)
    {
        return;
    }

    // Check destination (for us or broadcast)
    // Copy to aligned local variable first
    Ipv4Addr dst_ip;
    copy_ip(dst_ip, hdr->dst);
    Ipv4Addr our_ip = netif().ip();
    if (dst_ip != our_ip && !dst_ip.is_broadcast())
    {
        return;
    }

    // Get payload
    const u8 *payload = static_cast<const u8 *>(data) + ihl;
    usize payload_len = ntohs(hdr->total_length) - ihl;

    if (payload_len > len - ihl)
    {
        payload_len = len - ihl;
    }

    // Copy source IP to aligned local variable
    Ipv4Addr src_ip;
    copy_ip(src_ip, hdr->src);

    // Check for fragmentation
    u16 flags_frag = ntohs(hdr->flags_fragment);
    bool more_fragments = (flags_frag & ip_flags::MF) != 0;
    usize frag_offset = (flags_frag & ip_flags::OFFSET_MASK) * 8; // Convert to bytes

    // If this is a fragment (either MF set or offset > 0), handle reassembly
    if (more_fragments || frag_offset > 0)
    {
        u16 id = ntohs(hdr->identification);

        ReassemblyEntry *entry = find_reassembly_entry(src_ip, dst_ip, id, hdr->protocol);
        if (!entry)
        {
            serial::puts("[ip] Reassembly queue full, dropping fragment\n");
            return;
        }

        bool complete = add_fragment(entry, frag_offset, payload, payload_len, more_fragments);

        if (complete)
        {
            // Dispatch complete datagram
            dispatch_payload(entry->src, entry->protocol, entry->buffer, entry->total_len);
            entry->in_use = false; // Free entry
        }
        return;
    }

    // Non-fragmented packet - dispatch directly
    dispatch_payload(src_ip, hdr->protocol, payload, payload_len);
}

/**
 * @brief Send a single IP fragment.
 */
static bool send_fragment(const MacAddr &dst_mac,
                          const Ipv4Addr &dst,
                          u8 protocol,
                          u16 id,
                          usize offset,
                          const u8 *data,
                          usize len,
                          bool more_fragments)
{
    static u8 packet[IP_MTU] __attribute__((aligned(4)));
    Ipv4Header *hdr = reinterpret_cast<Ipv4Header *>(packet);

    hdr->version_ihl = 0x45; // IPv4, 5 words (20 bytes)
    hdr->dscp_ecn = 0;
    hdr->total_length = htons(static_cast<u16>(IPV4_HEADER_MIN + len));
    hdr->identification = htons(id);

    // Set fragment flags and offset
    u16 flags_offset = static_cast<u16>(offset / 8); // Offset in 8-byte units
    if (more_fragments)
    {
        flags_offset |= ip_flags::MF;
    }
    hdr->flags_fragment = htons(flags_offset);

    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->checksum = 0;

    Ipv4Addr our_ip = netif().ip();
    copy_ip(hdr->src, our_ip);
    copy_ip(hdr->dst, dst);

    hdr->checksum = checksum(hdr, IPV4_HEADER_MIN);

    // Copy fragment data
    u8 *pkt_payload = packet + IPV4_HEADER_MIN;
    for (usize i = 0; i < len; i++)
    {
        pkt_payload[i] = data[i];
    }

    return eth::tx_frame(dst_mac, eth::ethertype::IPV4, packet, IPV4_HEADER_MIN + len);
}

/** @copydoc net::ip::tx_packet */
bool tx_packet(const Ipv4Addr &dst, u8 protocol, const void *payload, usize len)
{
    // Determine next hop
    Ipv4Addr next_hop = netif().next_hop(dst);

    // Resolve MAC address
    MacAddr dst_mac;
    if (!arp::resolve(next_hop, &dst_mac))
    {
        // ARP request sent, caller should retry
        return false;
    }

    const u8 *data = static_cast<const u8 *>(payload);

    // Check if fragmentation is needed
    if (len <= IP_MAX_PAYLOAD)
    {
        // No fragmentation needed - send as single packet
        static u8 packet[IP_MTU] __attribute__((aligned(4)));
        Ipv4Header *hdr = reinterpret_cast<Ipv4Header *>(packet);

        hdr->version_ihl = 0x45;
        hdr->dscp_ecn = 0;
        hdr->total_length = htons(static_cast<u16>(IPV4_HEADER_MIN + len));
        hdr->identification = htons(g_ip_identification++);
        hdr->flags_fragment = 0; // No flags, offset = 0
        hdr->ttl = 64;
        hdr->protocol = protocol;
        hdr->checksum = 0;

        Ipv4Addr our_ip = netif().ip();
        copy_ip(hdr->src, our_ip);
        copy_ip(hdr->dst, dst);

        hdr->checksum = checksum(hdr, IPV4_HEADER_MIN);

        u8 *pkt_payload = packet + IPV4_HEADER_MIN;
        for (usize i = 0; i < len; i++)
        {
            pkt_payload[i] = data[i];
        }

        return eth::tx_frame(dst_mac, eth::ethertype::IPV4, packet, IPV4_HEADER_MIN + len);
    }

    // Fragmentation needed
    u16 id = g_ip_identification++;
    usize offset = 0;

    // Fragment size must be multiple of 8 bytes
    usize frag_data_size = (IP_MAX_PAYLOAD / 8) * 8;

    while (offset < len)
    {
        usize remaining = len - offset;
        usize frag_len = remaining < frag_data_size ? remaining : frag_data_size;
        bool more = (offset + frag_len) < len;

        if (!send_fragment(dst_mac, dst, protocol, id, offset, data + offset, frag_len, more))
        {
            return false;
        }

        offset += frag_len;
    }

    return true;
}

/** @copydoc net::ip::check_reassembly_timeout */
void check_reassembly_timeout()
{
    u64 now = timer::get_ms();

    for (usize i = 0; i < MAX_REASSEMBLY_ENTRIES; i++)
    {
        ReassemblyEntry &e = reassembly_queue[i];
        if (e.in_use && (now - e.timestamp) > REASSEMBLY_TIMEOUT_MS)
        {
            serial::puts("[ip] Reassembly timeout for ID ");
            serial::put_hex(e.id);
            serial::puts("\n");
            e.in_use = false;
        }
    }
}

/** @copydoc net::ip::get_reassembly_count */
u32 get_reassembly_count()
{
    u32 count = 0;
    for (usize i = 0; i < MAX_REASSEMBLY_ENTRIES; i++)
    {
        if (reassembly_queue[i].in_use)
        {
            count++;
        }
    }
    return count;
}

} // namespace ip
} // namespace net
