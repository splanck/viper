/**
 * @file netstack.cpp
 * @brief Kernel TCP/IP network stack implementation.
 */

#include "netstack.hpp"
#include "../../include/viperdos/net_stats.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"

namespace kc = kernel::constants;

namespace net {

// Simple memory operations
static void memcpy_net(void *dst, const void *src, usize n) {
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
}

// =============================================================================
// Network Interface
// =============================================================================
//
// The NetIf class encapsulates the network interface configuration and provides
// access to the underlying VirtIO network device. It stores the IP configuration
// (address, netmask, gateway, DNS) and determines routing for outbound packets.
// =============================================================================

/**
 * @class NetIf
 * @brief Network interface abstraction for IP configuration and routing.
 *
 * @details
 * Manages the network interface's IP configuration and provides routing
 * decisions for outbound packets. The current implementation assumes QEMU
 * user-mode networking defaults (10.0.2.x subnet).
 */
class NetIf {
  public:
    void init(virtio::NetDevice *dev) {
        dev_ = dev;
        dev->get_mac(mac_.bytes);

        // Default QEMU user-mode networking config
        ip_ = {{10, 0, 2, 15}};
        netmask_ = {{255, 255, 255, 0}};
        gateway_ = {{10, 0, 2, 2}};
        dns_ = {{10, 0, 2, 3}};
    }

    MacAddr mac() const {
        return mac_;
    }

    Ipv4Addr ip() const {
        return ip_;
    }

    Ipv4Addr netmask() const {
        return netmask_;
    }

    Ipv4Addr gateway() const {
        return gateway_;
    }

    Ipv4Addr dns() const {
        return dns_;
    }

    bool is_local(const Ipv4Addr &addr) const {
        return ip_.same_subnet(addr, netmask_);
    }

    Ipv4Addr next_hop(const Ipv4Addr &dest) const {
        if (is_local(dest))
            return dest;
        return gateway_;
    }

