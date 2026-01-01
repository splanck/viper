/**
 * @file netstack.hpp
 * @brief Simplified user-space network stack.
 *
 * @details
 * This is a simplified network stack for the netd server, providing:
 * - Ethernet frame handling
 * - ARP resolution
 * - IPv4 packet processing
 * - ICMP (ping)
 * - UDP sockets
 * - TCP connections (basic)
 * - DNS resolution
 */
#pragma once

#include "../../libvirtio/include/net.hpp"
#include "../../syscall.hpp"

namespace netstack
{

// =============================================================================
// Network Types
// =============================================================================

struct MacAddr
{
    u8 bytes[6];

    bool operator==(const MacAddr &other) const
    {
        for (int i = 0; i < 6; i++)
        {
            if (bytes[i] != other.bytes[i])
                return false;
        }
        return true;
    }

    bool is_broadcast() const
    {
        return bytes[0] == 0xff && bytes[1] == 0xff && bytes[2] == 0xff && bytes[3] == 0xff &&
               bytes[4] == 0xff && bytes[5] == 0xff;
    }

    static MacAddr broadcast()
    {
        return {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    }

    static MacAddr zero()
    {
        return {{0, 0, 0, 0, 0, 0}};
    }
} __attribute__((packed));

struct Ipv4Addr
{
    u8 bytes[4];

    bool operator==(const Ipv4Addr &other) const
    {
        return bytes[0] == other.bytes[0] && bytes[1] == other.bytes[1] &&
               bytes[2] == other.bytes[2] && bytes[3] == other.bytes[3];
    }

    bool operator!=(const Ipv4Addr &other) const
    {
        return !(*this == other);
    }

    u32 to_u32() const
    {
        return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
               (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
    }

    static Ipv4Addr from_u32(u32 addr)
    {
        return {{static_cast<u8>((addr >> 24) & 0xff),
                 static_cast<u8>((addr >> 16) & 0xff),
                 static_cast<u8>((addr >> 8) & 0xff),
                 static_cast<u8>(addr & 0xff)}};
    }

    bool is_broadcast() const
    {
        return bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255;
    }

    bool is_zero() const
    {
        return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
    }

    bool same_subnet(const Ipv4Addr &other, const Ipv4Addr &netmask) const
    {
        return (to_u32() & netmask.to_u32()) == (other.to_u32() & netmask.to_u32());
    }

    static Ipv4Addr zero()
    {
        return {{0, 0, 0, 0}};
    }

    static Ipv4Addr broadcast()
    {
        return {{255, 255, 255, 255}};
    }
} __attribute__((packed));

// Byte order conversion
inline u16 htons(u16 x)
{
    return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}

inline u16 ntohs(u16 x)
{
    return htons(x);
}

inline u32 htonl(u32 x)
{
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}

inline u32 ntohl(u32 x)
{
    return htonl(x);
}

// Internet checksum
inline u16 checksum(const void *data, usize len)
{
    const u16 *ptr = static_cast<const u16 *>(data);
    u32 sum = 0;

    while (len > 1)
    {
        sum += *ptr++;
        len -= 2;
    }

    if (len > 0)
    {
        sum += *reinterpret_cast<const u8 *>(ptr);
    }

    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~static_cast<u16>(sum);
}

// =============================================================================
// Protocol Headers
// =============================================================================

// Ethernet header
struct EthHeader
{
    MacAddr dst;
    MacAddr src;
    u16 ethertype;
} __attribute__((packed));

constexpr u16 ETH_TYPE_IPV4 = 0x0800;
constexpr u16 ETH_TYPE_ARP = 0x0806;

// ARP header
struct ArpHeader
{
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

// IPv4 header
struct Ipv4Header
{
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

// ICMP header
struct IcmpHeader
{
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 seq;
} __attribute__((packed));

constexpr u8 ICMP_ECHO_REQUEST = 8;
constexpr u8 ICMP_ECHO_REPLY = 0;

// UDP header
struct UdpHeader
{
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed));

// TCP header
struct TcpHeader
{
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
// Network Interface
// =============================================================================

class NetIf
{
  public:
    void init(virtio::NetDevice *dev);

