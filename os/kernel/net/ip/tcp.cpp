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
#include "../../include/config.hpp"
#include "../../lib/log.hpp"
#include "../../lib/spinlock.hpp"
#include "../../lib/timerwheel.hpp"
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

// TIME_WAIT duration: 2 * Maximum Segment Lifetime (2MSL)
// RFC 793 specifies MSL as 2 minutes, so 2MSL = 4 minutes
// We use 60 seconds as a practical compromise
constexpr u64 TIME_WAIT_DURATION_MS = 60000;

[[maybe_unused]] static inline bool tcp_debug_enabled()
{
    return log::get_level() == log::Level::Debug;
}

/**
 * @brief Timer callback for TIME_WAIT expiration.
 *
 * @details
 * Called by the timer wheel when 2MSL expires. Transitions the socket
 * from TIME_WAIT to CLOSED and releases it for reuse.
 *
 * @param context Pointer to the socket index (as uintptr_t).
 */
static void time_wait_expired(void *context)
{
    usize sock_idx = reinterpret_cast<usize>(context);
    if (sock_idx >= MAX_TCP_SOCKETS)
        return;

    SpinlockGuard guard(tcp_lock);

    TcpSocket *sock = &sockets[sock_idx];
    if (!sock->in_use || sock->state != TcpState::TIME_WAIT)
        return;

#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] TIME_WAIT expired for port ");
        serial::put_dec(sock->local_port);
        serial::puts(", releasing socket\n");
    }
#endif

    sock->state = TcpState::CLOSED;
    sock->in_use = false;
    sock->owner_viper_id = 0;
    sock->time_wait_timer = 0;
}

// Forward declarations for congestion control
static void on_congestion_event(TcpSocket *sock);
static void on_ack_received(TcpSocket *sock, u32 bytes_acked);

/**
 * @brief Enter TIME_WAIT state and schedule cleanup timer.
 *
 * @details
 * Sets the socket to TIME_WAIT state and schedules a timer for 2MSL
 * duration. When the timer expires, the socket will be cleaned up.
 *
 * @param sock Socket entering TIME_WAIT.
 * @param sock_idx Index of the socket in the socket table.
 */
static void enter_time_wait(TcpSocket *sock, usize sock_idx)
{
    sock->state = TcpState::TIME_WAIT;
    sock->unacked_len = 0; // Clear retransmit state

    // Schedule TIME_WAIT timer
    sock->time_wait_timer = timerwheel::schedule(
        TIME_WAIT_DURATION_MS, time_wait_expired, reinterpret_cast<void *>(sock_idx));

    if (sock->time_wait_timer == 0)
    {
        // Timer scheduling failed - fall back to immediate cleanup
#if VIPER_KERNEL_DEBUG_TCP
        if (tcp_debug_enabled())
        {
            serial::puts("[tcp] Warning: TIME_WAIT timer failed, immediate cleanup\n");
        }
#endif
        sock->state = TcpState::CLOSED;
        sock->in_use = false;
        sock->owner_viper_id = 0;
    }
    else
    {
#if VIPER_KERNEL_DEBUG_TCP
        if (tcp_debug_enabled())
        {
            serial::puts("[tcp] Entering TIME_WAIT for port ");
            serial::put_dec(sock->local_port);
            serial::puts(" (");
            serial::put_dec(TIME_WAIT_DURATION_MS / 1000);
            serial::puts("s)\n");
        }
#endif
    }
}

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
            sock->ooo_queue[i].len = static_cast<u16>(
                len > TcpSocket::OOO_SEGMENT_SIZE ? TcpSocket::OOO_SEGMENT_SIZE : len);
            sock->ooo_queue[i].valid = true;
            for (usize j = 0; j < sock->ooo_queue[i].len; j++)
            {
                sock->ooo_queue[i].data[j] = data[j];
            }
#if VIPER_KERNEL_DEBUG_TCP
            if (tcp_debug_enabled())
            {
                serial::puts("[tcp] OOO: stored seq ");
                serial::put_dec(seq);
                serial::puts(" len ");
                serial::put_dec(len);
                serial::puts("\n");
            }
#endif
            return true;
        }
    }

#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] OOO queue full, dropping segment\n");
    }
#endif
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

