//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

// Platform feature macros must appear before ANY includes.
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1 // macOS: expose BSD extensions
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 // Linux/ViperDOS: expose ip_mreq
#endif

//
// File: src/runtime/network/rt_network.c
// Purpose: TCP client and server support for Viper.Network.Tcp and TcpServer.
//   Contains platform initialization (WSA), socket helpers, TCP connection
//   creation, send/receive, and server accept. UDP lives in rt_network_udp.c,
//   DNS in rt_network_dns.c.
//
// Links: src/runtime/network/rt_network_internal.h (platform abstractions),
//        src/runtime/network/rt_network_udp.c (UDP sockets),
//        src/runtime/network/rt_network_dns.c (DNS resolution),
//        src/runtime/network/rt_network.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_network_internal.h"

//=============================================================================
// Tcp Connection Structure
//=============================================================================

typedef struct rt_tcp {
    socket_t sock;       // Socket descriptor
    char *host;          // Remote host (allocated)
    int port;            // Remote port
    int local_port;      // Local port
    bool is_open;        // Connection state
    int recv_timeout_ms; // Receive timeout (0 = none)
    int send_timeout_ms; // Send timeout (0 = none)
} rt_tcp_t;

//=============================================================================
// TcpServer Structure
//=============================================================================

typedef struct rt_tcp_server {
    socket_t sock;     // Listening socket
    char *address;     // Bound address (allocated)
    int port;          // Listening port
    bool is_listening; // Server state
} rt_tcp_server_t;

/// @brief Parse a numeric TCP service string returned by `getnameinfo`.
/// @details The accept path requests `NI_NUMERICSERV`, so successful calls
///          should return only decimal digits. This helper validates every
///          byte, rejects overflow, and falls back to 0 for malformed values
///          rather than accepting `atoi`'s partial parses.
/// @param service NUL-terminated numeric service string.
/// @return Port number in the inclusive range [0, 65535], or 0 on invalid input.
static int parse_numeric_service_port(const char *service) {
    unsigned int port = 0;
    if (!service || !*service)
        return 0;
    for (const unsigned char *p = (const unsigned char *)service; *p; ++p) {
        if (*p < '0' || *p > '9')
            return 0;
        unsigned int digit = (unsigned int)(*p - '0');
        if (port > (65535u - digit) / 10u)
            return 0;
        port = port * 10u + digit;
    }
    return (int)port;
}

int rt_net_cstr_no_embedded_nul(rt_string value, const char **out, size_t *len_out) {
    if (!out || !len_out)
        return 0;
    *out = NULL;
    *len_out = 0;
    if (!value)
        return 0;

    int64_t len64 = rt_str_len(value);
    if (len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX)
        return 0;
    const char *cstr = rt_string_cstr(value);
    if (!cstr && len64 > 0)
        return 0;
    size_t len = (size_t)len64;
    if (cstr && memchr(cstr, '\0', len) != NULL)
        return 0;

    *out = cstr ? cstr : "";
    *len_out = len;
    return 1;
}

/// @brief GC finalizer for TCP client — close the socket and free the host string.
static void rt_tcp_finalize(void *obj) {
    if (!obj)
        return;
    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (tcp->is_open) {
        CLOSE_SOCKET(tcp->sock);
        tcp->is_open = false;
    }
    if (tcp->host) {
        free(tcp->host);
        tcp->host = NULL;
    }
}

/// @brief GC finalizer for TCP server — close the listening socket and free the bound-address
/// string.
static void rt_tcp_server_finalize(void *obj) {
    if (!obj)
        return;
    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (server->is_listening) {
        CLOSE_SOCKET(server->sock);
        server->is_listening = false;
    }
    if (server->address) {
        free(server->address);
        server->address = NULL;
    }
}

//=============================================================================
// Socket Helpers
//=============================================================================