    MacAddr mac() const
    {
        return mac_;
    }

    Ipv4Addr ip() const
    {
        return ip_;
    }

    Ipv4Addr netmask() const
    {
        return netmask_;
    }

    Ipv4Addr gateway() const
    {
        return gateway_;
    }

    Ipv4Addr dns() const
    {
        return dns_;
    }

    void set_ip(const Ipv4Addr &ip)
    {
        ip_ = ip;
    }

    void set_netmask(const Ipv4Addr &mask)
    {
        netmask_ = mask;
    }

    void set_gateway(const Ipv4Addr &gw)
    {
        gateway_ = gw;
    }

    void set_dns(const Ipv4Addr &d)
    {
        dns_ = d;
    }

    bool is_local(const Ipv4Addr &addr) const
    {
        return ip_.same_subnet(addr, netmask_);
    }

    Ipv4Addr next_hop(const Ipv4Addr &dest) const
    {
        if (is_local(dest))
            return dest;
        return gateway_;
    }

    virtio::NetDevice *device()
    {
        return dev_;
    }

  private:
    virtio::NetDevice *dev_{nullptr};
    MacAddr mac_;
    Ipv4Addr ip_;
    Ipv4Addr netmask_;
    Ipv4Addr gateway_;
    Ipv4Addr dns_;
};

// =============================================================================
// ARP Cache
// =============================================================================

class ArpCache
{
  public:
    void init(NetIf *netif);

    // Lookup MAC for IP (returns zero MAC if not found)
    MacAddr lookup(const Ipv4Addr &ip);

    // Add entry
    void add(const Ipv4Addr &ip, const MacAddr &mac);

    // Send ARP request
    void send_request(const Ipv4Addr &ip);

    // Handle incoming ARP packet
    void handle_arp(const ArpHeader *arp, usize len);

  private:
    static constexpr usize CACHE_SIZE = 16;

    struct Entry
    {
        Ipv4Addr ip;
        MacAddr mac;
        bool valid;
    };

    Entry entries_[CACHE_SIZE];
    NetIf *netif_{nullptr};
};

// =============================================================================
// TCP Connection
// =============================================================================

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
 * @brief Minimal socket status flags for readiness queries.
 *
 * @details
 * These are intentionally aligned with the netd IPC protocol's
 * netproto::SocketStatusFlags so callers can forward them directly.
 */
enum SocketStatusFlags : u32
{
    SOCK_READABLE = (1u << 0),
    SOCK_WRITABLE = (1u << 1),
    SOCK_EOF = (1u << 2),
};

class TcpConnection
{
  public:
    bool in_use{false};
    TcpState state{TcpState::CLOSED};

    Ipv4Addr local_ip;
    u16 local_port{0};
    Ipv4Addr remote_ip;
    u16 remote_port{0};

    u32 snd_una{0};    // Oldest unacknowledged seq
    u32 snd_nxt{0};    // Next seq to send
    u32 rcv_nxt{0};    // Next expected seq
    u16 rcv_wnd{8192}; // Receive window

    // Receive buffer
    static constexpr usize RX_BUF_SIZE = 8192;
    u8 rx_buf[RX_BUF_SIZE];
    usize rx_head{0};
    usize rx_tail{0};

    // Send buffer
    static constexpr usize TX_BUF_SIZE = 8192;
    u8 tx_buf[TX_BUF_SIZE];
    usize tx_head{0};
    usize tx_tail{0};

    // Pending accept (for listening sockets)
    static constexpr usize MAX_BACKLOG = 8;

    struct PendingConn
    {
        bool valid;
        Ipv4Addr ip;
        u16 port;
        u32 seq;
    };

    PendingConn backlog[MAX_BACKLOG];
    usize backlog_count{0};

    usize rx_available() const
    {
        if (rx_tail >= rx_head)
            return rx_tail - rx_head;
        return RX_BUF_SIZE - rx_head + rx_tail;
    }

