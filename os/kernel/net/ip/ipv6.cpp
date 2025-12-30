/**
 * @file ipv6.cpp
 * @brief IPv6 implementation (receive demux + transmit construction).
 *
 * @details
 * Implements basic IPv6 processing for ViperOS:
 * - Inbound packets are validated and dispatched to ICMPv6/UDP/TCP handlers.
 * - Outbound packets are constructed and transmitted over Ethernet.
 * - Link-local address is auto-configured from MAC address.
 */

#include "ipv6.hpp"
#include "../../console/serial.hpp"
#include "../eth/ethernet.hpp"
#include "../netif.hpp"
#include "icmpv6.hpp"
#include "tcp.hpp"
#include "udp.hpp"

namespace net
{
namespace ipv6
{

namespace
{

// IPv6 enabled flag
bool g_enabled = false;

// Link-local address (fe80::)
Ipv6Addr g_link_local;

// Global address (if configured via SLAAC or manual)
Ipv6Addr g_global;
bool g_global_configured = false;

// Statistics
u32 g_rx_count = 0;
u32 g_tx_count = 0;

} // namespace

/** @copydoc net::ipv6::ipv6_init */
void ipv6_init()
{
    // Generate link-local address from MAC
    MacAddr mac = netif().mac();
    g_link_local = Ipv6Addr::link_local_from_mac(mac);

    g_enabled = true;

    serial::puts("[ipv6] IPv6 layer initialized\n");
    serial::puts("[ipv6] Link-local: fe80::");
    // Print interface ID portion
    serial::put_hex(g_link_local.bytes[8]);
    serial::put_hex(g_link_local.bytes[9]);
    serial::puts(":");
    serial::put_hex(g_link_local.bytes[10]);
    serial::put_hex(g_link_local.bytes[11]);
    serial::puts(":");
    serial::put_hex(g_link_local.bytes[12]);
    serial::put_hex(g_link_local.bytes[13]);
    serial::puts(":");
    serial::put_hex(g_link_local.bytes[14]);
    serial::put_hex(g_link_local.bytes[15]);
    serial::puts("\n");

    // Initialize ICMPv6
    icmpv6::icmpv6_init();

    // Send Router Solicitation to discover routers
    icmpv6::send_router_solicitation();
}

/** @copydoc net::ipv6::rx_packet */
void rx_packet(const void *data, usize len)
{
    if (!g_enabled)
        return;

    if (len < IPV6_HEADER_SIZE)
    {
        return;
    }

    const Ipv6Header *hdr = static_cast<const Ipv6Header *>(data);

    // Check version
    if (get_version(hdr->version_tc_flow) != 6)
    {
        return;
    }

    // Check destination
    // Accept if: our link-local, our global, multicast we're subscribed to
    Ipv6Addr dst;
    copy_ipv6(dst, hdr->dst);

    bool for_us = false;

    // Check if for our link-local
    if (dst == g_link_local)
    {
        for_us = true;
    }

    // Check if for our global address
    if (g_global_configured && dst == g_global)
    {
        for_us = true;
    }

    // Check multicast
    if (dst.is_multicast())
    {
        // Accept all-nodes multicast (ff02::1)
        if (dst.bytes[1] == 0x02 && dst.bytes[15] == 0x01)
        {
            for_us = true;
        }

        // Accept solicited-node multicast for our addresses
        Ipv6Addr sol_ll = g_link_local.solicited_node_multicast();
        if (dst == sol_ll)
        {
            for_us = true;
        }

        if (g_global_configured)
        {
            Ipv6Addr sol_g = g_global.solicited_node_multicast();
            if (dst == sol_g)
            {
                for_us = true;
            }
        }
    }

    if (!for_us)
    {
        return;
    }

    g_rx_count++;

    // Get payload
    u16 payload_len = ntohs(hdr->payload_length);
    if (payload_len + IPV6_HEADER_SIZE > len)
    {
        payload_len = static_cast<u16>(len - IPV6_HEADER_SIZE);
    }

    const u8 *payload = static_cast<const u8 *>(data) + IPV6_HEADER_SIZE;
    u8 next_header = hdr->next_header;

    // Copy source address
    Ipv6Addr src;
    copy_ipv6(src, hdr->src);

    // Skip extension headers (basic handling)
    usize offset = 0;
    while (offset < payload_len)
    {
        switch (next_header)
        {
            case next_header::HOP_BY_HOP:
            case next_header::ROUTING:
            case next_header::DEST_OPTIONS:
            {
                // Extension header format: next_header, length, data...
                if (offset + 2 > payload_len)
                    return;
                u8 ext_next = payload[offset];
                u8 ext_len = payload[offset + 1];
                next_header = ext_next;
                offset += 8 + ext_len * 8; // Length is in 8-byte units (minus header)
                break;
            }

            case next_header::FRAGMENT:
            {
                // Fragment header
                if (offset + FRAGMENT_HEADER_SIZE > payload_len)
                    return;
                // For now, don't handle fragmented IPv6 packets
                serial::puts("[ipv6] Fragment header not supported\n");
                return;
            }

            case next_header::ICMPV6:
                icmpv6::rx_packet(src, payload + offset, payload_len - offset);
                return;

            case next_header::TCP:
                // Pass to TCP with IPv6 source
                // Note: TCP would need to be extended for IPv6
                serial::puts("[ipv6] TCP over IPv6 not implemented\n");
                return;

            case next_header::UDP:
                // Note: UDP would need to be extended for IPv6
                serial::puts("[ipv6] UDP over IPv6 not implemented\n");
                return;

            case next_header::NO_NEXT:
                return;

            default:
                // Unknown next header
                return;
        }
    }
}

/** @copydoc net::ipv6::tx_packet */
bool tx_packet(const Ipv6Addr &dst, u8 next_hdr, const void *payload, usize len)
{
    if (!g_enabled)
        return false;

    if (len > IPV6_MAX_PAYLOAD)
    {
        serial::puts("[ipv6] Payload too large (fragmentation not implemented)\n");
        return false;
    }

    // Determine source address
    Ipv6Addr src;
    if (dst.is_link_local() || dst.is_multicast())
    {
        copy_ipv6(src, g_link_local);
    }
    else if (g_global_configured)
    {
        copy_ipv6(src, g_global);
    }
    else
    {
        // No global address, use link-local
        copy_ipv6(src, g_link_local);
    }

    // Resolve destination MAC
    MacAddr dst_mac;

    if (dst.is_multicast())
    {
        // Multicast MAC: 33:33:xx:xx:xx:xx (last 32 bits of IPv6)
        dst_mac.bytes[0] = 0x33;
        dst_mac.bytes[1] = 0x33;
        dst_mac.bytes[2] = dst.bytes[12];
        dst_mac.bytes[3] = dst.bytes[13];
        dst_mac.bytes[4] = dst.bytes[14];
        dst_mac.bytes[5] = dst.bytes[15];
    }
    else
    {
        // Unicast - use neighbor discovery
        if (!icmpv6::resolve_neighbor(dst, &dst_mac))
        {
            // Resolution pending
            return false;
        }
    }

    // Build packet
    static u8 packet[IPV6_MTU] __attribute__((aligned(4)));
    Ipv6Header *hdr = reinterpret_cast<Ipv6Header *>(packet);

    hdr->version_tc_flow = make_version_tc_flow(6, 0, 0);
    hdr->payload_length = htons(static_cast<u16>(len));
    hdr->next_header = next_hdr;
    hdr->hop_limit = 64;
    copy_ipv6(hdr->src, src);
    copy_ipv6(hdr->dst, dst);

    // Copy payload
    u8 *pkt_payload = packet + IPV6_HEADER_SIZE;
    const u8 *data = static_cast<const u8 *>(payload);
    for (usize i = 0; i < len; i++)
    {
        pkt_payload[i] = data[i];
    }

    g_tx_count++;

    return eth::tx_frame(dst_mac, eth::ethertype::IPV6, packet, IPV6_HEADER_SIZE + len);
}

/** @copydoc net::ipv6::is_enabled */
bool is_enabled()
{
    return g_enabled;
}

/** @copydoc net::ipv6::get_link_local */
Ipv6Addr get_link_local()
{
    return g_link_local;
}

/** @copydoc net::ipv6::get_global */
Ipv6Addr get_global()
{
    return g_global;
}

/** @copydoc net::ipv6::set_global */
void set_global(const Ipv6Addr &addr)
{
    copy_ipv6(g_global, addr);
    g_global_configured = true;

    serial::puts("[ipv6] Global address configured\n");
}

} // namespace ipv6
} // namespace net