#if VIPER_KERNEL_DEBUG_TCP
                    if (tcp_debug_enabled())
                    {
                        serial::puts("[tcp] OOO: delivered seq ");
                        serial::put_dec(sock->ooo_queue[i].seq);
                        serial::puts(" len ");
                        serial::put_dec(len);
                        serial::puts("\n");
                    }
#endif
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
 * @brief Build SACK blocks from the out-of-order queue.
 *
 * @details
 * Scans the OOO queue and constructs SACK blocks representing
 * received but out-of-order segments. SACK blocks are sorted by
 * sequence number. Adjacent or overlapping segments are merged.
 *
 * @param sock Socket with OOO queue to scan.
 * @param blocks Output array for SACK blocks (at least MAX_SACK_BLOCKS entries).
 * @return Number of SACK blocks generated (0 to MAX_SACK_BLOCKS).
 */
static u8 build_sack_blocks(TcpSocket *sock, TcpSocket::SackBlock *blocks)
{
    // Collect all valid OOO segments
    struct Segment
    {
        u32 left;
        u32 right;
    };

    Segment segments[TcpSocket::OOO_MAX_SEGMENTS];
    usize count = 0;

    for (usize i = 0; i < TcpSocket::OOO_MAX_SEGMENTS; i++)
    {
        if (sock->ooo_queue[i].valid)
        {
            segments[count].left = sock->ooo_queue[i].seq;
            segments[count].right = sock->ooo_queue[i].seq + sock->ooo_queue[i].len;
            count++;
        }
    }

    if (count == 0)
    {
        return 0;
    }

    // Simple bubble sort by sequence number
    for (usize i = 0; i < count - 1; i++)
    {
        for (usize j = 0; j < count - i - 1; j++)
        {
            if (segments[j].left > segments[j + 1].left)
            {
                Segment tmp = segments[j];
                segments[j] = segments[j + 1];
                segments[j + 1] = tmp;
            }
        }
    }

    // Merge overlapping/adjacent segments and build SACK blocks
    u8 num_blocks = 0;
    u32 cur_left = segments[0].left;
    u32 cur_right = segments[0].right;

    for (usize i = 1; i < count; i++)
    {
        if (segments[i].left <= cur_right)
        {
            // Overlapping or adjacent, extend current block
            if (segments[i].right > cur_right)
            {
                cur_right = segments[i].right;
            }
        }
        else
        {
            // Gap found, emit current block
            if (num_blocks < MAX_SACK_BLOCKS)
            {
                blocks[num_blocks].left = cur_left;
                blocks[num_blocks].right = cur_right;
                num_blocks++;
            }
            cur_left = segments[i].left;
            cur_right = segments[i].right;
        }
    }

    // Emit final block
    if (num_blocks < MAX_SACK_BLOCKS)
    {
        blocks[num_blocks].left = cur_left;
        blocks[num_blocks].right = cur_right;
        num_blocks++;
    }

    return num_blocks;
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
 * @brief Parsed TCP options structure.
 */
struct TcpOptions
{
    u16 mss;        // MSS value (0 if not present)
    u8 wscale;      // Window scale (255 if not present)
    bool sack_perm; // SACK permitted option present
    bool has_mss;
    bool has_wscale;

    // SACK blocks from incoming segment
    TcpSocket::SackBlock sack_blocks[MAX_SACK_BLOCKS];
    u8 num_sack_blocks;
};

/**
 * @brief Parse TCP options from a segment.
 *
 * @details
 * Scans TCP options and extracts MSS, window scale, SACK permitted,
 * and SACK blocks.
 *
 * @param options Pointer to TCP options (after 20-byte header).
 * @param options_len Length of options in bytes.
 * @param out Output structure for parsed options.
 */
static void parse_tcp_options(const u8 *options, usize options_len, TcpOptions *out)
{
    out->mss = 0;
    out->wscale = 255;
    out->sack_perm = false;
    out->has_mss = false;
    out->has_wscale = false;
    out->num_sack_blocks = 0;

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

        switch (kind)
        {
            case option::MSS:
                if (len == option::MSS_LEN && i + 4 <= options_len)
                {
                    out->mss = (static_cast<u16>(options[i + 2]) << 8) | options[i + 3];
                    out->has_mss = true;
                }
                break;

            case option::WSCALE:
                if (len == option::WSCALE_LEN && i + 3 <= options_len)
                {
                    out->wscale = options[i + 2];
                    if (out->wscale > MAX_WSCALE)
                    {
                        out->wscale = MAX_WSCALE; // Clamp to max
                    }
                    out->has_wscale = true;
                }
                break;

            case option::SACK_PERM:
                if (len == option::SACK_PERM_LEN)
                {
                    out->sack_perm = true;
                }
                break;

            case option::SACK:
                // SACK option: kind(1) + len(1) + blocks(8 bytes each)
                if (len >= 2)
                {
                    usize block_bytes = len - 2;
                    usize num_blocks = block_bytes / 8;
                    if (num_blocks > MAX_SACK_BLOCKS)
                    {
                        num_blocks = MAX_SACK_BLOCKS;
                    }
                    for (usize b = 0; b < num_blocks; b++)
                    {
                        usize offset = i + 2 + b * 8;
                        if (offset + 8 <= options_len)
                        {
                            out->sack_blocks[b].left =
                                (static_cast<u32>(options[offset]) << 24) |
                                (static_cast<u32>(options[offset + 1]) << 16) |
                                (static_cast<u32>(options[offset + 2]) << 8) | options[offset + 3];
                            out->sack_blocks[b].right =
                                (static_cast<u32>(options[offset + 4]) << 24) |
                                (static_cast<u32>(options[offset + 5]) << 16) |
                                (static_cast<u32>(options[offset + 6]) << 8) | options[offset + 7];
                            out->num_sack_blocks++;
                        }
                    }
                }
                break;

            default:
                // Unknown option, skip
                break;
        }

        i += len;
    }
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
    static u8 packet[TCP_HEADER_SYN_OPTS + DEFAULT_MSS] __attribute__((aligned(4)));
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(packet);

    // Determine header size (include full options for SYN packets)
    bool is_syn = (tcp_flags & flags::SYN) != 0;
    usize header_len = is_syn ? TCP_HEADER_SYN_OPTS : TCP_HEADER_MIN;

    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->snd_nxt);
    tcp->ack_num = htonl(sock->rcv_nxt);
    tcp->data_offset = static_cast<u8>((header_len / 4) << 4);
    tcp->flags = tcp_flags;

    // Advertise scaled window if negotiated, otherwise raw window
    u16 advertised_window = sock->rcv_wnd;
    if (sock->wscale_enabled && sock->rcv_wscale > 0)
    {
        // Scale down for advertisement (we store unscaled internally)
        advertised_window = static_cast<u16>(sock->rcv_wnd >> sock->rcv_wscale);
    }
    tcp->window = htons(advertised_window);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    // Add TCP options for SYN packets: MSS + NOP + WSCALE + SACK_PERM + NOP + NOP
    if (is_syn)
    {
        u8 *opts = packet + TCP_HEADER_MIN;
        // MSS option (4 bytes)
        opts[0] = option::MSS;
        opts[1] = option::MSS_LEN;
        opts[2] = (DEFAULT_MSS >> 8);
        opts[3] = (DEFAULT_MSS & 0xFF);
        // NOP + WSCALE option (4 bytes total)
        opts[4] = option::NOP;
        opts[5] = option::WSCALE;
        opts[6] = option::WSCALE_LEN;
        opts[7] = OUR_WSCALE; // Our window scale factor
        // SACK_PERM option + 2 NOPs for padding (4 bytes total)
        opts[8] = option::SACK_PERM;
        opts[9] = option::SACK_PERM_LEN;
        opts[10] = option::NOP;
        opts[11] = option::NOP;
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
 * @brief Send an ACK with SACK blocks if available.
 *
 * @details
 * Sends an ACK segment. If SACK is negotiated and there are out-of-order
 * segments buffered, includes SACK option with block information.
 *
 * @param sock Socket to send ACK on.
 * @return `true` if ACK was sent successfully.
 */
static bool send_ack_with_sack(TcpSocket *sock)
{
    // If SACK not permitted or no OOO segments, send regular ACK
    if (!sock->sack_permitted)
    {
        return send_segment(sock, flags::ACK, nullptr, 0);
    }

    // Build SACK blocks from OOO queue
    TcpSocket::SackBlock sack_blocks[MAX_SACK_BLOCKS];
    u8 num_blocks = build_sack_blocks(sock, sack_blocks);

    if (num_blocks == 0)
    {
        return send_segment(sock, flags::ACK, nullptr, 0);
    }

    // Calculate header size with SACK option
    // SACK option: kind(1) + len(1) + n*8 bytes for blocks
    // Pad to 4-byte boundary
    usize sack_option_len = 2 + (num_blocks * 8);
    usize options_len = ((sack_option_len + 3) / 4) * 4; // Round up to 4 bytes
    usize header_len = TCP_HEADER_MIN + options_len;

    static u8 packet[TCP_HEADER_MIN + 40] __attribute__((aligned(4))); // Max 4 SACK blocks
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(packet);

    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->snd_nxt);
    tcp->ack_num = htonl(sock->rcv_nxt);
    tcp->data_offset = static_cast<u8>((header_len / 4) << 4);
    tcp->flags = flags::ACK;

    // Advertise scaled window
    u16 advertised_window = sock->rcv_wnd;
    if (sock->wscale_enabled && sock->rcv_wscale > 0)
    {
        advertised_window = static_cast<u16>(sock->rcv_wnd >> sock->rcv_wscale);
    }
    tcp->window = htons(advertised_window);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    // Build SACK option
    u8 *opts = packet + TCP_HEADER_MIN;
    opts[0] = option::SACK;
    opts[1] = static_cast<u8>(sack_option_len);

    for (u8 i = 0; i < num_blocks; i++)
    {
        u32 left = htonl(sack_blocks[i].left);
        u32 right = htonl(sack_blocks[i].right);
        usize base = 2 + (i * 8);
        opts[base + 0] = (left >> 24) & 0xFF;
        opts[base + 1] = (left >> 16) & 0xFF;
        opts[base + 2] = (left >> 8) & 0xFF;
        opts[base + 3] = left & 0xFF;
        opts[base + 4] = (right >> 24) & 0xFF;
        opts[base + 5] = (right >> 16) & 0xFF;
        opts[base + 6] = (right >> 8) & 0xFF;
        opts[base + 7] = right & 0xFF;
    }

    // Pad with NOPs
    for (usize i = sack_option_len; i < options_len; i++)
    {
        opts[i] = option::NOP;
    }

    // Calculate checksum
    Ipv4Addr our_ip = netif().ip();
    tcp->checksum = htons(tcp_checksum(our_ip, sock->remote_ip, packet, header_len));

    return ip::tx_packet(sock->remote_ip, ip::protocol::TCP, packet, header_len);
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
        sockets[i].owner_viper_id = 0;
    }
    initialized = true;
