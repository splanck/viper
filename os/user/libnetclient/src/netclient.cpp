//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libnetclient/src/netclient.cpp
// Purpose: Network client library for ViperOS user-space.
// Key invariants: IPC-based communication with netd service.
// Ownership/Lifetime: Library; manages connections to netd.
// Links: user/libnetclient/include/netclient.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file netclient.cpp
 * @brief Network client library for communicating with netd.
 *
 * Provides high-level socket-like APIs that communicate with the network
 * daemon via IPC channels.
 */

#include "netclient.hpp"

namespace netclient
{

static usize bounded_strlen(const char *s, usize max_len)
{
    if (!s)
        return 0;
    usize n = 0;
    while (n < max_len && s[n])
    {
        n++;
    }
    return n;
}

static i64 recv_reply_blocking(i32 ch, void *buf, usize buf_len)
{
    while (true)
    {
        u32 handles[4];
        u32 handle_count = 4;
        i64 n = sys::channel_recv(ch, buf, buf_len, handles, &handle_count);
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        if (n >= 0 && handle_count != 0)
        {
            // netclient currently only supports inline replies. Close any unexpected
            // transferred handles to avoid capability table exhaustion.
            for (u32 i = 0; i < handle_count; i++)
            {
                if (handles[i] == 0)
                    continue;
                i32 close_err = sys::shm_close(handles[i]);
                if (close_err != 0)
                {
                    (void)sys::cap_revoke(handles[i]);
                }
            }
            return VERR_NOT_SUPPORTED;
        }
        return n;
    }
}

i32 Client::connect()
{
    if (channel_ >= 0)
    {
        return 0;
    }

    u32 handle = 0;
    i32 err = sys::assign_get("NETD", &handle);
    if (err != 0)
    {
        return err;
    }

    channel_ = static_cast<i32>(handle);
    return 0;
}

i32 Client::ensure_events()
{
    if (event_channel_recv_ >= 0)
    {
        return 0;
    }

    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    auto ev_ch = sys::channel_create();
    if (!ev_ch.ok())
    {
        return static_cast<i32>(ev_ch.error);
    }
    i32 event_send = static_cast<i32>(ev_ch.val0);
    i32 event_recv = static_cast<i32>(ev_ch.val1);

    auto reply_ch = sys::channel_create();
    if (!reply_ch.ok())
    {
        sys::channel_close(event_send);
        sys::channel_close(event_recv);
        return static_cast<i32>(reply_ch.error);
    }
    i32 reply_send = static_cast<i32>(reply_ch.val0);
    i32 reply_recv = static_cast<i32>(reply_ch.val1);

    netproto::SubscribeEventsRequest req = {};
    req.type = netproto::NET_SUBSCRIBE_EVENTS;
    req.request_id = next_request_id_++;

    u32 send_handles[2] = {static_cast<u32>(reply_send), static_cast<u32>(event_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 2);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        sys::channel_close(event_send);
        sys::channel_close(event_recv);
        return static_cast<i32>(send_err);
    }

    netproto::SubscribeEventsReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        sys::channel_close(event_recv);
        return static_cast<i32>(n);
    }

    if (reply.status != 0)
    {
        sys::channel_close(event_recv);
        return reply.status;
    }

    event_channel_recv_ = event_recv;
    return 0;
}

i32 Client::socket_create(u16 family, u16 sock_type, u32 protocol, u32 *out_socket_id)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    netproto::SocketCreateRequest req = {};
    req.type = netproto::NET_SOCKET_CREATE;
    req.request_id = next_request_id_++;
    req.family = family;
    req.sock_type = sock_type;
    req.protocol = protocol;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    netproto::SocketCreateReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    if (reply.status == 0 && out_socket_id)
    {
        *out_socket_id = reply.socket_id;
    }
    return reply.status;
}

i32 Client::socket_connect(u32 socket_id, u32 ip_be, u16 port_be)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    netproto::SocketConnectRequest req = {};
    req.type = netproto::NET_SOCKET_CONNECT;
    req.request_id = next_request_id_++;
    req.socket_id = socket_id;
    req.ip = ip_be;
    req.port = port_be;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    netproto::SocketConnectReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    return reply.status;
}

