//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_network_udp.c
// Purpose: UDP socket support for Viper.Network.Udp. Provides creation, binding,
//   send/receive, multicast group management, broadcast, and timeout control.
//
// Key invariants:
//   - UDP sockets are GC-managed via rt_obj_new_i64 with a finalizer.
//   - Send/receive use Berkeley sockets API with platform abstraction.
//   - Multicast uses IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP.
//   - Not thread-safe; external synchronization required for concurrent access.
//
// Ownership/Lifetime:
//   - rt_udp objects are GC-managed; the socket is closed by the finalizer.
//   - Received data is returned as GC-managed Bytes objects.
//
// Links: src/runtime/network/rt_network_internal.h (platform abstractions),
//        src/runtime/network/rt_network.c (TCP + platform init),
//        src/runtime/network/rt_network.h (public API)
//
//===----------------------------------------------------------------------===//

// Platform feature macros must appear before ANY includes
#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif

#include "rt_network_internal.h"

#include "rt_map.h"

typedef struct rt_udp {
    socket_t sock;                     // Socket descriptor
    char *address;                     // Bound address (allocated, or NULL if unbound)
    int port;                          // Bound port (0 if unbound)
    int family;                        // AF_INET or AF_INET6
    bool is_bound;                     // Whether socket is bound
    bool is_open;                      // Socket state
    char sender_host[INET6_ADDRSTRLEN]; // Last sender host
    int sender_port;                   // Last sender port
    int recv_timeout_ms;               // Receive timeout (0 = none)
} rt_udp_t;

static void rt_udp_finalize(void *obj);

#if defined(IPV6_JOIN_GROUP) && !defined(IPV6_ADD_MEMBERSHIP)
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif
#if defined(IPV6_LEAVE_GROUP) && !defined(IPV6_DROP_MEMBERSHIP)
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

static socket_t udp_create_socket(int family, int dual_stack) {
    socket_t sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK)
        return INVALID_SOCK;
    suppress_sigpipe(sock);
#ifdef IPV6_V6ONLY
    if (family == AF_INET6) {
        int v6only = dual_stack ? 0 : 1;
        setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));
    }
#endif
    return sock;
}

static void udp_make_v4_mapped(const struct sockaddr_in *src,
                               struct sockaddr_in6 *dst,
                               socklen_t *dst_len) {
    memset(dst, 0, sizeof(*dst));
    dst->sin6_family = AF_INET6;
    dst->sin6_port = src->sin_port;
    dst->sin6_addr.s6_addr[10] = 0xFF;
    dst->sin6_addr.s6_addr[11] = 0xFF;
    memcpy(&dst->sin6_addr.s6_addr[12], &src->sin_addr, sizeof(src->sin_addr));
    if (dst_len)
        *dst_len = (socklen_t)sizeof(*dst);
}

static void udp_store_sender_info(rt_udp_t *udp, const struct sockaddr *addr, socklen_t addr_len) {
    (void)addr_len;
    if (!udp || !addr) {
        return;
    }

    udp->sender_host[0] = '\0';
    udp->sender_port = 0;

    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &in->sin_addr, udp->sender_host, sizeof(udp->sender_host));
        udp->sender_port = ntohs(in->sin_port);
        return;
    }

    if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)addr;
#ifdef IN6_IS_ADDR_V4MAPPED
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
            struct in_addr mapped_v4;
            memcpy(&mapped_v4, &in6->sin6_addr.s6_addr[12], sizeof(mapped_v4));
            inet_ntop(AF_INET, &mapped_v4, udp->sender_host, sizeof(udp->sender_host));
        } else
#endif
        {
            inet_ntop(AF_INET6, &in6->sin6_addr, udp->sender_host, sizeof(udp->sender_host));
        }
        udp->sender_port = ntohs(in6->sin6_port);
    }
}

static int udp_resolve_destination(const char *host,
                                   int port,
                                   int socket_family,
                                   struct sockaddr_storage *addr_out,
                                   socklen_t *addr_len_out) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    char port_str[16];

    if (!host || !*host || !addr_out || !addr_len_out)
        return -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if (socket_family == AF_INET && rp->ai_family != AF_INET)
            continue;
        if (socket_family == AF_INET6 && rp->ai_family == AF_INET) {
            udp_make_v4_mapped((const struct sockaddr_in *)rp->ai_addr,
                               (struct sockaddr_in6 *)addr_out,
                               addr_len_out);
            freeaddrinfo(res);
            return 0;
        }
        if (socket_family != AF_INET6 && socket_family != AF_INET)
            continue;
        if (socket_family == AF_INET6 && rp->ai_family != AF_INET6)
            continue;
        if ((socklen_t)rp->ai_addrlen > (socklen_t)sizeof(*addr_out))
            continue;
        memcpy(addr_out, rp->ai_addr, rp->ai_addrlen);
        *addr_len_out = (socklen_t)rp->ai_addrlen;
        freeaddrinfo(res);
        return 0;
    }

    freeaddrinfo(res);
    return -1;
}

