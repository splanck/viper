/**
 * @file ipv4.cpp
 * @brief IPv4 implementation (receive demux + transmit construction).
 *
 * @details
 * Implements minimal IPv4 processing for ViperOS:
 * - Inbound packets are validated and dispatched to ICMP/UDP/TCP handlers.
 * - Outbound packets are constructed and transmitted over Ethernet, using ARP
 *   to resolve the next-hop MAC address.
 */

#include "ipv4.hpp"
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

/** @copydoc net::ip::ip_init */
void ip_init()
{
    serial::puts("[ip] IPv4 layer initialized\n");
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

    // Dispatch by protocol
    switch (hdr->protocol)
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

    // Build IP packet (aligned for struct access)
    static u8 packet[1500] __attribute__((aligned(4)));
    Ipv4Header *hdr = reinterpret_cast<Ipv4Header *>(packet);

    hdr->version_ihl = 0x45; // IPv4, 5 words (20 bytes)
    hdr->dscp_ecn = 0;
    hdr->total_length = htons(IPV4_HEADER_MIN + len);
    hdr->identification = htons(g_ip_identification++);
    hdr->flags_fragment = htons(0x4000); // Don't fragment
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    // Use byte-copy for IP addresses to avoid alignment issues
    Ipv4Addr our_ip = netif().ip();
    copy_ip(hdr->src, our_ip);
    copy_ip(hdr->dst, dst);

    // Calculate header checksum
    hdr->checksum = checksum(hdr, IPV4_HEADER_MIN);

    // Copy payload
    u8 *pkt_payload = packet + IPV4_HEADER_MIN;
    const u8 *src_payload = static_cast<const u8 *>(payload);
    for (usize i = 0; i < len; i++)
    {
        pkt_payload[i] = src_payload[i];
    }

    // Send via Ethernet
    return eth::tx_frame(dst_mac, eth::ethertype::IPV4, packet, IPV4_HEADER_MIN + len);
}

} // namespace ip
} // namespace net
