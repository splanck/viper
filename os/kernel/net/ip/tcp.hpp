#pragma once

/**
 * @file tcp.hpp
 * @brief Minimal TCP implementation and socket-style API.
 *
 * @details
 * Provides a simple TCP layer adequate for basic client/server communication
 * during bring-up (e.g., HTTP over port 80):
 * - Parses inbound TCP segments and drives a simplified TCP state machine.
 * - Maintains a fixed socket table and per-socket send/receive sequence state.
 * - Transmits segments via IPv4 and performs basic ACK handling.
 *
 * This is not a full-featured TCP implementation. Notable limitations:
 * - No TCP options (MSS is fixed, no window scaling, no timestamps).
 * - No robust retransmission logic or out-of-order reassembly.
 * - Minimal connection accept semantics (listening sockets are not cloned).
 * - Receive buffer is a simple ring; send waits for ACK in a simplified way.
 *
 * The API is designed to be easy to use from higher-level clients while the
 * kernel evolves.
 */

#include "../net.hpp"

namespace net
{
namespace tcp
{

/**
 * @brief TCP header (minimum 20 bytes, without options).
 *
 * @details
 * This structure matches the on-the-wire TCP header fields used by the current
 * implementation. Options are not supported; `data_offset` is set for a 20-byte
 * header.
 */
struct TcpHeader
{
    u16 src_port;
    u16 dst_port;
    u32 seq_num;
    u32 ack_num;
    u8 data_offset; // Upper 4 bits = data offset in 32-bit words
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urgent_ptr;
    // Options may follow
} __attribute__((packed));

/** @brief Minimum TCP header size in bytes (no options). */
constexpr usize TCP_HEADER_MIN = 20;

/** @brief TCP header size with MSS option (4 bytes aligned). */
constexpr usize TCP_HEADER_MSS = 24;

/** @brief TCP header size with full SYN options (MSS + WSCALE + SACK_PERM + padding). */
constexpr usize TCP_HEADER_SYN_OPTS = 32; // 20 + 4(MSS) + 4(NOP+WSCALE) + 4(SACK_PERM+NOP+NOP)

/**
 * @brief TCP option kinds used in option parsing/generation.
 */
namespace option
{
constexpr u8 END = 0;            ///< End of option list
constexpr u8 NOP = 1;            ///< No operation (padding)
constexpr u8 MSS = 2;            ///< Maximum Segment Size
constexpr u8 MSS_LEN = 4;        ///< MSS option length
constexpr u8 WSCALE = 3;         ///< Window Scale (RFC 7323)
constexpr u8 WSCALE_LEN = 3;     ///< Window scale option length
constexpr u8 SACK_PERM = 4;      ///< SACK Permitted (RFC 2018)
constexpr u8 SACK_PERM_LEN = 2;  ///< SACK permitted option length
constexpr u8 SACK = 5;           ///< SACK blocks (RFC 2018)
constexpr u8 TIMESTAMP = 8;      ///< Timestamps (RFC 7323)
constexpr u8 TIMESTAMP_LEN = 10; ///< Timestamp option length
} // namespace option

/** @brief Maximum window scale shift (RFC 7323: max 14). */
constexpr u8 MAX_WSCALE = 14;

/** @brief Our advertised window scale (shift count). */
constexpr u8 OUR_WSCALE = 7; // 128x scaling = 512KB max window

/** @brief Maximum SACK blocks we track. */
constexpr usize MAX_SACK_BLOCKS = 4;

/** @brief Default MSS for Ethernet (MTU 1500 - IP header - TCP header). */
constexpr u16 DEFAULT_MSS = 1460;

/**
 * @brief TCP flag bit values used in the header.
 *
 * @details
 * These values correspond to the standard TCP control bits.
 */
namespace flags
{
constexpr u8 FIN = 0x01;
constexpr u8 SYN = 0x02;
constexpr u8 RST = 0x04;
constexpr u8 PSH = 0x08;
constexpr u8 ACK = 0x10;
constexpr u8 URG = 0x20;
} // namespace flags

/**
 * @brief TCP connection state used by the simplified state machine.
 *
 * @details
 * The state machine implements the core handshake and teardown states required
 * for basic connections.
 */
enum class TcpState
{
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

/**
 * @brief TCP socket/control block stored in the fixed socket table.
 *
 * @details
 * Tracks the local/remote addressing tuple and the sequence/ack state used for
 * sending and receiving data. The receive buffer is a ring buffer indexed by
 * @ref rx_head/@ref rx_tail. Transmit buffering is minimal and primarily used
 * for bookkeeping during send.
 */
struct TcpSocket
{
    TcpState state;
    u16 local_port;
    u16 remote_port;
    Ipv4Addr remote_ip;
    bool in_use;
    u32 owner_viper_id; // 0 = kernel/unowned

