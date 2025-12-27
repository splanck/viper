/**
 * @file tcp.cpp
 * @brief Minimal TCP implementation (state machine + socket table).
 *
 * @details
 * Implements the TCP layer declared in `tcp.hpp`. This code targets bring-up
 * correctness for simple flows rather than complete TCP compliance.
 *
 * Key behaviors:
 * - Basic 3-way handshake (SYN/SYN+ACK/ACK) for connect and listen.
 * - In-order receive buffering and ACK generation.
 * - Simplified graceful close (FIN handling).
 * - Polling-based I/O driven by @ref net::network_poll.
 */

#include "tcp.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../../drivers/virtio/net.hpp"
#include "../../drivers/virtio/rng.hpp"
#include "../../lib/spinlock.hpp"
#include "../../sched/task.hpp"
#include "../netif.hpp"
#include "../network.hpp"
#include "ipv4.hpp"

namespace net
{
namespace tcp
{

// Socket table lock for thread-safe access
static Spinlock tcp_lock;

// Socket table
static TcpSocket sockets[MAX_TCP_SOCKETS];
static bool initialized = false;

// Forward declarations for congestion control
static void on_congestion_event(TcpSocket *sock);
static void on_ack_received(TcpSocket *sock, u32 bytes_acked);

/**
 * @brief Store an out-of-order segment for later reassembly.
 *
 * @param sock Socket receiving the segment.
 * @param seq Sequence number of the segment.
 * @param data Segment data.
 * @param len Length of segment data.
 * @return true if stored, false if queue is full.
 */
static bool ooo_store(TcpSocket *sock, u32 seq, const u8 *data, usize len)
{
    // Check if segment already exists
    for (usize i = 0; i < TcpSocket::OOO_MAX_SEGMENTS; i++)
    {
        if (sock->ooo_queue[i].valid && sock->ooo_queue[i].seq == seq)
        {
            return true; // Already stored
        }
    }

    // Find empty slot
    for (usize i = 0; i < TcpSocket::OOO_MAX_SEGMENTS; i++)
    {
        if (!sock->ooo_queue[i].valid)
        {
            sock->ooo_queue[i].seq = seq;
            sock->ooo_queue[i].len = static_cast<u16>(len > TcpSocket::OOO_SEGMENT_SIZE
                                                          ? TcpSocket::OOO_SEGMENT_SIZE
                                                          : len);
            sock->ooo_queue[i].valid = true;
            for (usize j = 0; j < sock->ooo_queue[i].len; j++)
            {
                sock->ooo_queue[i].data[j] = data[j];
            }
            serial::puts("[tcp] OOO: stored seq ");
            serial::put_dec(seq);
            serial::puts(" len ");
            serial::put_dec(len);
            serial::puts("\n");
            return true;
        }
    }

    serial::puts("[tcp] OOO queue full, dropping segment\n");
    return false;
}

/**
 * @brief Check OOO queue and deliver any segments that are now in order.
 *
 * @param sock Socket to check.
 * @return Number of bytes delivered from OOO queue.
 */
static usize ooo_deliver(TcpSocket *sock)
{
    usize total_delivered = 0;
    bool found;

    do
    {
        found = false;

        // Find segment matching rcv_nxt
        for (usize i = 0; i < TcpSocket::OOO_MAX_SEGMENTS; i++)
        {
            if (sock->ooo_queue[i].valid && sock->ooo_queue[i].seq == sock->rcv_nxt)
            {
                // Found in-order segment, deliver to RX buffer
                u16 len = sock->ooo_queue[i].len;
                usize avail = TcpSocket::RX_BUFFER_SIZE - (sock->rx_tail - sock->rx_head);

                if (avail >= len)
                {
                    for (u16 j = 0; j < len; j++)
                    {
                        sock->rx_buffer[sock->rx_tail % TcpSocket::RX_BUFFER_SIZE] =
                            sock->ooo_queue[i].data[j];
                        sock->rx_tail++;
                    }
                    sock->rcv_nxt += len;
                    total_delivered += len;

                    serial::puts("[tcp] OOO: delivered seq ");
                    serial::put_dec(sock->ooo_queue[i].seq);
                    serial::puts(" len ");
                    serial::put_dec(len);
                    serial::puts("\n");
                }

                // Mark slot as free
                sock->ooo_queue[i].valid = false;
                found = true;
                break; // Restart search
            }
        }
    } while (found);

    return total_delivered;
}

/**
 * @brief Generate a random initial sequence number (ISN).
 *
 * @details
 * Uses the virtio-rng device if available to generate a cryptographically
 * random ISN. Falls back to a simple timer-based counter if RNG is not
 * available. A random ISN prevents sequence number prediction attacks.
 *
 * @return Random 32-bit ISN value.
 */
static u32 generate_isn()
{
    u32 random_isn = 0;

    // Try to get random bytes from virtio-rng
    if (virtio::rng::is_available())
    {
        u8 bytes[4];
        usize got = virtio::rng::get_bytes(bytes, 4);
        if (got == 4)
        {
            random_isn = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
                         (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
            return random_isn;
        }
    }

    // Fallback: use timer ticks mixed with a counter
    // This is not cryptographically secure but better than a constant
    static u32 counter = 0;
    random_isn = static_cast<u32>(timer::get_ticks()) ^ (counter * 0x9e3779b9);
    counter++;
    return random_isn;
}

/**
 * @brief Compute TCP checksum for an IPv4 segment.
 *
 * @details
 * Computes the standard TCP checksum including the IPv4 pseudo-header. The
 * caller provides the TCP header + payload bytes and their total length.
 *
 * @param src IPv4 source address.
 * @param dst IPv4 destination address.
 * @param tcp_data Pointer to TCP header + payload bytes.
 * @param tcp_len Length of TCP data in bytes.
 * @return Checksum value in host order.
 */
static u16 tcp_checksum(const Ipv4Addr &src,
                        const Ipv4Addr &dst,
                        const void *tcp_data,
                        usize tcp_len)
{
    u32 sum = 0;

    // Pseudo-header
    const u8 *s = src.bytes;
    const u8 *d = dst.bytes;
    sum += (s[0] << 8) | s[1];
    sum += (s[2] << 8) | s[3];
    sum += (d[0] << 8) | d[1];
    sum += (d[2] << 8) | d[3];
    sum += ip::protocol::TCP;
    sum += tcp_len;

    // TCP header + data
    const u8 *p = static_cast<const u8 *>(tcp_data);
    for (usize i = 0; i + 1 < tcp_len; i += 2)
    {
        sum += (p[i] << 8) | p[i + 1];
    }
    if (tcp_len & 1)
    {
        sum += p[tcp_len - 1] << 8;
    }

    // Fold
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

/**
 * @brief Parse TCP options and extract MSS value.
 *
 * @details
 * Scans TCP options looking for MSS option (kind=2). If found, returns
 * the MSS value. Otherwise returns 0.
 *
 * @param options Pointer to TCP options (after 20-byte header).
 * @param options_len Length of options in bytes.
 * @return MSS value if found, or 0 if not present.
 */
static u16 parse_mss_option(const u8 *options, usize options_len)
{
    usize i = 0;
    while (i < options_len)
    {
        u8 kind = options[i];
        if (kind == option::END)
        {
            break;
        }
        if (kind == option::NOP)
        {
            i++;
            continue;
        }
        if (i + 1 >= options_len)
        {
            break;
        }
        u8 len = options[i + 1];
        if (len < 2 || i + len > options_len)
        {
            break;
        }
        if (kind == option::MSS && len == option::MSS_LEN && i + 4 <= options_len)
        {
            // MSS is big-endian
            return (static_cast<u16>(options[i + 2]) << 8) | options[i + 3];
        }
        i += len;
    }
    return 0;
}

/**
 * @brief Send a TCP segment for a socket.
 *
 * @details
 * Constructs a TCP header, optionally includes MSS option for SYN packets,
 * copies payload bytes (if any), computes checksum, and transmits the segment
 * via IPv4. Sequence number tracking is updated according to flags and payload
 * length.
 *
 * @param sock Socket control block (must have remote_ip/ports populated).
 * @param tcp_flags TCP flags to set on the segment (e.g., SYN, ACK, FIN).
 * @param data Optional payload pointer (may be `nullptr` for empty segments).
 * @param len Payload length in bytes.
 * @return `true` if the segment was transmitted, otherwise `false`.
 */
static bool send_segment(TcpSocket *sock, u8 tcp_flags, const void *data, usize len)
{
    static u8 packet[TCP_HEADER_MSS + DEFAULT_MSS] __attribute__((aligned(4)));
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(packet);

    // Determine header size (include MSS option for SYN packets)
    bool include_mss = (tcp_flags & flags::SYN) != 0;
    usize header_len = include_mss ? TCP_HEADER_MSS : TCP_HEADER_MIN;

    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->snd_nxt);
    tcp->ack_num = htonl(sock->rcv_nxt);
    tcp->data_offset = static_cast<u8>((header_len / 4) << 4);
    tcp->flags = tcp_flags;
    tcp->window = htons(sock->rcv_wnd);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    // Add MSS option for SYN packets
    if (include_mss)
    {
        u8 *opts = packet + TCP_HEADER_MIN;
        opts[0] = option::MSS;          // Kind
        opts[1] = option::MSS_LEN;      // Length
        opts[2] = (DEFAULT_MSS >> 8);   // MSS high byte
        opts[3] = (DEFAULT_MSS & 0xFF); // MSS low byte
    }

    // Copy data if any
    if (data && len > 0)
    {
        u8 *payload = packet + header_len;
        const u8 *src = static_cast<const u8 *>(data);
        for (usize i = 0; i < len; i++)
        {
            payload[i] = src[i];
        }
    }

    // Calculate checksum
    Ipv4Addr our_ip = netif().ip();
    tcp->checksum = htons(tcp_checksum(our_ip, sock->remote_ip, packet, header_len + len));

    // Update sequence number
    if (tcp_flags & (flags::SYN | flags::FIN))
    {
        sock->snd_nxt++; // SYN and FIN consume a sequence number
    }
    sock->snd_nxt += len;

    return ip::tx_packet(sock->remote_ip, ip::protocol::TCP, packet, header_len + len);
}

/**
 * @brief Send a TCP RST to reject an unexpected segment.
 *
 * @details
 * Constructs a minimal RST|ACK segment using the provided port/sequence numbers
 * and transmits it via IPv4. This is used when an inbound segment does not
 * match any active or listening socket.
 *
 * @param dst Destination IPv4 address for the RST (the source of the offending segment).
 * @param src_port Local port (destination port of the offending segment).
 * @param dst_port Remote port (source port of the offending segment).
 * @param seq Sequence number to send.
 * @param ack Acknowledgment number to send.
 */
static void send_rst(const Ipv4Addr &dst, u16 src_port, u16 dst_port, u32 seq, u32 ack)
{
    static u8 packet[TCP_HEADER_MIN] __attribute__((aligned(4)));
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(packet);

    tcp->src_port = htons(src_port);
    tcp->dst_port = htons(dst_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (TCP_HEADER_MIN / 4) << 4;
    tcp->flags = flags::RST | flags::ACK;
    tcp->window = 0;
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    Ipv4Addr our_ip = netif().ip();
    tcp->checksum = htons(tcp_checksum(our_ip, dst, packet, TCP_HEADER_MIN));

    ip::tx_packet(dst, ip::protocol::TCP, packet, TCP_HEADER_MIN);
}

/** @copydoc net::tcp::tcp_init */
void tcp_init()
{
    SpinlockGuard guard(tcp_lock);
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        sockets[i].in_use = false;
        sockets[i].state = TcpState::CLOSED;
    }
    initialized = true;
    serial::puts("[tcp] TCP layer initialized\n");
}

/** @copydoc net::tcp::rx_segment */
void rx_segment(const Ipv4Addr &src, const void *data, usize len)
{
    if (len < TCP_HEADER_MIN)
    {
        return;
    }

    // Copy to aligned buffer to avoid alignment faults (before lock)
    static u8 tcp_buf[TCP_HEADER_MIN + 64] __attribute__((aligned(4)));
    usize hdr_len = len > sizeof(tcp_buf) ? sizeof(tcp_buf) : len;
    const u8 *src_data = static_cast<const u8 *>(data);
    for (usize i = 0; i < hdr_len; i++)
    {
        tcp_buf[i] = src_data[i];
    }

    const TcpHeader *tcp = reinterpret_cast<const TcpHeader *>(tcp_buf);
    u16 src_port = ntohs(tcp->src_port);
    u16 dst_port = ntohs(tcp->dst_port);
    u32 seq = ntohl(tcp->seq_num);
    u32 ack = ntohl(tcp->ack_num);
    u8 tcp_flags = tcp->flags;
    u8 data_offset = (tcp->data_offset >> 4) * 4;

    if (data_offset > len)
    {
        return;
    }

    usize payload_len = len - data_offset;
    const u8 *payload = static_cast<const u8 *>(data) + data_offset;

    SpinlockGuard guard(tcp_lock);

    // Find matching socket
    TcpSocket *sock = nullptr;

    // First, look for an established/connecting socket matching this connection
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        if (sockets[i].in_use && sockets[i].local_port == dst_port)
        {
            if (sockets[i].state == TcpState::LISTEN)
            {
                // Remember listening socket but keep looking
                if (!sock)
                {
                    sock = &sockets[i];
                }
            }
            else if (sockets[i].remote_port == src_port)
            {
                // Check if IPs match
                bool ip_match = true;
                for (int j = 0; j < 4; j++)
                {
                    if (sockets[i].remote_ip.bytes[j] != src.bytes[j])
                    {
                        ip_match = false;
                        break;
                    }
                }
                if (ip_match)
                {
                    sock = &sockets[i];
                    break;
                }
            }
        }
    }

    if (!sock)
    {
        // No socket found, send RST if not RST
        if (!(tcp_flags & flags::RST))
        {
            if (tcp_flags & flags::ACK)
            {
                send_rst(src, dst_port, src_port, ack, 0);
            }
            else
            {
                send_rst(src, dst_port, src_port, 0, seq + 1);
            }
        }
        return;
    }

    sock->last_activity = timer::get_ticks();

    // State machine
    switch (sock->state)
    {
        case TcpState::LISTEN:
            if (tcp_flags & flags::SYN)
            {
                // Incoming connection
                copy_ip(sock->remote_ip, src);
                sock->remote_port = src_port;
                sock->rcv_nxt = seq + 1;
                sock->snd_nxt = generate_isn();
                sock->snd_una = sock->snd_nxt;
                sock->rcv_wnd = TcpSocket::RX_BUFFER_SIZE;
                sock->rx_head = sock->rx_tail = 0;

                // Parse MSS option from SYN
                sock->mss = DEFAULT_MSS;
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    u16 peer_mss = parse_mss_option(opt_ptr, data_offset - TCP_HEADER_MIN);
                    if (peer_mss > 0)
                    {
                        // Use the smaller of peer's MSS and our default
                        sock->mss = peer_mss < DEFAULT_MSS ? peer_mss : DEFAULT_MSS;
                    }
                }

                // Send SYN+ACK
                send_segment(sock, flags::SYN | flags::ACK, nullptr, 0);
                sock->state = TcpState::SYN_RECEIVED;
            }
            break;

        case TcpState::SYN_SENT:
            if ((tcp_flags & (flags::SYN | flags::ACK)) == (flags::SYN | flags::ACK))
            {
                // Server accepted our connection
                sock->rcv_nxt = seq + 1;
                sock->snd_una = ack;

                // Parse MSS option from SYN+ACK
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    u16 peer_mss = parse_mss_option(opt_ptr, data_offset - TCP_HEADER_MIN);
                    if (peer_mss > 0)
                    {
                        // Use the smaller of peer's MSS and our default
                        sock->mss = peer_mss < sock->mss ? peer_mss : sock->mss;
                    }
                }

                // Send ACK
                send_segment(sock, flags::ACK, nullptr, 0);
                sock->state = TcpState::ESTABLISHED;
            }
            else if (tcp_flags & flags::SYN)
            {
                // Simultaneous open (rare) - parse MSS
                sock->rcv_nxt = seq + 1;
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    u16 peer_mss = parse_mss_option(opt_ptr, data_offset - TCP_HEADER_MIN);
                    if (peer_mss > 0)
                    {
                        sock->mss = peer_mss < sock->mss ? peer_mss : sock->mss;
                    }
                }
                send_segment(sock, flags::SYN | flags::ACK, nullptr, 0);
                sock->state = TcpState::SYN_RECEIVED;
            }
            break;

