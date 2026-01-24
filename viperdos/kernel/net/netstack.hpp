/**
 * @file netstack.hpp
 * @brief Kernel TCP/IP network stack.
 *
 * @details
 * This is the kernel-space network stack providing:
 * - Ethernet frame handling
 * - ARP resolution
 * - IPv4 packet processing
 * - ICMP (ping)
 * - UDP sockets
 * - TCP connections
 * - DNS resolution
 */
#pragma once

#include "../drivers/virtio/net.hpp"
#include "../include/constants.hpp"
#include "../include/types.hpp"

namespace kc = kernel::constants;

namespace net {

// =============================================================================
// Network Types
// =============================================================================

struct MacAddr {
    u8 bytes[6];

    bool operator==(const MacAddr &other) const {
        for (int i = 0; i < 6; i++) {
            if (bytes[i] != other.bytes[i])
                return false;
        }
        return true;
    }

    bool is_broadcast() const {
        return bytes[0] == 0xff && bytes[1] == 0xff && bytes[2] == 0xff && bytes[3] == 0xff &&
               bytes[4] == 0xff && bytes[5] == 0xff;
    }

    static MacAddr broadcast() {
        return {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    }

    static MacAddr zero() {
        return {{0, 0, 0, 0, 0, 0}};
    }
} __attribute__((packed));

struct Ipv4Addr {
    u8 bytes[4];

    bool operator==(const Ipv4Addr &other) const {
        return bytes[0] == other.bytes[0] && bytes[1] == other.bytes[1] &&
               bytes[2] == other.bytes[2] && bytes[3] == other.bytes[3];
    }

    bool operator!=(const Ipv4Addr &other) const {
        return !(*this == other);
    }

    u32 to_u32() const {
        return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
               (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
    }

    static Ipv4Addr from_u32(u32 addr) {
        return {{static_cast<u8>((addr >> 24) & 0xff),
                 static_cast<u8>((addr >> 16) & 0xff),
                 static_cast<u8>((addr >> 8) & 0xff),
                 static_cast<u8>(addr & 0xff)}};
    }

    bool is_broadcast() const {
        return bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255;
    }

    bool is_zero() const {
        return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
    }

    bool same_subnet(const Ipv4Addr &other, const Ipv4Addr &netmask) const {
        return (to_u32() & netmask.to_u32()) == (other.to_u32() & netmask.to_u32());
    }

    static Ipv4Addr zero() {
        return {{0, 0, 0, 0}};
    }

    static Ipv4Addr broadcast() {
        return {{255, 255, 255, 255}};
    }
} __attribute__((packed));

// Byte order conversion
inline u16 htons(u16 x) {
    return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}

inline u16 ntohs(u16 x) {
    return htons(x);
}

inline u32 htonl(u32 x) {
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}

inline u32 ntohl(u32 x) {
    return htonl(x);
}

// Internet checksum (RFC 1071)
inline u16 checksum(const void *data, usize len) {
    const u16 *ptr = static_cast<const u16 *>(data);
    u32 sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len > 0) {
        sum += *reinterpret_cast<const u8 *>(ptr);
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~static_cast<u16>(sum);
}

// =============================================================================
// Protocol Headers
// =============================================================================

struct EthHeader {
    MacAddr dst;
    MacAddr src;
    u16 ethertype;
} __attribute__((packed));

constexpr u16 ETH_TYPE_IPV4 = 0x0800;
constexpr u16 ETH_TYPE_ARP = 0x0806;

struct ArpHeader {
    u16 hw_type;
    u16 proto_type;
    u8 hw_len;
    u8 proto_len;
    u16 operation;
    MacAddr sender_mac;
    Ipv4Addr sender_ip;
    MacAddr target_mac;
    Ipv4Addr target_ip;
} __attribute__((packed));

constexpr u16 ARP_HW_ETHERNET = 1;
constexpr u16 ARP_OP_REQUEST = 1;
constexpr u16 ARP_OP_REPLY = 2;

struct Ipv4Header {
    u8 version_ihl;
    u8 tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8 ttl;
    u8 protocol;
    u16 checksum;
    Ipv4Addr src;
    Ipv4Addr dst;
} __attribute__((packed));

constexpr u8 IP_PROTO_ICMP = 1;
constexpr u8 IP_PROTO_TCP = 6;
constexpr u8 IP_PROTO_UDP = 17;

struct IcmpHeader {
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 seq;
} __attribute__((packed));

constexpr u8 ICMP_ECHO_REQUEST = 8;
constexpr u8 ICMP_ECHO_REPLY = 0;

struct UdpHeader {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed));

struct TcpHeader {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8 data_offset;
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} __attribute__((packed));

// TCP flags
constexpr u8 TCP_FIN = 0x01;
constexpr u8 TCP_SYN = 0x02;
constexpr u8 TCP_RST = 0x04;
constexpr u8 TCP_PSH = 0x08;
constexpr u8 TCP_ACK = 0x10;

// =============================================================================
// TCP State
// =============================================================================

enum class TcpState {
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

// =============================================================================
// Socket Status Flags
// =============================================================================

enum SocketStatusFlags : u32 {
    SOCK_READABLE = (1u << 0),
    SOCK_WRITABLE = (1u << 1),
    SOCK_EOF = (1u << 2),
};

// =============================================================================
// Error Codes
// =============================================================================

constexpr i32 VERR_NO_RESOURCE = -12;
constexpr i32 VERR_NOT_SUPPORTED = -38;
constexpr i32 VERR_INVALID_HANDLE = -100;
constexpr i32 VERR_INVALID_ARG = -22;
constexpr i32 VERR_TIMEOUT = -110;
constexpr i32 VERR_CONNECTION = -111;
constexpr i32 VERR_WOULD_BLOCK = -300;

// =============================================================================
// TCP Connection
// =============================================================================

class TcpConnection {
  public:
    bool in_use{false};
    u32 owner_pid{0}; // Process ID that owns this socket
    TcpState state{TcpState::CLOSED};

