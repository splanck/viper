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
#include "../../drivers/virtio/rng.hpp"
#include "../../lib/spinlock.hpp"
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
 * @brief Send a TCP segment for a socket.
 *
 * @details
 * Constructs a minimal TCP header (no options), copies payload bytes (if any),
 * computes checksum, and transmits the segment via IPv4. Sequence number
 * tracking is updated according to flags and payload length.
 *
 * @param sock Socket control block (must have remote_ip/ports populated).
 * @param tcp_flags TCP flags to set on the segment (e.g., SYN, ACK, FIN).
 * @param data Optional payload pointer (may be `nullptr` for empty segments).
 * @param len Payload length in bytes.
 * @return `true` if the segment was transmitted, otherwise `false`.
 */
static bool send_segment(TcpSocket *sock, u8 tcp_flags, const void *data, usize len)
{
    static u8 packet[TCP_HEADER_MIN + 1460] __attribute__((aligned(4)));
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(packet);

    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->snd_nxt);
    tcp->ack_num = htonl(sock->rcv_nxt);
    tcp->data_offset = (TCP_HEADER_MIN / 4) << 4; // No options
    tcp->flags = tcp_flags;
    tcp->window = htons(sock->rcv_wnd);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    // Copy data if any
    if (data && len > 0)
    {
        u8 *payload = packet + TCP_HEADER_MIN;
        const u8 *src = static_cast<const u8 *>(data);
        for (usize i = 0; i < len; i++)
        {
            payload[i] = src[i];
        }
    }

    // Calculate checksum
    Ipv4Addr our_ip = netif().ip();
    tcp->checksum = htons(tcp_checksum(our_ip, sock->remote_ip, packet, TCP_HEADER_MIN + len));

    // Update sequence number
    if (tcp_flags & (flags::SYN | flags::FIN))
    {
        sock->snd_nxt++; // SYN and FIN consume a sequence number
    }
    sock->snd_nxt += len;

    return ip::tx_packet(sock->remote_ip, ip::protocol::TCP, packet, TCP_HEADER_MIN + len);
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

                // Send ACK
                send_segment(sock, flags::ACK, nullptr, 0);
                sock->state = TcpState::ESTABLISHED;
            }
            else if (tcp_flags & flags::SYN)
            {
                // Simultaneous open (rare)
                sock->rcv_nxt = seq + 1;
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
                // Handle incoming data
                if (payload_len > 0 && seq == sock->rcv_nxt)
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
                }

                // Handle ACK - update snd_una and clear retransmit state if data acked
                if (tcp_flags & flags::ACK)
                {
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
            // Initialize retransmit state
            sockets[i].unacked_len = 0;
            sockets[i].unacked_seq = 0;
            sockets[i].retransmit_time = 0;
            sockets[i].rto = TcpSocket::RTO_INITIAL;
            sockets[i].retransmit_count = 0;
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

    // Send data in segments
    const u8 *p = static_cast<const u8 *>(data);
    usize sent = 0;
    constexpr usize MSS = 1460; // Maximum segment size

    while (sent < len)
    {
        usize chunk = len - sent;
        if (chunk > MSS)
            chunk = MSS;

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

    // Poll for data (without lock)
    network_poll();

    SpinlockGuard guard(tcp_lock);

    // Check for closed connection
    if (s->state == TcpState::CLOSED || s->state == TcpState::CLOSE_WAIT)
    {
        if (s->rx_head == s->rx_tail)
        {
            return -1; // Connection closed and no more data
        }
    }

    // Copy available data
    usize avail = (s->rx_tail - s->rx_head);
    if (avail == 0)
    {
        return 0; // No data
    }

    usize copy_len = avail < max_len ? avail : max_len;
    u8 *dst = static_cast<u8 *>(buffer);

    for (usize i = 0; i < copy_len; i++)
    {
        dst[i] = s->rx_buffer[s->rx_head % TcpSocket::RX_BUFFER_SIZE];
        s->rx_head++;
    }

    return static_cast<i32>(copy_len);
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

} // namespace tcp
} // namespace net