        case TcpState::SYN_RECEIVED:
            if (tcp_flags & flags::ACK)
            {
                sock->snd_una = ack;
                sock->state = TcpState::ESTABLISHED;
            }
            break;

        case TcpState::ESTABLISHED:
            if (tcp_flags & flags::RST)
            {
                sock->state = TcpState::CLOSED;
                sock->unacked_len = 0; // Clear retransmit state
                break;
            }

            if (tcp_flags & flags::FIN)
            {
                sock->rcv_nxt = seq + payload_len + 1;
                send_segment(sock, flags::ACK, nullptr, 0);
                sock->state = TcpState::CLOSE_WAIT;
                sock->unacked_len = 0; // Clear retransmit state
            }
            else
            {
                // Handle incoming data with out-of-order reassembly
                if (payload_len > 0)
                {
                    if (seq == sock->rcv_nxt)
                    {
                        // In-order data, copy to buffer
                        usize avail = TcpSocket::RX_BUFFER_SIZE -
                                      ((sock->rx_tail - sock->rx_head) % TcpSocket::RX_BUFFER_SIZE);
                        if (avail == 0)
                            avail = TcpSocket::RX_BUFFER_SIZE;
                        usize copy_len = payload_len < avail ? payload_len : avail;

                        for (usize i = 0; i < copy_len; i++)
                        {
                            sock->rx_buffer[sock->rx_tail % TcpSocket::RX_BUFFER_SIZE] = payload[i];
                            sock->rx_tail++;
                        }
                        sock->rcv_nxt += copy_len;

                        // Check OOO queue for segments that are now in order
                        ooo_deliver(sock);
                    }
                    else if (seq > sock->rcv_nxt)
                    {
                        // Out-of-order segment - buffer it for later reassembly
                        ooo_store(sock, seq, payload, payload_len);
                    }
                    // else: seq < rcv_nxt means duplicate/old data, ignore
                }

                // Handle ACK - update snd_una and clear retransmit state if data acked
                if (tcp_flags & flags::ACK)
                {
                    // Calculate how many new bytes were acknowledged
                    u32 old_una = sock->snd_una;
                    u32 bytes_acked = 0;
                    if (ack > old_una)
                    {
                        bytes_acked = ack - old_una;
                    }

                    // Check if this ACK acknowledges our unacked data
                    if (sock->unacked_len > 0)
                    {
                        u32 unacked_end = sock->unacked_seq + sock->unacked_len;
                        // ACK number is the next expected sequence number
                        // If ack >= unacked_end, all our data was acknowledged
                        if (ack >= unacked_end || ack < sock->unacked_seq)
                        {
                            // Data acknowledged, clear retransmit state
                            sock->unacked_len = 0;
                            sock->rto = TcpSocket::RTO_INITIAL; // Reset RTO
                            sock->retransmit_count = 0;
                        }
                    }
                    sock->snd_una = ack;

                    // Update congestion window based on ACK
                    on_ack_received(sock, bytes_acked);
                }

                // Send ACK if we received data
                if (payload_len > 0)
                {
                    send_segment(sock, flags::ACK, nullptr, 0);
                }
            }
            break;

