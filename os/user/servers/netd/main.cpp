/**
 * @file main.cpp
 * @brief Network server (netd) main entry point.
 *
 * @details
 * This server provides network services to other user-space processes
 * via IPC. It:
 * - Finds and initializes a VirtIO-net device
 * - Creates a user-space TCP/IP stack
 * - Creates a service channel
 * - Registers with the assign system as "NETD:"
 * - Handles socket, DNS, and diagnostic requests
 */

#include "../../libvirtio/include/device.hpp"
#include "../../libvirtio/include/net.hpp"
#include "../../syscall.hpp"
#include "net_protocol.hpp"
#include "netstack.hpp"

// Debug output helper
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_hex(u64 val)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

static void debug_print_dec(u64 val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

static void debug_print_ip(u32 ip)
{
    debug_print_dec((ip >> 24) & 0xFF);
    debug_print(".");
    debug_print_dec((ip >> 16) & 0xFF);
    debug_print(".");
    debug_print_dec((ip >> 8) & 0xFF);
    debug_print(".");
    debug_print_dec(ip & 0xFF);
}

// Global state
static virtio::NetDevice g_device;
static netstack::NetworkStack g_stack;
static i32 g_service_channel = -1;
static i32 g_event_channel_send = -1;
static bool g_event_rx_signaled = false;

// QEMU virt machine VirtIO IRQ base
constexpr u32 VIRTIO_IRQ_BASE = 48;

static void recv_bootstrap_caps()
{
    // If this process was spawned by vinit, handle 0 is expected to be a
    // bootstrap channel recv endpoint used for initial capability delegation.
    constexpr i32 BOOTSTRAP_RECV = 0;

    u8 dummy[1];
    u32 handles[4];
    u32 handle_count = 4;

    for (u32 i = 0; i < 2000; i++)
    {
        handle_count = 4;
        i64 n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0)
        {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        return;
    }
}

/**
 * @brief Find VirtIO-net device in the system.
 *
 * @param mmio_phys Output: physical MMIO address.
 * @param irq Output: IRQ number.
 * @return true if found.
 */
static bool find_net_device(u64 *mmio_phys, u32 *irq)
{
    // Scan VirtIO MMIO range for network device
    constexpr u64 VIRTIO_BASE = 0x0a000000;
    constexpr u64 VIRTIO_END = 0x0a004000;
    constexpr u64 VIRTIO_STRIDE = 0x200;

    for (u64 addr = VIRTIO_BASE; addr < VIRTIO_END; addr += VIRTIO_STRIDE)
    {
        // Map the device temporarily to check type
        u64 virt = device::map_device(addr, VIRTIO_STRIDE);
        if (virt == 0)
        {
            continue;
        }

        volatile u32 *mmio = reinterpret_cast<volatile u32 *>(virt);

        // Check magic
        u32 magic = mmio[0];     // MAGIC at offset 0
        if (magic != 0x74726976) // "virt"
        {
            continue;
        }

        // Check device type (offset 0x008)
        u32 device_id = mmio[2]; // DEVICE_ID at offset 8
        if (device_id == virtio::device_type::NET)
        {
            // Skip devices already configured (e.g., claimed by the kernel)
            u32 status = mmio[virtio::reg::STATUS / 4];
            if (status != 0)
            {
                continue;
            }

            *mmio_phys = addr;
            *irq = VIRTIO_IRQ_BASE + static_cast<u32>((addr - VIRTIO_BASE) / VIRTIO_STRIDE);
            return true;
        }
    }

    return false;
}

// Simple memory copy
static void memcpy_bytes(void *dst, const void *src, usize n)
{
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
}

static void maybe_notify_rx()
{
    if (g_event_channel_send < 0)
    {
        return;
    }

    bool any_readable = g_stack.any_socket_readable();
    if (!any_readable)
    {
        g_event_rx_signaled = false;
        return;
    }

    if (g_event_rx_signaled)
    {
        return;
    }

    u8 one = 1;
    i64 rc = sys::channel_send(g_event_channel_send, &one, sizeof(one), nullptr, 0);
    if (rc == 0 || rc == VERR_WOULD_BLOCK)
    {
        // If it would block, the channel is full and therefore already readable.
        g_event_rx_signaled = true;
        return;
    }

    // Event channel became invalid; stop trying to use it.
    (void)sys::channel_close(g_event_channel_send);
    g_event_channel_send = -1;
    g_event_rx_signaled = false;
}

// =============================================================================
// Request Handlers
// =============================================================================

/**
 * @brief Handle NET_SOCKET_CREATE request.
 */
static void handle_socket_create(const netproto::SocketCreateRequest *req, i32 reply_channel)
{
    netproto::SocketCreateReply reply;
    reply.type = netproto::NET_SOCKET_CREATE_REPLY;
    reply.request_id = req->request_id;

    // Map socket type
    u16 sock_type = 0;
    if (req->sock_type == netproto::SOCK_STREAM)
    {
        sock_type = 1; // TCP
    }
    else if (req->sock_type == netproto::SOCK_DGRAM)
    {
        sock_type = 2; // UDP
    }
    else
    {
        reply.status = VERR_NOT_SUPPORTED;
        reply.socket_id = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    i32 sock_id = g_stack.socket_create(sock_type);
    if (sock_id < 0)
    {
        reply.status = sock_id;
        reply.socket_id = 0;
    }
    else
    {
        reply.status = 0;
        reply.socket_id = static_cast<u32>(sock_id);
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_CONNECT request.
 */
static void handle_socket_connect(const netproto::SocketConnectRequest *req, i32 reply_channel)
{
    netproto::SocketConnectReply reply;
    reply.type = netproto::NET_SOCKET_CONNECT_REPLY;
    reply.request_id = req->request_id;

    // Convert IP from network byte order to our format
    netstack::Ipv4Addr ip = netstack::Ipv4Addr::from_u32(netstack::ntohl(req->ip));
    u16 port = netstack::ntohs(req->port);

    i32 result = g_stack.socket_connect(req->socket_id, ip, port);
    reply.status = result;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_BIND request.
 */
static void handle_socket_bind(const netproto::SocketBindRequest *req, i32 reply_channel)
{
    netproto::SocketBindReply reply;
    reply.type = netproto::NET_SOCKET_BIND_REPLY;
    reply.request_id = req->request_id;

    u16 port = netstack::ntohs(req->port);
    i32 result = g_stack.socket_bind(req->socket_id, port);
    reply.status = result;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_LISTEN request.
 */
static void handle_socket_listen(const netproto::SocketListenRequest *req, i32 reply_channel)
{
    netproto::SocketListenReply reply;
    reply.type = netproto::NET_SOCKET_LISTEN_REPLY;
    reply.request_id = req->request_id;

    i32 result = g_stack.socket_listen(req->socket_id, req->backlog);
    reply.status = result;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_ACCEPT request.
 */
static void handle_socket_accept(const netproto::SocketAcceptRequest *req, i32 reply_channel)
{
    netproto::SocketAcceptReply reply;
    reply.type = netproto::NET_SOCKET_ACCEPT_REPLY;
    reply.request_id = req->request_id;

    netstack::Ipv4Addr remote_ip;
    u16 remote_port = 0;

    i32 result = g_stack.socket_accept(req->socket_id, &remote_ip, &remote_port);
    if (result < 0)
    {
        reply.status = result;
        reply.new_socket_id = 0;
        reply.remote_ip = 0;
        reply.remote_port = 0;
    }
    else
    {
        reply.status = 0;
        reply.new_socket_id = static_cast<u32>(result);
        reply.remote_ip = netstack::htonl(remote_ip.to_u32());
        reply.remote_port = netstack::htons(remote_port);
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_SEND request.
 */
static void handle_socket_send(const netproto::SocketSendRequest *req,
                               i32 reply_channel,
                               u32 shm_handle)
{
    netproto::SocketSendReply reply;
    reply.type = netproto::NET_SOCKET_SEND_REPLY;
    reply.request_id = req->request_id;

    const void *data = nullptr;
    usize len = req->len;
    u64 shm_virt = 0;

    if (len <= sizeof(req->data))
    {
        // Inline data
        data = req->data;
    }
    else if (shm_handle != 0)
    {
        // Shared memory data
        auto shm_result = sys::shm_map(shm_handle);
        if (shm_result.error != 0)
        {
            reply.status = static_cast<i32>(shm_result.error);
            reply.bytes_sent = 0;
            sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
            return;
        }
        shm_virt = shm_result.virt_addr;
        data = reinterpret_cast<const void *>(shm_virt);
    }
    else
    {
        reply.status = VERR_INVALID_ARG;
        reply.bytes_sent = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    i32 result = g_stack.socket_send(req->socket_id, data, len);
    if (result < 0)
    {
        reply.status = result;
        reply.bytes_sent = 0;
    }
    else
    {
        reply.status = 0;
        reply.bytes_sent = static_cast<u32>(result);
    }

    if (shm_virt != 0)
    {
        sys::shm_unmap(shm_virt);
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_SOCKET_RECV request.
 */
static void handle_socket_recv(const netproto::SocketRecvRequest *req, i32 reply_channel)
{
    netproto::SocketRecvReply reply;
    reply.type = netproto::NET_SOCKET_RECV_REPLY;
    reply.request_id = req->request_id;

    // For simplicity, we only support inline data for now (up to 200 bytes)
    u8 buf[200];
    usize max_len = req->max_len;
    if (max_len > sizeof(buf))
    {
        max_len = sizeof(buf);
    }

    i32 result = g_stack.socket_recv(req->socket_id, buf, max_len);
    if (result < 0)
    {
        reply.status = result;
        reply.bytes_recv = 0;
    }
    else
    {
        reply.status = 0;
        reply.bytes_recv = static_cast<u32>(result);
        memcpy_bytes(reply.data, buf, static_cast<usize>(result));
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_socket_status(const netproto::SocketStatusRequest *req, i32 reply_channel)
{
    netproto::SocketStatusReply reply = {};
    reply.type = netproto::NET_SOCKET_STATUS_REPLY;
    reply.request_id = req->request_id;

    u32 flags = 0;
    u32 rx_avail = 0;
    i32 rc = g_stack.socket_status(req->socket_id, &flags, &rx_avail);
    if (rc != 0)
    {
        reply.status = rc;
        reply.flags = 0;
        reply.rx_available = 0;
    }
    else
    {
        reply.status = 0;
        reply.flags = flags;
        reply.rx_available = rx_avail;
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static u32 handle_subscribe_events(const netproto::SubscribeEventsRequest *req,
                                  i32 reply_channel,
                                  const u32 *handles,
                                  u32 handle_count)
{
    netproto::SubscribeEventsReply reply = {};
    reply.type = netproto::NET_SUBSCRIBE_EVENTS_REPLY;
    reply.request_id = req->request_id;

    if (!handles || handle_count < 1 || handles[0] == 0)
    {
        reply.status = VERR_INVALID_ARG;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return 0;
    }

    // Replace any existing subscriber.
    if (g_event_channel_send >= 0)
    {
        (void)sys::channel_close(g_event_channel_send);
    }

    g_event_channel_send = static_cast<i32>(handles[0]);
    g_event_rx_signaled = false;

    reply.status = 0;
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);

    // Keep handles[0] (the transferred event channel send endpoint).
    return 1u << 0;
}

/**
 * @brief Handle NET_SOCKET_CLOSE request.
 */
static void handle_socket_close(const netproto::SocketCloseRequest *req, i32 reply_channel)
{
    netproto::SocketCloseReply reply;
    reply.type = netproto::NET_SOCKET_CLOSE_REPLY;
    reply.request_id = req->request_id;

    i32 result = g_stack.socket_close(req->socket_id);
    reply.status = result;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_DNS_RESOLVE request.
 */
static void handle_dns_resolve(const netproto::DnsResolveRequest *req, i32 reply_channel)
{
    netproto::DnsResolveReply reply;
    reply.type = netproto::NET_DNS_RESOLVE_REPLY;
    reply.request_id = req->request_id;

    netstack::Ipv4Addr resolved;
    i32 result = g_stack.dns_resolve(req->hostname, &resolved);
    if (result < 0)
    {
        reply.status = result;
        reply.ip = 0;
    }
    else
    {
        reply.status = 0;
        reply.ip = netstack::htonl(resolved.to_u32());
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_PING request.
 */
static void handle_ping(const netproto::PingRequest *req, i32 reply_channel)
{
    netproto::PingReply reply;
    reply.type = netproto::NET_PING_REPLY;
    reply.request_id = req->request_id;

    netstack::Ipv4Addr ip = netstack::Ipv4Addr::from_u32(netstack::ntohl(req->ip));
    i32 result = g_stack.ping(ip, req->timeout_ms);
    if (result < 0)
    {
        reply.status = result;
        reply.rtt_us = 0;
    }
    else
    {
        reply.status = 0;
        reply.rtt_us = static_cast<u32>(result);
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_INFO request.
 */
static void handle_info(const netproto::InfoRequest *req, i32 reply_channel)
{
    netproto::InfoReply reply;
    reply.type = netproto::NET_INFO_REPLY;
    reply.request_id = req->request_id;
    reply.status = 0;

    // Get MAC
    u8 mac[6];
    g_device.get_mac(mac);
    for (int i = 0; i < 6; i++)
    {
        reply.mac[i] = mac[i];
    }

    // Get IP config
    reply.ip = netstack::htonl(g_stack.netif()->ip().to_u32());
    reply.netmask = netstack::htonl(g_stack.netif()->netmask().to_u32());
    reply.gateway = netstack::htonl(g_stack.netif()->gateway().to_u32());
    reply.dns = netstack::htonl(g_stack.netif()->dns().to_u32());

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle NET_STATS request.
 */
static void handle_stats(const netproto::StatsRequest *req, i32 reply_channel)
{
    netproto::StatsReply reply;
    reply.type = netproto::NET_STATS_REPLY;
    reply.request_id = req->request_id;
    reply.status = 0;

    reply.tx_packets = g_stack.tx_packets();
    reply.rx_packets = g_stack.rx_packets();
    reply.tx_bytes = g_stack.tx_bytes();
    reply.rx_bytes = g_stack.rx_bytes();
    reply.tx_dropped = 0;
    reply.rx_dropped = 0;
    reply.tcp_conns = g_stack.tcp_conn_count();
    reply.udp_sockets = g_stack.udp_sock_count();

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle incoming request.
 */
static u32 handle_request(const u8 *msg,
                          usize len,
                          i32 reply_channel,
                          const u32 *handles,
                          u32 handle_count)
{
    if (len < 4)
    {
        return 0;
    }

    u32 type = *reinterpret_cast<const u32 *>(msg);

    switch (type)
    {
        case netproto::NET_SOCKET_CREATE:
            if (len >= sizeof(netproto::SocketCreateRequest))
            {
                handle_socket_create(reinterpret_cast<const netproto::SocketCreateRequest *>(msg),
                                     reply_channel);
            }
            break;

        case netproto::NET_SOCKET_CONNECT:
            if (len >= sizeof(netproto::SocketConnectRequest))
            {
                handle_socket_connect(reinterpret_cast<const netproto::SocketConnectRequest *>(msg),
                                      reply_channel);
            }
            break;

        case netproto::NET_SOCKET_BIND:
            if (len >= sizeof(netproto::SocketBindRequest))
            {
                handle_socket_bind(reinterpret_cast<const netproto::SocketBindRequest *>(msg),
                                   reply_channel);
            }
            break;

        case netproto::NET_SOCKET_LISTEN:
            if (len >= sizeof(netproto::SocketListenRequest))
            {
                handle_socket_listen(reinterpret_cast<const netproto::SocketListenRequest *>(msg),
                                     reply_channel);
            }
            break;

        case netproto::NET_SOCKET_ACCEPT:
            if (len >= sizeof(netproto::SocketAcceptRequest))
            {
                handle_socket_accept(reinterpret_cast<const netproto::SocketAcceptRequest *>(msg),
                                     reply_channel);
            }
            break;

        case netproto::NET_SOCKET_SEND:
            if (len >= sizeof(netproto::SocketSendRequest))
            {
                u32 shm_handle = (handles && handle_count >= 1) ? handles[0] : 0;
                handle_socket_send(reinterpret_cast<const netproto::SocketSendRequest *>(msg),
                                   reply_channel,
                                   shm_handle);
            }
            break;

        case netproto::NET_SOCKET_RECV:
            if (len >= sizeof(netproto::SocketRecvRequest))
            {
                handle_socket_recv(reinterpret_cast<const netproto::SocketRecvRequest *>(msg),
                                   reply_channel);
            }
            break;

        case netproto::NET_SOCKET_CLOSE:
            if (len >= sizeof(netproto::SocketCloseRequest))
            {
                handle_socket_close(reinterpret_cast<const netproto::SocketCloseRequest *>(msg),
                                    reply_channel);
            }
            break;

        case netproto::NET_SOCKET_STATUS:
            if (len >= sizeof(netproto::SocketStatusRequest))
            {
                handle_socket_status(reinterpret_cast<const netproto::SocketStatusRequest *>(msg),
                                     reply_channel);
            }
            break;

        case netproto::NET_DNS_RESOLVE:
            if (len >= sizeof(netproto::DnsResolveRequest))
            {
                handle_dns_resolve(reinterpret_cast<const netproto::DnsResolveRequest *>(msg),
                                   reply_channel);
            }
            break;

        case netproto::NET_PING:
            if (len >= sizeof(netproto::PingRequest))
            {
                handle_ping(reinterpret_cast<const netproto::PingRequest *>(msg), reply_channel);
            }
            break;

        case netproto::NET_INFO:
            if (len >= sizeof(netproto::InfoRequest))
            {
                handle_info(reinterpret_cast<const netproto::InfoRequest *>(msg), reply_channel);
            }
            break;

        case netproto::NET_STATS:
            if (len >= sizeof(netproto::StatsRequest))
            {
                handle_stats(reinterpret_cast<const netproto::StatsRequest *>(msg), reply_channel);
            }
            break;

        case netproto::NET_SUBSCRIBE_EVENTS:
            if (len >= sizeof(netproto::SubscribeEventsRequest))
            {
                return handle_subscribe_events(
                    reinterpret_cast<const netproto::SubscribeEventsRequest *>(msg),
                    reply_channel,
                    handles,
                    handle_count);
            }
            break;

        default:
            debug_print("[netd] Unknown request type: ");
            debug_print_dec(type);
            debug_print("\n");
            break;
    }

    return 0;
}

/**
 * @brief Server main loop.
 */
static void server_loop()
{
    debug_print("[netd] Entering server loop\n");

    while (true)
    {
        // Poll for incoming packets
        g_stack.poll();
        maybe_notify_rx();

        // Receive IPC message (non-blocking)
        u8 msg_buf[512];
        u32 handles[4];
        u32 handle_count = 4;

        i64 len =
            sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);
        if (len < 0)
        {
            // Would block or error, yield and retry
            sys::yield();
            continue;
        }

        // First handle should be the reply channel
        if (handle_count < 1)
        {
            debug_print("[netd] No reply channel in request\n");
            continue;
        }

        i32 reply_channel = static_cast<i32>(handles[0]);
        const u32 *extra_handles = (handle_count >= 2) ? &handles[1] : nullptr;
        u32 extra_count = (handle_count >= 2) ? (handle_count - 1) : 0;

        // Handle the request
        u32 keep_mask = handle_request(msg_buf,
                                       static_cast<usize>(len),
                                       reply_channel,
                                       extra_handles,
                                       extra_count);

        // Close the reply channel
        sys::channel_close(reply_channel);

        // Close any additional transferred handles (e.g., send payload SHM).
        for (u32 i = 0; i < extra_count; i++)
        {
            if (!extra_handles || extra_handles[i] == 0)
                continue;
            if (keep_mask & (1u << i))
                continue;

            i32 close_err = sys::shm_close(extra_handles[i]);
            if (close_err != 0)
            {
                // Best-effort fallback: at least drop the handle to avoid cap table exhaustion.
                (void)sys::cap_revoke(extra_handles[i]);
            }
        }
    }
}

/**
 * @brief Main entry point.
 */
extern "C" void _start()
{
    debug_print("[netd] Network server starting\n");
    recv_bootstrap_caps();

    // Find VirtIO-net device
    u64 mmio_phys = 0;
    u32 irq = 0;
    if (!find_net_device(&mmio_phys, &irq))
    {
        debug_print("[netd] No VirtIO-net device found\n");
        sys::exit(1);
    }

    debug_print("[netd] Found device at ");
    debug_print_hex(mmio_phys);
    debug_print(" IRQ ");
    debug_print_dec(irq);
    debug_print("\n");

    // Initialize the device
    if (!g_device.init(mmio_phys, irq))
    {
        debug_print("[netd] Device init failed\n");
        sys::exit(1);
    }

    // Print MAC
    u8 mac[6];
    g_device.get_mac(mac);
    debug_print("[netd] MAC: ");
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
            debug_print(":");
        debug_print_hex(mac[i] >> 4);
        char lo = "0123456789abcdef"[mac[i] & 0xF];
        char buf[2] = {lo, '\0'};
        sys::print(buf);
    }
    debug_print("\n");

    // Initialize network stack
    if (!g_stack.init(&g_device))
    {
        debug_print("[netd] Stack init failed\n");
        sys::exit(1);
    }

    // Print configured IP
    debug_print("[netd] IP: ");
    debug_print_ip(g_stack.netif()->ip().to_u32());
    debug_print("\n");

    // Create service channel
    auto result = sys::channel_create();
    if (result.error != 0)
    {
        debug_print("[netd] Failed to create channel\n");
        sys::exit(1);
    }
    i32 send_ep = static_cast<i32>(result.val0);
    i32 recv_ep = static_cast<i32>(result.val1);
    // Server only needs the receive endpoint.
    sys::channel_close(send_ep);
    g_service_channel = recv_ep;

    debug_print("[netd] Service channel created: ");
    debug_print_dec(static_cast<u64>(g_service_channel));
    debug_print("\n");

    // Register with assign system
    i32 err = sys::assign_set("NETD", static_cast<u32>(g_service_channel));
    if (err != 0)
    {
        debug_print("[netd] Failed to register assign: ");
        debug_print_dec(static_cast<u64>(-err));
        debug_print("\n");
        // Continue anyway
    }
    else
    {
        debug_print("[netd] Registered as NETD:\n");
    }

    // Enter the server loop
    server_loop();

    // Should never reach here
    sys::exit(0);
}
