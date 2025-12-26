#pragma once

/**
 * @file udp.hpp
 * @brief UDP (User Datagram Protocol) implementation and simple socket API.
 *
 * @details
 * Provides a minimal UDP layer sufficient for DNS and simple datagram-based
 * communication:
 * - Parses inbound UDP datagrams and delivers them to a small fixed socket
 *   table keyed by destination port.
 * - Constructs outbound UDP datagrams (including checksum) and transmits them
 *   via IPv4.
 *
 * The socket API is intentionally simple:
 * - Sockets are indexed by a small integer in a fixed array.
 * - Only one receive datagram is buffered per socket.
 * - Receive is non-blocking; callers can poll using @ref socket_recv.
 */

#include "../net.hpp"

namespace net
{
namespace udp
{

/**
 * @brief UDP header (8 bytes).
 *
 * @details
 * This structure matches the on-the-wire UDP header. Fields are transmitted in
 * network byte order.
 */
struct UdpHeader
{
    u16 src_port;
    u16 dst_port;
    u16 length; // Header + data length
    u16 checksum;
} __attribute__((packed));

/** @brief Size of the UDP header in bytes. */
constexpr usize UDP_HEADER_SIZE = 8;

/**
 * @brief Maximum UDP payload for Ethernet MTU 1500 (no fragmentation).
 *
 * @details
 * For an Ethernet MTU of 1500, IPv4 header minimum is 20 bytes and UDP header is
 * 8 bytes, leaving 1472 bytes for UDP payload.
 */
constexpr usize UDP_MAX_PAYLOAD = 1472; // 1500 - 20 (IP) - 8 (UDP)

/**
 * @brief Internal UDP socket representation.
 *
 * @details
 * Each socket tracks a local port and buffers at most one received datagram.
 * When a datagram is delivered, it is copied into @ref rx_buffer and marked as
 * ready until consumed by @ref socket_recv.
 */
struct UdpSocket
{
    u16 local_port;
    bool bound;

    // Receive buffer
    static constexpr usize RX_BUFFER_SIZE = 2048;
    u8 rx_buffer[RX_BUFFER_SIZE];
    usize rx_len;
    Ipv4Addr rx_src_ip;
    u16 rx_src_port;
    bool rx_ready;
};

/** @brief Maximum number of concurrently allocated UDP sockets. */
constexpr usize MAX_UDP_SOCKETS = 16;

/**
 * @brief Initialize the UDP layer and clear the socket table.
 *
 * @details
 * Resets internal socket state and marks the layer initialized. Should be
 * called during network stack initialization.
 */
void udp_init();

/**
 * @brief Process a received UDP packet (payload of an IPv4 UDP datagram).
 *
 * @details
 * Parses the UDP header, identifies a socket bound to the destination port, and
 * copies the payload into the socket's receive buffer if it is currently empty.
 * If the socket already has pending data, the new datagram is dropped.
 *
 * @param src Source IPv4 address.
 * @param data Pointer to UDP header + payload.
 * @param len Length of UDP header + payload.
 */
void rx_packet(const Ipv4Addr &src, const void *data, usize len);

/**
 * @brief Allocate a UDP socket from the fixed socket table.
 *
 * @details
 * Returns an integer socket handle that can later be bound and used for send
 * and receive. The socket is not bound to a port until @ref socket_bind is
 * called.
 *
 * @return Socket index on success, or -1 if no sockets are available.
 */
i32 socket_create();

/**
 * @brief Bind a socket to a local UDP port.
 *
 * @details
 * Associates the socket with a local port and marks it ready to receive
 * datagrams addressed to that port. Binding fails if the port is already in
 * use by another bound socket.
 *
 * @param sock Socket index from @ref socket_create.
 * @param port Local UDP port number in host order.
 * @return `true` on success, otherwise `false`.
 */
bool socket_bind(i32 sock, u16 port);

/**
 * @brief Close a UDP socket.
 *
 * @details
 * Unbinds the socket and clears any pending receive data, making the slot
 * available for reuse.
 *
 * @param sock Socket index.
 */
void socket_close(i32 sock);

/**
 * @brief Send a UDP datagram using a bound socket.
 *
 * @details
 * Uses the socket's bound local port as the source port and transmits a UDP
 * datagram to `dst:dst_port`.
 *
 * @param sock Socket index (must be bound).
 * @param dst Destination IPv4 address.
 * @param dst_port Destination UDP port in host order.
 * @param data Pointer to payload bytes.
 * @param len Payload length in bytes.
 * @return `true` if the datagram was transmitted, otherwise `false`.
 */
bool socket_send(i32 sock, const Ipv4Addr &dst, u16 dst_port, const void *data, usize len);

/**
 * @brief Receive a UDP datagram from a socket (non-blocking).
 *
 * @details
 * Polls the network stack to process inbound packets, then checks whether the
 * socket has a datagram buffered. If so, copies up to `max_len` bytes into
 * `buffer` and returns the number of bytes copied.
 *
 * Only one datagram is buffered at a time; receiving consumes the buffered
 * datagram.
 *
 * @param sock Socket index (must be bound).
 * @param buffer Output buffer for payload bytes.
 * @param max_len Maximum number of bytes to copy.
 * @param src_ip Optional output for the source IPv4 address.
 * @param src_port Optional output for the source UDP port.
 * @return Bytes received (>0), 0 if none available, or -1 on error.
 */
i32 socket_recv(i32 sock, void *buffer, usize max_len, Ipv4Addr *src_ip, u16 *src_port);

/**
 * @brief Transmit a UDP datagram without allocating a socket.
 *
 * @details
 * Convenience routine used by higher-level clients such as DNS. It constructs a
 * UDP datagram with explicit source/destination ports and sends it via IPv4.
 *
 * @param dst Destination IPv4 address.
 * @param src_port Source UDP port.
 * @param dst_port Destination UDP port.
 * @param data Pointer to payload bytes.
 * @param len Payload length in bytes.
 * @return `true` if the datagram was transmitted, otherwise `false`.
 */
bool send(const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len);

} // namespace udp
} // namespace net