static void *rt_udp_bind_impl(const char *address, int64_t port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    int family = AF_INET;
    int actual_port = (int)port;
    char *addr_cstr = NULL;
    char port_str[16];

    rt_net_init_wsa();

    if (port < 0 || port > 65535)
        rt_trap("Network: invalid port number");
    if (address && *address == '\0')
        rt_trap("Network: invalid address");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (!address)
        hints.ai_flags = AI_PASSIVE;

    snprintf(port_str, sizeof(port_str), "%d", (int)port);
    if (getaddrinfo(address, port_str, &hints, &res) != 0 || !res)
        rt_trap("Network: invalid address");

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int reuse = 1;
        socket_t candidate = udp_create_socket(rp->ai_family, rp->ai_family == AF_INET6);
        if (candidate == INVALID_SOCK)
            continue;

        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

        if (bind(candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            sock = candidate;
            family = rp->ai_family;
            break;
        }

        last_err = GET_LAST_ERROR();
        CLOSE_SOCKET(candidate);
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        if (last_err == ADDR_IN_USE)
            rt_trap_net("Network: port already in use", Err_NetworkError);
        if (last_err == PERM_DENIED)
            rt_trap_net("Network: permission denied (port < 1024?)", Err_NetworkError);
        rt_trap_net("Network: bind failed", Err_NetworkError);
    }

    if (port == 0) {
        struct sockaddr_storage bound_addr;
        socklen_t len = sizeof(bound_addr);
        if (getsockname(sock, (struct sockaddr *)&bound_addr, &len) == 0) {
            if (((struct sockaddr *)&bound_addr)->sa_family == AF_INET) {
                actual_port = ntohs(((struct sockaddr_in *)&bound_addr)->sin_port);
            } else if (((struct sockaddr *)&bound_addr)->sa_family == AF_INET6) {
                actual_port = ntohs(((struct sockaddr_in6 *)&bound_addr)->sin6_port);
            }
        }
    }

    {
        const char *addr_ptr = address ? address : (family == AF_INET6 ? "::" : "0.0.0.0");
        size_t addr_len = strlen(addr_ptr);
        addr_cstr = (char *)malloc(addr_len + 1);
        if (!addr_cstr) {
            CLOSE_SOCKET(sock);
            rt_trap("Network: memory allocation failed");
        }
        memcpy(addr_cstr, addr_ptr, addr_len + 1);
    }

    {
        rt_udp_t *udp = (rt_udp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_udp_t));
        if (!udp) {
            CLOSE_SOCKET(sock);
            free(addr_cstr);
            rt_trap("Network: memory allocation failed");
        }
        rt_obj_set_finalizer(udp, rt_udp_finalize);

        udp->sock = sock;
        udp->address = addr_cstr;
        udp->port = actual_port;
        udp->family = family;
        udp->is_bound = true;
        udp->is_open = true;
        udp->sender_host[0] = '\0';
        udp->sender_port = 0;
        udp->recv_timeout_ms = 0;
        return udp;
    }
}

/// @brief GC finalizer: close the socket if still open and free the bound-address string.
static void rt_udp_finalize(void *obj) {
    if (!obj)
        return;
    rt_udp_t *udp = (rt_udp_t *)obj;
    if (udp->is_open) {
        CLOSE_SOCKET(udp->sock);
        udp->is_open = false;
    }
    udp->is_bound = false;
    if (udp->address) {
        free(udp->address);
        udp->address = NULL;
    }
}

//=============================================================================
// Udp - Creation
//=============================================================================