    virtio::NetDevice *device() {
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
//
// The ArpCache class manages IP-to-MAC address resolution using the Address
// Resolution Protocol. It maintains a fixed-size cache of resolved addresses
// and handles ARP request/response packets.
// =============================================================================

/**
 * @class ArpCache
 * @brief ARP cache for IP-to-MAC address resolution.
 *
 * @details
 * Implements ARP caching with a simple fixed-size table. When a lookup misses,
 * the caller can initiate an ARP request and poll for resolution. Entries are
 * stored without timeout (simple implementation suitable for embedded use).
 */
class ArpCache {
  public:
    void init(NetIf *netif) {
        netif_ = netif;
    }

    MacAddr lookup(const Ipv4Addr &ip) {
        for (usize i = 0; i < CACHE_SIZE; i++) {
            if (entries_[i].valid && entries_[i].ip == ip) {
                return entries_[i].mac;
            }
        }
        return MacAddr::zero();
    }

    void add(const Ipv4Addr &ip, const MacAddr &mac) {
        for (usize i = 0; i < CACHE_SIZE; i++) {
            if (entries_[i].valid && entries_[i].ip == ip) {
                entries_[i].mac = mac;
                return;
            }
        }
        for (usize i = 0; i < CACHE_SIZE; i++) {
            if (!entries_[i].valid) {
                entries_[i].ip = ip;
                entries_[i].mac = mac;
                entries_[i].valid = true;
                return;
            }
        }
        entries_[0].ip = ip;
        entries_[0].mac = mac;
    }

    void send_request(const Ipv4Addr &ip) {
        u8 frame[kc::net::ARP_FRAME_SIZE];
        EthHeader *eth = reinterpret_cast<EthHeader *>(frame);
        ArpHeader *arp = reinterpret_cast<ArpHeader *>(frame + sizeof(EthHeader));

        eth->dst = MacAddr::broadcast();
        eth->src = netif_->mac();
        eth->ethertype = htons(ETH_TYPE_ARP);

        arp->hw_type = htons(ARP_HW_ETHERNET);
        arp->proto_type = htons(ETH_TYPE_IPV4);
        arp->hw_len = 6;
        arp->proto_len = 4;
        arp->operation = htons(ARP_OP_REQUEST);
        arp->sender_mac = netif_->mac();
        arp->sender_ip = netif_->ip();
        arp->target_mac = MacAddr::zero();
        arp->target_ip = ip;

        netif_->device()->transmit(frame, sizeof(EthHeader) + sizeof(ArpHeader));
    }

    void handle_arp(const ArpHeader *arp, usize len) {
        (void)len;

        if (ntohs(arp->hw_type) != ARP_HW_ETHERNET)
            return;
        if (ntohs(arp->proto_type) != ETH_TYPE_IPV4)
            return;

        u16 op = ntohs(arp->operation);
        (void)op;
        add(arp->sender_ip, arp->sender_mac);

        if (op == ARP_OP_REQUEST && arp->target_ip == netif_->ip()) {
            u8 frame[kc::net::ARP_FRAME_SIZE];
            EthHeader *eth_reply = reinterpret_cast<EthHeader *>(frame);
            ArpHeader *arp_reply = reinterpret_cast<ArpHeader *>(frame + sizeof(EthHeader));

            eth_reply->dst = arp->sender_mac;
            eth_reply->src = netif_->mac();
            eth_reply->ethertype = htons(ETH_TYPE_ARP);

            arp_reply->hw_type = htons(ARP_HW_ETHERNET);
            arp_reply->proto_type = htons(ETH_TYPE_IPV4);
            arp_reply->hw_len = 6;
            arp_reply->proto_len = 4;
            arp_reply->operation = htons(ARP_OP_REPLY);
            arp_reply->sender_mac = netif_->mac();
            arp_reply->sender_ip = netif_->ip();
            arp_reply->target_mac = arp->sender_mac;
            arp_reply->target_ip = arp->sender_ip;

            netif_->device()->transmit(frame, sizeof(EthHeader) + sizeof(ArpHeader));
        }
    }

  private:
    static constexpr usize CACHE_SIZE = kc::net::ARP_CACHE_SIZE;

    struct Entry {
        Ipv4Addr ip;
        MacAddr mac;
        bool valid{false};
    };

    Entry entries_[CACHE_SIZE];
    NetIf *netif_{nullptr};
};

// =============================================================================
// Network Stack State
// =============================================================================

static NetIf g_netif;
static ArpCache g_arp;
static TcpConnection g_tcp_conns[MAX_TCP_CONNS];
static UdpSocket g_udp_sockets[MAX_UDP_SOCKETS];
static bool g_initialized = false;

// Port allocation
static u16 g_next_ephemeral_port = kc::net::EPHEMERAL_PORT_START;

// Statistics
static u64 g_tx_packets = 0;
static u64 g_rx_packets = 0;
static u64 g_tx_bytes = 0;
static u64 g_rx_bytes = 0;

// Packet ID counter
static u16 g_ip_id = 1;

// DNS state
static u16 g_dns_txid = 1;
static bool g_dns_pending = false;
static Ipv4Addr g_dns_result;

// ICMP state
static u16 g_icmp_seq = 1;
static bool g_icmp_pending = false;
static bool g_icmp_received = false;

// Forward declarations
static bool send_frame(const MacAddr &dst, u16 ethertype, const void *data, usize len);
static bool send_ip_packet(const Ipv4Addr &dst, u8 protocol, const void *data, usize len);
static bool send_tcp_segment(TcpConnection *conn, u8 flags, const void *data, usize len);
static bool send_udp_datagram(
    const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len);
static void handle_arp(const u8 *data, usize len);
static void handle_ipv4(const u8 *data, usize len);
static void handle_icmp(const Ipv4Header *ip, const u8 *data, usize len);
static void handle_udp(const Ipv4Header *ip, const u8 *data, usize len);
static void handle_tcp(const Ipv4Header *ip, const u8 *data, usize len);
static TcpConnection *find_tcp_conn(const Ipv4Addr &remote_ip, u16 remote_port, u16 local_port);
static TcpConnection *find_listening_socket(u16 local_port);
static u16 alloc_port();

// =============================================================================
// Public API Implementation
// =============================================================================

void network_init() {
    serial::puts("[netstack] network_init() called\n");

    if (g_initialized) {
        serial::puts("[netstack] Already initialized\n");
        return;
    }

    virtio::net_init();
    virtio::NetDevice *dev = virtio::net_device();
    if (!dev) {
        serial::puts("[netstack] No network device available\n");
        return;
    }

    g_netif.init(dev);
    g_arp.init(&g_netif);

    for (usize i = 0; i < MAX_TCP_CONNS; i++) {
        g_tcp_conns[i].in_use = false;
    }
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++) {
        g_udp_sockets[i].in_use = false;
    }

    g_initialized = true;
    serial::puts("[netstack] Network stack initialized\n");
}

void network_poll() {
    if (!g_initialized)
        return;

    virtio::NetDevice *dev = virtio::net_device();
    if (!dev)
        return;

    dev->poll_rx();

    u8 buf[kc::net::RX_BUFFER_SIZE];
    while (true) {
        i32 len = dev->receive(buf, sizeof(buf));
        if (len <= 0)
            break;

        if (static_cast<usize>(len) < sizeof(EthHeader))
            continue;

        g_rx_packets++;
        g_rx_bytes += len;

        const EthHeader *eth = reinterpret_cast<const EthHeader *>(buf);
        u16 ethertype = ntohs(eth->ethertype);

        const u8 *payload = buf + sizeof(EthHeader);
        usize payload_len = len - sizeof(EthHeader);

        switch (ethertype) {
            case ETH_TYPE_ARP:
                handle_arp(payload, payload_len);
                break;
            case ETH_TYPE_IPV4:
                handle_ipv4(payload, payload_len);
                break;
        }
    }
}

bool is_available() {
    return g_initialized;
}

// =============================================================================
// =============================================================================
// TCP Socket API
// =============================================================================
//
// The TCP socket API provides a BSD-style socket interface for user processes
// to establish TCP connections. Each socket is associated with an owner process
// and tracks connection state according to the TCP state machine.
//
// Socket lifecycle:
//   1. socket_create() - Allocate a socket slot, returns socket ID
//   2. socket_connect() - Initiate TCP 3-way handshake to remote host
//   3. socket_send()/socket_recv() - Transfer data on established connection
//   4. socket_close() - Initiate connection teardown
//
// Sockets are identified by integer IDs (0 to MAX_TCP_CONNS-1).
// =============================================================================

namespace tcp {

/**
 * @brief Create a new TCP socket for a process.
 *
 * @details
 * Allocates an unused TCP connection slot and initializes it for the specified
 * process. The socket starts in the CLOSED state, ready for a connect() call.
 *
 * @param process_id The owning process ID for access control.
 * @return Socket ID (>= 0) on success, or negative error code (VERR_NO_RESOURCE).
 */
i64 socket_create(u32 process_id) {
    for (usize i = 0; i < MAX_TCP_CONNS; i++) {
        if (!g_tcp_conns[i].in_use) {
            TcpConnection *conn = &g_tcp_conns[i];
            conn->in_use = true;
            conn->owner_pid = process_id;
            conn->state = TcpState::CLOSED;
            conn->local_ip = g_netif.ip();
            conn->local_port = 0;
            conn->remote_ip = Ipv4Addr::zero();
            conn->remote_port = 0;
            conn->snd_una = 0;
            conn->snd_nxt = 0;
            conn->rcv_nxt = 0;
            conn->rx_head = 0;
            conn->rx_tail = 0;
            conn->tx_head = 0;
            conn->tx_tail = 0;
            conn->backlog_count = 0;
            return static_cast<i64>(i);
        }
    }
    return VERR_NO_RESOURCE;
}

/**
 * @brief Initiate a TCP connection to a remote host.
 *
 * @details
 * Performs the TCP 3-way handshake (SYN, SYN-ACK, ACK) with the specified
 * remote address. This function blocks until the connection is established
 * or the timeout expires. On success, the socket transitions to ESTABLISHED
 * state and is ready for data transfer.
 *
 * @param sock   Socket ID returned by socket_create().
 * @param ip     Remote IPv4 address to connect to.
 * @param port   Remote TCP port number.
 * @return true if connection established, false on timeout or error.
 */
bool socket_connect(i32 sock, const Ipv4Addr &ip, u16 port) {
    serial::puts("[tcp] connect: sock=");
    serial::put_dec(sock);
    serial::puts(" ip=");
    serial::put_ipv4(ip.bytes);
    serial::puts(" port=");
    serial::put_dec(port);
    serial::putc('\n');

    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS) {
        serial::puts("[tcp] connect: invalid socket\n");
        return false;
    }