    usize tx_available() const
    {
        if (tx_tail >= tx_head)
            return TX_BUF_SIZE - (tx_tail - tx_head) - 1;
        return tx_head - tx_tail - 1;
    }
};

// =============================================================================
// UDP Socket
// =============================================================================

class UdpSocket
{
  public:
    bool in_use{false};
    Ipv4Addr local_ip;
    u16 local_port{0};

    // Receive buffer (stores datagrams)
    static constexpr usize RX_BUF_SIZE = 4096;
    u8 rx_buf[RX_BUF_SIZE];
    usize rx_len{0};
    Ipv4Addr rx_src_ip;
    u16 rx_src_port{0};
    bool has_data{false};
};

// =============================================================================
// Network Stack
// =============================================================================

class NetworkStack
{
  public:
    bool init(virtio::NetDevice *dev);

    // Packet reception
    void poll();
    void process_frame(const u8 *data, usize len);

    // High-level operations
    i32 socket_create(u16 type);
    i32 socket_connect(u32 sock_id, const Ipv4Addr &ip, u16 port);
    i32 socket_bind(u32 sock_id, u16 port);
    i32 socket_listen(u32 sock_id, u32 backlog);
    i32 socket_accept(u32 sock_id, Ipv4Addr *remote_ip, u16 *remote_port);
    i32 socket_send(u32 sock_id, const void *data, usize len);
    i32 socket_recv(u32 sock_id, void *buf, usize max_len);
    i32 socket_close(u32 sock_id);

    // Socket readiness / status
    i32 socket_status(u32 sock_id, u32 *out_flags, u32 *out_rx_available) const;
    bool any_socket_readable() const;

    // DNS
    i32 dns_resolve(const char *hostname, Ipv4Addr *out);

    // ICMP
    i32 ping(const Ipv4Addr &ip, u32 timeout_ms);

    // Network info
    NetIf *netif()
    {
        return &netif_;
    }

    // Statistics
    u64 tx_packets() const
    {
        return tx_packets_;
    }

    u64 rx_packets() const
    {
        return rx_packets_;
    }

    u64 tx_bytes() const
    {
        return tx_bytes_;
    }

    u64 rx_bytes() const
    {
        return rx_bytes_;
    }

    u32 tcp_conn_count() const;
    u32 udp_sock_count() const;

  private:
    NetIf netif_;
    ArpCache arp_;

    // TCP connections
    static constexpr usize MAX_TCP_CONNS = 32;
    TcpConnection tcp_conns_[MAX_TCP_CONNS];

    // UDP sockets
    static constexpr usize MAX_UDP_SOCKETS = 16;
    UdpSocket udp_sockets_[MAX_UDP_SOCKETS];

    // Port allocation
    u16 next_ephemeral_port_{49152};

    // Statistics
    u64 tx_packets_{0};
    u64 rx_packets_{0};
    u64 tx_bytes_{0};
    u64 rx_bytes_{0};

    // Packet ID counter
    u16 ip_id_{1};

    // DNS state
    u16 dns_txid_{1};
    bool dns_pending_{false};
    Ipv4Addr dns_result_;

    // ICMP state
    u16 icmp_seq_{1};
    bool icmp_pending_{false};
    bool icmp_received_{false};

    // Packet processing
    void handle_arp(const u8 *data, usize len);
    void handle_ipv4(const u8 *data, usize len);
    void handle_icmp(const Ipv4Header *ip, const u8 *data, usize len);
    void handle_udp(const Ipv4Header *ip, const u8 *data, usize len);
    void handle_tcp(const Ipv4Header *ip, const u8 *data, usize len);

    // Packet sending
    bool send_frame(const MacAddr &dst, u16 ethertype, const void *data, usize len);
    bool send_ip_packet(const Ipv4Addr &dst, u8 protocol, const void *data, usize len);
    bool send_tcp_segment(TcpConnection *conn, u8 flags, const void *data, usize len);
    bool send_udp_datagram(
        const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len);

    // TCP helpers
    TcpConnection *find_tcp_conn(const Ipv4Addr &remote_ip, u16 remote_port, u16 local_port);
    TcpConnection *find_listening_socket(u16 local_port);
    u16 alloc_port();
};

} // namespace netstack
