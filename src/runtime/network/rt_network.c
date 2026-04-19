//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

// Platform feature macros must appear before ANY includes
#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1 // macOS: expose BSD extensions
#define _GNU_SOURCE 1      // Linux/ViperDOS: expose ip_mreq
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

/// @brief GC finalizer for TCP server — close the listening socket and free the bound-address string.
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
// Windows WSA Initialization
//=============================================================================

#ifdef _WIN32
static volatile LONG wsa_init_state = 0; // 0=uninit, 1=in-progress, 2=done

/// @brief atexit handler — calls `WSACleanup` so Windows releases its socket subsystem.
static void rt_net_cleanup_wsa(void) {
    WSACleanup();
}

/// @brief Lazily initialise WinSock 2.2 (Windows only — POSIX is a no-op).
///
/// Three-state machine (`uninit/in-progress/done`) makes initialisation
/// idempotent and thread-safe without a heavyweight mutex. Multiple
/// threads racing to first-call this function will see exactly one
/// `WSAStartup`; all others spin briefly until it completes.
void rt_net_init_wsa(void) {
    // Fast path: already done.
    if (wsa_init_state == 2)
        return;

    // Try to claim the init slot (0→1). Losers spin until 2.
    LONG prev = InterlockedCompareExchange(&wsa_init_state, 1, 0);
    if (prev == 2)
        return;
    if (prev == 1) {
        while (wsa_init_state != 2)
            Sleep(0);
        return;
    }

    // We won (prev == 0, state is now 1). Do the actual init.
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        InterlockedExchange(&wsa_init_state, 0);
        rt_trap("Network: WSAStartup failed");
    }
    // Ensure WSACleanup is called on process exit.
    atexit(rt_net_cleanup_wsa);
    InterlockedExchange(&wsa_init_state, 2);
}
#else
void rt_net_init_wsa(void) {}
#endif

//=============================================================================
// Socket Helpers
//=============================================================================

/// @brief Set socket to non-blocking mode.
/// @return true on success, false if the syscall failed.
static bool set_nonblocking(socket_t sock, bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    int new_flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(sock, F_SETFL, new_flags) == 0;
#endif
}

/// @brief Enable TCP_NODELAY on socket.
static void set_nodelay(socket_t sock) {
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

/// @brief Set socket timeout.
void set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv) {
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(
        sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/// @brief Wait for socket to become readable/writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result;
    if (for_write)
        result = select((int)(sock + 1), NULL, &fds, NULL, &tv);
    else
        result = select((int)(sock + 1), &fds, NULL, NULL, &tv);

    return result;
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

/// @brief Cross-platform check: did the last socket op fail because of a timeout / would-block?
static bool socket_recv_timed_out(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT;
#endif
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
        if (!set_nonblocking(sock, true)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }

        int connect_result = connect(sock, addr, addrlen);
        if (connect_result == SOCK_ERROR) {
            int err = GET_LAST_ERROR();
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
#else
            if (err == EINPROGRESS)
#endif
            {
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

        if (!set_nonblocking(sock, false)) {
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

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0') {
        rt_trap("Network: invalid host");
    }

    if (port < 1 || port > 65535) {
        rt_trap("Network: invalid port number");
    }

    // Copy host string
    size_t host_len = strlen(host_ptr);
    char *host_cstr = (char *)malloc(host_len + 1);
    if (!host_cstr) {
        rt_trap("Network: memory allocation failed");
    }
    memcpy(host_cstr, host_ptr, host_len + 1);

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
    }

    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        socket_t candidate = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (candidate == INVALID_SOCK)
            continue;

        suppress_sigpipe(candidate);
        if (connect_socket_with_timeout(
                candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen, (int)timeout_ms, &last_err)) {
            sock = candidate;
            break;
        }

        CLOSE_SOCKET(candidate);
    }
    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        free(host_cstr);
        if (last_err == CONN_REFUSED)
            rt_trap_net("Network: connection refused", Err_ConnectionRefused);
#ifdef _WIN32
        if (last_err == WSAETIMEDOUT)
#else
        if (last_err == ETIMEDOUT)
#endif
            rt_trap_net("Network: connection timeout", Err_Timeout);
        rt_trap_net("Network: connection failed", Err_NetworkError);
    }

    // Enable TCP_NODELAY
    set_nodelay(sock);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_t));
    if (!tcp) {
        CLOSE_SOCKET(sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
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
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return rt_const_cstr(tcp->host);
}

/// @brief Return the remote port of the connection (the one passed to `connect`).
int64_t rt_tcp_port(void *obj) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->port;
}

/// @brief Return the local (ephemeral) port the OS assigned to this socket.
int64_t rt_tcp_local_port(void *obj) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->local_port;
}

/// @brief 1 if the underlying socket is still open; 0 if closed (or `obj` is NULL).
int8_t rt_tcp_is_open(void *obj) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->is_open ? 1 : 0;
}

/// @brief Number of bytes available for non-blocking read (best-effort `FIONREAD`).
/// 0 means "unknown" or "nothing pending"; the actual recv may still block briefly.
int64_t rt_tcp_available(void *obj) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        return 0;