    TcpConnection *conn = &g_tcp_conns[sock];
    if (!conn->in_use || conn->state != TcpState::CLOSED) {
        serial::puts("[tcp] connect: socket not in valid state\n");
        return false;
    }

    conn->remote_ip = ip;
    conn->remote_port = port;
    conn->local_port = alloc_port();

    // Initialize sequence numbers
    conn->snd_una = 0x12345678; // Should be random
    conn->snd_nxt = conn->snd_una;
    conn->rcv_nxt = 0;

    // Send SYN
    conn->state = TcpState::SYN_SENT;
    bool syn_sent = send_tcp_segment(conn, TCP_SYN, nullptr, 0);
    serial::puts("[tcp] SYN sent: ");
    serial::puts(syn_sent ? "yes" : "no");
    serial::putc('\n');

    // Use timer-based timeout for more reliable connection timing
    u64 start_ticks = timer::get_ticks();
    u64 timeout_ticks = kc::net::TCP_CONNECT_TIMEOUT_MS;
    int syn_retries = 0;

    while (timer::get_ticks() - start_ticks < timeout_ticks) {
        network_poll();
        if (conn->state == TcpState::ESTABLISHED) {
            serial::puts("[tcp] connect: ESTABLISHED\n");
            return true;
        }

        if (!syn_sent && syn_retries < static_cast<int>(kc::net::CONNECT_RETRY_COUNT)) {
            conn->snd_nxt = conn->snd_una;
            syn_sent = send_tcp_segment(conn, TCP_SYN, nullptr, 0);
            syn_retries++;
            serial::puts("[tcp] SYN retry ");
            serial::put_dec(syn_retries);
            serial::puts(": ");
            serial::puts(syn_sent ? "sent" : "failed");
            serial::putc('\n');
        }

        // Small delay between polls
        for (volatile u32 j = 0; j < 100; j = j + 1)
            ;
    }

    serial::puts("[tcp] connect: timeout after ");
    serial::put_dec(timer::get_ticks() - start_ticks);
    serial::puts("ms, state=");
    serial::put_dec(static_cast<u32>(conn->state));
    serial::putc('\n');