#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] TCP layer initialized\n");
    }
#endif
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

    // Debug: show incoming segment
#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] RX ");
        serial::put_dec(src_port);
        serial::puts("->");
        serial::put_dec(dst_port);
        serial::puts(" seq=");
        serial::put_hex(seq);
        serial::puts(" len=");
        serial::put_dec(payload_len);
        serial::puts(" flags=");
        if (tcp_flags & flags::SYN)
            serial::puts("S");
        if (tcp_flags & flags::ACK)
            serial::puts("A");
        if (tcp_flags & flags::FIN)
            serial::puts("F");
        if (tcp_flags & flags::RST)
            serial::puts("R");
        if (tcp_flags & flags::PSH)
            serial::puts("P");
        serial::puts("\n");
    }
#endif

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

    // Compute socket index for timer callbacks
    usize sock_idx = static_cast<usize>(sock - sockets);

#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] sock[");
        serial::put_dec(sock_idx);
        serial::puts("] state=");
        serial::put_dec(static_cast<int>(sock->state));
        serial::puts("\n");
    }
#endif

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

                // Parse TCP options from SYN
                sock->mss = DEFAULT_MSS;
                sock->wscale_enabled = false;
                sock->snd_wscale = 0;
                sock->sack_permitted = false;
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    TcpOptions opts;
                    parse_tcp_options(opt_ptr, data_offset - TCP_HEADER_MIN, &opts);

                    // MSS negotiation
                    if (opts.has_mss && opts.mss > 0)
                    {
                        sock->mss = opts.mss < DEFAULT_MSS ? opts.mss : DEFAULT_MSS;
                    }

                    // Window scaling (RFC 7323): both sides must offer for it to be enabled
                    if (opts.has_wscale)
                    {
                        sock->snd_wscale = opts.wscale < MAX_WSCALE ? opts.wscale : MAX_WSCALE;
                        sock->wscale_enabled = true;
                    }

                    // SACK permitted (RFC 2018)
                    if (opts.sack_perm)
                    {
                        sock->sack_permitted = true;
                    }
                }

                // Send SYN+ACK (includes our WSCALE and SACK_PERM options)
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

                // Parse TCP options from SYN+ACK
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    TcpOptions opts;
                    parse_tcp_options(opt_ptr, data_offset - TCP_HEADER_MIN, &opts);

                    // MSS negotiation
                    if (opts.has_mss && opts.mss > 0)
                    {
                        sock->mss = opts.mss < sock->mss ? opts.mss : sock->mss;
                    }

                    // Window scaling: only enable if peer also offered it
                    // We already sent our WSCALE in SYN, peer must respond with theirs
                    if (opts.has_wscale)
                    {
                        sock->snd_wscale = opts.wscale < MAX_WSCALE ? opts.wscale : MAX_WSCALE;
                        sock->wscale_enabled = true;
                    }
                    else
                    {
                        // Peer didn't offer window scaling, disable it
                        sock->wscale_enabled = false;
                        sock->rcv_wscale = 0;
                    }

                    // SACK permitted: only enable if peer also offered it
                    sock->sack_permitted = opts.sack_perm;
                }
                else
                {
                    // No options in SYN+ACK, disable optional features
                    sock->wscale_enabled = false;
                    sock->rcv_wscale = 0;
                    sock->sack_permitted = false;
                }

                // Send ACK
                send_segment(sock, flags::ACK, nullptr, 0);
                sock->state = TcpState::ESTABLISHED;
            }
            else if (tcp_flags & flags::SYN)
            {
                // Simultaneous open (rare) - parse full options
                sock->rcv_nxt = seq + 1;
                if (data_offset > TCP_HEADER_MIN)
                {
                    const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                    TcpOptions opts;
                    parse_tcp_options(opt_ptr, data_offset - TCP_HEADER_MIN, &opts);

                    if (opts.has_mss && opts.mss > 0)
                    {
                        sock->mss = opts.mss < sock->mss ? opts.mss : sock->mss;
                    }
                    if (opts.has_wscale)
                    {
                        sock->snd_wscale = opts.wscale < MAX_WSCALE ? opts.wscale : MAX_WSCALE;
                        sock->wscale_enabled = true;
                    }
                    sock->sack_permitted = opts.sack_perm;
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
#if VIPER_KERNEL_DEBUG_TCP
                if (tcp_debug_enabled())
                {
                    serial::puts("[tcp] ESTABLISHED: received RST, closing\n");
                }
#endif
                sock->state = TcpState::CLOSED;
                sock->unacked_len = 0; // Clear retransmit state
                break;
            }

            if (tcp_flags & flags::FIN)
            {
#if VIPER_KERNEL_DEBUG_TCP
                if (tcp_debug_enabled())
                {
                    serial::puts("[tcp] ESTABLISHED: received FIN, transitioning to CLOSE_WAIT\n");
                }
#endif
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
#if VIPER_KERNEL_DEBUG_TCP
                    if (tcp_debug_enabled())
                    {
                        serial::puts("[tcp] DATA: seq=");
                        serial::put_hex(seq);
                        serial::puts(" rcv_nxt=");
                        serial::put_hex(sock->rcv_nxt);
                        serial::puts(" len=");
                        serial::put_dec(payload_len);
                    }
#endif

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

#if VIPER_KERNEL_DEBUG_TCP
                        if (tcp_debug_enabled())
                        {
                            serial::puts(" -> copied ");
                            serial::put_dec(copy_len);
                            serial::puts(" bytes\n");
                        }
#endif

                        // Check OOO queue for segments that are now in order
                        ooo_deliver(sock);
                    }
                    else if (seq > sock->rcv_nxt)
                    {
                        // Out-of-order segment - buffer it for later reassembly
#if VIPER_KERNEL_DEBUG_TCP
                        if (tcp_debug_enabled())
                        {
                            serial::puts(" -> OOO, buffering\n");
                        }
#endif
                        ooo_store(sock, seq, payload, payload_len);
                    }
                    else
                    {
#if VIPER_KERNEL_DEBUG_TCP
                        if (tcp_debug_enabled())
                        {
                            serial::puts(" -> OLD, ignoring\n");
                        }
#endif
                    }
                }

                // Handle ACK - update snd_una, window, and clear retransmit state if data acked
                if (tcp_flags & flags::ACK)
                {
                    // Update send window from peer's advertised window
                    u16 raw_window = ntohs(tcp->window);
                    if (sock->wscale_enabled)
                    {
                        // Apply window scaling factor
                        sock->snd_wnd = static_cast<u16>(
                            (static_cast<u32>(raw_window) << sock->snd_wscale) > 65535
                                ? 65535
                                : raw_window << sock->snd_wscale);
                    }
                    else
                    {
                        sock->snd_wnd = raw_window;
                    }

                    // Parse SACK blocks from options if SACK is enabled
                    if (sock->sack_permitted && data_offset > TCP_HEADER_MIN)
                    {
                        const u8 *opt_ptr = static_cast<const u8 *>(data) + TCP_HEADER_MIN;
                        TcpOptions opts;
                        parse_tcp_options(opt_ptr, data_offset - TCP_HEADER_MIN, &opts);

                        // Store received SACK blocks for selective retransmission
                        sock->num_sack_blocks = opts.num_sack_blocks;
                        for (u8 i = 0; i < opts.num_sack_blocks; i++)
                        {
                            sock->sack_blocks[i].left = opts.sack_blocks[i].left;
                            sock->sack_blocks[i].right = opts.sack_blocks[i].right;
                        }
                    }

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

                // Send ACK if we received data (with SACK blocks if applicable)
                if (payload_len > 0)
                {
                    send_ack_with_sack(sock);
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
                    enter_time_wait(sock, sock_idx);
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
                enter_time_wait(sock, sock_idx);
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
            // Handle late retransmitted segments during 2MSL wait
            if (tcp_flags & flags::FIN)
            {
                // Peer retransmitted FIN - re-ACK and restart 2MSL timer
                send_segment(sock, flags::ACK, nullptr, 0);

                // Cancel old timer and schedule new one
                if (sock->time_wait_timer != 0)
                {
                    timerwheel::cancel(sock->time_wait_timer);
                }
                sock->time_wait_timer = timerwheel::schedule(
                    TIME_WAIT_DURATION_MS, time_wait_expired, reinterpret_cast<void *>(sock_idx));
            }
            // Other segments are ignored during TIME_WAIT
            break;

        default:
            break;
    }
}