/// @brief Enable TCP_NODELAY on socket.
static void set_nodelay(socket_t sock) {
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

/// @brief Convert an int64 timeout argument to the range supported by socket helpers.
int rt_net_timeout_ms_to_int(int64_t timeout_ms, int *out_timeout_ms) {
    if (!out_timeout_ms || timeout_ms < 0 || timeout_ms > INT_MAX)
        return 0;
    *out_timeout_ms = (int)timeout_ms;
    return 1;
}

/// @brief Convert an int64 byte count to an int-sized recv/send length.
int rt_net_i64_len_to_int(int64_t byte_count, int *out_len) {
    if (!out_len || byte_count < 0 || byte_count > INT_MAX)
        return 0;
    *out_len = (int)byte_count;
    return 1;
}

/// @brief Get local port from socket.
static int get_local_port(socket_t sock) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &len) == 0) {
        if (addr.ss_family == AF_INET)
            return ntohs(((struct sockaddr_in *)&addr)->sin_port);
        if (addr.ss_family == AF_INET6)
            return ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    }
    return 0;
}

/// @brief Connect to `addr`, optionally with a timeout in `timeout_ms` (0 = blocking).
///
/// Implementation: when timed, switches the socket to non-blocking,
/// calls `connect`, then `select`s for write-readiness. If the
/// `select` returns ready, peeks `SO_ERROR` to confirm success.
/// Restores blocking mode at end. Returns false + writes the OS
/// errno into `*err_out` on any failure (including ETIMEDOUT for the
/// select-timeout path).
static bool connect_socket_with_timeout(
    socket_t sock, const struct sockaddr *addr, socklen_t addrlen, int timeout_ms, int *err_out) {
    if (err_out)
        *err_out = 0;

    if (timeout_ms > 0) {
        if (!rt_socket_set_nonblocking(sock, true)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }

        int connect_result = connect(sock, addr, addrlen);
        if (connect_result == SOCK_ERROR) {
            int err = GET_LAST_ERROR();
            if (rt_socket_error_is_in_progress(err)) {
                int ready = wait_socket(sock, timeout_ms, true);
                if (ready <= 0) {
                    if (err_out)
                        *err_out = ready == 0 ? ETIMEDOUT : GET_LAST_ERROR();
                    return false;
                }

                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                if (so_error != 0) {
                    if (err_out)
                        *err_out = so_error;
                    return false;
                }
            } else {
                if (err_out)
                    *err_out = err;
                return false;
            }
        }

        if (!rt_socket_set_nonblocking(sock, false)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }
        return true;
    }

    if (connect(sock, addr, addrlen) == SOCK_ERROR) {
        if (err_out)
            *err_out = GET_LAST_ERROR();
        return false;
    }

    return true;
}

//=============================================================================
// Tcp Client - Connection Creation
//=============================================================================

// ===========================================================================
// TCP client public API
//
// Each `rt_tcp_*` function operates on a GC-managed `rt_tcp_t`
// (or NULL). Connect functions return a fresh client; send/recv
// take a client handle; properties (`host`, `port`, `is_open`)
// expose state for diagnostics.
// ===========================================================================

/// @brief Connect to `host:port` with the default 30-second timeout.
/// @see rt_tcp_connect_for
void *rt_tcp_connect(rt_string host, int64_t port) {
    // Default 30-second timeout prevents indefinite blocking on unreachable hosts.
    return rt_tcp_connect_for(host, port, 30000);
}