    conn->state = TcpState::CLOSED;
    return false;
}

/**
 * @brief Send data on an established TCP connection.
 *
 * @details
 * Transmits data to the remote peer over an established connection. Data is
 * segmented according to TCP_MAX_CHUNK and sent with PSH flag to encourage
 * immediate delivery. This is a synchronous operation that returns after all
 * data has been queued for transmission.
 *
 * @param sock Socket ID of an ESTABLISHED connection.
 * @param buf  Pointer to data to send.
 * @param len  Number of bytes to send.
 * @return Number of bytes sent on success, or negative error code.
 */
i64 socket_send(i32 sock, const void *buf, usize len) {
    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *conn = &g_tcp_conns[sock];
    if (!conn->in_use || conn->state != TcpState::ESTABLISHED)
        return VERR_CONNECTION;

    const u8 *ptr = static_cast<const u8 *>(buf);
    usize sent = 0;

    while (sent < len) {
        usize chunk = len - sent;
        if (chunk > kc::net::TCP_MAX_CHUNK)
            chunk = kc::net::TCP_MAX_CHUNK;

        send_tcp_segment(conn, TCP_ACK | TCP_PSH, ptr + sent, chunk);
        sent += chunk;
    }

    return static_cast<i64>(sent);
}

/**
 * @brief Receive data from a TCP connection.
 *
 * @details
 * Reads available data from the socket's receive buffer. This is a non-blocking
 * operation that returns immediately with whatever data is available:
 * - Returns data count if data is available
 * - Returns VERR_WOULD_BLOCK if no data and connection is open
 * - Returns 0 (EOF) if connection is closing/closed
 *
 * @param sock Socket ID.
 * @param buf  Buffer to receive data into.
 * @param len  Maximum number of bytes to read.
 * @return Number of bytes read, 0 for EOF, or negative error code.
 */
i64 socket_recv(i32 sock, void *buf, usize len) {
    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *conn = &g_tcp_conns[sock];
    if (!conn->in_use)
        return VERR_INVALID_HANDLE;

    usize available = conn->rx_available();

    if (available == 0) {
        if (conn->state == TcpState::CLOSE_WAIT || conn->state == TcpState::CLOSED)
            return 0; // EOF
        return VERR_WOULD_BLOCK;
    }

    usize to_read = available;
    if (to_read > len)
        to_read = len;

    u8 *dst = static_cast<u8 *>(buf);
    for (usize i = 0; i < to_read; i++) {
        dst[i] = conn->rx_buf[conn->rx_head];
        conn->rx_head = (conn->rx_head + 1) % TcpConnection::RX_BUF_SIZE;
    }

    return static_cast<i64>(to_read);
}

/**
 * @brief Close a TCP socket and release its resources.
 *
 * @details
 * Initiates graceful connection teardown by sending FIN and waiting for
 * acknowledgment. If the socket is in ESTABLISHED state, performs the
 * TCP 4-way close handshake. The socket slot is marked as unused and can
 * be reused by subsequent socket_create() calls.
 *
 * @param sock Socket ID to close.
 */
void socket_close(i32 sock) {
    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS)
        return;

    TcpConnection *conn = &g_tcp_conns[sock];
    if (!conn->in_use)
        return;

    if (conn->state == TcpState::ESTABLISHED) {
        conn->state = TcpState::FIN_WAIT_1;
        send_tcp_segment(conn, TCP_FIN | TCP_ACK, nullptr, 0);

        for (u32 i = 0; i < kc::net::TCP_CLOSE_POLL_ITERATIONS; i++) {
            network_poll();
            if (conn->state == TcpState::CLOSED)
                break;
            for (volatile u32 j = 0; j < kc::net::BUSY_WAIT_ITERATIONS; j = j + 1)
                ;
        }
    }

    conn->in_use = false;
    conn->state = TcpState::CLOSED;
}

/**
 * @brief Check if a socket is owned by a specific process.
 *
 * @details
 * Verifies that the specified socket exists and belongs to the given process.
 * Used by syscall handlers to enforce process isolation for socket operations.
 *
 * @param sock       Socket ID to check.
 * @param process_id Process ID to verify ownership against.
 * @return true if socket exists and is owned by process, false otherwise.
 */
bool socket_owned_by(i32 sock, u32 process_id) {
    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS)
        return false;

    TcpConnection *conn = &g_tcp_conns[sock];
    return conn->in_use && conn->owner_pid == process_id;
}

/**
 * @brief Query socket status for poll/select operations.
 *
 * @details
 * Returns the current readiness state of a socket. This is used by the poll
 * syscall to determine which sockets have pending data or are ready for I/O.
 *
 * Flags returned in out_flags:
 * - SOCK_READABLE (1): Data available in receive buffer
 * - SOCK_WRITABLE (2): Socket can accept data for sending
 * - SOCK_EOF (4): Connection has been closed by peer
 *
 * @param sock             Socket ID to query.
 * @param out_flags        Output: Bitmask of socket status flags.
 * @param out_rx_available Output: Number of bytes available to read.
 * @return 0 on success, negative error code on failure.
 */
i32 socket_status(i32 sock, u32 *out_flags, u32 *out_rx_available) {
    if (!out_flags || !out_rx_available)
        return VERR_INVALID_ARG;

    *out_flags = 0;
    *out_rx_available = 0;

    if (sock < 0 || static_cast<usize>(sock) >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *conn = &g_tcp_conns[sock];
    if (!conn->in_use)
        return VERR_INVALID_HANDLE;

    usize avail = conn->rx_available();
    if (avail > 0) {
        *out_flags |= SOCK_READABLE;
        if (avail > 0xFFFFFFFFu)
            avail = 0xFFFFFFFFu;
        *out_rx_available = static_cast<u32>(avail);
    }

    if (conn->state == TcpState::ESTABLISHED) {
        *out_flags |= SOCK_WRITABLE;
    }

    if ((conn->state == TcpState::CLOSE_WAIT || conn->state == TcpState::CLOSED) && avail == 0) {
        *out_flags |= SOCK_EOF;
        *out_flags |= SOCK_READABLE;
    }

    return 0;
}

} // namespace tcp

// =============================================================================
// DNS API
// =============================================================================