#ifdef _WIN32
    u_long bytes_available = 0;
    ioctlsocket(tcp->sock, FIONREAD, &bytes_available);
    return (int64_t)bytes_available;
#else
    int bytes_available = 0;
    ioctl(tcp->sock, FIONREAD, &bytes_available);
    return (int64_t)bytes_available;
#endif
}

//=============================================================================
// Tcp Client - Send Methods
//=============================================================================

/// @brief Send a Bytes payload — may write fewer bytes than requested if the kernel buffer is full.
/// @return Bytes actually sent, or -1 on error.
int64_t rt_tcp_send(void *obj, void *data) {
    if (!obj)
        rt_trap("Network: NULL connection");
    if (!data)
        rt_trap("Network: NULL data");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

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
    }

    return sent;
}

/// @brief Send a string payload as UTF-8 bytes. May short-write — see `rt_tcp_send`.
int64_t rt_tcp_send_str(void *obj, rt_string text) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

    const char *text_ptr = rt_string_cstr(text);
    size_t len = strlen(text_ptr);
    if (len == 0)
        return 0;

    // Clamp to INT_MAX to prevent silent truncation on large strings
    int to_send = (len > INT_MAX) ? INT_MAX : (int)len;
    int sent = send(tcp->sock, text_ptr, to_send, SEND_FLAGS);
    if (sent == SOCK_ERROR) {
        tcp->is_open = false;
        rt_trap_net("Network: send failed", net_classify_errno());
    }

    return sent;
}

/// @brief Send a Bytes payload, looping on partial writes until every byte is delivered.
/// Traps on send failure (closed peer, broken pipe, etc.).
void rt_tcp_send_all(void *obj, void *data) {
    if (!obj)
        rt_trap("Network: NULL connection");
    if (!data)
        rt_trap("Network: NULL data");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

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
        }
        if (sent == 0) {
            tcp->is_open = false;
            rt_trap_net("Network: connection closed by peer", Err_ConnectionClosed);
        }
        total_sent += sent;
    }
}

/// @brief Internal: send `len` bytes from a raw buffer without looking up Bytes metadata.
/// Used by HTTP / WebSocket layers that already have raw pointers.
void rt_tcp_send_all_raw(void *obj, const void *data, int64_t len) {
    if (!obj)
        rt_trap("Network: NULL connection");
    if (!data && len > 0)
        rt_trap("Network: NULL data");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

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
        }
        if (sent == 0) {
            tcp->is_open = false;
            rt_trap_net("Network: connection closed by peer", Err_ConnectionClosed);
        }
        total_sent += sent;
    }
}

//=============================================================================
// Tcp Client - Receive Methods
//=============================================================================

/// @brief Read up to `max_bytes` from the socket; returns a Bytes object (possibly empty).
///
/// Single-call recv — may return fewer bytes than requested. An
/// empty Bytes signals connection closed by peer; a trap means a
/// real socket error.
void *rt_tcp_recv(void *obj, int64_t max_bytes) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

    if (max_bytes <= 0)
        return rt_bytes_new(0);

    // Allocate receive buffer
    void *result = rt_bytes_new(max_bytes);
    uint8_t *buf = bytes_data(result);

    int received = recv(tcp->sock, (char *)buf, (int)max_bytes, 0);
    if (received == SOCK_ERROR) {
        if (socket_recv_timed_out()) {
            // Timeout - release over-allocated buffer and return empty bytes
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            return rt_bytes_new(0);
        }
        tcp->is_open = false;
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        rt_trap_net("Network: receive failed", net_classify_errno());
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
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

    if (count <= 0)
        return rt_bytes_new(0);

    void *result = rt_bytes_new(count);
    uint8_t *buf = bytes_data(result);

    int64_t total_received = 0;
    while (total_received < count) {
        int received =
            recv(tcp->sock, (char *)(buf + total_received), (int)(count - total_received), 0);
        if (received == SOCK_ERROR) {
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            if (socket_recv_timed_out())
                rt_trap_net("Network: receive timeout", Err_Timeout);
            tcp->is_open = false;
            rt_trap_net("Network: receive failed", net_classify_errno());
        }
        if (received == 0) {
            tcp->is_open = false;
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            rt_trap_net("Network: connection closed before receiving all data",
                        Err_ConnectionClosed);
        }
        total_received += received;
    }

    return result;
}