/// @brief Create an unbound UDP socket. Prefers an IPv6 dual-stack socket when available so the
/// same handle can send to IPv4 and IPv6 destinations; falls back to IPv4-only when the platform
/// cannot create a dual-stack datagram socket.
void *rt_udp_new(void) {
    rt_net_init_wsa();

    socket_t sock = udp_create_socket(AF_INET6, 1);
    int family = AF_INET6;
    if (sock == INVALID_SOCK) {
        sock = udp_create_socket(AF_INET, 0);
        family = AF_INET;
    }
    if (sock == INVALID_SOCK) {
        rt_trap("Network: failed to create UDP socket");
    }

    rt_udp_t *udp = (rt_udp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_udp_t));
    if (!udp) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }
    rt_obj_set_finalizer(udp, rt_udp_finalize);

    udp->sock = sock;
    udp->address = NULL;
    udp->port = 0;
    udp->family = family;
    udp->is_bound = false;
    udp->is_open = true;
    udp->sender_host[0] = '\0';
    udp->sender_port = 0;
    udp->recv_timeout_ms = 0;

    return udp;
}

/// @brief Create a UDP socket and bind it to all interfaces (`0.0.0.0`) on `port`. Pass
/// `port=0` to let the OS pick a free port (read it back via `rt_udp_port`). Convenience wrapper
/// over `rt_udp_bind_at` for the common "listen on any address" case.
void *rt_udp_bind(int64_t port) {
    return rt_udp_bind_impl(NULL, port);
}

/// @brief Create and bind a UDP socket to `(address, port)`. Supports IPv4 literals, IPv6
/// literals, and hostnames that resolve to a local interface address. When `port==0` the OS
/// assigns a free port and the actual port is queried back via `getsockname`.
void *rt_udp_bind_at(rt_string address, int64_t port) {
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: invalid address");
    return rt_udp_bind_impl(addr_ptr, port);
}

//=============================================================================
// Udp - Properties
//=============================================================================

/// @brief Read the bound port (0 for unbound sockets created via `rt_udp_new`). Useful when the
/// constructor used `port=0` and you need to discover which ephemeral port the OS assigned.
int64_t rt_udp_port(void *obj) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->port;
}

/// @brief Read the bound address as an rt_string ("0.0.0.0" if bound to all interfaces). Returns
/// the empty string for unbound sockets.
rt_string rt_udp_address(void *obj) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (udp->address)
        return rt_const_cstr(udp->address);
    return rt_str_empty();
}

/// @brief Returns 1 if the socket was created via `bind()` (i.e. has a fixed local port); 0 if
/// it was created via `rt_udp_new` and only sends.
int8_t rt_udp_is_bound(void *obj) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->is_bound ? 1 : 0;
}

//=============================================================================
// Udp - Send Methods
//=============================================================================

/// @brief Send a Bytes payload as a single UDP datagram to `(host, port)`. Caps payload at the
/// IPv4 UDP max of 65507 bytes (65535 IP MTU − 20 IP header − 8 UDP header) to avoid silent
/// kernel fragmentation. Resolves `host` through `getaddrinfo`, supporting IPv4, IPv6, and DNS.
/// Returns the byte count actually sent. Traps with specific kinds for EMSGSIZE, host-not-found,
/// and generic send errors so callers can distinguish recoverable failures.
int64_t rt_udp_send_to(void *obj, rt_string host, int64_t port, void *data) {
    if (!obj)
        rt_trap("Network: NULL socket");
    if (!data)
        rt_trap("Network: NULL data");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: invalid host");

    if (port < 1 || port > 65535)
        rt_trap("Network: invalid port number");

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    if (len == 0)
        return 0;

    // Check packet size
    if (len > 65507)
        rt_trap_net("Network: message too large (max 65507 bytes for UDP)", Err_NetworkError);

    // Resolve destination
    struct sockaddr_storage dest_addr;
    socklen_t dest_len = 0;
    if (udp_resolve_destination(host_ptr, (int)port, udp->family, &dest_addr, &dest_len) != 0)
        rt_trap_net("Network: host not found", Err_HostNotFound);

    int sent = sendto(udp->sock,
                      (const char *)buf,
                      (int)len,
                      SEND_FLAGS,
                      (struct sockaddr *)&dest_addr,
                      dest_len);
    if (sent == SOCK_ERROR) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEMSGSIZE)
            rt_trap_net("Network: message too large", Err_NetworkError);
#else
        if (errno == EMSGSIZE)
            rt_trap_net("Network: message too large", Err_NetworkError);
#endif
        rt_trap_net("Network: send failed", net_classify_errno());
    }

    return sent;
}