namespace dns {

bool resolve(const char *hostname, Ipv4Addr *out, u32 timeout_ms) {
    (void)timeout_ms;

    if (!g_initialized) {
        serial::puts("[dns] resolve failed: network not initialized\n");
        return false;
    }
    if (!out) {
        serial::puts("[dns] resolve failed: null output pointer\n");
        return false;
    }

    // Build DNS query
    u8 query[kc::net::DNS_QUERY_BUFFER_SIZE];
    usize pos = 0;

    g_dns_txid++;
    query[pos++] = static_cast<u8>(g_dns_txid >> 8);
    query[pos++] = static_cast<u8>(g_dns_txid & 0xff);

    // Flags: standard query
    query[pos++] = 0x01;
    query[pos++] = 0x00;

    // Questions: 1
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;

    // Encode hostname
    const char *ptr = hostname;
    while (*ptr) {
        const char *dot = ptr;
        while (*dot && *dot != '.')
            dot++;
        usize label_len = static_cast<usize>(dot - ptr);
        query[pos++] = static_cast<u8>(label_len);
        for (usize i = 0; i < label_len; i++)
            query[pos++] = static_cast<u8>(ptr[i]);
        ptr = dot;
        if (*ptr == '.')
            ptr++;
    }
    query[pos++] = 0;

    // QTYPE: A (1), QCLASS: IN (1)
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    query[pos++] = 0x00;
    query[pos++] = 0x01;

    g_dns_pending = true;
    g_dns_result = Ipv4Addr::zero();

    u16 src_port = alloc_port();

    bool sent = false;
    for (u32 attempt = 0; attempt < kc::net::CONNECT_RETRY_COUNT && !sent; attempt++) {
        sent = send_udp_datagram(g_netif.dns(), src_port, kc::net::DNS_PORT, query, pos);
        if (!sent) {
            for (int j = 0; j < 20; j++) {
                network_poll();
                for (volatile u32 k = 0; k < kc::net::BUSY_WAIT_ITERATIONS; k = k + 1)
                    ;
            }
        }
    }

    if (!sent) {
        g_dns_pending = false;
        return false;
    }

    // Use timer-based timeout instead of iteration count for reliable timing
    u64 start_ticks = timer::get_ticks();
    u64 timeout_ticks = timeout_ms; // timer runs at 1kHz so ticks ~= ms

    while (timer::get_ticks() - start_ticks < timeout_ticks) {
        network_poll();
        if (!g_dns_pending) {
            *out = g_dns_result;
            return true;
        }
        // Small delay between polls
        for (volatile int j = 0; j < 100; j = j + 1)
            ;
    }

    g_dns_pending = false;
    return false;
}

} // namespace dns

// =============================================================================
// ICMP API
// =============================================================================

namespace icmp {

i32 ping(const Ipv4Addr &ip, u32 timeout_ms) {
    (void)timeout_ms;

    if (!g_initialized)
        return VERR_TIMEOUT;

    u8 icmp_data[kc::net::ICMP_BUFFER_SIZE];
    IcmpHeader *icmp = reinterpret_cast<IcmpHeader *>(icmp_data);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(0x1234);
    icmp->seq = htons(g_icmp_seq++);
    icmp->checksum = 0;

    for (u32 i = 0; i < kc::net::ICMP_DATA_SIZE; i++) {
        icmp_data[sizeof(IcmpHeader) + i] = static_cast<u8>(i);
    }

    icmp->checksum = checksum(icmp_data, sizeof(IcmpHeader) + kc::net::ICMP_DATA_SIZE);

    g_icmp_pending = true;
    g_icmp_received = false;

    send_ip_packet(ip, IP_PROTO_ICMP, icmp_data, sizeof(IcmpHeader) + kc::net::ICMP_DATA_SIZE);

    for (u32 i = 0; i < kc::net::ICMP_POLL_ITERATIONS; i++) {
        network_poll();
        if (g_icmp_received) {
            return 0;
        }
        for (volatile u32 j = 0; j < kc::net::BUSY_WAIT_ITERATIONS; j = j + 1)
            ;
    }

    g_icmp_pending = false;
    return VERR_TIMEOUT;
}

} // namespace icmp

// =============================================================================
// Internal Implementation
// =============================================================================

static u16 alloc_port() {
    u16 port = g_next_ephemeral_port++;
    if (g_next_ephemeral_port > kc::net::EPHEMERAL_PORT_MAX)
        g_next_ephemeral_port = kc::net::EPHEMERAL_PORT_START;
    return port;
}

static bool send_frame(const MacAddr &dst, u16 ethertype, const void *data, usize len) {
    u8 frame[kc::net::FRAME_MAX_SIZE];
    if (len + sizeof(EthHeader) > sizeof(frame))
        return false;

    EthHeader *eth = reinterpret_cast<EthHeader *>(frame);
    eth->dst = dst;
    eth->src = g_netif.mac();
    eth->ethertype = htons(ethertype);

    memcpy_net(frame + sizeof(EthHeader), data, len);

    virtio::NetDevice *dev = virtio::net_device();
    if (!dev)
        return false;

    bool ok = dev->transmit(frame, sizeof(EthHeader) + len);
    if (ok) {
        g_tx_packets++;
        g_tx_bytes += sizeof(EthHeader) + len;
    }
    return ok;
}

