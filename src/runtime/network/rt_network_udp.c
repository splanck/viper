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
    bool is_bound;                     // Whether socket is bound
    bool is_open;                      // Socket state
    char sender_host[INET_ADDRSTRLEN]; // Last sender host
    int sender_port;                   // Last sender port
    int recv_timeout_ms;               // Receive timeout (0 = none)
} rt_udp_t;

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

/// @brief Create an unbound UDP socket (AF_INET, SOCK_DGRAM). Suppresses SIGPIPE on platforms
/// that need it. Returns a GC-managed handle wired to `rt_udp_finalize`. Traps on socket()
/// failure or OOM. Use this for client-side senders that don't need to receive on a fixed port.
void *rt_udp_new(void) {
    rt_net_init_wsa();

    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        rt_trap("Network: failed to create UDP socket");
    }
    suppress_sigpipe(sock);

    rt_udp_t *udp = (rt_udp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_udp_t));
    if (!udp) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }
    rt_obj_set_finalizer(udp, rt_udp_finalize);

    udp->sock = sock;
    udp->address = NULL;
    udp->port = 0;
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
    return rt_udp_bind_at(rt_const_cstr("0.0.0.0"), port);
}

/// @brief Create and bind a UDP socket to `(address, port)`. `address` must be a valid IPv4 dotted
/// quad. Sets `SO_REUSEADDR` so re-binding after process restart doesn't fail with TIME_WAIT.
/// When `port==0` the OS assigns a free port; the actual port is queried back via `getsockname`
/// and stored in `udp->port`. Translates kernel errors (EADDRINUSE → "port already in use",
/// EACCES → "permission denied (port < 1024?)") into specific traps for ergonomic diagnostics.
void *rt_udp_bind_at(rt_string address, int64_t port) {
    rt_net_init_wsa();

    if (port < 0 || port > 65535) {
        rt_trap("Network: invalid port number");
    }

    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0') {
        rt_trap("Network: invalid address");
    }

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        rt_trap("Network: failed to create UDP socket");
    }
    suppress_sigpipe(sock);

    // Enable address reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    // Bind to address
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, addr_ptr, &bind_addr.sin_addr) != 1) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: invalid address");
    }

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == SOCK_ERROR) {
        int err = GET_LAST_ERROR();
        CLOSE_SOCKET(sock);
        if (err == ADDR_IN_USE)
            rt_trap_net("Network: port already in use", Err_NetworkError);
        if (err == PERM_DENIED)
            rt_trap_net("Network: permission denied (port < 1024?)", Err_NetworkError);
        rt_trap_net("Network: bind failed", Err_NetworkError);
    }

    // Get actual port if 0 was specified
    int actual_port = (int)port;
    if (port == 0) {
        struct sockaddr_in bound_addr;
        socklen_t len = sizeof(bound_addr);
        if (getsockname(sock, (struct sockaddr *)&bound_addr, &len) == 0) {
            actual_port = ntohs(bound_addr.sin_port);
        }
    }

    // Copy address string
    size_t addr_len = strlen(addr_ptr);
    char *addr_cstr = (char *)malloc(addr_len + 1);
    if (!addr_cstr) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }
    memcpy(addr_cstr, addr_ptr, addr_len + 1);

    // Create UDP object
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
    udp->is_bound = true;
    udp->is_open = true;
    udp->sender_host[0] = '\0';
    udp->sender_port = 0;
    udp->recv_timeout_ms = 0;

    return udp;
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

/// @brief Resolve a host string (IPv4 dotted quad OR DNS name) into an IPv4 sockaddr_in.
/// Tries `inet_pton` first (zero-allocation common path); falls back to `getaddrinfo` for
/// real DNS. The returned sockaddr is filled with `(family, port, addr)`. Returns -1 on
/// resolution failure so the caller can trap with a meaningful error class.
static int resolve_host(const char *host, int port, struct sockaddr_in *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);

    // Try parsing as IP address first
    if (inet_pton(AF_INET, host, &addr->sin_addr) == 1) {
        return 0;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return -1;
    }

    struct sockaddr_in *resolved = (struct sockaddr_in *)res->ai_addr;
    addr->sin_addr = resolved->sin_addr;
    freeaddrinfo(res);

    return 0;
}

/// @brief Send a Bytes payload as a single UDP datagram to `(host, port)`. Caps payload at the
/// IPv4 UDP max of 65507 bytes (65535 IP MTU − 20 IP header − 8 UDP header) to avoid silent
/// kernel fragmentation. Resolves `host` via `resolve_host` (IP-literal fast path + DNS fallback).
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
    struct sockaddr_in dest_addr;
    if (resolve_host(host_ptr, (int)port, &dest_addr) != 0)
        rt_trap_net("Network: host not found", Err_HostNotFound);

    int sent = sendto(udp->sock,
                      (const char *)buf,
                      (int)len,
                      SEND_FLAGS,
                      (struct sockaddr *)&dest_addr,
                      sizeof(dest_addr));
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
    struct sockaddr_in dest_addr;
    if (resolve_host(host_ptr, (int)port, &dest_addr) != 0)
        rt_trap_net("Network: host not found", Err_HostNotFound);

    int sent = sendto(udp->sock,
                      text_ptr,
                      (int)len,
                      SEND_FLAGS,
                      (struct sockaddr *)&dest_addr,
                      sizeof(dest_addr));
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

    struct sockaddr_in sender_addr;
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
    inet_ntop(AF_INET, &sender_addr.sin_addr, udp->sender_host, sizeof(udp->sender_host));
    udp->sender_port = ntohs(sender_addr.sin_port);

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

/// @brief Subscribe to an IPv4 multicast group via `IP_ADD_MEMBERSHIP`. Validates that
/// `group_addr` falls in the canonical 224.0.0.0/4 range (high nibble == 0xE) before issuing
/// the syscall — catches mistakes that would otherwise silently no-op. Joins on `INADDR_ANY`
/// (let the kernel pick the routing interface).
void rt_udp_join_group(void *obj, rt_string group_addr) {
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap_net("Network: socket closed", Err_ConnectionClosed);

    const char *addr_ptr = rt_string_cstr(group_addr);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: invalid multicast address");

    // Validate multicast address (224.0.0.0 - 239.255.255.255)
    struct in_addr mcast_addr;
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) != 1) {
        rt_trap("Network: invalid multicast address");
    }

    uint32_t addr_val = ntohl(mcast_addr.s_addr);
    if ((addr_val & 0xF0000000) != 0xE0000000) {
        rt_trap("Network: invalid multicast address (must be 224.0.0.0 - 239.255.255.255)");
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr = mcast_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(udp->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) ==
        SOCK_ERROR) {
        rt_trap_net("Network: failed to join multicast group", Err_NetworkError);
    }
}

/// @brief Unsubscribe from a multicast group via `IP_DROP_MEMBERSHIP`. Tolerant of bad input —
/// silently no-ops on closed sockets, empty addresses, or malformed IPs (so cleanup paths can
/// be unconditional without fear of throwing during teardown).
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
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) != 1)
        return;

    struct ip_mreq mreq;
    mreq.imr_multiaddr = mcast_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    setsockopt(udp->sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
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