        case TcpState::FIN_WAIT_1:
            if (tcp_flags & flags::ACK)
            {
                sock->snd_una = ack;
                if (tcp_flags & flags::FIN)
                {
                    sock->rcv_nxt = seq + 1;
                    send_segment(sock, flags::ACK, nullptr, 0);
                    sock->state = TcpState::TIME_WAIT;
                }
                else
                {
                    sock->state = TcpState::FIN_WAIT_2;
                }
            }
            break;

        case TcpState::FIN_WAIT_2:
            if (tcp_flags & flags::FIN)
            {
                sock->rcv_nxt = seq + 1;
                send_segment(sock, flags::ACK, nullptr, 0);
                sock->state = TcpState::TIME_WAIT;
            }
            break;

        case TcpState::CLOSE_WAIT:
            // Waiting for application to close
            break;

        case TcpState::LAST_ACK:
            if (tcp_flags & flags::ACK)
            {
                sock->state = TcpState::CLOSED;
            }
            break;

        case TcpState::TIME_WAIT:
            // Wait 2*MSL, then close (simplified: immediate close)
            sock->state = TcpState::CLOSED;
            break;

        default:
            break;
    }
}

/** @copydoc net::tcp::socket_create */
i32 socket_create()
{
    SpinlockGuard guard(tcp_lock);
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        if (!sockets[i].in_use)
        {
            sockets[i].in_use = true;
            sockets[i].state = TcpState::CLOSED;
            sockets[i].local_port = 0;
            sockets[i].remote_port = 0;
            sockets[i].rx_head = sockets[i].rx_tail = 0;
            sockets[i].tx_len = 0;
            sockets[i].rcv_wnd = TcpSocket::RX_BUFFER_SIZE;
            sockets[i].mss = DEFAULT_MSS; // Will be negotiated during handshake
            // Initialize retransmit state
            sockets[i].unacked_len = 0;
            sockets[i].unacked_seq = 0;
            sockets[i].retransmit_time = 0;
            sockets[i].rto = TcpSocket::RTO_INITIAL;
            sockets[i].retransmit_count = 0;
            // Initialize congestion control (RFC 5681)
            sockets[i].cwnd = TcpSocket::INITIAL_CWND_SEGMENTS * DEFAULT_MSS;
            sockets[i].ssthresh = 65535; // Initial ssthresh (arbitrarily high)
            sockets[i].dup_acks = 0;
            sockets[i].srtt = 0;
            sockets[i].rttvar = 0;
            sockets[i].rtt_measured = false;
            sockets[i].bytes_in_flight = 0;
            // Initialize out-of-order queue
            for (usize j = 0; j < TcpSocket::OOO_MAX_SEGMENTS; j++)
            {
                sockets[i].ooo_queue[j].valid = false;
                sockets[i].ooo_queue[j].len = 0;
            }
            return static_cast<i32>(i);
        }
    }
    return -1;
}

