/**
 * @file netstack.cpp
 * @brief Simplified user-space network stack implementation.
 */

#include "netstack.hpp"

namespace netstack
{

// Simple memory operations
static void memcpy_net(void *dst, const void *src, usize n)
{
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
}

// =============================================================================
// NetIf Implementation
// =============================================================================

void NetIf::init(virtio::NetDevice *dev)
{
    dev_ = dev;
    dev->get_mac(mac_.bytes);

    // Default QEMU user-mode networking config
    ip_ = {{10, 0, 2, 15}};
    netmask_ = {{255, 255, 255, 0}};
    gateway_ = {{10, 0, 2, 2}};
    dns_ = {{10, 0, 2, 3}};
}

// =============================================================================
// ARP Cache Implementation
// =============================================================================

void ArpCache::init(NetIf *netif)
{
    netif_ = netif;
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        entries_[i].valid = false;
    }
}

MacAddr ArpCache::lookup(const Ipv4Addr &ip)
{
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (entries_[i].valid && entries_[i].ip == ip)
        {
            return entries_[i].mac;
        }
    }
    return MacAddr::zero();
}

void ArpCache::add(const Ipv4Addr &ip, const MacAddr &mac)
{
    // Look for existing entry
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (entries_[i].valid && entries_[i].ip == ip)
        {
            entries_[i].mac = mac;
            return;
        }
    }

    // Find empty slot
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (!entries_[i].valid)
        {
            entries_[i].ip = ip;
            entries_[i].mac = mac;
            entries_[i].valid = true;
            return;
        }
    }

    // Overwrite first entry if full
    entries_[0].ip = ip;
    entries_[0].mac = mac;
}