    Ipv4Addr local_ip;
    u16 local_port{0};
    Ipv4Addr remote_ip;
    u16 remote_port{0};

    u32 snd_una{0};
    u32 snd_nxt{0};
    u32 rcv_nxt{0};

    // Receive buffer (32KB for SSH/SFTP traffic)
    static constexpr usize RX_BUF_SIZE = kc::net::TCP_RX_BUFFER_SIZE;
    u8 rx_buf[RX_BUF_SIZE];
    usize rx_head{0};
    usize rx_tail{0};

    // Send buffer
    static constexpr usize TX_BUF_SIZE = kc::net::TCP_TX_BUFFER_SIZE;
    u8 tx_buf[TX_BUF_SIZE];
    usize tx_head{0};
    usize tx_tail{0};

    // Pending accept (for listening sockets)
    static constexpr usize MAX_BACKLOG = kc::net::TCP_BACKLOG_SIZE;

    struct PendingConn {
        bool valid;
        Ipv4Addr ip;
        u16 port;
        u32 seq;
    };

    PendingConn backlog[MAX_BACKLOG];
    usize backlog_count{0};

    usize rx_available() const {
        if (rx_tail >= rx_head)
            return rx_tail - rx_head;
        return RX_BUF_SIZE - rx_head + rx_tail;
    }

    u16 rx_window() const {
        usize used = rx_available();
        usize free = (RX_BUF_SIZE > used) ? (RX_BUF_SIZE - used - 1) : 0;
        return (free > 65535) ? 65535 : static_cast<u16>(free);
    }

    usize tx_available() const {
        if (tx_tail >= tx_head)
            return TX_BUF_SIZE - (tx_tail - tx_head) - 1;
        return tx_head - tx_tail - 1;
    }
};

// =============================================================================
// UDP Socket
// =============================================================================

class UdpSocket {
  public:
    bool in_use{false};
    u32 owner_pid{0};
    Ipv4Addr local_ip;
    u16 local_port{0};

    static constexpr usize RX_BUF_SIZE = kc::net::UDP_RX_BUFFER_SIZE;
    u8 rx_buf[RX_BUF_SIZE];
    usize rx_len{0};
    Ipv4Addr rx_src_ip;
    u16 rx_src_port{0};
    bool has_data{false};
};

// =============================================================================
// Network Stack (Singleton)
// =============================================================================

constexpr usize MAX_TCP_CONNS = kc::net::MAX_TCP_CONNS;
constexpr usize MAX_UDP_SOCKETS = kc::net::MAX_UDP_SOCKETS;

// Initialize the network stack
void network_init();

// Poll for incoming packets (call periodically)
void network_poll();

// Check if network is available
bool is_available();

// =============================================================================
// TCP Socket API (for syscall handlers)
// =============================================================================

namespace tcp {

// Create a new TCP socket
// Returns socket ID (0..MAX_TCP_CONNS-1) or negative error
i64 socket_create(u32 process_id);

// Connect to a remote host
// Returns true on success
bool socket_connect(i32 sock, const Ipv4Addr &ip, u16 port);

// Send data on a connected socket
// Returns bytes sent or negative error
i64 socket_send(i32 sock, const void *buf, usize len);

// Receive data from a connected socket
// Returns bytes received, 0 on EOF, or negative error (VERR_WOULD_BLOCK if no data)
i64 socket_recv(i32 sock, void *buf, usize len);

// Close a socket
void socket_close(i32 sock);

// Check if a socket is owned by a process
bool socket_owned_by(i32 sock, u32 process_id);

// Get socket status (for poll support)
i32 socket_status(i32 sock, u32 *out_flags, u32 *out_rx_available);

} // namespace tcp

// =============================================================================
// DNS API
// =============================================================================

namespace dns {

// Resolve a hostname to an IP address
// Returns true on success, false on failure/timeout
bool resolve(const char *hostname, Ipv4Addr *out, u32 timeout_ms);

} // namespace dns

// =============================================================================
// ICMP API
// =============================================================================

namespace icmp {

// Send a ping and wait for reply
// Returns RTT in ms on success, negative on timeout
i32 ping(const Ipv4Addr &ip, u32 timeout_ms);

} // namespace icmp

} // namespace net

// =============================================================================
// Network Statistics
// =============================================================================

struct NetStats; // Forward declaration from viperdos/net_stats.hpp

namespace net {

// Get network statistics
void get_stats(::NetStats *out);

} // namespace net