    // Sequence numbers
    u32 snd_una; // Send unacknowledged
    u32 snd_nxt; // Send next
    u32 rcv_nxt; // Receive next
    u16 snd_wnd; // Send window (before scaling)
    u16 rcv_wnd; // Receive window (before scaling)
    u16 mss;     // Negotiated Maximum Segment Size

    // Window scaling (RFC 7323)
    u8 snd_wscale;       // Peer's window scale factor (shift count)
    u8 rcv_wscale;       // Our window scale factor (shift count)
    bool wscale_enabled; // Window scaling negotiated successfully

    // SACK support (RFC 2018)
    bool sack_permitted; // SACK was negotiated

    struct SackBlock
    {
        u32 left;  // Left edge of SACK block
        u32 right; // Right edge of SACK block
    };

    SackBlock sack_blocks[MAX_SACK_BLOCKS]; // Received SACK blocks
    u8 num_sack_blocks;                     // Number of valid SACK blocks

    // Receive buffer
    static constexpr usize RX_BUFFER_SIZE = 4096;
    u8 rx_buffer[RX_BUFFER_SIZE];
    usize rx_head; // Read position
    usize rx_tail; // Write position

    // Transmit buffer
    static constexpr usize TX_BUFFER_SIZE = 4096;
    u8 tx_buffer[TX_BUFFER_SIZE];
    usize tx_len; // Bytes waiting to send

    // Retransmission state
    static constexpr usize UNACKED_BUFFER_SIZE = 1460; // MSS
    u8 unacked_data[UNACKED_BUFFER_SIZE];              // Copy of sent but unacked data
    usize unacked_len;                                 // Length of unacked data
    u32 unacked_seq;                                   // Sequence number of unacked data
    u64 retransmit_time;                               // Timestamp when to retransmit (ms)
    u32 rto;                                           // Retransmission timeout (ms)
    u8 retransmit_count;                               // Number of retries attempted

    // Retransmit constants
    static constexpr u32 RTO_INITIAL = 1000; // Initial RTO: 1 second
    static constexpr u32 RTO_MAX = 60000;    // Max RTO: 60 seconds
    static constexpr u8 RETRANSMIT_MAX = 5;  // Max retries before giving up

    // Timeout tracking
    u64 last_activity;

    // TIME_WAIT timer ID (0 if not active)
    u32 time_wait_timer;

    // Congestion control (RFC 5681)
    u32 cwnd;     // Congestion window (bytes)
    u32 ssthresh; // Slow start threshold (bytes)
    u32 dup_acks; // Duplicate ACK count (for fast retransmit)

    // RTT estimation (RFC 6298)
    u32 srtt;          // Smoothed RTT (microseconds * 8)
    u32 rttvar;        // RTT variance (microseconds * 4)
    bool rtt_measured; // Have we measured an RTT sample?

    // Flight size tracking
    u32 bytes_in_flight; // Unacknowledged bytes currently in network

    // Congestion control constants
    static constexpr u32 INITIAL_CWND_SEGMENTS = 10; // IW = 10 segments (RFC 6928)
    static constexpr u32 MIN_SSTHRESH = 2 * 1460;    // Min ssthresh = 2 * MSS
    static constexpr u32 DUP_ACK_THRESHOLD = 3;      // Fast retransmit threshold