static bool send_ip_packet(const Ipv4Addr &dst, u8 protocol, const void *data, usize len) {
    u8 packet[kc::net::IP_PACKET_MAX];
    if (len + sizeof(Ipv4Header) > sizeof(packet))
        return false;

    Ipv4Header *ip = reinterpret_cast<Ipv4Header *>(packet);
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(static_cast<u16>(sizeof(Ipv4Header) + len));
    ip->id = htons(g_ip_id++);
    ip->flags_frag = 0;
    ip->ttl = kc::net::IP_TTL_DEFAULT;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src = g_netif.ip();
    ip->dst = dst;
    ip->checksum = checksum(ip, sizeof(Ipv4Header));

    memcpy_net(packet + sizeof(Ipv4Header), data, len);

    Ipv4Addr next_hop = g_netif.next_hop(dst);
    MacAddr dst_mac = g_arp.lookup(next_hop);

    if (dst_mac == MacAddr::zero()) {
        serial::puts("[ip] ARP lookup miss for ");
        serial::put_ipv4(next_hop.bytes);
        serial::puts(", sending ARP request\n");

        g_arp.send_request(next_hop);

        for (u32 i = 0; i < kc::net::ARP_REQUEST_POLL_ITERATIONS; i++) {
            network_poll();
            dst_mac = g_arp.lookup(next_hop);
            if (dst_mac != MacAddr::zero()) {
                serial::puts("[ip] ARP resolved after ");
                serial::put_dec(i);
                serial::puts(" polls\n");
                break;
            }
            for (volatile u32 j = 0; j < kc::net::BUSY_WAIT_ITERATIONS; j = j + 1)
                ;
        }

        if (dst_mac == MacAddr::zero()) {
            serial::puts("[ip] ARP resolution FAILED\n");
            return false;
        }
    }

    return send_frame(dst_mac, ETH_TYPE_IPV4, packet, sizeof(Ipv4Header) + len);
}

static bool send_tcp_segment(TcpConnection *conn, u8 flags, const void *data, usize len) {
    u8 segment[kc::net::TCP_SEGMENT_MAX];
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(segment);

    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq = htonl(conn->snd_nxt);
    tcp->ack = htonl(conn->rcv_nxt);
    tcp->data_offset = (5 << 4);
    tcp->flags = flags;
    tcp->window = htons(conn->rx_window());
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (data && len > 0) {
        memcpy_net(segment + sizeof(TcpHeader), data, len);
    }

    // Calculate TCP checksum
    struct PseudoHeader {
        Ipv4Addr src;
        Ipv4Addr dst;
        u8 zero;
        u8 protocol;
        u16 length;
    } __attribute__((packed));

    u8 csum_buf[sizeof(PseudoHeader) + sizeof(TcpHeader) + 1460];
    PseudoHeader *pseudo = reinterpret_cast<PseudoHeader *>(csum_buf);
    pseudo->src = g_netif.ip();
    pseudo->dst = conn->remote_ip;
    pseudo->zero = 0;
    pseudo->protocol = IP_PROTO_TCP;
    pseudo->length = htons(static_cast<u16>(sizeof(TcpHeader) + len));

    memcpy_net(csum_buf + sizeof(PseudoHeader), segment, sizeof(TcpHeader) + len);
    tcp->checksum = checksum(csum_buf, sizeof(PseudoHeader) + sizeof(TcpHeader) + len);

    if (flags & TCP_SYN)
        conn->snd_nxt++;
    if (flags & TCP_FIN)
        conn->snd_nxt++;
    conn->snd_nxt += static_cast<u32>(len);

    bool ok = send_ip_packet(conn->remote_ip, IP_PROTO_TCP, segment, sizeof(TcpHeader) + len);
    if (flags & TCP_SYN) {
        serial::puts("[tcp] send_tcp_segment SYN to ");
        serial::put_ipv4(conn->remote_ip.bytes);
        serial::putc(':');
        serial::put_dec(conn->remote_port);
        serial::puts(" result=");
        serial::puts(ok ? "ok" : "failed");
        serial::putc('\n');
    }
    return ok;
}

static bool send_udp_datagram(
    const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len) {
    u8 datagram[kc::net::UDP_DATAGRAM_MAX];
    if (len + sizeof(UdpHeader) > sizeof(datagram))
        return false;

    UdpHeader *udp = reinterpret_cast<UdpHeader *>(datagram);
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(static_cast<u16>(sizeof(UdpHeader) + len));
    udp->checksum = 0;

    if (data && len > 0) {
        memcpy_net(datagram + sizeof(UdpHeader), data, len);
    }

    return send_ip_packet(dst, IP_PROTO_UDP, datagram, sizeof(UdpHeader) + len);
}

static void handle_arp(const u8 *data, usize len) {
    if (len < sizeof(ArpHeader))
        return;
    g_arp.handle_arp(reinterpret_cast<const ArpHeader *>(data), len);
}

static void handle_ipv4(const u8 *data, usize len) {
    if (len < sizeof(Ipv4Header))
        return;

    const Ipv4Header *ip = reinterpret_cast<const Ipv4Header *>(data);

    if ((ip->version_ihl >> 4) != 4)
        return;


    if (ip->dst != g_netif.ip() && !ip->dst.is_broadcast())
        return;

    u8 ihl = (ip->version_ihl & 0x0f) * 4;
    const u8 *payload = data + ihl;
    usize payload_len = ntohs(ip->total_len) - ihl;

    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            handle_icmp(ip, payload, payload_len);
            break;
        case IP_PROTO_UDP:
            handle_udp(ip, payload, payload_len);
            break;
        case IP_PROTO_TCP:
            handle_tcp(ip, payload, payload_len);
            break;
    }
}