/** @copydoc net::tcp::socket_bind */
bool socket_bind(i32 sock, u16 port)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return false;
    }

    SpinlockGuard guard(tcp_lock);

    if (!sockets[sock].in_use)
    {
        return false;
    }

    // Check if port is in use
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        if (sockets[i].in_use && sockets[i].local_port == port && i != static_cast<usize>(sock))
        {
            return false;
        }
    }

    sockets[sock].local_port = port;
    return true;
}

/** @copydoc net::tcp::socket_listen */
bool socket_listen(i32 sock)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return false;
    }

    SpinlockGuard guard(tcp_lock);

    if (!sockets[sock].in_use || sockets[sock].local_port == 0)
    {
        return false;
    }

    sockets[sock].state = TcpState::LISTEN;
    return true;
}

/** @copydoc net::tcp::socket_accept */
i32 socket_accept(i32 sock)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return -1;
    }

    {
        SpinlockGuard guard(tcp_lock);
        if (!sockets[sock].in_use || sockets[sock].state != TcpState::LISTEN)
        {
            return -1;
        }
    }

    // Poll network (without lock - may call rx_segment which acquires lock)
    network_poll();

    // Check if connection completed (SYN_RECEIVED -> ESTABLISHED handled in rx)
    SpinlockGuard guard(tcp_lock);
    if (sockets[sock].state == TcpState::ESTABLISHED)
    {
        // This socket is now the connection socket
        // In a real implementation, we'd clone the listening socket
        return sock;
    }

    return -1; // No connection ready
}

