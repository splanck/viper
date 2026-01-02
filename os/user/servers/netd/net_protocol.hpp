/**
 * @file net_protocol.hpp
 * @brief Network server IPC protocol definitions.
 *
 * @details
 * Defines the message formats for network operations between clients
 * and the network server (netd).
 */
#pragma once

#include "../../syscall.hpp"

namespace netproto
{

/**
 * @brief Network request message types.
 */
enum MsgType : u32
{
    // Socket operations (client -> server)
    NET_SOCKET_CREATE = 1,
    NET_SOCKET_CONNECT = 2,
    NET_SOCKET_BIND = 3,
    NET_SOCKET_LISTEN = 4,
    NET_SOCKET_ACCEPT = 5,
    NET_SOCKET_SEND = 6,
    NET_SOCKET_RECV = 7,
    NET_SOCKET_CLOSE = 8,
    NET_SOCKET_SHUTDOWN = 9,
    NET_SOCKET_STATUS = 10,

    // DNS
    NET_DNS_RESOLVE = 20,

    // Diagnostics
    NET_PING = 40,
    NET_STATS = 41,
    NET_INFO = 42,
    NET_SUBSCRIBE_EVENTS = 43,

    // Replies (server -> client)
    NET_SOCKET_CREATE_REPLY = 0x81,
    NET_SOCKET_CONNECT_REPLY = 0x82,
    NET_SOCKET_BIND_REPLY = 0x83,
    NET_SOCKET_LISTEN_REPLY = 0x84,
    NET_SOCKET_ACCEPT_REPLY = 0x85,
    NET_SOCKET_SEND_REPLY = 0x86,
    NET_SOCKET_RECV_REPLY = 0x87,
    NET_SOCKET_CLOSE_REPLY = 0x88,
    NET_SOCKET_SHUTDOWN_REPLY = 0x89,
    NET_SOCKET_STATUS_REPLY = 0x8A,
    NET_DNS_RESOLVE_REPLY = 0xA0,
    NET_PING_REPLY = 0xC0,
    NET_STATS_REPLY = 0xC1,
    NET_INFO_REPLY = 0xC2,
    NET_SUBSCRIBE_EVENTS_REPLY = 0xC3,
};

/**
 * @brief Socket status flags (NET_SOCKET_STATUS).
 */
enum SocketStatusFlags : u32
{
    NET_SOCK_READABLE = (1u << 0),
    NET_SOCK_WRITABLE = (1u << 1),
    NET_SOCK_EOF = (1u << 2),
};

/**
 * @brief Socket address family.
 */
enum AddressFamily : u16
{
    AF_INET = 2, // IPv4
};

/**
 * @brief Socket type.
 */
enum SocketType : u16
{
    SOCK_STREAM = 1, // TCP
    SOCK_DGRAM = 2,  // UDP
};

// =============================================================================
// Socket Operations
// =============================================================================

/**
 * @brief NET_SOCKET_CREATE request.
 */
struct SocketCreateRequest
{
    u32 type;       ///< NET_SOCKET_CREATE
    u32 request_id; ///< For matching replies
    u16 family;     ///< AF_INET
    u16 sock_type;  ///< SOCK_STREAM or SOCK_DGRAM
    u32 protocol;   ///< 0 = default
};

/**
 * @brief NET_SOCKET_CREATE reply.
 */
struct SocketCreateReply
{
    u32 type;       ///< NET_SOCKET_CREATE_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 socket_id;  ///< Server-side socket identifier
};

/**
 * @brief NET_SOCKET_CONNECT request.
 */
struct SocketConnectRequest
{
    u32 type;       ///< NET_SOCKET_CONNECT
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to connect
    u32 ip;         ///< IPv4 address (network byte order)
    u16 port;       ///< Port (network byte order)
    u16 _pad;
};

/**
 * @brief NET_SOCKET_CONNECT reply.
 */
struct SocketConnectReply
{
    u32 type;       ///< NET_SOCKET_CONNECT_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief NET_SOCKET_BIND request.
 */
struct SocketBindRequest
{
    u32 type;       ///< NET_SOCKET_BIND
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to bind
    u32 ip;         ///< Local IP (0 = any)
    u16 port;       ///< Local port
    u16 _pad;
};

/**
 * @brief NET_SOCKET_BIND reply.
 */
struct SocketBindReply
{
    u32 type;       ///< NET_SOCKET_BIND_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief NET_SOCKET_LISTEN request.
 */
struct SocketListenRequest
{
    u32 type;       ///< NET_SOCKET_LISTEN
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to put in listen mode
    u32 backlog;    ///< Connection backlog
};

/**
 * @brief NET_SOCKET_LISTEN reply.
 */
struct SocketListenReply
{
    u32 type;       ///< NET_SOCKET_LISTEN_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief NET_SOCKET_ACCEPT request.
 */
struct SocketAcceptRequest
{
    u32 type;       ///< NET_SOCKET_ACCEPT
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Listening socket
    u32 _pad;
};

/**
 * @brief NET_SOCKET_ACCEPT reply.
 */
struct SocketAcceptReply
{
    u32 type;          ///< NET_SOCKET_ACCEPT_REPLY
    u32 request_id;    ///< Matches request
    i32 status;        ///< 0 = success, negative = error
    u32 new_socket_id; ///< New connected socket
    u32 remote_ip;     ///< Remote IP address
    u16 remote_port;   ///< Remote port
    u16 _pad;
};

/**
 * @brief NET_SOCKET_SEND request.
 *
 * For small data (<= 200 bytes), data is included inline.
 * For larger data, a shared memory handle is passed.
 */
struct SocketSendRequest
{
    u32 type;       ///< NET_SOCKET_SEND
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to send on
    u32 len;        ///< Bytes to send
    u32 flags;      ///< Send flags (0 for now)
    u32 _pad;
    u8 data[200]; ///< Inline data (if len <= 200)
    // For larger sends, handle[0] = shared memory
};

/**
 * @brief NET_SOCKET_SEND reply.
 */
struct SocketSendReply
{
    u32 type;       ///< NET_SOCKET_SEND_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 bytes_sent; ///< Bytes actually sent
};

/**
 * @brief NET_SOCKET_RECV request.
 */
struct SocketRecvRequest
{
    u32 type;       ///< NET_SOCKET_RECV
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to receive from
    u32 max_len;    ///< Maximum bytes to receive
    u32 flags;      ///< Receive flags (0 for now)
    u32 _pad;
};

/**
 * @brief NET_SOCKET_RECV reply.
 *
 * For small data (<= 200 bytes), data is included inline.
 * For larger data, a shared memory handle is passed.
 */
struct SocketRecvReply
{
    u32 type;       ///< NET_SOCKET_RECV_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 bytes_recv; ///< Bytes actually received
    u8 data[200];   ///< Inline data (if bytes_recv <= 200)
    // For larger receives, handle[0] = shared memory
};

/**
 * @brief NET_SOCKET_CLOSE request.
 */
struct SocketCloseRequest
{
    u32 type;       ///< NET_SOCKET_CLOSE
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to close
    u32 _pad;
};

/**
 * @brief NET_SOCKET_CLOSE reply.
 */
struct SocketCloseReply
{
    u32 type;       ///< NET_SOCKET_CLOSE_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief NET_SOCKET_STATUS request.
 */
struct SocketStatusRequest
{
    u32 type;       ///< NET_SOCKET_STATUS
    u32 request_id; ///< For matching replies
    u32 socket_id;  ///< Socket to query
    u32 _pad;
};

/**
 * @brief NET_SOCKET_STATUS reply.
 */
struct SocketStatusReply
{
    u32 type;           ///< NET_SOCKET_STATUS_REPLY
    u32 request_id;     ///< Matches request
    i32 status;         ///< 0 = success, negative = error
    u32 flags;          ///< SocketStatusFlags
    u32 rx_available;   ///< Bytes currently readable without blocking
    u32 _pad;
};

// =============================================================================
// DNS Operations
// =============================================================================

/**
 * @brief NET_DNS_RESOLVE request.
 *
 * Note: hostname limited to 244 chars to fit within 256-byte channel message limit.
 */
struct DnsResolveRequest
{
    u32 type;           ///< NET_DNS_RESOLVE
    u32 request_id;     ///< For matching replies
    u16 hostname_len;   ///< Length of hostname
    char hostname[244]; ///< Hostname to resolve (max 243 chars + NUL)
};

/**
 * @brief NET_DNS_RESOLVE reply.
 */
struct DnsResolveReply
{
    u32 type;       ///< NET_DNS_RESOLVE_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 ip;         ///< Resolved IPv4 address (network byte order)
};

// =============================================================================
// Diagnostics
// =============================================================================

/**
 * @brief NET_PING request.
 */
struct PingRequest
{
    u32 type;       ///< NET_PING
    u32 request_id; ///< For matching replies
    u32 ip;         ///< Target IP (network byte order)
    u32 timeout_ms; ///< Timeout in milliseconds
};

/**
 * @brief NET_PING reply.
 */
struct PingReply
{
    u32 type;       ///< NET_PING_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 rtt_us;     ///< Round-trip time in microseconds
};

/**
 * @brief NET_INFO request.
 */
struct InfoRequest
{
    u32 type;       ///< NET_INFO
    u32 request_id; ///< For matching replies
};

/**
 * @brief NET_INFO reply.
 */
struct InfoReply
{
    u32 type;       ///< NET_INFO_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success
    u8 mac[6];      ///< MAC address
    u16 _pad;
    u32 ip;      ///< Local IP (network byte order)
    u32 netmask; ///< Netmask
    u32 gateway; ///< Gateway
    u32 dns;     ///< DNS server
};

/**
 * @brief NET_STATS request.
 */
struct StatsRequest
{
    u32 type;       ///< NET_STATS
    u32 request_id; ///< For matching replies
};

/**
 * @brief NET_STATS reply.
 */
struct StatsReply
{
    u32 type;       ///< NET_STATS_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success
    u32 _pad;
    u64 tx_packets;  ///< Packets transmitted
    u64 rx_packets;  ///< Packets received
    u64 tx_bytes;    ///< Bytes transmitted
    u64 rx_bytes;    ///< Bytes received
    u64 tx_dropped;  ///< TX drops
    u64 rx_dropped;  ///< RX drops
    u32 tcp_conns;   ///< Active TCP connections
    u32 udp_sockets; ///< Active UDP sockets
};

// =============================================================================
// Event subscription
// =============================================================================

/**
 * @brief NET_SUBSCRIBE_EVENTS request.
 *
 * Transfers a single Channel send endpoint handle (handles[0]) that netd will
 * use to send readiness notifications to the client.
 */
struct SubscribeEventsRequest
{
    u32 type;       ///< NET_SUBSCRIBE_EVENTS
    u32 request_id; ///< For matching replies
};

/**
 * @brief NET_SUBSCRIBE_EVENTS reply.
 */
struct SubscribeEventsReply
{
    u32 type;       ///< NET_SUBSCRIBE_EVENTS_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

} // namespace netproto
