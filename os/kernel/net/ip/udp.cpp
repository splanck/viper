/**
 * @file udp.cpp
 * @brief UDP implementation (socket table, receive dispatch, transmit).
 *
 * @details
 * Implements the minimal UDP stack described in `udp.hpp`. The code maintains a
 * fixed table of sockets keyed by destination port and provides helpers to
 * construct UDP datagrams with checksums.
 */

#include "udp.hpp"
#include "../../console/serial.hpp"
#include "../netif.hpp"
#include "../network.hpp"
#include "ipv4.hpp"

namespace net
{
namespace udp
{

// Socket table
static UdpSocket sockets[MAX_UDP_SOCKETS];
static bool initialized = false;

/**
 * @brief IPv4 pseudo-header used in UDP checksum computation.
 *
 * @details
 * UDP checksum covers a pseudo-header consisting of the IPv4 source/destination
 * addresses, protocol number, and UDP length. This struct is provided for
 * clarity; the implementation computes the sum explicitly to avoid alignment
 * issues.
 */
struct PseudoHeader
{
    Ipv4Addr src;
    Ipv4Addr dst;
    u8 zero;
    u8 protocol;
    u16 udp_length;
} __attribute__((packed));

/**
 * @brief Compute the UDP checksum for an IPv4 datagram.
 *
 * @details
 * Computes the standard UDP checksum including the IPv4 pseudo-header. The
 * implementation returns `0xFFFF` if the computed checksum is zero, matching
 * UDP's "0 means no checksum" convention.
 *
 * @param src Source IPv4 address.
 * @param dst Destination IPv4 address.
 * @param udp Pointer to UDP header (with fields in network order).
 * @param data UDP payload bytes.
 * @param data_len Payload length in bytes.
 * @return Checksum value in host order.
 */
static u16 udp_checksum(const Ipv4Addr &src,
                        const Ipv4Addr &dst,
                        const UdpHeader *udp,
                        const void *data,
                        usize data_len)
{
    u32 sum = 0;

    // Pseudo-header
    const u8 *s = src.bytes;
    const u8 *d = dst.bytes;
    sum += (s[0] << 8) | s[1];
    sum += (s[2] << 8) | s[3];
    sum += (d[0] << 8) | d[1];
    sum += (d[2] << 8) | d[3];
    sum += ip::protocol::UDP; // Protocol
    sum += ntohs(udp->length);

    // UDP header
    const u16 *p = reinterpret_cast<const u16 *>(udp);
    for (usize i = 0; i < UDP_HEADER_SIZE / 2; i++)
    {
        sum += ntohs(p[i]);
    }

    // Data
    const u8 *dp = static_cast<const u8 *>(data);
    for (usize i = 0; i + 1 < data_len; i += 2)
    {
        sum += (dp[i] << 8) | dp[i + 1];
    }
    if (data_len & 1)
    {
        sum += dp[data_len - 1] << 8;
    }

    // Fold
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    u16 result = ~sum;
    return result == 0 ? 0xFFFF : result; // 0 means no checksum
}

/** @copydoc net::udp::udp_init */
void udp_init()
{
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        sockets[i].bound = false;
        sockets[i].rx_ready = false;
        sockets[i].local_port = 0;
        sockets[i].rx_len = 0;
    }
    initialized = true;
    serial::puts("[udp] UDP layer initialized\n");
}

/** @copydoc net::udp::rx_packet */
void rx_packet(const Ipv4Addr &src, const void *data, usize len)
{
    if (len < UDP_HEADER_SIZE)
    {
        return;
    }

    const UdpHeader *udp = static_cast<const UdpHeader *>(data);
    u16 dst_port = ntohs(udp->dst_port);
    u16 src_port = ntohs(udp->src_port);
    u16 udp_len = ntohs(udp->length);

    if (udp_len > len || udp_len < UDP_HEADER_SIZE)
    {
        return;
    }

    usize payload_len = udp_len - UDP_HEADER_SIZE;
    const u8 *payload = static_cast<const u8 *>(data) + UDP_HEADER_SIZE;

    // Find socket bound to this port
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        if (sockets[i].bound && sockets[i].local_port == dst_port)
        {
            // Copy to socket buffer if not already full
            if (!sockets[i].rx_ready)
            {
                if (payload_len > UdpSocket::RX_BUFFER_SIZE)
                {
                    payload_len = UdpSocket::RX_BUFFER_SIZE;
                }
                for (usize j = 0; j < payload_len; j++)
                {
                    sockets[i].rx_buffer[j] = payload[j];
                }
                sockets[i].rx_len = payload_len;
                copy_ip(sockets[i].rx_src_ip, src);
                sockets[i].rx_src_port = src_port;
                sockets[i].rx_ready = true;
            }
            return;
        }
    }
}