/** @copydoc net::tcp::socket_connect */
bool socket_connect(i32 sock, const Ipv4Addr &dst, u16 port)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return false;
    }

    TcpSocket *s = &sockets[sock];

    {
        SpinlockGuard guard(tcp_lock);

        if (!s->in_use || s->state != TcpState::CLOSED)
        {
            return false;
        }

        // Assign ephemeral port if not bound
        if (s->local_port == 0)
        {
            static u16 next_port = 49152;
            s->local_port = next_port;
            next_port++;
            if (next_port < 49152)
                next_port = 49152; // Wrap around
        }

        // Set up connection
        copy_ip(s->remote_ip, dst);
        s->remote_port = port;
        s->snd_nxt = generate_isn();
        s->snd_una = s->snd_nxt;
        s->rcv_wnd = TcpSocket::RX_BUFFER_SIZE;
        s->rx_head = s->rx_tail = 0;
        s->last_activity = timer::get_ticks();

        // Send SYN
        s->state = TcpState::SYN_SENT;
        send_segment(s, flags::SYN, nullptr, 0);
    }

    // Wait for connection (with timeout) - poll without holding lock
    u64 start = timer::get_ticks();
    u64 timeout = 5000; // 5 seconds

    while (timer::get_ticks() - start < timeout)
    {
        network_poll();

        tcp_lock.acquire();
        TcpState current_state = s->state;
        tcp_lock.release();

        if (current_state == TcpState::ESTABLISHED)
        {
            return true;
        }
        if (current_state == TcpState::CLOSED)
        {
            return false; // Connection refused
        }
        asm volatile("wfi");
    }

    // Timeout
    SpinlockGuard guard(tcp_lock);
    s->state = TcpState::CLOSED;
    return false;
}