void ArpCache::send_request(const Ipv4Addr &ip)
{
    u8 frame[64];
    EthHeader *eth = reinterpret_cast<EthHeader *>(frame);
    ArpHeader *arp = reinterpret_cast<ArpHeader *>(frame + sizeof(EthHeader));

    // Ethernet header
    eth->dst = MacAddr::broadcast();
    eth->src = netif_->mac();
    eth->ethertype = htons(ETH_TYPE_ARP);

    // ARP request
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

void ArpCache::handle_arp(const ArpHeader *arp, usize len)
{
    (void)len;

    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET)
        return;
    if (ntohs(arp->proto_type) != ETH_TYPE_IPV4)
        return;

    u16 op = ntohs(arp->operation);

    // Always learn from ARP packets
    add(arp->sender_ip, arp->sender_mac);

    if (op == ARP_OP_REQUEST)
    {
        // If they're asking for our IP, reply
        if (arp->target_ip == netif_->ip())
        {
            u8 frame[64];
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
}

// =============================================================================
// NetworkStack Implementation
// =============================================================================

bool NetworkStack::init(virtio::NetDevice *dev)
{
    netif_.init(dev);
    arp_.init(&netif_);

    // Initialize connections/sockets
    for (usize i = 0; i < MAX_TCP_CONNS; i++)
    {
        tcp_conns_[i].in_use = false;
    }
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        udp_sockets_[i].in_use = false;
    }

    return true;
}

void NetworkStack::poll()
{
    netif_.device()->poll_rx();

    u8 buf[2048];
    while (true)
    {
        i32 len = netif_.device()->receive(buf, sizeof(buf));
        if (len <= 0)
            break;

        process_frame(buf, static_cast<usize>(len));
    }
}

void NetworkStack::process_frame(const u8 *data, usize len)
{
    if (len < sizeof(EthHeader))
        return;

    rx_packets_++;
    rx_bytes_ += len;

    const EthHeader *eth = reinterpret_cast<const EthHeader *>(data);
    u16 ethertype = ntohs(eth->ethertype);

    const u8 *payload = data + sizeof(EthHeader);
    usize payload_len = len - sizeof(EthHeader);

    switch (ethertype)
    {
        case ETH_TYPE_ARP:
            handle_arp(payload, payload_len);
            break;
        case ETH_TYPE_IPV4:
            handle_ipv4(payload, payload_len);
            break;
    }
}

void NetworkStack::handle_arp(const u8 *data, usize len)
{
    if (len < sizeof(ArpHeader))
        return;
    arp_.handle_arp(reinterpret_cast<const ArpHeader *>(data), len);
}

void NetworkStack::handle_ipv4(const u8 *data, usize len)
{
    if (len < sizeof(Ipv4Header))
        return;

    const Ipv4Header *ip = reinterpret_cast<const Ipv4Header *>(data);

    // Verify version
    if ((ip->version_ihl >> 4) != 4)
        return;

    // Check if it's for us
    if (ip->dst != netif_.ip() && !ip->dst.is_broadcast())
        return;

    u8 ihl = (ip->version_ihl & 0x0f) * 4;
    const u8 *payload = data + ihl;
    usize payload_len = ntohs(ip->total_len) - ihl;

    switch (ip->protocol)
    {
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

void NetworkStack::handle_icmp(const Ipv4Header *ip, const u8 *data, usize len)
{
    if (len < sizeof(IcmpHeader))
        return;

    const IcmpHeader *icmp = reinterpret_cast<const IcmpHeader *>(data);

    if (icmp->type == ICMP_ECHO_REQUEST)
    {
        // Send echo reply
        u8 reply[64];
        IcmpHeader *reply_icmp = reinterpret_cast<IcmpHeader *>(reply);

        reply_icmp->type = ICMP_ECHO_REPLY;
        reply_icmp->code = 0;
        reply_icmp->id = icmp->id;
        reply_icmp->seq = icmp->seq;
        reply_icmp->checksum = 0;

        // Copy any data after header
        usize data_len = len - sizeof(IcmpHeader);
        if (data_len > sizeof(reply) - sizeof(IcmpHeader))
            data_len = sizeof(reply) - sizeof(IcmpHeader);
        memcpy_net(reply + sizeof(IcmpHeader), data + sizeof(IcmpHeader), data_len);

        reply_icmp->checksum = checksum(reply, sizeof(IcmpHeader) + data_len);

        send_ip_packet(ip->src, IP_PROTO_ICMP, reply, sizeof(IcmpHeader) + data_len);
    }
    else if (icmp->type == ICMP_ECHO_REPLY)
    {
        // Check if we're waiting for this
        if (icmp_pending_)
        {
            icmp_received_ = true;
            icmp_pending_ = false;
        }
    }
}

void NetworkStack::handle_udp(const Ipv4Header *ip, const u8 *data, usize len)
{
    if (len < sizeof(UdpHeader))
        return;

    const UdpHeader *udp = reinterpret_cast<const UdpHeader *>(data);
    u16 dst_port = ntohs(udp->dst_port);
    u16 src_port = ntohs(udp->src_port);
    u16 udp_len = ntohs(udp->length);

    // Check for DNS reply (port 53)
    if (src_port == 53 && dns_pending_)
    {
        // Parse simple DNS response
        const u8 *dns_data = data + sizeof(UdpHeader);
        usize dns_len = udp_len - sizeof(UdpHeader);

        if (dns_len >= 12)
        {
            u16 txid = (static_cast<u16>(dns_data[0]) << 8) | dns_data[1];
            u16 flags = (static_cast<u16>(dns_data[2]) << 8) | dns_data[3];
            u16 ancount = (static_cast<u16>(dns_data[6]) << 8) | dns_data[7];

            if (txid == dns_txid_ && (flags & 0x8000) && ancount > 0)
            {
                // Skip header (12 bytes) and query (find first answer)
                usize pos = 12;
                // Skip query name
                while (pos < dns_len && dns_data[pos] != 0)
                {
                    if ((dns_data[pos] & 0xc0) == 0xc0)
                    {
                        pos += 2;
                        break;
                    }
                    pos += dns_data[pos] + 1;
                }
                if (dns_data[pos] == 0)
                    pos++;
                pos += 4; // Skip QTYPE and QCLASS

                // Now at first answer
                if (pos + 12 <= dns_len)
                {
                    // Skip name (could be pointer)
                    if ((dns_data[pos] & 0xc0) == 0xc0)
                        pos += 2;
                    else
                    {
                        while (pos < dns_len && dns_data[pos] != 0)
                            pos += dns_data[pos] + 1;
                        pos++;
                    }

                    if (pos + 10 <= dns_len)
                    {
                        u16 rtype = (static_cast<u16>(dns_data[pos]) << 8) | dns_data[pos + 1];
                        u16 rdlen = (static_cast<u16>(dns_data[pos + 8]) << 8) | dns_data[pos + 9];

                        if (rtype == 1 && rdlen == 4 && pos + 10 + 4 <= dns_len)
                        {
                            dns_result_.bytes[0] = dns_data[pos + 10];
                            dns_result_.bytes[1] = dns_data[pos + 11];
                            dns_result_.bytes[2] = dns_data[pos + 12];
                            dns_result_.bytes[3] = dns_data[pos + 13];
                            dns_pending_ = false;
                        }
                    }
                }
            }
        }
        return;
    }

    // Find matching UDP socket
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        if (udp_sockets_[i].in_use && udp_sockets_[i].local_port == dst_port)
        {
            UdpSocket *sock = &udp_sockets_[i];
            const u8 *payload = data + sizeof(UdpHeader);
            usize payload_len = udp_len - sizeof(UdpHeader);

            if (payload_len <= UdpSocket::RX_BUF_SIZE)
            {
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

void NetworkStack::handle_tcp(const Ipv4Header *ip, const u8 *data, usize len)
{
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

    // Find existing connection
    TcpConnection *conn = find_tcp_conn(ip->src, src_port, dst_port);

    if (conn)
    {
        // Handle based on state
        switch (conn->state)
        {
            case TcpState::SYN_SENT:
                if ((flags & TCP_SYN) && (flags & TCP_ACK))
                {
                    conn->rcv_nxt = seq + 1;
                    conn->snd_una = ack;
                    conn->state = TcpState::ESTABLISHED;

                    // Send ACK
                    send_tcp_segment(conn, TCP_ACK, nullptr, 0);
                }
                break;

            case TcpState::ESTABLISHED:
                if (flags & TCP_FIN)
                {
                    conn->rcv_nxt = seq + 1;
                    conn->state = TcpState::CLOSE_WAIT;
                    send_tcp_segment(conn, TCP_ACK, nullptr, 0);
                }
                else if (payload_len > 0)
                {
                    // Receive data
                    if (seq == conn->rcv_nxt)
                    {
                        usize space =
                            TcpConnection::RX_BUF_SIZE -
                            ((conn->rx_tail - conn->rx_head + TcpConnection::RX_BUF_SIZE) %
                             TcpConnection::RX_BUF_SIZE);
                        if (payload_len <= space)
                        {
                            for (usize i = 0; i < payload_len; i++)
                            {
                                conn->rx_buf[conn->rx_tail] = payload[i];
                                conn->rx_tail = (conn->rx_tail + 1) % TcpConnection::RX_BUF_SIZE;
                            }
                            conn->rcv_nxt += static_cast<u32>(payload_len);
                        }
                        send_tcp_segment(conn, TCP_ACK, nullptr, 0);
                    }
                }
                else if (flags & TCP_ACK)
                {
                    conn->snd_una = ack;
                }
                break;

            case TcpState::FIN_WAIT_1:
                if (flags & TCP_ACK)
                {
                    conn->state = TcpState::FIN_WAIT_2;
                }
                break;

            case TcpState::FIN_WAIT_2:
                if (flags & TCP_FIN)
                {
                    conn->rcv_nxt = seq + 1;
                    send_tcp_segment(conn, TCP_ACK, nullptr, 0);
                    conn->state = TcpState::TIME_WAIT;
                    // Should wait 2MSL, for now just close
                    conn->state = TcpState::CLOSED;
                    conn->in_use = false;
                }
                break;

            default:
                break;
        }
    }
    else
    {
        // Check for listening socket
        TcpConnection *listener = find_listening_socket(dst_port);
        if (listener && (flags & TCP_SYN) && !(flags & TCP_ACK))
        {
            // Add to backlog
            if (listener->backlog_count < TcpConnection::MAX_BACKLOG)
            {
                auto &pending = listener->backlog[listener->backlog_count++];
                pending.valid = true;
                pending.ip = ip->src;
                pending.port = src_port;
                pending.seq = seq;
            }
        }
    }
}

bool NetworkStack::send_frame(const MacAddr &dst, u16 ethertype, const void *data, usize len)
{
    u8 frame[1518];
    if (len + sizeof(EthHeader) > sizeof(frame))
        return false;

    EthHeader *eth = reinterpret_cast<EthHeader *>(frame);
    eth->dst = dst;
    eth->src = netif_.mac();
    eth->ethertype = htons(ethertype);

    memcpy_net(frame + sizeof(EthHeader), data, len);

    bool ok = netif_.device()->transmit(frame, sizeof(EthHeader) + len);
    if (ok)
    {
        tx_packets_++;
        tx_bytes_ += sizeof(EthHeader) + len;
    }
    return ok;
}

bool NetworkStack::send_ip_packet(const Ipv4Addr &dst, u8 protocol, const void *data, usize len)
{
    u8 packet[1500];
    if (len + sizeof(Ipv4Header) > sizeof(packet))
        return false;

    Ipv4Header *ip = reinterpret_cast<Ipv4Header *>(packet);
    ip->version_ihl = 0x45; // IPv4, 20-byte header
    ip->tos = 0;
    ip->total_len = htons(static_cast<u16>(sizeof(Ipv4Header) + len));
    ip->id = htons(ip_id_++);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src = netif_.ip();
    ip->dst = dst;
    ip->checksum = checksum(ip, sizeof(Ipv4Header));

    memcpy_net(packet + sizeof(Ipv4Header), data, len);

    // Resolve next hop
    Ipv4Addr next_hop = netif_.next_hop(dst);
    MacAddr dst_mac = arp_.lookup(next_hop);

    if (dst_mac == MacAddr::zero())
    {
        // Need ARP resolution
        arp_.send_request(next_hop);
        // In a real implementation, would queue and retry
        return false;
    }

    return send_frame(dst_mac, ETH_TYPE_IPV4, packet, sizeof(Ipv4Header) + len);
}

bool NetworkStack::send_tcp_segment(TcpConnection *conn, u8 flags, const void *data, usize len)
{
    u8 segment[1460];
    TcpHeader *tcp = reinterpret_cast<TcpHeader *>(segment);

    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq = htonl(conn->snd_nxt);
    tcp->ack = htonl(conn->rcv_nxt);
    tcp->data_offset = (5 << 4); // 20 bytes
    tcp->flags = flags;
    tcp->window = htons(conn->rcv_wnd);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (data && len > 0)
    {
        memcpy_net(segment + sizeof(TcpHeader), data, len);
    }

    // Calculate TCP checksum (pseudo header + TCP header + data)
    struct PseudoHeader
    {
        Ipv4Addr src;
        Ipv4Addr dst;
        u8 zero;
        u8 protocol;
        u16 length;
    } __attribute__((packed));

    u8 csum_buf[sizeof(PseudoHeader) + sizeof(TcpHeader) + 1460];
    PseudoHeader *pseudo = reinterpret_cast<PseudoHeader *>(csum_buf);
    pseudo->src = netif_.ip();
    pseudo->dst = conn->remote_ip;
    pseudo->zero = 0;
    pseudo->protocol = IP_PROTO_TCP;
    pseudo->length = htons(static_cast<u16>(sizeof(TcpHeader) + len));

    memcpy_net(csum_buf + sizeof(PseudoHeader), segment, sizeof(TcpHeader) + len);
    tcp->checksum = checksum(csum_buf, sizeof(PseudoHeader) + sizeof(TcpHeader) + len);

    // Update snd_nxt
    if (flags & TCP_SYN)
        conn->snd_nxt++;
    if (flags & TCP_FIN)
        conn->snd_nxt++;
    conn->snd_nxt += static_cast<u32>(len);

    return send_ip_packet(conn->remote_ip, IP_PROTO_TCP, segment, sizeof(TcpHeader) + len);
}

bool NetworkStack::send_udp_datagram(
    const Ipv4Addr &dst, u16 src_port, u16 dst_port, const void *data, usize len)
{
    u8 datagram[1472];
    if (len + sizeof(UdpHeader) > sizeof(datagram))
        return false;

    UdpHeader *udp = reinterpret_cast<UdpHeader *>(datagram);
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(static_cast<u16>(sizeof(UdpHeader) + len));
    udp->checksum = 0; // Optional for UDP over IPv4

    if (data && len > 0)
    {
        memcpy_net(datagram + sizeof(UdpHeader), data, len);
    }

    return send_ip_packet(dst, IP_PROTO_UDP, datagram, sizeof(UdpHeader) + len);
}

TcpConnection *NetworkStack::find_tcp_conn(const Ipv4Addr &remote_ip,
                                           u16 remote_port,
                                           u16 local_port)
{
    for (usize i = 0; i < MAX_TCP_CONNS; i++)
    {
        TcpConnection *c = &tcp_conns_[i];
        if (c->in_use && c->state != TcpState::LISTEN && c->local_port == local_port &&
            c->remote_port == remote_port && c->remote_ip == remote_ip)
        {
            return c;
        }
    }
    return nullptr;
}

TcpConnection *NetworkStack::find_listening_socket(u16 local_port)
{
    for (usize i = 0; i < MAX_TCP_CONNS; i++)
    {
        if (tcp_conns_[i].in_use && tcp_conns_[i].state == TcpState::LISTEN &&
            tcp_conns_[i].local_port == local_port)
        {
            return &tcp_conns_[i];
        }
    }
    return nullptr;
}

u16 NetworkStack::alloc_port()
{
    u16 port = next_ephemeral_port_++;
    if (next_ephemeral_port_ > 65534)
        next_ephemeral_port_ = 49152;
    return port;
}

// =============================================================================
// Socket API Implementation
// =============================================================================

i32 NetworkStack::socket_create(u16 type)
{
    if (type == 1) // SOCK_STREAM (TCP)
    {
        for (usize i = 0; i < MAX_TCP_CONNS; i++)
        {
            if (!tcp_conns_[i].in_use)
            {
                tcp_conns_[i].in_use = true;
                tcp_conns_[i].state = TcpState::CLOSED;
                tcp_conns_[i].local_ip = netif_.ip();
                tcp_conns_[i].local_port = 0;
                tcp_conns_[i].remote_ip = Ipv4Addr::zero();
                tcp_conns_[i].remote_port = 0;
                tcp_conns_[i].rx_head = 0;
                tcp_conns_[i].rx_tail = 0;
                tcp_conns_[i].tx_head = 0;
                tcp_conns_[i].tx_tail = 0;
                tcp_conns_[i].backlog_count = 0;
                return static_cast<i32>(i);
            }
        }
        return VERR_NO_RESOURCE;
    }
    else if (type == 2) // SOCK_DGRAM (UDP)
    {
        for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
        {
            if (!udp_sockets_[i].in_use)
            {
                udp_sockets_[i].in_use = true;
                udp_sockets_[i].local_ip = netif_.ip();
                udp_sockets_[i].local_port = 0;
                udp_sockets_[i].has_data = false;
                return static_cast<i32>(i + MAX_TCP_CONNS);
            }
        }
        return VERR_NO_RESOURCE;
    }
    return VERR_NOT_SUPPORTED;
}

i32 NetworkStack::socket_connect(u32 sock_id, const Ipv4Addr &ip, u16 port)
{
    if (sock_id >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *conn = &tcp_conns_[sock_id];
    if (!conn->in_use || conn->state != TcpState::CLOSED)
        return VERR_INVALID_HANDLE;

    conn->remote_ip = ip;
    conn->remote_port = port;
    conn->local_port = alloc_port();

    // Initialize sequence numbers
    conn->snd_una = 0x12345678; // Should be random
    conn->snd_nxt = conn->snd_una;
    conn->rcv_nxt = 0;

    // Send SYN (may fail if ARP not resolved yet)
    conn->state = TcpState::SYN_SENT;
    bool syn_sent = send_tcp_segment(conn, TCP_SYN, nullptr, 0);

    // Poll for SYN-ACK, retrying SYN if it wasn't sent (ARP resolution needed)
    int syn_retries = 0;
    for (int i = 0; i < 200; i++)
    {
        poll();
        if (conn->state == TcpState::ESTABLISHED)
            return 0;

        // Retry SYN if it wasn't sent (ARP may have resolved now)
        if (!syn_sent && syn_retries < 5)
        {
            // Reset snd_nxt to resend SYN with same sequence number
            conn->snd_nxt = conn->snd_una;
            syn_sent = send_tcp_segment(conn, TCP_SYN, nullptr, 0);
            syn_retries++;
        }

        sys::yield();
    }

    conn->state = TcpState::CLOSED;
    return VERR_TIMEOUT;
}

i32 NetworkStack::socket_bind(u32 sock_id, u16 port)
{
    if (sock_id < MAX_TCP_CONNS)
    {
        TcpConnection *conn = &tcp_conns_[sock_id];
        if (!conn->in_use)
            return VERR_INVALID_HANDLE;
        conn->local_port = port;
        return 0;
    }
    else if (sock_id < MAX_TCP_CONNS + MAX_UDP_SOCKETS)
    {
        UdpSocket *sock = &udp_sockets_[sock_id - MAX_TCP_CONNS];
        if (!sock->in_use)
            return VERR_INVALID_HANDLE;
        sock->local_port = port;
        return 0;
    }
    return VERR_INVALID_HANDLE;
}

i32 NetworkStack::socket_listen(u32 sock_id, u32 backlog)
{
    (void)backlog;
    if (sock_id >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *conn = &tcp_conns_[sock_id];
    if (!conn->in_use || conn->local_port == 0)
        return VERR_INVALID_HANDLE;

    conn->state = TcpState::LISTEN;
    conn->backlog_count = 0;
    return 0;
}

i32 NetworkStack::socket_accept(u32 sock_id, Ipv4Addr *remote_ip, u16 *remote_port)
{
    if (sock_id >= MAX_TCP_CONNS)
        return VERR_INVALID_HANDLE;

    TcpConnection *listener = &tcp_conns_[sock_id];
    if (!listener->in_use || listener->state != TcpState::LISTEN)
        return VERR_INVALID_HANDLE;

    // Check for pending connection
    if (listener->backlog_count == 0)
        return VERR_WOULD_BLOCK;

    // Get pending connection
    auto &pending = listener->backlog[0];

    // Find free connection slot
    i32 new_sock = socket_create(1);
    if (new_sock < 0)
        return new_sock;

    TcpConnection *conn = &tcp_conns_[new_sock];
    conn->remote_ip = pending.ip;
    conn->remote_port = pending.port;
    conn->local_port = listener->local_port;
    conn->snd_una = 0x87654321;
    conn->snd_nxt = conn->snd_una;
    conn->rcv_nxt = pending.seq + 1;

    // Send SYN-ACK
    conn->state = TcpState::SYN_RECEIVED;
    send_tcp_segment(conn, TCP_SYN | TCP_ACK, nullptr, 0);

    // Remove from backlog
    for (usize i = 0; i < listener->backlog_count - 1; i++)
    {
        listener->backlog[i] = listener->backlog[i + 1];
    }
    listener->backlog_count--;

    // Wait for ACK
    for (int i = 0; i < 50; i++)
    {
        poll();
        // Check if we got ACK (simplified - just check state progression)
        conn->state = TcpState::ESTABLISHED;
        break;
    }

    if (remote_ip)
        *remote_ip = conn->remote_ip;
    if (remote_port)
        *remote_port = conn->remote_port;

    return new_sock;
}

i32 NetworkStack::socket_send(u32 sock_id, const void *data, usize len)
{
    if (sock_id < MAX_TCP_CONNS)
    {
        TcpConnection *conn = &tcp_conns_[sock_id];
        if (!conn->in_use || conn->state != TcpState::ESTABLISHED)
            return VERR_CONNECTION;

        // Send data in segments
        const u8 *ptr = static_cast<const u8 *>(data);
        usize sent = 0;

        while (sent < len)
        {
            usize chunk = len - sent;
            if (chunk > 1400)
                chunk = 1400;

            send_tcp_segment(conn, TCP_ACK | TCP_PSH, ptr + sent, chunk);
            sent += chunk;
        }

        return static_cast<i32>(sent);
    }
    return VERR_INVALID_HANDLE;
}

i32 NetworkStack::socket_recv(u32 sock_id, void *buf, usize max_len)
{
    if (sock_id < MAX_TCP_CONNS)
    {
        TcpConnection *conn = &tcp_conns_[sock_id];
        if (!conn->in_use)
            return VERR_INVALID_HANDLE;

        usize available = conn->rx_available();
        if (available == 0)
        {
            // Remote FIN: readable-at-EOF.
            if (conn->state == TcpState::CLOSE_WAIT || conn->state == TcpState::CLOSED)
                return 0;
            return VERR_WOULD_BLOCK;
        }

        usize to_read = available;
        if (to_read > max_len)
            to_read = max_len;

        u8 *dst = static_cast<u8 *>(buf);
        for (usize i = 0; i < to_read; i++)
        {
            dst[i] = conn->rx_buf[conn->rx_head];
            conn->rx_head = (conn->rx_head + 1) % TcpConnection::RX_BUF_SIZE;
        }

        return static_cast<i32>(to_read);
    }
    else if (sock_id < MAX_TCP_CONNS + MAX_UDP_SOCKETS)
    {
        UdpSocket *sock = &udp_sockets_[sock_id - MAX_TCP_CONNS];
        if (!sock->in_use)
            return VERR_INVALID_HANDLE;
        if (!sock->has_data)
            return VERR_WOULD_BLOCK;

        usize to_read = sock->rx_len;
        if (to_read > max_len)
            to_read = max_len;

        memcpy_net(buf, sock->rx_buf, to_read);
        sock->has_data = false;

        return static_cast<i32>(to_read);
    }
    return VERR_INVALID_HANDLE;
}

i32 NetworkStack::socket_close(u32 sock_id)
{
    if (sock_id < MAX_TCP_CONNS)
    {
        TcpConnection *conn = &tcp_conns_[sock_id];
        if (!conn->in_use)
            return VERR_INVALID_HANDLE;

        if (conn->state == TcpState::ESTABLISHED)
        {
            // Send FIN
            conn->state = TcpState::FIN_WAIT_1;
            send_tcp_segment(conn, TCP_FIN | TCP_ACK, nullptr, 0);

            // Wait briefly for reply
            for (int i = 0; i < 20; i++)
            {
                poll();
                if (conn->state == TcpState::CLOSED)
                    break;
                sys::yield();
            }
        }

        conn->in_use = false;
        conn->state = TcpState::CLOSED;
        return 0;
    }
    else if (sock_id < MAX_TCP_CONNS + MAX_UDP_SOCKETS)
    {
        UdpSocket *sock = &udp_sockets_[sock_id - MAX_TCP_CONNS];
        if (!sock->in_use)
            return VERR_INVALID_HANDLE;
        sock->in_use = false;
        return 0;
    }
    return VERR_INVALID_HANDLE;
}

i32 NetworkStack::socket_status(u32 sock_id, u32 *out_flags, u32 *out_rx_available) const
{
    if (!out_flags || !out_rx_available)
    {
        return VERR_INVALID_ARG;
    }

    *out_flags = 0;
    *out_rx_available = 0;

    if (sock_id < MAX_TCP_CONNS)
    {
        const TcpConnection *conn = &tcp_conns_[sock_id];
        if (!conn->in_use)
        {
            return VERR_INVALID_HANDLE;
        }

        usize avail = conn->rx_available();
        if (avail > 0)
        {
            *out_flags |= SOCK_READABLE;
            if (avail > 0xFFFFFFFFu)
                avail = 0xFFFFFFFFu;
            *out_rx_available = static_cast<u32>(avail);
        }

        if (conn->state == TcpState::ESTABLISHED)
        {
            *out_flags |= SOCK_WRITABLE;
        }

        if ((conn->state == TcpState::CLOSE_WAIT || conn->state == TcpState::CLOSED) && avail == 0)
        {
            *out_flags |= SOCK_EOF;
            *out_flags |= SOCK_READABLE;
        }

        return 0;
    }

    if (sock_id < MAX_TCP_CONNS + MAX_UDP_SOCKETS)
    {
        const UdpSocket *sock = &udp_sockets_[sock_id - MAX_TCP_CONNS];
        if (!sock->in_use)
        {
            return VERR_INVALID_HANDLE;
        }

        if (sock->has_data)
        {
            *out_flags |= SOCK_READABLE;
            usize avail = sock->rx_len;
            if (avail > 0xFFFFFFFFu)
                avail = 0xFFFFFFFFu;
            *out_rx_available = static_cast<u32>(avail);
        }

        // UDP send is always "writable" in this simplified stack.
        *out_flags |= SOCK_WRITABLE;

        return 0;
    }

    return VERR_INVALID_HANDLE;
}

bool NetworkStack::any_socket_readable() const
{
    for (usize i = 0; i < MAX_TCP_CONNS; i++)
    {
        const TcpConnection *conn = &tcp_conns_[i];
        if (!conn->in_use)
            continue;
        if (conn->rx_available() > 0)
            return true;
        if (conn->state == TcpState::CLOSE_WAIT)
            return true; // readable-at-EOF
    }

    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        const UdpSocket *sock = &udp_sockets_[i];
        if (!sock->in_use)
            continue;
        if (sock->has_data)
            return true;
    }

    return false;
}

i32 NetworkStack::dns_resolve(const char *hostname, Ipv4Addr *out)
{
    // Build DNS query
    u8 query[256];
    usize pos = 0;

    // Transaction ID
    dns_txid_++;
    query[pos++] = static_cast<u8>(dns_txid_ >> 8);
    query[pos++] = static_cast<u8>(dns_txid_ & 0xff);

    // Flags: standard query
    query[pos++] = 0x01;
    query[pos++] = 0x00;

    // Questions: 1, Answers: 0, Authority: 0, Additional: 0
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
    while (*ptr)
    {
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
    query[pos++] = 0; // End of name

    // QTYPE: A (1)
    query[pos++] = 0x00;
    query[pos++] = 0x01;

    // QCLASS: IN (1)
    query[pos++] = 0x00;
    query[pos++] = 0x01;

    // Send query
    dns_pending_ = true;
    dns_result_ = Ipv4Addr::zero();

    send_udp_datagram(netif_.dns(), alloc_port(), 53, query, pos);

    // Wait for response
    for (int i = 0; i < 100; i++)
    {
        poll();
        if (!dns_pending_)
        {
            *out = dns_result_;
            return 0;
        }
        sys::yield();
    }

    dns_pending_ = false;
    return VERR_TIMEOUT;
}

i32 NetworkStack::ping(const Ipv4Addr &ip, u32 timeout_ms)
{
    (void)timeout_ms;

    // Build ICMP echo request
    u8 icmp_data[64];
    IcmpHeader *icmp = reinterpret_cast<IcmpHeader *>(icmp_data);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(0x1234);
    icmp->seq = htons(icmp_seq_++);
    icmp->checksum = 0;

    // Fill data
    for (int i = 0; i < 56; i++)
    {
        icmp_data[sizeof(IcmpHeader) + i] = static_cast<u8>(i);
    }

    icmp->checksum = checksum(icmp_data, sizeof(IcmpHeader) + 56);

    icmp_pending_ = true;
    icmp_received_ = false;

    send_ip_packet(ip, IP_PROTO_ICMP, icmp_data, sizeof(IcmpHeader) + 56);

    // Wait for reply
    for (u32 i = 0; i < 100; i++)
    {
        poll();
        if (icmp_received_)
        {
            return 0;
        }
        sys::yield();
    }

    icmp_pending_ = false;
    return VERR_TIMEOUT;
}

u32 NetworkStack::tcp_conn_count() const
{
    u32 count = 0;
    for (usize i = 0; i < MAX_TCP_CONNS; i++)
    {
        if (tcp_conns_[i].in_use && tcp_conns_[i].state == TcpState::ESTABLISHED)
            count++;
    }
    return count;
}

u32 NetworkStack::udp_sock_count() const
{
    u32 count = 0;
    for (usize i = 0; i < MAX_UDP_SOCKETS; i++)
    {
        if (udp_sockets_[i].in_use)
            count++;
    }
    return count;
}

} // namespace netstack