/// @brief Connect to `host:port` with explicit `timeout_ms`.
///
/// Resolves `host` via `getaddrinfo` (AF_UNSPEC — IPv4 and IPv6
/// candidates), tries each address in turn, and uses
/// `connect_socket_with_timeout` so a slow / unreachable host fails
/// in bounded time. Enables `TCP_NODELAY` on success.
/// @throws Err_HostNotFound on resolution failure,
///         Err_ConnectionRefused on connect failure,
///         Err_Timeout on `timeout_ms` expiry.
void *rt_tcp_connect_for(rt_string host, int64_t port, int64_t timeout_ms) {
    rt_net_init_wsa();

    const char *host_ptr = NULL;
    size_t host_len = 0;
    if (!rt_net_cstr_no_embedded_nul(host, &host_ptr, &host_len) || host_len == 0) {
        rt_trap("Network: invalid host");
        return NULL;
    }

    if (port < 1 || port > 65535) {
        rt_trap("Network: invalid port number");
        return NULL;
    }
    int timeout_int = 0;
    if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int)) {
        rt_trap("Network: invalid timeout");
        return NULL;
    }

    // Copy host string
    char *host_cstr = (char *)malloc(host_len + 1);
    if (!host_cstr) {
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    memcpy(host_cstr, host_ptr, host_len);
    host_cstr[host_len] = '\0';

    // Resolve hostname
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    int status = getaddrinfo(host_cstr, port_str, &hints, &res);
    if (status != 0) {
        free(host_cstr);
        rt_trap_net("Network: host not found", Err_HostNotFound);
        return NULL;
    }

    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        socket_t candidate = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (candidate == INVALID_SOCK)
            continue;

        suppress_sigpipe(candidate);
        if (connect_socket_with_timeout(
                candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen, timeout_int, &last_err)) {
            sock = candidate;
            break;
        }

        CLOSE_SOCKET(candidate);
    }
    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        free(host_cstr);
        if (last_err == CONN_REFUSED) {
            rt_trap_net("Network: connection refused", Err_ConnectionRefused);
            return NULL;
        }
        if (rt_socket_error_is_timeout(last_err))
            rt_trap_net("Network: connection timeout", Err_Timeout);
        rt_trap_net("Network: connection failed", Err_NetworkError);
        return NULL;
    }

    // Enable TCP_NODELAY
    set_nodelay(sock);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_t));
    if (!tcp) {
        CLOSE_SOCKET(sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(tcp, rt_tcp_finalize);

    tcp->sock = sock;
    tcp->host = host_cstr;
    tcp->port = (int)port;
    tcp->local_port = get_local_port(sock);
    tcp->is_open = true;
    tcp->recv_timeout_ms = 0;
    tcp->send_timeout_ms = 0;

    return tcp;
}

//=============================================================================
// Tcp Client - Properties
//=============================================================================

// ---------------------------------------------------------------------------
// TCP client property accessors — null-safe getters returning the
// stored host/port, the local socket port (post-bind), and
// open/available state. Each is a trivial reach into `rt_tcp_t`.
// ---------------------------------------------------------------------------

/// @brief Return the host string the connection was opened with (empty for NULL/closed).
rt_string rt_tcp_host(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return rt_str_empty();
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return rt_const_cstr(tcp->host);
}

/// @brief Return the remote port of the connection (the one passed to `connect`).
int64_t rt_tcp_port(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return 0;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->port;
}

/// @brief Return the local (ephemeral) port the OS assigned to this socket.
int64_t rt_tcp_local_port(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return 0;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->local_port;
}

/// @brief 1 if the underlying socket is still open; 0 if closed (or `obj` is NULL).
int8_t rt_tcp_is_open(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return 0;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->is_open ? 1 : 0;
}

/// @brief Number of bytes available for non-blocking read (best-effort `FIONREAD`).
/// 0 means "unknown" or "nothing pending"; the actual recv may still block briefly.
int64_t rt_tcp_available(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return 0;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        return 0;

    int64_t bytes_available = 0;
    if (!rt_socket_available_bytes(tcp->sock, &bytes_available))
        return 0;
    return bytes_available;
}

//=============================================================================
// Tcp Client - Send Methods
//=============================================================================

/// @brief Send a Bytes payload — may write fewer bytes than requested if the kernel buffer is full.
/// @return Bytes actually sent, or -1 on error.
int64_t rt_tcp_send(void *obj, void *data) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return -1;
    }
    if (!data) {
        rt_trap("Network: NULL data");
        return -1;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return -1;
    }

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    if (len == 0)
        return 0;

    // Clamp to INT_MAX to prevent silent truncation on large buffers
    int to_send = (len > INT_MAX) ? INT_MAX : (int)len;
    int sent = send(tcp->sock, (const char *)buf, to_send, SEND_FLAGS);
    if (sent == SOCK_ERROR) {
        tcp->is_open = false;
        rt_trap_net("Network: send failed", net_classify_errno());
        return -1;
    }

    return sent;
}

/// @brief Send a string payload as UTF-8 bytes. May short-write — see `rt_tcp_send`.
int64_t rt_tcp_send_str(void *obj, rt_string text) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return -1;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return -1;
    }

    const char *text_ptr = rt_string_cstr(text);
    if (!text_ptr) {
        rt_trap("Network: NULL string");
        return -1;
    }
    int64_t len64 = rt_str_len(text);
    if (len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX) {
        rt_trap("Network: invalid string length");
        return -1;
    }
    size_t len = (size_t)len64;
    if (len == 0)
        return 0;

    // Clamp to INT_MAX to prevent silent truncation on large strings
    int to_send = (len > INT_MAX) ? INT_MAX : (int)len;
    int sent = send(tcp->sock, text_ptr, to_send, SEND_FLAGS);
    if (sent == SOCK_ERROR) {
        tcp->is_open = false;
        rt_trap_net("Network: send failed", net_classify_errno());
        return -1;
    }

    return sent;
}