i64 Client::socket_send(u32 socket_id, const void *data, u32 len)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    netproto::SocketSendRequest req = {};
    req.type = netproto::NET_SOCKET_SEND;
    req.request_id = next_request_id_++;
    req.socket_id = socket_id;
    req.len = len;
    req.flags = 0;

    u32 shm_handle = 0;
    u64 shm_virt = 0;
    bool mapped = false;

    if (len <= sizeof(req.data))
    {
        const u8 *src = static_cast<const u8 *>(data);
        for (u32 i = 0; i < len; i++)
        {
            req.data[i] = src[i];
        }
    }
    else
    {
        auto shm = sys::shm_create(len);
        if (shm.error != 0)
        {
            return shm.error;
        }
        shm_handle = shm.handle;
        shm_virt = shm.virt_addr;
        mapped = true;

        u8 *dst = reinterpret_cast<u8 *>(shm_virt);
        const u8 *src = static_cast<const u8 *>(data);
        for (u32 i = 0; i < len; i++)
        {
            dst[i] = src[i];
        }
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        if (mapped)
        {
            (void)sys::shm_unmap(shm_virt);
            (void)sys::shm_close(shm_handle);
        }
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[2] = {static_cast<u32>(reply_send), shm_handle};
    u32 send_handle_count = mapped ? 2u : 1u;
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, send_handle_count);

    if (mapped)
    {
        (void)sys::shm_unmap(shm_virt);
    }

    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        if (mapped)
        {
            // Handle was not transferred; close it explicitly.
            (void)sys::shm_close(shm_handle);
        }
        return send_err;
    }

    netproto::SocketSendReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return n;
    }

    if (reply.status != 0)
    {
        return reply.status;
    }
    return static_cast<i64>(reply.bytes_sent);
}

i64 Client::socket_recv(u32 socket_id, void *buf, u32 max_len)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    netproto::SocketRecvRequest req = {};
    req.type = netproto::NET_SOCKET_RECV;
    req.request_id = next_request_id_++;
    req.socket_id = socket_id;
    req.max_len = max_len;
    req.flags = 0;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return send_err;
    }

    netproto::SocketRecvReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return n;
    }

    if (reply.status != 0)
    {
        return reply.status;
    }

    u32 to_copy = reply.bytes_recv;
    if (to_copy > max_len)
    {
        to_copy = max_len;
    }

    u8 *dst = static_cast<u8 *>(buf);
    for (u32 i = 0; i < to_copy; i++)
    {
        dst[i] = reply.data[i];
    }

    return static_cast<i64>(to_copy);
}

i32 Client::socket_close(u32 socket_id)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    netproto::SocketCloseRequest req = {};
    req.type = netproto::NET_SOCKET_CLOSE;
    req.request_id = next_request_id_++;
    req.socket_id = socket_id;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    netproto::SocketCloseReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    return reply.status;
}

i32 Client::socket_status(u32 socket_id, u32 *out_flags, u32 *out_rx_available)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    if (!out_flags || !out_rx_available)
    {
        return VERR_INVALID_ARG;
    }

    netproto::SocketStatusRequest req = {};
    req.type = netproto::NET_SOCKET_STATUS;
    req.request_id = next_request_id_++;
    req.socket_id = socket_id;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    netproto::SocketStatusReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    if (reply.status != 0)
    {
        return reply.status;
    }

    *out_flags = reply.flags;
    *out_rx_available = reply.rx_available;
    return 0;
}

i32 Client::dns_resolve(const char *hostname, u32 *out_ip_be)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }
    if (!hostname || !out_ip_be)
    {
        return VERR_INVALID_ARG;
    }

    usize name_len = bounded_strlen(hostname, sizeof(netproto::DnsResolveRequest::hostname));
    if (name_len == 0 || name_len >= sizeof(netproto::DnsResolveRequest::hostname))
    {
        return VERR_INVALID_ARG;
    }

    netproto::DnsResolveRequest req = {};
    req.type = netproto::NET_DNS_RESOLVE;
    req.request_id = next_request_id_++;
    req.hostname_len = static_cast<u16>(name_len);
    for (usize i = 0; i < name_len; i++)
    {
        req.hostname[i] = hostname[i];
    }
    req.hostname[name_len] = '\0';

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};

    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    netproto::DnsResolveReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    if (reply.status != 0)
    {
        return reply.status;
    }

    *out_ip_be = reply.ip;
    return 0;
}

} // namespace netclient