/// @brief String-payload variant of `rt_udp_send_to`. Sends the rt_string's UTF-8 bytes
/// (without a NUL terminator) as a single datagram. Same 65507-byte cap and error semantics.
int64_t rt_udp_send_to_str(void *obj, rt_string host, int64_t port, rt_string text) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: invalid host");

    if (port < 1 || port > 65535)
        rt_trap("Network: invalid port number");

    const char *text_ptr = rt_string_cstr(text);
    size_t len = strlen(text_ptr);

    if (len == 0)
        return 0;

    if (len > 65507)
        rt_trap_net("Network: message too large (max 65507 bytes for UDP)", Err_NetworkError);

    // Resolve destination
    struct sockaddr_storage dest_addr;
    socklen_t dest_len = 0;
    if (udp_resolve_destination(host_ptr, (int)port, udp->family, &dest_addr, &dest_len) != 0)
        rt_trap_net("Network: host not found", Err_HostNotFound);

    int sent = sendto(udp->sock,
                      text_ptr,
                      (int)len,
                      SEND_FLAGS,
                      (struct sockaddr *)&dest_addr,
                      dest_len);
    if (sent == SOCK_ERROR) {
        rt_trap_net("Network: send failed", net_classify_errno());
    }

    return sent;
}

//=============================================================================
// Udp - Receive Methods
//=============================================================================

/// @brief Convenience alias for `rt_udp_recv_from` — receive a single datagram up to `max_bytes`
/// long. The sender's address is recorded and accessible via `rt_udp_sender_host` / `_port`.
void *rt_udp_recv(void *obj, int64_t max_bytes) {
    return rt_udp_recv_from(obj, max_bytes);
}

/// @brief Receive one UDP datagram, capturing the sender's address into `udp->sender_host/port`.
/// Right-sizes the result `Bytes` if the actual datagram was shorter than `max_bytes` (datagram
/// boundaries are meaningful in UDP; trailing zeros must NOT be exposed). On socket timeout
/// (EAGAIN / WSAETIMEDOUT, configured via `set_recv_timeout`), returns an empty Bytes rather
/// than trapping — lets receive loops poll cheaply.
void *rt_udp_recv_from(void *obj, int64_t max_bytes) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    if (max_bytes <= 0)
        return rt_bytes_new(0);

    // Allocate receive buffer
    void *result = rt_bytes_new(max_bytes);
    uint8_t *buf = bytes_data(result);

    struct sockaddr_storage sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    int received = recvfrom(
        udp->sock, (char *)buf, (int)max_bytes, 0, (struct sockaddr *)&sender_addr, &sender_len);

    if (received == SOCK_ERROR) {
        // Check for timeout
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
        {
            // Release over-allocated buffer and return empty bytes on timeout
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            return rt_bytes_new(0);
        }
        rt_trap_net("Network: receive failed", net_classify_errno());
    }

    // Store sender info
    udp_store_sender_info(udp, (const struct sockaddr *)&sender_addr, sender_len);

    // Return exact size received
    if (received < max_bytes) {
        void *exact = rt_bytes_new(received);
        memcpy(bytes_data(exact), buf, received);
        // Release the over-allocated buffer
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        return exact;
    }

    return result;
}

/// @brief Bounded-wait variant: `select()` for up to `timeout_ms`, then receive (or return NULL
/// on no-data). Distinct from setting socket-level recv timeout: this is one-shot and returns
/// NULL on expiry, while `set_recv_timeout` sets a persistent socket option that returns empty Bytes.
void *rt_udp_recv_for(void *obj, int64_t max_bytes, int64_t timeout_ms) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    // Use select for timeout
    if (timeout_ms > 0) {
        int ready = wait_socket(udp->sock, (int)timeout_ms, false);
        if (ready == 0) {
            // Timeout - return NULL
            return NULL;
        }
        if (ready < 0) {
            rt_trap_net("Network: receive failed", net_classify_errno());
        }
    }

    return rt_udp_recv_from(obj, max_bytes);
}

/// @brief Read the source IPv4 of the most recently received datagram. Empty until the first
/// successful `recv*`.
rt_string rt_udp_sender_host(void *obj) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return rt_const_cstr(udp->sender_host);
}

/// @brief Read the source port of the most recently received datagram. 0 until the first recv*.
int64_t rt_udp_sender_port(void *obj) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->sender_port;
}

//=============================================================================
// Udp - Options and Close
//=============================================================================