/// @brief Send a Bytes payload, looping on partial writes until every byte is delivered.
/// Traps on send failure (closed peer, broken pipe, etc.).
void rt_tcp_send_all(void *obj, void *data) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return;
    }
    if (!data) {
        rt_trap("Network: NULL data");
        return;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return;
    }

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    int64_t total_sent = 0;
    while (total_sent < len) {
        int64_t remaining = len - total_sent;
        int chunk = (int)(remaining > INT_MAX ? INT_MAX : remaining);
        int sent = send(tcp->sock, (const char *)(buf + total_sent), chunk, SEND_FLAGS);
        if (sent == SOCK_ERROR) {
            tcp->is_open = false;
            rt_trap_net("Network: send failed", net_classify_errno());
            return;
        }
        if (sent == 0) {
            tcp->is_open = false;
            rt_trap_net("Network: connection closed by peer", Err_ConnectionClosed);
            return;
        }
        total_sent += sent;
    }
}

/// @brief Internal: send `len` bytes from a raw buffer without looking up Bytes metadata.
/// Used by HTTP / WebSocket layers that already have raw pointers.
void rt_tcp_send_all_raw(void *obj, const void *data, int64_t len) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return;
    }
    if (!data && len > 0) {
        rt_trap("Network: NULL data");
        return;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return;
    }

    if (len <= 0)
        return;

    const uint8_t *buf = (const uint8_t *)data;
    int64_t total_sent = 0;
    while (total_sent < len) {
        int64_t remaining = len - total_sent;
        int chunk = (int)(remaining > INT_MAX ? INT_MAX : remaining);
        int sent = send(tcp->sock, (const char *)(buf + total_sent), chunk, SEND_FLAGS);
        if (sent == SOCK_ERROR) {
            tcp->is_open = false;
            rt_trap_net("Network: send failed", net_classify_errno());
            return;
        }
        if (sent == 0) {
            tcp->is_open = false;
            rt_trap_net("Network: connection closed by peer", Err_ConnectionClosed);
            return;
        }
        total_sent += sent;
    }
}

//=============================================================================
// Tcp Client - Receive Methods
//=============================================================================

/// @brief Read up to `max_bytes` from the socket; returns a Bytes object (possibly empty).
///
/// Single-call recv — may return fewer bytes than requested. Empty Bytes means
/// either orderly peer close or expiry of a persistent receive timeout; peer
/// close also clears `is_open`. Other socket errors trap.
void *rt_tcp_recv(void *obj, int64_t max_bytes) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return NULL;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return NULL;
    }

    if (max_bytes <= 0)
        return rt_bytes_new(0);
    int recv_len = 0;
    if (!rt_net_i64_len_to_int(max_bytes, &recv_len)) {
        rt_trap("Network: receive size too large");
        return NULL;
    }

    // Allocate receive buffer
    void *result = rt_bytes_new(max_bytes);
    if (!result) {
        rt_trap("Network: receive allocation failed");
        return NULL;
    }
    uint8_t *buf = bytes_data(result);

    int received = recv(tcp->sock, (char *)buf, recv_len, 0);
    if (received == SOCK_ERROR) {
        if (rt_socket_recv_timed_out()) {
            // Timeout - release over-allocated buffer and return empty bytes
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            return rt_bytes_new(0);
        }
        tcp->is_open = false;
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        rt_trap_net("Network: receive failed", net_classify_errno());
        return NULL;
    }

    if (received == 0) {
        // Connection closed by peer
        tcp->is_open = false;
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        return rt_bytes_new(0);
    }

    // Return exact size received (release over-allocated buffer)
    if (received < max_bytes) {
        void *exact = rt_bytes_new(received);
        if (!exact) {
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            rt_trap("Network: receive allocation failed");
            return NULL;
        }
        memcpy(bytes_data(exact), buf, received);
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        return exact;
    }

    return result;
}