    // Out-of-order segment queue for reassembly
    static constexpr usize OOO_MAX_SEGMENTS = 8;    // Max out-of-order segments to buffer
    static constexpr usize OOO_SEGMENT_SIZE = 1460; // Max size per OOO segment

    struct OooSegment
    {
        u32 seq;                   // Sequence number of segment
        u16 len;                   // Length of segment data
        bool valid;                // Segment is valid
        u8 data[OOO_SEGMENT_SIZE]; // Segment data
    };

    OooSegment ooo_queue[OOO_MAX_SEGMENTS]; // Out-of-order segment queue
};

/** @brief Maximum number of concurrently allocated TCP sockets. */
constexpr usize MAX_TCP_SOCKETS = 16;

/**
 * @brief Initialize the TCP layer and clear the socket table.
 *
 * @details
 * Resets all socket entries to CLOSED and marks the layer initialized.
 */
void tcp_init();

/**
 * @brief Process a received TCP segment.
 *
 * @details
 * Parses the TCP header, finds the matching socket (or a listening socket on
 * the destination port), and advances the socket state machine:
 * - Handles SYN/SYN+ACK/ACK for connection establishment.
 * - Buffers in-order payload data and advances `rcv_nxt`.
 * - Responds with ACKs and handles FIN/RST for teardown.
 *
 * If no matching socket exists, the implementation sends a TCP RST for segments
 * that warrant it.
 *
 * @param src Source IPv4 address.
 * @param data Pointer to TCP header + payload bytes.
 * @param len Length of segment in bytes.
 */
void rx_segment(const Ipv4Addr &src, const void *data, usize len);

/**
 * @brief Allocate a TCP socket from the fixed socket table.
 *
 * @details
 * Returns a socket index that can be bound, used for connect/listen, and used
 * for send/receive. Newly created sockets start in @ref TcpState::CLOSED.
 *
 * @return Socket index on success, or -1 if none are available.
 */
i32 socket_create();

/**
 * @brief Allocate a TCP socket owned by a specific Viper.
 *
 * @details
 * User-space sockets are global in the TCP stack; this owner tag is used to:
 * - Scope poll readiness (HANDLE_NETWORK_RX) to the current process.
 * - Enforce basic cross-process isolation in socket syscalls.
 *
 * Kernel callers should continue to use @ref socket_create (owner ID = 0).
 *
 * @param owner_viper_id Owning Viper ID.
 * @return Socket index on success, or -1 if none are available.
 */
i32 socket_create(u32 owner_viper_id);

/**
 * @brief Check whether a socket is owned by the given Viper.
 *
 * @param sock Socket index.
 * @param owner_viper_id Expected owner Viper ID.
 * @return `true` if the socket is valid, in-use, and owned by the Viper.
 */
bool socket_owned_by(i32 sock, u32 owner_viper_id);

/**
 * @brief Check if any socket owned by a Viper has receive data available.
 *
 * @param owner_viper_id Owning Viper ID.
 * @return `true` if any owned socket has buffered RX data.
 */
bool any_socket_ready(u32 owner_viper_id);

/**
 * @brief Force-close all sockets owned by a Viper.
 *
 * @details
 * Used for cleanup on process exit to avoid leaked sockets keeping the global
 * HANDLE_NETWORK_RX pseudo-handle permanently "ready" for other processes.
 *
 * This is a best-effort cleanup and does not attempt a graceful FIN teardown.
 *
 * @param owner_viper_id Owning Viper ID.
 */
void close_all_owned(u32 owner_viper_id);

/**
 * @brief Bind a TCP socket to a local port.
 *
 * @details
 * Associates the socket with a local port number. Binding fails if another
 * in-use socket already uses the same local port.
 *
 * @param sock Socket index.
 * @param port Local TCP port in host order.
 * @return `true` on success, otherwise `false`.
 */
bool socket_bind(i32 sock, u16 port);

/**
 * @brief Put a bound socket into listening state.
 *
 * @details
 * Marks the socket as a listener for inbound connections on its local port.
 *
 * @param sock Socket index (must be bound to a non-zero port).
 * @return `true` on success, otherwise `false`.
 */
bool socket_listen(i32 sock);

/**
 * @brief Accept an incoming connection on a listening socket.
 *
 * @details
 * This bring-up implementation does not clone listening sockets. When a
 * connection completes, the listening socket itself transitions into the
 * established connection and this function returns the same socket index.
 *
 * Callers should treat this as a temporary API; a future implementation should
 * keep the listening socket in LISTEN and return a new socket for each accepted
 * connection.
 *
 * @param sock Listening socket index.
 * @return Socket index for the established connection, or -1 if none ready.
 */
i32 socket_accept(i32 sock);

/**
 * @brief Connect a socket to a remote host/port (client).
 *
 * @details
 * Performs an active open:
 * - Assigns an ephemeral local port if not already bound.
 * - Sends a SYN.
 * - Polls the network until the handshake completes or times out.
 *
 * @param sock Socket index in CLOSED state.
 * @param dst Destination IPv4 address.
 * @param port Destination TCP port.
 * @return `true` if the connection reached ESTABLISHED, otherwise `false`.
 */
bool socket_connect(i32 sock, const Ipv4Addr &dst, u16 port);

/**
 * @brief Send application data on an established connection.
 *
 * @details
 * Splits `data` into segments (MSS-sized) and transmits each as PSH|ACK. The
 * implementation performs a simplified "wait for ACK" loop after each segment
 * and retries transmission for a short period to allow ARP resolution.
 *
 * @param sock Socket index.
 * @param data Pointer to payload bytes.
 * @param len Payload length in bytes.
 * @return Number of bytes successfully sent, or -1 on error.
 */
i32 socket_send(i32 sock, const void *data, usize len);

/**
 * @brief Receive data from a socket (non-blocking).
 *
 * @details
 * Polls the network stack and then reads available bytes from the socket's
 * receive ring buffer. If no data is available, returns 0. If the connection is
 * closed and no data remains, returns -1.
 *
 * @param sock Socket index.
 * @param buffer Output buffer.
 * @param max_len Maximum number of bytes to copy.
 * @return Bytes copied (>0), 0 if no data available, or -1 on error/closed.
 */
i32 socket_recv(i32 sock, void *buffer, usize max_len);

/**
 * @brief Close a TCP socket.
 *
 * @details
 * Initiates a graceful close for established connections by sending FIN|ACK and
 * waiting briefly for teardown to complete. For sockets in CLOSE_WAIT, sends the
 * final FIN and transitions toward CLOSED.
 *
 * The implementation then marks the socket entry free for reuse.
 *
 * @param sock Socket index.
 */
void socket_close(i32 sock);

/**
 * @brief Check whether a socket is currently connected.
 *
 * @param sock Socket index.
 * @return `true` if the socket is in use and ESTABLISHED, otherwise `false`.
 */
bool socket_connected(i32 sock);

/**
 * @brief Get the number of bytes currently buffered for receive.
 *
 * @details
 * Returns the difference between @ref TcpSocket::rx_tail and @ref TcpSocket::rx_head,
 * representing bytes queued in the receive ring buffer.
 *
 * @param sock Socket index.
 * @return Number of buffered bytes, or 0 if socket is invalid/not in use.
 */
usize socket_available(i32 sock);

/**
 * @brief Check for and handle TCP retransmissions.
 *
 * @details
 * Iterates through all active TCP sockets and retransmits unacknowledged
 * data when the retransmission timer expires. Uses exponential backoff
 * (doubling RTO on each retry, up to RTO_MAX). Gives up after RETRANSMIT_MAX
 * attempts and closes the connection.
 *
 * This function should be called periodically from network_poll().
 */
void check_retransmit();

/**
 * @brief Get count of active TCP connections.
 *
 * @return Number of sockets in ESTABLISHED or similar active states.
 */
u32 get_active_count();

/**
 * @brief Get count of listening TCP sockets.
 *
 * @return Number of sockets in LISTEN state.
 */
u32 get_listen_count();

} // namespace tcp
} // namespace net