/** @copydoc net::tcp::socket_send */
i32 socket_send(i32 sock, const void *data, usize len)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return -1;
    }
    TcpSocket *s = &sockets[sock];

    {
        SpinlockGuard guard(tcp_lock);
        if (!s->in_use)
        {
            return -1;
        }
        if (s->state != TcpState::ESTABLISHED)
        {
            return -1;
        }
    }

    // Send data in segments using negotiated MSS
    const u8 *p = static_cast<const u8 *>(data);
    usize sent = 0;
    usize mss = s->mss; // Use negotiated MSS

    while (sent < len)
    {
        usize chunk = len - sent;
        if (chunk > mss)
            chunk = mss;

        // Retry send with ARP resolution
        bool segment_sent = false;
        u64 retry_start = timer::get_ticks();
        while (!segment_sent && timer::get_ticks() - retry_start < 2000)
        {
            tcp_lock.acquire();

            // Save data for retransmission before sending
            s->unacked_seq = s->snd_nxt;
            if (chunk <= TcpSocket::UNACKED_BUFFER_SIZE)
            {
                for (usize i = 0; i < chunk; i++)
                {
                    s->unacked_data[i] = p[sent + i];
                }
                s->unacked_len = chunk;
            }

            bool result = send_segment(s, flags::ACK | flags::PSH, p + sent, chunk);

            if (result)
            {
                // Set retransmit timer
                s->retransmit_time = timer::get_ticks() + s->rto;
                s->retransmit_count = 0;
                segment_sent = true;
            }

            tcp_lock.release();

            if (!segment_sent)
            {
                // Wait for ARP resolution
                for (int i = 0; i < 100; i++)
                {
                    network_poll();
                    asm volatile("wfi");
                }
            }
        }
        if (!segment_sent)
        {
            break;
        }
        sent += chunk;

        // Wait for ACK (with retransmit support)
        u64 start = timer::get_ticks();
        while (timer::get_ticks() - start < 5000) // Extended timeout for retransmits
        {
            network_poll();

            tcp_lock.acquire();
            bool ack_received = s->snd_una >= s->snd_nxt;
            if (ack_received)
            {
                // Clear retransmit state on ACK
                s->unacked_len = 0;
                s->rto = TcpSocket::RTO_INITIAL; // Reset RTO on success
            }
            tcp_lock.release();

            if (ack_received)
            {
                break; // ACK received
            }

            // Check if we've exceeded max retries
            tcp_lock.acquire();
            bool give_up = s->retransmit_count >= TcpSocket::RETRANSMIT_MAX;
            tcp_lock.release();

            if (give_up)
            {
                // Connection failed
                return sent > 0 ? static_cast<i32>(sent) : -1;
            }

            asm volatile("wfi");
        }
    }

    return static_cast<i32>(sent);
}