/// @brief Read up to `max_bytes` and decode as a UTF-8 string. Convenience over `rt_tcp_recv`.
rt_string rt_tcp_recv_str(void *obj, int64_t max_bytes) {
    void *bytes = rt_tcp_recv(obj, max_bytes);
    rt_string str = rt_bytes_to_str(bytes);
    if (bytes && rt_obj_release_check0(bytes))
        rt_obj_free(bytes);
    return str;
}

/// @brief Receive *exactly* `count` bytes, looping until the buffer fills or the connection drops.
/// Traps on short read (premature EOF) — use `rt_tcp_recv` if partial reads are acceptable.
void *rt_tcp_recv_exact(void *obj, int64_t count) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return NULL;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return NULL;
    }

    if (count <= 0)
        return rt_bytes_new(0);
    if (count > INT_MAX) {
        rt_trap("Network: receive size too large");
        return NULL;
    }

    void *result = rt_bytes_new(count);
    uint8_t *buf = bytes_data(result);

    int64_t total_received = 0;
    while (total_received < count) {
        int chunk_len = 0;
        if (!rt_net_i64_len_to_int(count - total_received, &chunk_len)) {
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            rt_trap("Network: receive size too large");
            return NULL;
        }
        int received = recv(tcp->sock, (char *)(buf + total_received), chunk_len, 0);
        if (received == SOCK_ERROR) {
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            if (rt_socket_recv_timed_out()) {
                rt_trap_net("Network: receive timeout", Err_Timeout);
                return NULL;
            }
            tcp->is_open = false;
            rt_trap_net("Network: receive failed", net_classify_errno());
            return NULL;
        }
        if (received == 0) {
            tcp->is_open = false;
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            rt_trap_net("Network: connection closed before receiving all data",
                        Err_ConnectionClosed);
            return NULL;
        }
        total_received += received;
    }

    return result;
}

/// @brief Read until `\n` is seen (CR is also stripped) and return the line as a string.
///
/// Caps at 64 KB to prevent unbounded growth from a malicious peer.
/// Traps if the connection closes or a configured receive timeout expires
/// before a newline is received.
rt_string rt_tcp_recv_line(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return rt_str_empty();
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open) {
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);
        return rt_str_empty();
    }

    // Line buffer with initial capacity
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line) {
        rt_trap("Network: memory allocation failed");
        return rt_str_empty();
    }

    while (1) {
        char c;
        int received = recv(tcp->sock, &c, 1, 0);
        if (received == SOCK_ERROR) {
            free(line);
            if (rt_socket_recv_timed_out()) {
                rt_trap_net("Network: receive timeout", Err_Timeout);
                return rt_str_empty();
            }
            tcp->is_open = false;
            rt_trap_net("Network: receive failed", net_classify_errno());
            return rt_str_empty();
        }
        if (received == 0) {
            free(line);
            tcp->is_open = false;
            rt_trap_net("Network: connection closed before end of line", Err_ConnectionClosed);
        }

        if (c == '\n') {
            // Strip trailing CR if present
            if (len > 0 && line[len - 1] == '\r') {
                len--;
            }
            break;
        }

        // Cap at 64KB to prevent unbounded memory growth from a malicious peer.
        if (len >= 65536) {
            free(line);
            rt_trap_net("Network: line exceeds 64KB limit", Err_ProtocolError);
            return rt_str_empty();
        }

        // Add character to buffer, growing if needed.
        if (len >= cap) {
            cap *= 2;
            if (cap > 65536)
                cap = 65536;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line) {
                free(line);
                rt_trap("Network: memory allocation failed");
                return rt_str_empty();
            }
            line = new_line;
        }
        line[len++] = c;
    }

    // Create string result
    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

//=============================================================================
// Tcp Client - Timeout and Close
//=============================================================================

/// @brief Apply `SO_RCVTIMEO` to the socket — recv operations fail with timeout after `timeout_ms`.
void rt_tcp_set_recv_timeout(void *obj, int64_t timeout_ms) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    int timeout_int = 0;
    if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int)) {
        rt_trap("Network: invalid timeout");
        return;
    }
    tcp->recv_timeout_ms = timeout_int;
    set_socket_timeout(tcp->sock, timeout_int, true);
}