/// @brief Toggle SO_BROADCAST on the socket. Required before sending to 255.255.255.255 or any
/// directed-broadcast address; without it the kernel returns EACCES.
void rt_udp_set_broadcast(void *obj, int8_t enable) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    int flag = enable ? 1 : 0;
    if (setsockopt(udp->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag)) ==
        SOCK_ERROR) {
        rt_trap_net("Network: failed to set broadcast option", Err_NetworkError);
    }
}

/// @brief Subscribe to an IPv4 or IPv6 multicast group. IPv4 uses `IP_ADD_MEMBERSHIP`;
/// IPv6 uses `IPV6_ADD_MEMBERSHIP`.
void rt_udp_join_group(void *obj, rt_string group_addr) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    const char *addr_ptr = rt_string_cstr(group_addr);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: invalid multicast address");

    struct in_addr mcast_addr;
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) == 1) {
        uint32_t addr_val = ntohl(mcast_addr.s_addr);
        if ((addr_val & 0xF0000000) != 0xE0000000)
            rt_trap("Network: invalid multicast address (must be 224.0.0.0 - 239.255.255.255)");

        {
            struct ip_mreq mreq;
            mreq.imr_multiaddr = mcast_addr;
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);

            if (setsockopt(
                    udp->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) ==
                SOCK_ERROR) {
                rt_trap_net("Network: failed to join multicast group", Err_NetworkError);
            }
        }
        return;
    }

    {
        struct in6_addr mcast_addr6;
        if (inet_pton(AF_INET6, addr_ptr, &mcast_addr6) != 1)
            rt_trap("Network: invalid multicast address");
        if (mcast_addr6.s6_addr[0] != 0xFF)
            rt_trap("Network: invalid multicast address (IPv6 multicast must be ff00::/8)");
#ifdef IPV6_ADD_MEMBERSHIP
        {
            struct ipv6_mreq mreq6;
            memset(&mreq6, 0, sizeof(mreq6));
            mreq6.ipv6mr_multiaddr = mcast_addr6;
            mreq6.ipv6mr_interface = 0;
            if (setsockopt(udp->sock,
                           IPPROTO_IPV6,
                           IPV6_ADD_MEMBERSHIP,
                           (const char *)&mreq6,
                           sizeof(mreq6)) == SOCK_ERROR) {
                rt_trap_net("Network: failed to join multicast group", Err_NetworkError);
            }
        }
        return;
#else
        rt_trap_net("Network: IPv6 multicast is not supported on this platform", Err_NetworkError);
#endif
    }
}

/// @brief Unsubscribe from an IPv4 or IPv6 multicast group. Tolerant of bad input — silently
/// no-ops on closed sockets, empty addresses, or malformed IPs.
void rt_udp_leave_group(void *obj, rt_string group_addr) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        return; // Silently ignore if closed

    const char *addr_ptr = rt_string_cstr(group_addr);
    if (!addr_ptr || *addr_ptr == '\0')
        return;

    struct in_addr mcast_addr;
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) == 1) {
        struct ip_mreq mreq;
        mreq.imr_multiaddr = mcast_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(udp->sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
        return;
    }

#ifdef IPV6_DROP_MEMBERSHIP
    {
        struct in6_addr mcast_addr6;
        if (inet_pton(AF_INET6, addr_ptr, &mcast_addr6) != 1)
            return;
        {
            struct ipv6_mreq mreq6;
            memset(&mreq6, 0, sizeof(mreq6));
            mreq6.ipv6mr_multiaddr = mcast_addr6;
            mreq6.ipv6mr_interface = 0;
            setsockopt(udp->sock,
                       IPPROTO_IPV6,
                       IPV6_DROP_MEMBERSHIP,
                       (const char *)&mreq6,
                       sizeof(mreq6));
        }
    }
#endif
}

/// @brief Set a persistent socket-level recv timeout via SO_RCVTIMEO. Subsequent `recv*` calls
/// that exceed this duration return empty Bytes (rather than the per-call NULL of `recv_for`).
/// Pass `0` to clear (block indefinitely).
void rt_udp_set_recv_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    udp->recv_timeout_ms = (int)timeout_ms;
    set_socket_timeout(udp->sock, (int)timeout_ms, true);
}

/// @brief Explicit close — releases the kernel socket immediately rather than waiting for GC.
/// Idempotent (no-op on already-closed sockets). The handle remains valid for is_open queries.
void rt_udp_close(void *obj) {
    if (!obj)
        return;

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (udp->is_open) {
        CLOSE_SOCKET(udp->sock);
        udp->is_open = false;
        udp->is_bound = false;
    }
}