static void handle_icmp(const Ipv4Header *ip, const u8 *data, usize len) {
    if (len < sizeof(IcmpHeader))
        return;

    const IcmpHeader *icmp = reinterpret_cast<const IcmpHeader *>(data);

    if (icmp->type == ICMP_ECHO_REQUEST) {
        u8 reply[kc::net::ICMP_BUFFER_SIZE];
        IcmpHeader *reply_icmp = reinterpret_cast<IcmpHeader *>(reply);

        reply_icmp->type = ICMP_ECHO_REPLY;
        reply_icmp->code = 0;
        reply_icmp->id = icmp->id;
        reply_icmp->seq = icmp->seq;
        reply_icmp->checksum = 0;

        usize data_len = len - sizeof(IcmpHeader);
        if (data_len > sizeof(reply) - sizeof(IcmpHeader))
            data_len = sizeof(reply) - sizeof(IcmpHeader);
        memcpy_net(reply + sizeof(IcmpHeader), data + sizeof(IcmpHeader), data_len);

        reply_icmp->checksum = checksum(reply, sizeof(IcmpHeader) + data_len);

        send_ip_packet(ip->src, IP_PROTO_ICMP, reply, sizeof(IcmpHeader) + data_len);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        if (g_icmp_pending) {
            g_icmp_received = true;
            g_icmp_pending = false;
        }
    }
}

static void handle_udp(const Ipv4Header *ip, const u8 *data, usize len) {
    if (len < sizeof(UdpHeader))
        return;

    const UdpHeader *udp = reinterpret_cast<const UdpHeader *>(data);
    u16 src_port = ntohs(udp->src_port);
    u16 udp_len = ntohs(udp->length);

    // Check for DNS reply
    if (src_port == kc::net::DNS_PORT && g_dns_pending) {
        const u8 *dns_data = data + sizeof(UdpHeader);
        usize dns_len = udp_len - sizeof(UdpHeader);

        if (dns_len >= 12) {
            u16 txid = (static_cast<u16>(dns_data[0]) << 8) | dns_data[1];
            u16 flags = (static_cast<u16>(dns_data[2]) << 8) | dns_data[3];
            u16 ancount = (static_cast<u16>(dns_data[6]) << 8) | dns_data[7];

            if (txid == g_dns_txid && (flags & 0x8000) && ancount > 0) {
                usize pos = 12;
                // Skip query name
                while (pos < dns_len && dns_data[pos] != 0) {
                    if ((dns_data[pos] & 0xc0) == 0xc0) {
                        pos += 2;
                        break;
                    }
                    pos += dns_data[pos] + 1;
                }
                if (pos < dns_len && dns_data[pos] == 0)
                    pos++;
                pos += 4;

                if (pos + 12 <= dns_len) {
                    if ((dns_data[pos] & 0xc0) == 0xc0)
                        pos += 2;
                    else {
                        while (pos < dns_len && dns_data[pos] != 0)
                            pos += dns_data[pos] + 1;
                        pos++;
                    }

                    if (pos + 10 <= dns_len) {
                        u16 rtype = (static_cast<u16>(dns_data[pos]) << 8) | dns_data[pos + 1];
                        u16 rdlen = (static_cast<u16>(dns_data[pos + 8]) << 8) | dns_data[pos + 9];

                        if (rtype == 1 && rdlen == 4 && pos + 10 + 4 <= dns_len) {
                            g_dns_result.bytes[0] = dns_data[pos + 10];
                            g_dns_result.bytes[1] = dns_data[pos + 11];
                            g_dns_result.bytes[2] = dns_data[pos + 12];
                            g_dns_result.bytes[3] = dns_data[pos + 13];
                            g_dns_pending = false;
                        }
                    }
                }
            }
        }
        return;
    }

    // Find matching UDP socket
    u16 dst_port = ntohs(udp->dst_port);
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (g_udp_sockets[i].in_use && g_udp_sockets[i].local_port == dst_port) {
            UdpSocket *sock = &g_udp_sockets[i];
            const u8 *payload = data + sizeof(UdpHeader);
            usize payload_len = udp_len - sizeof(UdpHeader);

            if (payload_len <= UdpSocket::RX_BUF_SIZE) {
                memcpy_net(sock->rx_buf, payload, payload_len);
                sock->rx_len = payload_len;
                sock->rx_src_ip = ip->src;
                sock->rx_src_port = src_port;
                sock->has_data = true;
            }
            return;
        }
    }
}

static TcpConnection *find_tcp_conn(const Ipv4Addr &remote_ip, u16 remote_port, u16 local_port) {
    for (usize i = 0; i < MAX_TCP_CONNS; i++) {
        TcpConnection *c = &g_tcp_conns[i];
        if (c->in_use && c->state != TcpState::LISTEN && c->local_port == local_port &&
            c->remote_port == remote_port && c->remote_ip == remote_ip) {
            return c;
        }
    }
    return nullptr;
}

static TcpConnection *find_listening_socket(u16 local_port) {
    for (usize i = 0; i < MAX_TCP_CONNS; i++) {
        if (g_tcp_conns[i].in_use && g_tcp_conns[i].state == TcpState::LISTEN &&
            g_tcp_conns[i].local_port == local_port) {
            return &g_tcp_conns[i];
        }
    }
    return nullptr;
}

// =============================================================================
// TCP State Handlers
// =============================================================================