/// @brief Apply `SO_SNDTIMEO` to the socket — send operations fail with timeout after `timeout_ms`.
void rt_tcp_set_send_timeout(void *obj, int64_t timeout_ms) {
    if (!obj) {
        rt_trap("Network: NULL connection");
        return;
    }

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    int timeout_int = 0;
    if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int)) {
        rt_trap("Network: invalid timeout");
        return;
    }
    tcp->send_timeout_ms = timeout_int;
    set_socket_timeout(tcp->sock, timeout_int, false);
}

/// @brief Close the socket immediately. Subsequent send/recv calls return error.
void rt_tcp_close(void *obj) {
    if (!obj)
        return;

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (tcp->is_open) {
        CLOSE_SOCKET(tcp->sock);
        tcp->is_open = false;
    }
}

/// @brief Expose the underlying socket FD — used by `select`/`poll` integration and by TLS bind.
socket_t rt_tcp_socket_fd(void *obj) {
    if (!obj)
        return INVALID_SOCK;
    return ((rt_tcp_t *)obj)->sock;
}

/// @brief Forget the socket without closing it — caller takes ownership of the FD.
///
/// Used when TLS or a higher-level protocol needs to assume
/// ownership of the connection (and therefore of socket lifetime).
void rt_tcp_detach_socket(void *obj) {
    if (!obj)
        return;
    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    tcp->sock = INVALID_SOCK;
    tcp->is_open = false;
}

//=============================================================================
// TcpServer - Creation
//=============================================================================

/// @brief Internal listener factory — bind to `address:port`, listen with `SOMAXCONN` backlog.
///
/// Used by both `rt_tcp_server_listen` (binds to all interfaces)
/// and `rt_tcp_server_listen_at` (binds to a specific interface).
/// Sets `SO_REUSEADDR` so a hot-restart doesn't fail with
/// "Address already in use" while the prior socket lingers in TIME_WAIT.
static void *rt_tcp_server_listen_impl(const char *address, int64_t port) {
    rt_net_init_wsa();

    if (port < 0 || port > 65535) {
        rt_trap("Network: invalid port number");
        return NULL;
    }

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (!address)
        hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(address, port_str, &hints, &res) != 0) {
        rt_trap("Network: invalid address");
        return NULL;
    }

    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    int bound_family = AF_UNSPEC;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        socket_t candidate = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (candidate == INVALID_SOCK)
            continue;

        suppress_sigpipe(candidate);

        int reuse = 1;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#ifdef IPV6_V6ONLY
        if (rp->ai_family == AF_INET6) {
            int v6only = address ? 1 : 0;
            setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));
        }
#endif

        if (bind(candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0 &&
            listen(candidate, SOMAXCONN) == 0) {
            sock = candidate;
            bound_family = rp->ai_family;
            break;
        }

        last_err = GET_LAST_ERROR();
        CLOSE_SOCKET(candidate);
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        if (last_err == ADDR_IN_USE) {
            rt_trap_net("Network: port already in use", Err_NetworkError);
            return NULL;
        }
        if (last_err == PERM_DENIED) {
            rt_trap_net("Network: permission denied (port < 1024?)", Err_NetworkError);
            return NULL;
        }
        rt_trap_net("Network: bind failed", Err_NetworkError);
        return NULL;
    }

    const char *bound_addr = address ? address : (bound_family == AF_INET6 ? "::" : "0.0.0.0");
    char *addr_cstr = (char *)malloc(strlen(bound_addr) + 1);
    if (!addr_cstr) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    memcpy(addr_cstr, bound_addr, strlen(bound_addr) + 1);

    rt_tcp_server_t *server =
        (rt_tcp_server_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_server_t));
    if (!server) {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(server, rt_tcp_server_finalize);

    server->sock = sock;
    server->address = addr_cstr;
    server->port = port == 0 ? get_local_port(sock) : (int)port;
    server->is_listening = true;

    return server;
}

/// @brief Listen on `port` on all local interfaces (`0.0.0.0`).
void *rt_tcp_server_listen(int64_t port) {
    return rt_tcp_server_listen_impl(NULL, port);
}

/// @brief Listen on `port` bound to a specific local address (e.g. `"127.0.0.1"` or `"::1"`).
void *rt_tcp_server_listen_at(rt_string address, int64_t port) {
    const char *addr_ptr = NULL;
    size_t addr_len = 0;
    if (!rt_net_cstr_no_embedded_nul(address, &addr_ptr, &addr_len) || addr_len == 0) {
        rt_trap("Network: invalid address");
        return NULL;
    }
    return rt_tcp_server_listen_impl(addr_ptr, port);
}