/** @copydoc net::udp::socket_create */
i32 socket_create()
{
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        if (!sockets[i].bound)
        {
            sockets[i].bound = false; // Not bound until socket_bind
            sockets[i].rx_ready = false;
            sockets[i].local_port = 0;
            return static_cast<i32>(i);
        }
    }
    return -1; // No free sockets
}

/** @copydoc net::udp::socket_bind */
bool socket_bind(i32 sock, u16 port)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_UDP_SOCKETS))
    {
        return false;
    }

    // Check if port is already in use
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        if (sockets[i].bound && sockets[i].local_port == port)
        {
            return false; // Port in use
        }
    }

    sockets[sock].local_port = port;
    sockets[sock].bound = true;
    sockets[sock].rx_ready = false;
    return true;
}

/** @copydoc net::udp::socket_close */
void socket_close(i32 sock)
{
    if (sock >= 0 && sock < static_cast<i32>(MAX_UDP_SOCKETS))
    {
        sockets[sock].bound = false;
        sockets[sock].rx_ready = false;
        sockets[sock].local_port = 0;
    }
}

/** @copydoc net::udp::socket_send */
bool socket_send(i32 sock, const Ipv4Addr &dst, u16 dst_port, const void *data, usize len)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_UDP_SOCKETS))
    {
        return false;
    }
    if (!sockets[sock].bound)
    {
        return false;
    }

    return send(dst, sockets[sock].local_port, dst_port, data, len);
}

/** @copydoc net::udp::socket_recv */
i32 socket_recv(i32 sock, void *buffer, usize max_len, Ipv4Addr *src_ip, u16 *src_port)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_UDP_SOCKETS))
    {
        return -1;
    }
    if (!sockets[sock].bound)
    {
        return -1;
    }

    // Poll for network packets
    network_poll();

    if (!sockets[sock].rx_ready)
    {
        return 0; // No data
    }

    // Copy data
    usize copy_len = sockets[sock].rx_len;
    if (copy_len > max_len)
    {
        copy_len = max_len;
    }

    u8 *dst = static_cast<u8 *>(buffer);
    for (usize i = 0; i < copy_len; i++)
    {
        dst[i] = sockets[sock].rx_buffer[i];
    }

    if (src_ip)
    {
        copy_ip(*src_ip, sockets[sock].rx_src_ip);
    }
    if (src_port)
    {
        *src_port = sockets[sock].rx_src_port;
    }

    // Mark as consumed
    sockets[sock].rx_ready = false;

    return static_cast<i32>(copy_len);
}

/** @copydoc net::udp::send */
bool send(const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len)
{
    if (len > UDP_MAX_PAYLOAD)
    {
        return false;
    }

    // Build UDP packet (aligned buffer)
    static u8 packet[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD] __attribute__((aligned(4)));
    UdpHeader *udp = reinterpret_cast<UdpHeader *>(packet);

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(UDP_HEADER_SIZE + len);
    udp->checksum = 0;

    // Copy payload
    u8 *payload = packet + UDP_HEADER_SIZE;
    const u8 *src_data = static_cast<const u8 *>(data);
    for (usize i = 0; i < len; i++)
    {
        payload[i] = src_data[i];
    }

    // Calculate checksum
    Ipv4Addr our_ip = netif().ip();
    udp->checksum = htons(udp_checksum(our_ip, dst, udp, data, len));

    // Send via IP
    return ip::tx_packet(dst, ip::protocol::UDP, packet, UDP_HEADER_SIZE + len);
}

} // namespace udp
} // namespace net