/** @copydoc net::tcp::socket_create */
i32 socket_create()
{
    return socket_create(0);
}

i32 socket_create(u32 owner_viper_id)
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
            sockets[i].owner_viper_id = owner_viper_id;
            sockets[i].rcv_wnd = TcpSocket::RX_BUFFER_SIZE;
            sockets[i].snd_wnd = 0;       // Will be set from peer's advertised window
            sockets[i].mss = DEFAULT_MSS; // Will be negotiated during handshake
            // Initialize window scaling (RFC 7323)
            sockets[i].snd_wscale = 0;
            sockets[i].rcv_wscale = OUR_WSCALE;
            sockets[i].wscale_enabled = false;
            // Initialize SACK (RFC 2018)
            sockets[i].sack_permitted = false;
            sockets[i].num_sack_blocks = 0;
            for (usize j = 0; j < MAX_SACK_BLOCKS; j++)
            {
                sockets[i].sack_blocks[j].left = 0;
                sockets[i].sack_blocks[j].right = 0;
            }
            // Initialize retransmit state
            sockets[i].unacked_len = 0;
            sockets[i].unacked_seq = 0;
            sockets[i].retransmit_time = 0;
            sockets[i].rto = TcpSocket::RTO_INITIAL;
            sockets[i].retransmit_count = 0;
            sockets[i].time_wait_timer = 0;
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

