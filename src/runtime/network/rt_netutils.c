//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_netutils.c
// Purpose: Static network utility functions for port checking, CIDR matching,
//          and IP address classification.
// Key invariants:
//   - All functions are stateless and thread-safe.
// Ownership/Lifetime:
//   - Pure functions; returned strings are GC-managed.
// Links: rt_netutils.h (API)
//
//===----------------------------------------------------------------------===//

#include "rt_netutils.h"

#include "rt_internal.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET(s) close(s)
#endif

#ifdef _WIN32
extern void rt_net_init_wsa(void);
#else
static inline void rt_net_init_wsa(void) {}
#endif

//=============================================================================
// Port Checking
//=============================================================================

int8_t rt_netutils_is_port_open(rt_string host, int64_t port, int64_t timeout_ms) {
    rt_net_init_wsa();

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || port < 1 || port > 65535)
        return 0;
    if (timeout_ms <= 0)
        timeout_ms = 1000;

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    if (getaddrinfo(host_ptr, port_str, &hints, &res) != 0)
        return 0;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        socket_t sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#ifdef _WIN32
        if (sock == INVALID_SOCKET)
#else
        if (sock < 0)
#endif
            continue;

        // Set non-blocking for timeout
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0)
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

        int result = connect(sock, rp->ai_addr, (int)rp->ai_addrlen);
        if (result == 0) {
            CLOSE_SOCKET(sock);
            freeaddrinfo(res);
            return 1;
        }

        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);

        struct timeval tv;
        tv.tv_sec = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

        int ready = select((int)(sock + 1), NULL, &write_fds, NULL, &tv);
        if (ready > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
            CLOSE_SOCKET(sock);
            if (so_error == 0) {
                freeaddrinfo(res);
                return 1;
            }
        } else {
            CLOSE_SOCKET(sock);
        }
    }

    freeaddrinfo(res);
    return 0;
}

int64_t rt_netutils_get_free_port(void) {
    rt_net_init_wsa();

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (sock == INVALID_SOCKET)
        return 0;
#else
    if (sock < 0)
        return 0;
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
    addr.sin_port = 0;                        // OS assigns a free port

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(sock);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
        CLOSE_SOCKET(sock);
        return 0;
    }

    int64_t port = ntohs(addr.sin_port);
    CLOSE_SOCKET(sock);
    return port;
}

//=============================================================================
// CIDR Matching
//=============================================================================

/// @brief Parse an IPv4 address string to a uint32_t in host byte order.
static bool parse_ipv4_addr(const char *str, uint32_t *out) {
    struct in_addr addr;
    if (inet_pton(AF_INET, str, &addr) != 1)
        return false;
    *out = ntohl(addr.s_addr);
    return true;
}

int8_t rt_netutils_match_cidr(rt_string ip, rt_string cidr) {
    const char *ip_str = rt_string_cstr(ip);
    const char *cidr_str = rt_string_cstr(cidr);
    if (!ip_str || !cidr_str)
        return 0;

    // Parse CIDR notation: "10.0.0.0/8"
    char network[64];
    int prefix_len = 32;
    const char *slash = strchr(cidr_str, '/');
    if (slash) {
        size_t net_len = (size_t)(slash - cidr_str);
        if (net_len >= sizeof(network))
            return 0;
        memcpy(network, cidr_str, net_len);
        network[net_len] = '\0';
        prefix_len = atoi(slash + 1);
        if (prefix_len < 0 || prefix_len > 32)
            return 0;
    } else {
        size_t len = strlen(cidr_str);
        if (len >= sizeof(network))
            return 0;
        memcpy(network, cidr_str, len + 1);
    }

    uint32_t ip_val, net_val;
    if (!parse_ipv4_addr(ip_str, &ip_val))
        return 0;
    if (!parse_ipv4_addr(network, &net_val))
        return 0;

    if (prefix_len == 0)
        return 1; // /0 matches everything

    uint32_t mask = ~((1u << (32 - prefix_len)) - 1);
    return (ip_val & mask) == (net_val & mask) ? 1 : 0;
}

int8_t rt_netutils_is_private_ip(rt_string ip) {
    const char *ip_str = rt_string_cstr(ip);
    if (!ip_str)
        return 0;

    uint32_t addr;
    if (!parse_ipv4_addr(ip_str, &addr))
        return 0;

    // RFC 1918 private ranges:
    // 10.0.0.0/8       (10.0.0.0 – 10.255.255.255)
    // 172.16.0.0/12    (172.16.0.0 – 172.31.255.255)
    // 192.168.0.0/16   (192.168.0.0 – 192.168.255.255)
    // Also: 127.0.0.0/8 (loopback)

    if ((addr >> 24) == 10)
        return 1;
    if ((addr >> 20) == (172 << 4 | 1)) // 172.16-31
        return 1;
    if ((addr >> 16) == (192 << 8 | 168))
        return 1;
    if ((addr >> 24) == 127)
        return 1;

    return 0;
}

rt_string rt_netutils_local_ipv4(void) {
    rt_net_init_wsa();

    // Create a UDP socket and "connect" to a public IP to determine
    // which local interface would be used (no actual traffic sent).
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sock == INVALID_SOCKET)
        return rt_string_from_bytes("127.0.0.1", 9);
#else
    if (sock < 0)
        return rt_string_from_bytes("127.0.0.1", 9);
#endif

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53); // DNS port (arbitrary)
    inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        CLOSE_SOCKET(sock);
        return rt_string_from_bytes("127.0.0.1", 9);
    }

    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr *)&local_addr, &len) != 0) {
        CLOSE_SOCKET(sock);
        return rt_string_from_bytes("127.0.0.1", 9);
    }

    CLOSE_SOCKET(sock);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, sizeof(ip_str));
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}