/** @copydoc net::tcp::socket_recv */
i32 socket_recv(i32 sock, void *buffer, usize max_len)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return -1;
    }
    TcpSocket *s = &sockets[sock];

    {
        SpinlockGuard guard(tcp_lock);
        if (!s->in_use)
        {
            return -1;
        }
    }

    // Get current task for blocking
    task::Task *current = task::current();
    virtio::NetDevice *net = virtio::net_device();

    // Retry loop - wait for data with interrupt-driven wakeup
    u64 start = timer::get_ticks();
    constexpr u64 RECV_TIMEOUT_MS = 30000; // 30 second timeout

    while (true)
    {
        // Poll for new packets (processes any queued data)
        network_poll();

        tcp_lock.acquire();

        // Check for closed connection
        if (s->state == TcpState::CLOSED || s->state == TcpState::CLOSE_WAIT)
        {
            if (s->rx_head == s->rx_tail)
            {
                tcp_lock.release();
                return -1; // Connection closed and no more data
            }
        }

        // Check for available data
        usize avail = (s->rx_tail - s->rx_head);
        if (avail > 0)
        {
            // Copy available data
            usize copy_len = avail < max_len ? avail : max_len;
            u8 *dst = static_cast<u8 *>(buffer);

            for (usize i = 0; i < copy_len; i++)
            {
                dst[i] = s->rx_buffer[s->rx_head % TcpSocket::RX_BUFFER_SIZE];
                s->rx_head++;
            }

            tcp_lock.release();

            // Unregister from wait queue
            if (net && current)
            {
                net->unregister_rx_waiter(current);
            }

            return static_cast<i32>(copy_len);
        }

        tcp_lock.release();

        // Check timeout
        if (timer::get_ticks() - start > RECV_TIMEOUT_MS)
        {
            if (net && current)
            {
                net->unregister_rx_waiter(current);
            }
            return 0; // Timeout, no data
        }

        // No data available - block waiting for network interrupt
        if (net && current)
        {
            // Register as waiter and block
            net->register_rx_waiter(current);
            current->state = task::TaskState::Blocked;
            task::yield();
        }
        else
        {
            // No interrupt support - just yield
            asm volatile("wfi");
        }
    }
}

/** @copydoc net::tcp::socket_close */
void socket_close(i32 sock)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return;
    }
    TcpSocket *s = &sockets[sock];

    TcpState current_state;
    {
        SpinlockGuard guard(tcp_lock);
        if (!s->in_use)
        {
            return;
        }
        current_state = s->state;
    }

    if (current_state == TcpState::ESTABLISHED)
    {
        // Send FIN
        tcp_lock.acquire();
        send_segment(s, flags::FIN | flags::ACK, nullptr, 0);
        s->state = TcpState::FIN_WAIT_1;
        tcp_lock.release();

        // Wait for close (simplified) - poll without lock
        u64 start = timer::get_ticks();
        while (timer::get_ticks() - start < 2000)
        {
            network_poll();

            tcp_lock.acquire();
            TcpState state = s->state;
            tcp_lock.release();

            if (state == TcpState::CLOSED || state == TcpState::TIME_WAIT)
            {
                break;
            }
            asm volatile("wfi");
        }
    }
    else if (current_state == TcpState::CLOSE_WAIT)
    {
        SpinlockGuard guard(tcp_lock);
        send_segment(s, flags::FIN | flags::ACK, nullptr, 0);
        s->state = TcpState::LAST_ACK;
    }

    SpinlockGuard guard(tcp_lock);
    s->state = TcpState::CLOSED;
    s->in_use = false;
}

/** @copydoc net::tcp::socket_connected */
bool socket_connected(i32 sock)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return false;
    }
    SpinlockGuard guard(tcp_lock);
    return sockets[sock].in_use && sockets[sock].state == TcpState::ESTABLISHED;
}

/** @copydoc net::tcp::socket_available */
usize socket_available(i32 sock)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return 0;
    }
    SpinlockGuard guard(tcp_lock);
    TcpSocket *s = &sockets[sock];
    if (!s->in_use)
    {
        return 0;
    }
    return s->rx_tail - s->rx_head;
}

/**
 * @brief Handle congestion event (timeout or loss detection).
 *
 * @details
 * Per RFC 5681, on timeout:
 * - ssthresh = max(FlightSize/2, 2*SMSS)
 * - cwnd = 1 segment (loss window)
 *
 * @param sock Socket experiencing congestion.
 */
static void on_congestion_event(TcpSocket *sock)
{
    // Set ssthresh to half of flight size, minimum 2 segments
    u32 flight_size = sock->bytes_in_flight > 0 ? sock->bytes_in_flight : sock->cwnd;
    sock->ssthresh = flight_size / 2;
    if (sock->ssthresh < TcpSocket::MIN_SSTHRESH)
    {
        sock->ssthresh = TcpSocket::MIN_SSTHRESH;
    }

    // On timeout, cwnd = 1 segment (enter slow start)
    sock->cwnd = sock->mss;

    serial::puts("[tcp] Congestion: ssthresh=");
    serial::put_dec(sock->ssthresh);
    serial::puts(" cwnd=");
    serial::put_dec(sock->cwnd);
    serial::puts("\n");
}

/**
 * @brief Process a new ACK and update congestion window.
 *
 * @details
 * RFC 5681 congestion avoidance:
 * - Slow start (cwnd < ssthresh): cwnd += MSS per ACK
 * - Congestion avoidance (cwnd >= ssthresh): cwnd += MSS*MSS/cwnd per ACK
 *
 * @param sock Socket receiving the ACK.
 * @param bytes_acked Number of new bytes acknowledged.
 */