bool socket_owned_by(i32 sock, u32 owner_viper_id)
{
    if (sock < 0 || sock >= static_cast<i32>(MAX_TCP_SOCKETS))
    {
        return false;
    }
    SpinlockGuard guard(tcp_lock);
    TcpSocket *s = &sockets[sock];
    return s->in_use && s->owner_viper_id == owner_viper_id;
}

bool any_socket_ready(u32 owner_viper_id)
{
    SpinlockGuard guard(tcp_lock);
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        TcpSocket *s = &sockets[i];
        if (!s->in_use || s->owner_viper_id != owner_viper_id)
        {
            continue;
        }
        if (s->rx_tail != s->rx_head)
        {
            return true;
        }
    }
    return false;
}

void close_all_owned(u32 owner_viper_id)
{
    SpinlockGuard guard(tcp_lock);
    for (usize i = 0; i < MAX_TCP_SOCKETS; i++)
    {
        TcpSocket *s = &sockets[i];
        if (!s->in_use || s->owner_viper_id != owner_viper_id)
        {
            continue;
        }

        if (s->time_wait_timer != 0)
        {
            timerwheel::cancel(s->time_wait_timer);
            s->time_wait_timer = 0;
        }

        s->state = TcpState::CLOSED;
        s->in_use = false;
        s->owner_viper_id = 0;
        s->rx_head = s->rx_tail = 0;
        s->tx_len = 0;
        s->unacked_len = 0;
    }
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
#if VIPER_KERNEL_DEBUG_TCP
                if (tcp_debug_enabled())
                {
                    serial::puts("[tcp] socket_recv: connection closed, state=");
                    serial::put_dec(static_cast<int>(s->state));
                    serial::puts(" rx empty\n");
                }
#endif
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
            // IMPORTANT: Set Blocked state BEFORE registering to avoid race with
            // wake_rx_waiters() which checks the state before waking
#if VIPER_KERNEL_DEBUG_TCP
            if (tcp_debug_enabled())
            {
                serial::puts("[tcp] socket_recv: blocking, rx_head=");
                serial::put_dec(s->rx_head);
                serial::puts(" rx_tail=");
                serial::put_dec(s->rx_tail);
                serial::puts("\n");
            }
#endif
            current->state = task::TaskState::Blocked;
            net->register_rx_waiter(current);

            // Re-check for data after registering (handles race where data arrived
            // between our first check and registering as waiter)
            tcp_lock.acquire();
            usize recheck_avail = (s->rx_tail - s->rx_head);
            tcp_lock.release();

            if (recheck_avail > 0)
            {
                // Data arrived while we were registering - unblock and continue
                current->state = task::TaskState::Ready;
                net->unregister_rx_waiter(current);
#if VIPER_KERNEL_DEBUG_TCP
                if (tcp_debug_enabled())
                {
                    serial::puts("[tcp] socket_recv: data arrived during registration\n");
                }
#endif
                continue;
            }

            task::yield();
#if VIPER_KERNEL_DEBUG_TCP
            if (tcp_debug_enabled())
            {
                serial::puts("[tcp] socket_recv: woke up\n");
            }
#endif
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

    // Cancel TIME_WAIT timer if active
    // NOTE: If we are currently in TIME_WAIT, keep the timer so it can
    // release the socket later. Canceling it would leak the socket.
    if (s->time_wait_timer != 0 && s->state != TcpState::TIME_WAIT)
    {
        timerwheel::cancel(s->time_wait_timer);
        s->time_wait_timer = 0;
    }

    // Don't immediately close if in TIME_WAIT - let the timer handle it
    // unless we're being explicitly closed (abort)
    if (s->state != TcpState::TIME_WAIT)
    {
        s->state = TcpState::CLOSED;
        s->in_use = false;
        s->owner_viper_id = 0;
    }
    // If in TIME_WAIT, the timer callback will clean up
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

#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] Congestion: ssthresh=");
        serial::put_dec(sock->ssthresh);
        serial::puts(" cwnd=");
        serial::put_dec(sock->cwnd);
        serial::puts("\n");
    }
#endif
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
#if VIPER_KERNEL_DEBUG_TCP
            if (tcp_debug_enabled())
            {
                serial::puts("[tcp] Fast retransmit triggered\n");
            }
#endif

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

#if VIPER_KERNEL_DEBUG_TCP
    if (tcp_debug_enabled())
    {
        serial::puts("[tcp] Retransmit #");
        serial::put_dec(sock->retransmit_count);
        serial::puts(" for port ");
        serial::put_dec(sock->local_port);
        serial::puts(", RTO=");
        serial::put_dec(sock->rto);
        serial::puts("ms\n");
    }
#endif
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
#if VIPER_KERNEL_DEBUG_TCP
                if (tcp_debug_enabled())
                {
                    serial::puts("[tcp] Max retransmits exceeded, closing connection\n");
                }
#endif
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
