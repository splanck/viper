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

    // Sequence numbers
    u32 snd_una; // Send unacknowledged
    u32 snd_nxt; // Send next
    u32 rcv_nxt; // Receive next
    u16 snd_wnd; // Send window
    u16 rcv_wnd; // Receive window

    // Receive buffer
    static constexpr usize RX_BUFFER_SIZE = 4096;
    u8 rx_buffer[RX_BUFFER_SIZE];
    usize rx_head; // Read position
    usize rx_tail; // Write position

    // Transmit buffer
    static constexpr usize TX_BUFFER_SIZE = 4096;
    u8 tx_buffer[TX_BUFFER_SIZE];
    usize tx_len; // Bytes waiting to send

    // Timeout tracking
    u64 last_activity;
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

} // namespace tcp
} // namespace net
