//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/netd_backend.cpp
// Purpose: libc-to-netd bridge for socket and DNS operations.
// Key invariants: Global client connects on demand; socket IDs map to netd.
// Ownership/Lifetime: Library; global client persists for process lifetime.
// Links: user/libnetclient, user/servers/netd
//
//===----------------------------------------------------------------------===//

/**
 * @file netd_backend.cpp
 * @brief libc-to-netd bridge for socket and DNS operations.
 *
 * @details
 * This file provides the bridge between libc socket functions and netd:
 *
 * Connection Management:
 * - __viper_netd_is_available: Check if netd is running
 * - __viper_netd_poll_handle: Get event channel for poll()
 *
 * Socket Operations:
 * - __viper_netd_socket_create: Create socket via netd
 * - __viper_netd_socket_connect: Connect to remote host
 * - __viper_netd_socket_send/recv: Send/receive data
 * - __viper_netd_socket_close: Close socket
 * - __viper_netd_socket_status: Get socket state for poll()
 *
 * DNS Resolution:
 * - __viper_netd_dns_resolve: Resolve hostname to IP address
 *
 * All functions use the libnetclient library to communicate with netd
 * via IPC channels. Socket IDs are netd-internal identifiers.
 */

#include "netclient.hpp"
#include "syscall.hpp"

// Global client instance - avoid static initialization issues
static netclient::Client g_netd_client;

extern "C" int __viper_netd_is_available()
{
    i32 rc = g_netd_client.connect();
    if (rc != 0)
    {
        return 0;
    }
    return 1;
}

extern "C" unsigned int __viper_netd_poll_handle()
{
    if (g_netd_client.ensure_events() != 0)
    {
        return 0xFFFFFFFFu;
    }
    i32 ch = g_netd_client.event_channel_recv();
    if (ch < 0)
    {
        return 0xFFFFFFFFu;
    }
    return static_cast<unsigned int>(ch);
}

extern "C" int __viper_netd_socket_create(int domain, int type, int protocol, int *out_socket_id)
{
    u32 id = 0;
    i32 rc = g_netd_client.socket_create(
        static_cast<u16>(domain), static_cast<u16>(type), (u32)protocol, &id);
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

extern "C" int __viper_netd_socket_connect(int socket_id,
                                           unsigned int ip_be,
                                           unsigned short port_be)
{
    return g_netd_client.socket_connect(static_cast<u32>(socket_id), ip_be, port_be);
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
    return static_cast<long>(
        g_netd_client.socket_send(static_cast<u32>(socket_id), buf, static_cast<u32>(len)));
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
    return static_cast<long>(
        g_netd_client.socket_recv(static_cast<u32>(socket_id), buf, static_cast<u32>(len)));
}

extern "C" int __viper_netd_socket_close(int socket_id)
{
    return g_netd_client.socket_close(static_cast<u32>(socket_id));
}

extern "C" int __viper_netd_socket_status(int socket_id,
                                          unsigned int *out_flags,
                                          unsigned int *out_rx_available)
{
    u32 flags = 0;
    u32 rx = 0;
    i32 rc = g_netd_client.socket_status(static_cast<u32>(socket_id), &flags, &rx);
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
    i32 rc = g_netd_client.dns_resolve(hostname, &ip);
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