static void handle_tcp_syn_sent(TcpConnection *conn, u8 flags, u32 seq, u32 ack) {
    if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
        serial::puts("[tcp] got SYN-ACK, transitioning to ESTABLISHED\n");
        conn->rcv_nxt = seq + 1;
        conn->snd_una = ack;
        conn->state = TcpState::ESTABLISHED;
        send_tcp_segment(conn, TCP_ACK, nullptr, 0);
    } else if (flags & TCP_RST) {
        serial::puts("[tcp] got RST, connection refused\n");
        conn->state = TcpState::CLOSED;
    }
}

static void handle_tcp_established_data(TcpConnection *conn,
                                        u32 seq,
                                        const u8 *payload,
                                        usize payload_len) {
    if (seq != conn->rcv_nxt) {
        serial::puts("[tcp] DROP: seq mismatch, got=");
        serial::put_hex(seq);
        serial::puts(" expect=");
        serial::put_hex(conn->rcv_nxt);
        serial::putc('\n');
        return;
    }

    usize space =
        TcpConnection::RX_BUF_SIZE -
        ((conn->rx_tail - conn->rx_head + TcpConnection::RX_BUF_SIZE) % TcpConnection::RX_BUF_SIZE);

    if (payload_len <= space) {
        for (usize i = 0; i < payload_len; i++) {
            conn->rx_buf[conn->rx_tail] = payload[i];
            conn->rx_tail = (conn->rx_tail + 1) % TcpConnection::RX_BUF_SIZE;
        }
        conn->rcv_nxt += static_cast<u32>(payload_len);
        serial::puts("[tcp] copied ");
        serial::put_dec(payload_len);
        serial::puts(" bytes to rx_buf\n");
    } else {
        serial::puts("[tcp] DROP: no space\n");
    }
}

static void handle_tcp_established(
    TcpConnection *conn, u8 flags, u32 seq, u32 ack, const u8 *payload, usize payload_len) {
    if (flags & TCP_ACK)
        conn->snd_una = ack;

    if (flags & TCP_FIN) {
        conn->rcv_nxt = seq + 1;
        conn->state = TcpState::CLOSE_WAIT;
        send_tcp_segment(conn, TCP_ACK, nullptr, 0);
    } else if (payload_len > 0) {
        handle_tcp_established_data(conn, seq, payload, payload_len);
        send_tcp_segment(conn, TCP_ACK, nullptr, 0);
    }
}

static void handle_tcp_fin_wait(TcpConnection *conn, u8 flags, u32 seq) {
    if (conn->state == TcpState::FIN_WAIT_1 && (flags & TCP_ACK)) {
        conn->state = TcpState::FIN_WAIT_2;
    } else if (conn->state == TcpState::FIN_WAIT_2 && (flags & TCP_FIN)) {
        conn->rcv_nxt = seq + 1;
        send_tcp_segment(conn, TCP_ACK, nullptr, 0);
        conn->state = TcpState::CLOSED;
        conn->in_use = false;
    }
}

static void handle_tcp_incoming_syn(u16 dst_port, const Ipv4Addr &src_ip, u16 src_port, u32 seq) {
    TcpConnection *listener = find_listening_socket(dst_port);
    if (listener && listener->backlog_count < TcpConnection::MAX_BACKLOG) {
        auto &pending = listener->backlog[listener->backlog_count++];
        pending.valid = true;
        pending.ip = src_ip;
        pending.port = src_port;
        pending.seq = seq;
    }
}

// =============================================================================
// TCP Packet Handler
// =============================================================================

static void handle_tcp(const Ipv4Header *ip, const u8 *data, usize len) {
    if (len < sizeof(TcpHeader))
        return;

    const TcpHeader *tcp = reinterpret_cast<const TcpHeader *>(data);
    u16 dst_port = ntohs(tcp->dst_port);
    u16 src_port = ntohs(tcp->src_port);
    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    u8 flags = tcp->flags;
    u8 data_offset = (tcp->data_offset >> 4) * 4;

    const u8 *payload = data + data_offset;
    usize payload_len = len - data_offset;

    serial::puts("[tcp] rx: flags=");
    serial::put_hex(flags);
    serial::puts(" src=");
    serial::put_ipv4(ip->src.bytes);
    serial::putc(':');
    serial::put_dec(src_port);
    serial::puts(" dst=:");
    serial::put_dec(dst_port);
    serial::putc('\n');

    TcpConnection *conn = find_tcp_conn(ip->src, src_port, dst_port);

    if (conn) {
        switch (conn->state) {
            case TcpState::SYN_SENT:
                handle_tcp_syn_sent(conn, flags, seq, ack);
                break;
            case TcpState::ESTABLISHED:
                handle_tcp_established(conn, flags, seq, ack, payload, payload_len);
                break;
            case TcpState::FIN_WAIT_1:
            case TcpState::FIN_WAIT_2:
                handle_tcp_fin_wait(conn, flags, seq);
                break;
            default:
                break;
        }
    } else if ((flags & TCP_SYN) && !(flags & TCP_ACK)) {
        handle_tcp_incoming_syn(dst_port, ip->src, src_port, seq);
    }
}

// =============================================================================
// Network Statistics
// =============================================================================

void get_stats(::NetStats *out) {
    // Zero-initialize the structure
    for (usize i = 0; i < sizeof(::NetStats); i++) {
        reinterpret_cast<u8 *>(out)[i] = 0;
    }

    // TODO: Implement actual statistics tracking
    // For now, return zeroed stats
}

} // namespace net