static void on_ack_received(TcpSocket *sock, u32 bytes_acked)
{
    if (bytes_acked == 0)
    {
        // Duplicate ACK
        sock->dup_acks++;

        if (sock->dup_acks == TcpSocket::DUP_ACK_THRESHOLD)
        {
            // Fast retransmit (RFC 5681 Section 3.2)
            serial::puts("[tcp] Fast retransmit triggered\n");

            // Set ssthresh to half of cwnd
            sock->ssthresh = sock->cwnd / 2;
            if (sock->ssthresh < TcpSocket::MIN_SSTHRESH)
            {
                sock->ssthresh = TcpSocket::MIN_SSTHRESH;
            }

            // Enter fast recovery: cwnd = ssthresh + 3*MSS
            sock->cwnd = sock->ssthresh + 3 * sock->mss;
        }
        else if (sock->dup_acks > TcpSocket::DUP_ACK_THRESHOLD)
        {
            // Fast recovery: inflate cwnd by MSS for each additional dup ACK
            sock->cwnd += sock->mss;
        }
        return;
    }

    // New data acknowledged - exit fast recovery if we were in it
    sock->dup_acks = 0;

    // Update flight size
    if (sock->bytes_in_flight >= bytes_acked)
    {
        sock->bytes_in_flight -= bytes_acked;
    }
    else
    {
        sock->bytes_in_flight = 0;
    }

    // Congestion window update
    if (sock->cwnd < sock->ssthresh)
    {
        // Slow start: increase cwnd by bytes_acked (up to MSS per ACK)
        u32 increase = bytes_acked > sock->mss ? sock->mss : bytes_acked;
        sock->cwnd += increase;
    }
    else
    {
        // Congestion avoidance: increase cwnd by ~1 segment per RTT
        // Approximation: cwnd += MSS * MSS / cwnd per ACK
        u32 increase = (sock->mss * sock->mss) / sock->cwnd;
        if (increase == 0)
            increase = 1;
        sock->cwnd += increase;
    }
}

/**
 * @brief Retransmit unacked data for a socket.
 *
 * @details
 * Internal helper that resends the buffered unacked data using the original
 * sequence number. Updates the retransmit timer with exponential backoff.
 *
 * @param sock Socket to retransmit for.
 */
static void retransmit_segment(TcpSocket *sock)
{
    if (sock->unacked_len == 0)
    {
        return;
    }

    // Congestion event on timeout
    on_congestion_event(sock);

    // Save current snd_nxt and restore sequence number for retransmit
    u32 saved_snd_nxt = sock->snd_nxt;
    sock->snd_nxt = sock->unacked_seq;

    // Retransmit the data
    send_segment(sock, flags::ACK | flags::PSH, sock->unacked_data, sock->unacked_len);

    // Restore snd_nxt to where it should be (after the retransmitted data)
    // send_segment already advanced it by unacked_len, which is correct
    // But we need to make sure we're at saved_snd_nxt (the original next seq)
    sock->snd_nxt = saved_snd_nxt;

    // Apply exponential backoff
    sock->rto *= 2;
    if (sock->rto > TcpSocket::RTO_MAX)
    {
        sock->rto = TcpSocket::RTO_MAX;
    }

    // Set next retransmit time
    sock->retransmit_time = timer::get_ticks() + sock->rto;
    sock->retransmit_count++;

    serial::puts("[tcp] Retransmit #");
    serial::put_dec(sock->retransmit_count);
    serial::puts(" for port ");
    serial::put_dec(sock->local_port);
    serial::puts(", RTO=");
    serial::put_dec(sock->rto);
    serial::puts("ms\n");
}

/** @copydoc net::tcp::check_retransmit */
void check_retransmit()
{
    if (!initialized)
    {
        return;
    }

    u64 now = timer::get_ticks();

    SpinlockGuard guard(tcp_lock);

    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        TcpSocket *s = &sockets[i];

        // Only check established sockets with unacked data
        if (!s->in_use || s->state != TcpState::ESTABLISHED)
        {
            continue;
        }

        if (s->unacked_len == 0)
        {
            continue; // No unacked data
        }

        // Check if retransmit timer expired
        if (now >= s->retransmit_time)
        {
            if (s->retransmit_count >= TcpSocket::RETRANSMIT_MAX)
            {
                // Too many retries, give up and close connection
                serial::puts("[tcp] Max retransmits exceeded, closing connection\n");
                s->state = TcpState::CLOSED;
                s->unacked_len = 0;
            }
            else
            {
                retransmit_segment(s);
            }
        }
    }
}

u32 get_active_count()
{
    u32 count = 0;
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        if (sockets[i].in_use)
        {
            TcpState state = sockets[i].state;
            if (state == TcpState::ESTABLISHED || state == TcpState::SYN_SENT ||
                state == TcpState::SYN_RECEIVED || state == TcpState::FIN_WAIT_1 ||
                state == TcpState::FIN_WAIT_2 || state == TcpState::CLOSE_WAIT)
            {
                count++;
            }
        }
    }
    return count;
}

u32 get_listen_count()
{
    u32 count = 0;
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        if (sockets[i].in_use && sockets[i].state == TcpState::LISTEN)
        {
            count++;
        }
    }
    return count;
}

} // namespace tcp
} // namespace net