//=============================================================================
// TcpServer - Properties
//=============================================================================

/// @brief Listening port (useful when the caller passed 0 to ask the OS to pick).
int64_t rt_tcp_server_port(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL server");
        return 0;
    }

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->port;
}

/// @brief Bound listen address (e.g. "0.0.0.0", "::1"), or empty string if not listening.
rt_string rt_tcp_server_address(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL server");
        return rt_str_empty();
    }

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return rt_const_cstr(server->address);
}

/// @brief 1 if the server socket is still in the `listen()` state.
int8_t rt_tcp_server_is_listening(void *obj) {
    if (!obj) {
        rt_trap("Network: NULL server");
        return 0;
    }

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->is_listening ? 1 : 0;
}

//=============================================================================
// TcpServer - Accept and Close
//=============================================================================

/// @brief Accept the next pending connection — blocks indefinitely. Returns a connected
/// `rt_tcp_t*`.
void *rt_tcp_server_accept(void *obj) {
    return rt_tcp_server_accept_for(obj, 0);
}

/// @brief Accept with a timeout — uses `select` first to wait for readability.
/// @return A connected client `rt_tcp_t*` on accept, NULL on timeout.
void *rt_tcp_server_accept_for(void *obj, int64_t timeout_ms) {
    if (!obj) {
        rt_trap("Network: NULL server");
        return NULL;
    }

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (!server->is_listening) {
        rt_trap_net("Network: server not listening", Err_ConnectionClosed);
        return NULL;
    }
    if (timeout_ms < 0) {
        rt_trap("Network: invalid timeout");
        return NULL;
    }

    // Use select for timeout
    if (timeout_ms > 0) {
        int timeout_int = 0;
        if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int)) {
            rt_trap("Network: invalid timeout");
            return NULL;
        }
        int ready = wait_socket(server->sock, timeout_int, false);
        if (ready == 0) {
            // Timeout - return NULL
            return NULL;
        }
        if (ready < 0) {
            int err = GET_LAST_ERROR();
            if (!server->is_listening || rt_socket_accept_interrupted_by_close(err))
                return NULL;
            rt_trap_net("Network: accept failed", Err_NetworkError);
            return NULL;
        }
    }

    // Accept connection
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);

    socket_t client_sock = accept(server->sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock == INVALID_SOCK) {
        int err = GET_LAST_ERROR();
        // Check if server was closed
        if (!server->is_listening || rt_socket_accept_interrupted_by_close(err))
            return NULL;
        rt_trap_net("Network: accept failed", Err_NetworkError);
        return NULL;
    }

    suppress_sigpipe(client_sock);

    // Enable TCP_NODELAY
    set_nodelay(client_sock);

    // Get client info
    char host_buf[NI_MAXHOST];
    char service_buf[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *)&client_addr,
                    client_len,
                    host_buf,
                    sizeof(host_buf),
                    service_buf,
                    sizeof(service_buf),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(host_buf, sizeof(host_buf), "%s", "");
        snprintf(service_buf, sizeof(service_buf), "%d", 0);
    }

    char *host_cstr = (char *)malloc(strlen(host_buf) + 1);
    if (!host_cstr) {
        CLOSE_SOCKET(client_sock);
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    memcpy(host_cstr, host_buf, strlen(host_buf) + 1);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_t));
    if (!tcp) {
        CLOSE_SOCKET(client_sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(tcp, rt_tcp_finalize);

    tcp->sock = client_sock;
    tcp->host = host_cstr;
    tcp->port = parse_numeric_service_port(service_buf);
    tcp->local_port = get_local_port(client_sock);
    tcp->is_open = true;
    tcp->recv_timeout_ms = 0;
    tcp->send_timeout_ms = 0;

    return tcp;
}

/// @brief Close the listening socket — pending `accept` calls return error.
void rt_tcp_server_close(void *obj) {
    if (!obj)
        return;

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (server->is_listening) {
        server->is_listening = false;
        CLOSE_SOCKET(server->sock);
    }
}

//=============================================================================
// Udp Socket Structure
//=============================================================================