/// @brief Read until `\n` is seen (CR is also stripped) and return the line as a string.
///
/// Caps at 64 KB to prevent unbounded growth from a malicious peer.
/// Returns the empty string on connection close.
rt_string rt_tcp_recv_line(void *obj) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap_net("Network: connection closed", Err_ConnectionClosed);

    // Line buffer with initial capacity
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        rt_trap("Network: memory allocation failed");

    while (1) {
        char c;
        int received = recv(tcp->sock, &c, 1, 0);
        if (received == SOCK_ERROR) {
            free(line);
            if (socket_recv_timed_out())
                rt_trap_net("Network: receive timeout", Err_Timeout);
            tcp->is_open = false;
            rt_trap_net("Network: receive failed", net_classify_errno());
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
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    tcp->recv_timeout_ms = (int)timeout_ms;
    set_socket_timeout(tcp->sock, (int)timeout_ms, true);
}

/// @brief Apply `SO_SNDTIMEO` to the socket — send operations fail with timeout after `timeout_ms`.
void rt_tcp_set_send_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    tcp->send_timeout_ms = (int)timeout_ms;
    set_socket_timeout(tcp->sock, (int)timeout_ms, false);
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

/// @brief Internal listener factory — bind to `address:port`, listen with backlog 128.
///
/// Used by both `rt_tcp_server_listen` (binds to all interfaces)
/// and `rt_tcp_server_listen_at` (binds to a specific interface).
/// Sets `SO_REUSEADDR` so a hot-restart doesn't fail with
/// "Address already in use" while the prior socket lingers in TIME_WAIT.
static void *rt_tcp_server_listen_impl(const char *address, int64_t port) {
    rt_net_init_wsa();

    if (port < 1 || port > 65535)
        rt_trap("Network: invalid port number");

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

    if (getaddrinfo(address, port_str, &hints, &res) != 0)
        rt_trap("Network: invalid address");

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
        if (last_err == ADDR_IN_USE)
            rt_trap_net("Network: port already in use", Err_NetworkError);
        if (last_err == PERM_DENIED)
            rt_trap_net("Network: permission denied (port < 1024?)", Err_NetworkError);
        rt_trap_net("Network: bind failed", Err_NetworkError);
    }

    const char *bound_addr = address ? address : (bound_family == AF_INET6 ? "::" : "0.0.0.0");
    char *addr_cstr = (char *)malloc(strlen(bound_addr) + 1);
    if (!addr_cstr) {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }
    memcpy(addr_cstr, bound_addr, strlen(bound_addr) + 1);

    rt_tcp_server_t *server =
        (rt_tcp_server_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_server_t));
    if (!server) {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: memory allocation failed");
    }
    rt_obj_set_finalizer(server, rt_tcp_server_finalize);

    server->sock = sock;
    server->address = addr_cstr;
    server->port = (int)port;
    server->is_listening = true;

    return server;
}

/// @brief Listen on `port` on all local interfaces (`0.0.0.0`).
void *rt_tcp_server_listen(int64_t port) {
    return rt_tcp_server_listen_impl(NULL, port);
}

/// @brief Listen on `port` bound to a specific local address (e.g. `"127.0.0.1"` or `"::1"`).
void *rt_tcp_server_listen_at(rt_string address, int64_t port) {
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: invalid address");
    return rt_tcp_server_listen_impl(addr_ptr, port);
}

//=============================================================================
// TcpServer - Properties
//=============================================================================

/// @brief Listening port (useful when the caller passed 0 to ask the OS to pick).
int64_t rt_tcp_server_port(void *obj) {
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->port;
}

/// @brief Bound listen address (e.g. "0.0.0.0", "::1"), or empty string if not listening.
rt_string rt_tcp_server_address(void *obj) {
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return rt_const_cstr(server->address);
}

/// @brief 1 if the server socket is still in the `listen()` state.
int8_t rt_tcp_server_is_listening(void *obj) {
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->is_listening ? 1 : 0;
}

//=============================================================================
// TcpServer - Accept and Close
//=============================================================================

/// @brief Accept the next pending connection — blocks indefinitely. Returns a connected `rt_tcp_t*`.
void *rt_tcp_server_accept(void *obj) {
    return rt_tcp_server_accept_for(obj, 0);
}

/// @brief Accept with a timeout — uses `select` first to wait for readability.
/// @return A connected client `rt_tcp_t*` on accept, NULL on timeout.
void *rt_tcp_server_accept_for(void *obj, int64_t timeout_ms) {
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (!server->is_listening)
        rt_trap_net("Network: server not listening", Err_ConnectionClosed);

    // Use select for timeout
    if (timeout_ms > 0) {
        int ready = wait_socket(server->sock, (int)timeout_ms, false);
        if (ready == 0) {
            // Timeout - return NULL
            return NULL;
        }
        if (ready < 0) {
            if (!server->is_listening)
                return NULL;
            rt_trap_net("Network: accept failed", Err_NetworkError);
        }
    }

    // Accept connection
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);

    socket_t client_sock = accept(server->sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock == INVALID_SOCK) {
        // Check if server was closed
        if (!server->is_listening)
            return NULL;
        rt_trap_net("Network: accept failed", Err_NetworkError);
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
    }
    memcpy(host_cstr, host_buf, strlen(host_buf) + 1);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tcp_t));
    if (!tcp) {
        CLOSE_SOCKET(client_sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
    }
    rt_obj_set_finalizer(tcp, rt_tcp_finalize);

    tcp->sock = client_sock;
    tcp->host = host_cstr;
    tcp->port = atoi(service_buf);
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
