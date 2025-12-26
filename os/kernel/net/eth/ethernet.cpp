/**
 * @file ethernet.cpp
 * @brief Ethernet layer implementation.
 *
 * @details
 * Implements Ethernet II framing on top of the virtio-net device and performs
 * simple ethertype-based demultiplexing for inbound frames.
 */

#include "ethernet.hpp"
#include "../../console/serial.hpp"
#include "../../drivers/virtio/net.hpp"
#include "../ip/ipv4.hpp"
#include "../netif.hpp"
#include "arp.hpp"

namespace net
{
namespace eth
{

/** @copydoc net::eth::eth_init */
void eth_init()
{
    serial::puts("[eth] Ethernet layer initialized\n");
}

/** @copydoc net::eth::tx_frame */
bool tx_frame(const MacAddr &dst, u16 ethertype, const void *payload, usize len)
{
    if (!virtio::net_device())
    {
        return false;
    }

    if (len > ETH_MAX_PAYLOAD)
    {
        serial::puts("[eth] Payload too large\n");
        return false;
    }

    // Build frame in a static buffer (aligned for struct access)
    static u8 frame_buf[ETH_MAX_FRAME] __attribute__((aligned(4)));

    // Ethernet header (use byte-by-byte copy to avoid alignment issues)
    EthHeader *hdr = reinterpret_cast<EthHeader *>(frame_buf);
    for (int i = 0; i < 6; i++)
    {
        hdr->dst.bytes[i] = dst.bytes[i];
    }
    MacAddr our_mac = netif().mac();
    for (int i = 0; i < 6; i++)
    {
        hdr->src.bytes[i] = our_mac.bytes[i];
    }
    // ethertype is at offset 12, 2-byte aligned access is ok
    hdr->ethertype = htons(ethertype);

    // Copy payload
    u8 *payload_dst = frame_buf + ETH_HEADER_SIZE;
    const u8 *payload_src = static_cast<const u8 *>(payload);
    for (usize i = 0; i < len; i++)
    {
        payload_dst[i] = payload_src[i];
    }

    // Pad if necessary
    usize frame_len = ETH_HEADER_SIZE + len;
    if (frame_len < ETH_MIN_FRAME)
    {
        for (usize i = frame_len; i < ETH_MIN_FRAME; i++)
        {
            frame_buf[i] = 0;
        }
        frame_len = ETH_MIN_FRAME;
    }

    // Transmit
    return virtio::net_device()->transmit(frame_buf, frame_len);
}

/** @copydoc net::eth::rx_frame */
void rx_frame(const void *frame, usize len)
{
    if (len < ETH_HEADER_SIZE)
    {
        return; // Too short
    }

    const EthHeader *hdr = static_cast<const EthHeader *>(frame);
    const u8 *payload = static_cast<const u8 *>(frame) + ETH_HEADER_SIZE;
    usize payload_len = len - ETH_HEADER_SIZE;

    // Check destination MAC (our MAC or broadcast)
    MacAddr our_mac = netif().mac();
    bool for_us = (hdr->dst == our_mac) || hdr->dst.is_broadcast() || hdr->dst.is_multicast();

    if (!for_us)
    {
        return; // Not for us
    }

    // Dispatch by ethertype
    u16 etype = ntohs(hdr->ethertype);
    switch (etype)
    {
        case ethertype::ARP:
            arp::rx_packet(payload, payload_len);
            break;
        case ethertype::IPV4:
            net::ip::rx_packet(payload, payload_len);
            break;
        default:
            // Unknown protocol, ignore
            break;
    }
}

} // namespace eth
} // namespace net
