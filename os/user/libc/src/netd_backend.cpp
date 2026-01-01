// libc ↔ netd bridge (Phase 5): route libc sockets/DNS to netd when present.

#include "netclient.hpp"

namespace
{

static netclient::Client &netd()
{
    static netclient::Client client;
    return client;
}

} // namespace

extern "C" int __viper_netd_is_available()
{
    if (netd().connect() != 0)
    {
        return 0;
    }
    if (netd().ensure_events() != 0)
    {
        return 0;
    }
    return 1;
}

extern "C" unsigned int __viper_netd_poll_handle()
{
    if (netd().ensure_events() != 0)
    {
        return 0xFFFFFFFFu;
    }
    i32 ch = netd().event_channel_recv();
    if (ch < 0)
    {
        return 0xFFFFFFFFu;
    }
    return static_cast<unsigned int>(ch);
}

extern "C" int __viper_netd_socket_create(int domain, int type, int protocol, int *out_socket_id)
{
    u32 id = 0;
    i32 rc = netd().socket_create(static_cast<u16>(domain), static_cast<u16>(type), (u32)protocol, &id);
    if (rc != 0)
    {
        return rc;
    }
    if (out_socket_id)
    {
        *out_socket_id = static_cast<int>(id);
    }
    return 0;
}

extern "C" int __viper_netd_socket_connect(int socket_id, unsigned int ip_be, unsigned short port_be)
{
    return netd().socket_connect(static_cast<u32>(socket_id), ip_be, port_be);
}

extern "C" long __viper_netd_socket_send(int socket_id, const void *buf, unsigned long len)
{
    if (!buf && len != 0)
    {
        return VERR_INVALID_ARG;
    }
    if (len > 0xFFFFFFFFul)
    {
        len = 0xFFFFFFFFul;
    }
    return static_cast<long>(netd().socket_send(static_cast<u32>(socket_id), buf, static_cast<u32>(len)));
}

extern "C" long __viper_netd_socket_recv(int socket_id, void *buf, unsigned long len)
{
    if (!buf && len != 0)
    {
        return VERR_INVALID_ARG;
    }
    if (len > 0xFFFFFFFFul)
    {
        len = 0xFFFFFFFFul;
    }

    // Non-blocking semantics: return immediately with available data or WOULD_BLOCK.
    // This allows callers to properly poll() for both network and console input.
    return static_cast<long>(netd().socket_recv(static_cast<u32>(socket_id), buf, static_cast<u32>(len)));
}

extern "C" int __viper_netd_socket_close(int socket_id)
{
    return netd().socket_close(static_cast<u32>(socket_id));
}

extern "C" int __viper_netd_socket_status(int socket_id, unsigned int *out_flags, unsigned int *out_rx_available)
{
    u32 flags = 0;
    u32 rx = 0;
    i32 rc = netd().socket_status(static_cast<u32>(socket_id), &flags, &rx);
    if (rc != 0)
    {
        return rc;
    }
    if (out_flags)
    {
        *out_flags = flags;
    }
    if (out_rx_available)
    {
        *out_rx_available = rx;
    }
    return 0;
}

extern "C" int __viper_netd_dns_resolve(const char *hostname, unsigned int *out_ip_be)
{
    u32 ip = 0;
    i32 rc = netd().dns_resolve(hostname, &ip);
    if (rc != 0)
    {
        return rc;
    }
    if (out_ip_be)
    {
        *out_ip_be = ip;
    }
    return 0;
}

