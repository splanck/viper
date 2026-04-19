//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ws_server.c
// Purpose: WebSocket server with broadcast and per-client messaging.
// Key invariants:
//   - Accept loop runs in background thread.
//   - Clients tracked in a fixed-size array protected by mutex.
//   - WebSocket framing reuses logic from rt_websocket.c.
// Ownership/Lifetime:
//   - Server GC-managed. Stop() or finalizer closes all clients.
// Links: rt_ws_server.h (API), rt_websocket.h (framing)
//
//===----------------------------------------------------------------------===//

#include "rt_ws_server.h"
#include "rt_websocket.h"

#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
typedef SOCKET ws_socket_t;
#define WS_INVALID_SOCK INVALID_SOCKET
#define WS_SOCK_ERROR SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int ws_socket_t;
#define WS_INVALID_SOCK (-1)
#define WS_SOCK_ERROR (-1)
#endif

#ifdef _WIN32
typedef CRITICAL_SECTION ws_mutex_t;
#define WS_MUTEX_INIT(m) InitializeCriticalSection(m)
#define WS_MUTEX_LOCK(m) EnterCriticalSection(m)
#define WS_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define WS_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
typedef pthread_mutex_t ws_mutex_t;
#define WS_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define WS_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define WS_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define WS_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

// SIGPIPE suppression
#if defined(__linux__) || defined(__viperdos__)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

#include "rt_trap.h"
extern char *rt_ws_compute_accept_key(const char *key_cstr); // from rt_websocket.c
extern ws_socket_t rt_tcp_socket_fd(void *obj);

//=============================================================================
// Internal Structures
//=============================================================================

#define WS_SERVER_MAX_CLIENTS 128

// WebSocket opcodes (duplicated from rt_websocket.c to avoid dependency)
#define WS_OP_TEXT 0x01
#define WS_OP_BINARY 0x02
#define WS_OP_CONTINUATION 0x00
#define WS_OP_CLOSE 0x08
#define WS_OP_PING 0x09
#define WS_OP_PONG 0x0A
#define WS_FIN 0x80
#define WS_MASK 0x80
#define WS_CLOSE_PROTOCOL_ERROR 1002
#define WS_CLOSE_INVALID_DATA 1007

typedef struct {
    void *tcp; // TCP connection
    bool active;
} ws_client_t;

typedef struct {
    void *tcp_server;
    int64_t port;
    char *subprotocol;
    bool running;
    ws_client_t clients[WS_SERVER_MAX_CLIENTS];
    int client_count;
    ws_mutex_t lock;
    bool lock_initialized;
#ifdef _WIN32
    HANDLE accept_thread;
#else
    pthread_t accept_thread;
#endif
    bool thread_started;
} rt_ws_server_impl;

#include "rt_ws_shared.inc"

static void ws_release_tcp(void **tcp_ptr) {
    if (!tcp_ptr || !*tcp_ptr)
        return;
    rt_tcp_close(*tcp_ptr);
    if (rt_obj_release_check0(*tcp_ptr))
        rt_obj_free(*tcp_ptr);
    *tcp_ptr = NULL;
}

static int ws_server_send_raw(void *tcp, const void *data, size_t len) {
    ws_socket_t sock = rt_tcp_socket_fd(tcp);
    size_t total = 0;

    if (sock == WS_INVALID_SOCK)
        return 0;

    while (total < len) {
        size_t remaining = len - total;
        int chunk = (int)(remaining > (size_t)INT_MAX ? INT_MAX : remaining);
        int sent = send(sock, (const char *)data + total, chunk, SEND_FLAGS);
        if (sent == WS_SOCK_ERROR || sent == 0)
            return 0;
        total += (size_t)sent;
    }

    return 1;
}

//=============================================================================
// WebSocket Framing Helpers
//=============================================================================

/// @brief Send a WebSocket frame over a raw TCP connection (server-side: no masking).
static int ws_server_send_frame(void *tcp, uint8_t opcode, const void *data, size_t len) {
    uint8_t header[10];
    size_t header_len = 2;

    header[0] = WS_FIN | opcode;

    // Server frames are NOT masked (RFC 6455 §5.1)
    if (len < 126) {
        header[1] = (uint8_t)len;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len);
        header_len = 4;
    } else {
        header[1] = 127;
        ws_encode_u64_len(header + 2, len);
        header_len = 10;
    }

    // Send header
    if (!ws_server_send_raw(tcp, header, header_len))
        return 0;

    // Send data
    if (len > 0 && data)
        return ws_server_send_raw(tcp, data, len);

    return 1;
}

/// @brief Read a WebSocket frame from a TCP connection (client frames are masked).
static int ws_server_recv_frame(
    void *tcp, uint8_t *fin_out, uint8_t *opcode_out, uint8_t **data_out, size_t *len_out) {
    *data_out = NULL;
    *len_out = 0;

    // Read 2-byte header
    void *hdr = rt_tcp_recv_exact(tcp, 2);
    if (!hdr)
        return 0;

    typedef struct {
        int64_t l;
        uint8_t *d;
    } bi;

    uint8_t *h = ((bi *)hdr)->d;
    uint8_t first = h[0];
    uint8_t second = h[1];
    *fin_out = (first & WS_FIN) ? 1 : 0;
    *opcode_out = first & 0x0F;
    uint8_t masked = second & WS_MASK;
    size_t payload_len = second & 0x7F;
    if (rt_obj_release_check0(hdr))
        rt_obj_free(hdr);

    if ((first & 0x70) != 0 || !ws_is_valid_opcode(*opcode_out)) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
        return 0;
    }

    if (!masked) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
        return 0;
    }

    // Extended length
    if (payload_len == 126) {
        void *ext = rt_tcp_recv_exact(tcp, 2);
        if (!ext)
            return 0;
        uint8_t *e = ((bi *)ext)->d;
        payload_len = ((size_t)e[0] << 8) | e[1];
        if (rt_obj_release_check0(ext))
            rt_obj_free(ext);
    } else if (payload_len == 127) {
        void *ext = rt_tcp_recv_exact(tcp, 8);
        if (!ext)
            return 0;
        uint8_t *e = ((bi *)ext)->d;
        if (!ws_decode_u64_len(e, &payload_len)) {
            if (rt_obj_release_check0(ext))
                rt_obj_free(ext);
            ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
            return 0;
        }
        if (rt_obj_release_check0(ext))
            rt_obj_free(ext);
    }

    if (*opcode_out >= 0x08 && (!*fin_out || payload_len > 125)) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
        return 0;
    }

    if (payload_len > 64 * 1024 * 1024) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xF1", 2);
        return 0; // Too large
    }

    void *m = rt_tcp_recv_exact(tcp, 4);
    if (!m)
        return 0;
    uint8_t mask[4];
    memcpy(mask, ((bi *)m)->d, 4);
    if (rt_obj_release_check0(m))
        rt_obj_free(m);

    // Read payload
    if (payload_len > 0) {
        void *payload = rt_tcp_recv_exact(tcp, (int64_t)payload_len);
        if (!payload)
            return 0;
        uint8_t *p = ((bi *)payload)->d;

        // Unmask
        for (size_t i = 0; i < payload_len; i++)
            p[i] ^= mask[i % 4];

        *data_out = (uint8_t *)malloc(payload_len);
        if (*data_out) {
            memcpy(*data_out, p, payload_len);
            *len_out = payload_len;
        }
        if (rt_obj_release_check0(payload))
            rt_obj_free(payload);
    }

    return 1;
}

/// @brief Perform server-side WebSocket upgrade handshake.
static int ws_server_handshake(void *tcp, const char *required_subprotocol) {
    // Read HTTP upgrade request
    rt_string line = rt_tcp_recv_line(tcp);
    if (!line)
        return 0;
    const char *request_line = rt_string_cstr(line);
    if (!ws_request_line_is_valid(request_line)) {
        rt_string_unref(line);
        return 0;
    }
    rt_string_unref(line);

    char ws_key[128] = {0};
    char host_header[256] = {0};
    char origin_header[256] = {0};
    char protocol_header[256] = {0};
    int saw_upgrade = 0;
    int saw_connection = 0;
    int saw_version = 0;

    // Read headers
    while (1) {
        rt_string hdr = rt_tcp_recv_line(tcp);
        if (!hdr)
            return 0;
        const char *h = rt_string_cstr(hdr);
        if (!h || *h == '\0') {
            rt_string_unref(hdr);
            break;
        }
        // Look for Sec-WebSocket-Key
        if (strncasecmp(h, "Sec-WebSocket-Key:", 18) == 0) {
            const char *val = h + 18;
            while (*val == ' ')
                val++;
            strncpy(ws_key, val, sizeof(ws_key) - 1);
        } else if (strncasecmp(h, "Host:", 5) == 0) {
            char *trimmed = ws_strdup_trimmed(h + 5);
            if (!trimmed) {
                rt_string_unref(hdr);
                return 0;
            }
            snprintf(host_header, sizeof(host_header), "%s", trimmed);
            free(trimmed);
        } else if (strncasecmp(h, "Origin:", 7) == 0) {
            char *trimmed = ws_strdup_trimmed(h + 7);
            if (!trimmed) {
                rt_string_unref(hdr);
                return 0;
            }
            snprintf(origin_header, sizeof(origin_header), "%s", trimmed);
            free(trimmed);
        } else if (strncasecmp(h, "Sec-WebSocket-Protocol:", 23) == 0) {
            char *trimmed = ws_strdup_trimmed(h + 23);
            if (!trimmed) {
                rt_string_unref(hdr);
                return 0;
            }
            snprintf(protocol_header, sizeof(protocol_header), "%s", trimmed);
            free(trimmed);
        } else if (strncasecmp(h, "Upgrade:", 8) == 0) {
            const char *val = h + 8;
            while (*val == ' ')
                val++;
            saw_upgrade = strcasecmp(val, "websocket") == 0;
        } else if (strncasecmp(h, "Connection:", 11) == 0) {
            const char *val = h + 11;
            while (*val == ' ')
                val++;
            saw_connection = ws_header_has_upgrade_token(val);
        } else if (strncasecmp(h, "Sec-WebSocket-Version:", 22) == 0) {
            const char *val = h + 22;
            while (*val == ' ')
                val++;
            saw_version = strcmp(val, "13") == 0;
        }
        rt_string_unref(hdr);
    }

    if (!ws_sec_key_is_valid(ws_key) || !ws_host_header_is_valid(host_header) || !saw_upgrade ||
        !saw_connection ||
        !saw_version || !ws_origin_matches_expected(origin_header, host_header, "http", 80)) {
        return 0;
    }
    if (protocol_header[0] != '\0' && !ws_protocol_header_is_valid(protocol_header))
        return 0;
    if (required_subprotocol && *required_subprotocol &&
        !ws_protocol_list_contains(protocol_header, required_subprotocol))
        return 0;

    // Compute accept key
    char *accept = rt_ws_compute_accept_key(ws_key);
    if (!accept)
        return 0;

    // Send upgrade response
    char response[768];
    int rlen = snprintf(response,
                        sizeof(response),
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: %s\r\n",
                        accept);
    free(accept);
    if (rlen <= 0 || (size_t)rlen >= sizeof(response))
        return 0;
    if (required_subprotocol && *required_subprotocol) {
        int wrote = snprintf(response + rlen,
                             sizeof(response) - (size_t)rlen,
                             "Sec-WebSocket-Protocol: %s\r\n",
                             required_subprotocol);
        if (wrote <= 0 || (size_t)wrote >= sizeof(response) - (size_t)rlen)
            return 0;
        rlen += wrote;
    }
    if ((size_t)rlen + 2 >= sizeof(response))
        return 0;
    memcpy(response + rlen, "\r\n", 3);
    rlen += 2;

    rt_string resp = rt_string_from_bytes(response, (size_t)rlen);
    rt_tcp_send_str(tcp, resp);
    rt_string_unref(resp);

    return 1;
}

//=============================================================================
// Finalizer
//=============================================================================

/// @brief GC finalizer: stop the accept thread and close all client connections, then destroy the
/// platform mutex. Calling `_stop` first is idempotent, so this is safe even if the user already
/// stopped the server explicitly.
static void rt_ws_server_finalize(void *obj) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    rt_ws_server_stop(s);
    free(s->subprotocol);
    s->subprotocol = NULL;
    if (s->lock_initialized)
        WS_MUTEX_DESTROY(&s->lock);
}

//=============================================================================
// Accept Loop
//=============================================================================

#ifdef _WIN32
static DWORD WINAPI ws_accept_loop(LPVOID arg)
#else
static void *ws_accept_loop(void *arg)
#endif
{
    rt_ws_server_impl *s = (rt_ws_server_impl *)arg;

    while (s->running) {
        void *tcp = rt_tcp_server_accept_for(s->tcp_server, 1000);
        if (!tcp)
            continue;
        if (!s->running) {
            ws_release_tcp(&tcp);
            break;
        }

        // Perform WebSocket handshake
        if (!ws_server_handshake(tcp, s->subprotocol)) {
            ws_release_tcp(&tcp);
            continue;
        }

        // Add client
        WS_MUTEX_LOCK(&s->lock);
        int slot = -1;
        for (int i = 0; i < s->client_count; i++) {
            if (!s->clients[i].active) {
                slot = i;
                break;
            }
        }
        if (slot < 0 && s->client_count < WS_SERVER_MAX_CLIENTS)
            slot = s->client_count++;

        if (slot >= 0) {
            s->clients[slot].tcp = tcp;
            s->clients[slot].active = true;
        } else {
            ws_release_tcp(&tcp);
        }
        WS_MUTEX_UNLOCK(&s->lock);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Construct a WebSocket server bound (lazily) to `port`. Validates port range (1–65535)
/// up front; allocates the impl with mutex + finalizer, but does NOT bind the TCP socket — that
/// happens on `_start`. Returns a GC-managed handle.
void *rt_ws_server_new(int64_t port) {
    if (port < 1 || port > 65535)
        rt_trap("WsServer: invalid port");

    rt_ws_server_impl *s =
        (rt_ws_server_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ws_server_impl));
    if (!s)
        rt_trap("WsServer: memory allocation failed");
    memset(s, 0, sizeof(*s));
    rt_obj_set_finalizer(s, rt_ws_server_finalize);
    s->port = port;
    WS_MUTEX_INIT(&s->lock);
    s->lock_initialized = true;
    return s;
}

/// @brief Start listening: bind the TCP server, mark `running=true`, and spawn the accept loop
/// on a dedicated thread (Win32 `CreateThread` or POSIX `pthread_create`). Idempotent — calling
/// while already running is a no-op.
void rt_ws_server_start(void *obj) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (s->running)
        return;

    s->tcp_server = rt_tcp_server_listen(s->port);
    if (!s->tcp_server)
        rt_trap("WsServer: failed to bind listener");
    s->port = rt_tcp_server_port(s->tcp_server);
    s->running = true;

#ifdef _WIN32
    s->accept_thread = CreateThread(NULL, 0, ws_accept_loop, s, 0, NULL);
    s->thread_started = s->accept_thread != NULL;
#else
    s->thread_started = pthread_create(&s->accept_thread, NULL, ws_accept_loop, s) == 0;
#endif
    if (!s->thread_started) {
        s->running = false;
        rt_tcp_server_close(s->tcp_server);
        if (rt_obj_release_check0(s->tcp_server))
            rt_obj_free(s->tcp_server);
        s->tcp_server = NULL;
        rt_trap("WsServer: failed to start accept thread");
    }
}

/// @brief Stop the server: set `running=false`, close the TCP listener (which unblocks
/// `accept_for`), join the accept thread (5s wait on Win32), then close every active client
/// connection under the mutex. Designed to be safely called from any thread.
void rt_ws_server_stop(void *obj) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    s->running = false;

    if (s->tcp_server) {
        rt_tcp_server_close(s->tcp_server);
        if (rt_obj_release_check0(s->tcp_server))
            rt_obj_free(s->tcp_server);
        s->tcp_server = NULL;
    }

    if (s->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(s->accept_thread, 5000);
        CloseHandle(s->accept_thread);
#else
        pthread_join(s->accept_thread, NULL);
#endif
        s->thread_started = false;
    }

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp)
            ws_release_tcp(&s->clients[i].tcp);
        s->clients[i].active = false;
    }
    s->client_count = 0;
    WS_MUTEX_UNLOCK(&s->lock);
}

void rt_ws_server_set_subprotocol(void *obj, rt_string subprotocol) {
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    const char *protocol = subprotocol ? rt_string_cstr(subprotocol) : NULL;
    char *copy = NULL;

    if (!s)
        return;
    if (s->running)
        rt_trap("WsServer.SetSubprotocol: cannot change subprotocol while server is running");
    if (protocol && *protocol) {
        if (!ws_token_is_valid(protocol))
            rt_trap("WsServer.SetSubprotocol: invalid subprotocol token");
        copy = strdup(protocol);
        if (!copy)
            rt_trap("WsServer.SetSubprotocol: memory allocation failed");
    }

    free(s->subprotocol);
    s->subprotocol = copy;
}

rt_string rt_ws_server_subprotocol(void *obj) {
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (!s || !s->subprotocol)
        return rt_str_empty();
    return rt_string_from_bytes(s->subprotocol, strlen(s->subprotocol));
}

/// @brief Send a TEXT frame to every connected client. Clients whose send fails are marked
/// inactive and their TCP handles released — handles dead-client cleanup as a side effect.
/// Holds the client-list mutex during the entire broadcast (caller blocks on long sends).
void rt_ws_server_broadcast(void *obj, rt_string message) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp && rt_tcp_is_open(s->clients[i].tcp)) {
            if (!ws_server_send_frame(s->clients[i].tcp, WS_OP_TEXT, msg, len)) {
                ws_release_tcp(&s->clients[i].tcp);
                s->clients[i].active = false;
            }
        }
    }
    WS_MUTEX_UNLOCK(&s->lock);
}

/// @brief Binary-frame variant of `_broadcast`. `data` is interpreted as a `(int64 length, uint8*)`
/// pair (the runtime's Bytes layout); same dead-client cleanup as the text variant.
void rt_ws_server_broadcast_bytes(void *obj, void *data) {
    if (!obj || !data)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;

    typedef struct {
        int64_t l;
        uint8_t *d;
    } bi;

    int64_t len = ((bi *)data)->l;
    uint8_t *ptr = ((bi *)data)->d;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp && rt_tcp_is_open(s->clients[i].tcp)) {
            if (!ws_server_send_frame(s->clients[i].tcp, WS_OP_BINARY, ptr, (size_t)len)) {
                ws_release_tcp(&s->clients[i].tcp);
                s->clients[i].active = false;
            }
        }
    }
    WS_MUTEX_UNLOCK(&s->lock);
}

/// @brief Count active clients (only counts slots flagged active — slots from disconnected
/// clients are skipped). Reads under the mutex for a consistent snapshot.
int64_t rt_ws_server_client_count(void *obj) {
    if (!obj)
        return 0;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    WS_MUTEX_LOCK(&s->lock);
    int64_t count = 0;
    for (int i = 0; i < s->client_count; i++)
        if (s->clients[i].active)
            count++;
    WS_MUTEX_UNLOCK(&s->lock);
    return count;
}

/// @brief Read the configured port. Always returns the value passed to `_new`, even before
/// `_start` has bound the socket.
int64_t rt_ws_server_port(void *obj) {
    if (!obj)
        return 0;
    return ((rt_ws_server_impl *)obj)->port;
}

/// @brief Returns 1 between successful `_start` and `_stop`; 0 otherwise.
int8_t rt_ws_server_is_running(void *obj) {
    if (!obj)
        return 0;
    return ((rt_ws_server_impl *)obj)->running ? 1 : 0;
}

/// @brief **Synchronous** accept (alternative to the background `_start` thread). Blocks until
/// a client connects, performs the WebSocket handshake, returns the per-client TCP handle.
/// Returns NULL on accept failure or handshake rejection. Use this when the application owns
/// the accept loop instead of relying on the background broadcast model.
void *rt_ws_server_accept(void *obj) {
    if (!obj)
        return NULL;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (!s->tcp_server)
        return NULL;

    void *tcp = rt_tcp_server_accept(s->tcp_server);
    if (!tcp)
        return NULL;

    if (!ws_server_handshake(tcp, s->subprotocol)) {
        ws_release_tcp(&tcp);
        return NULL;
    }

    return tcp;
}

/// @brief Receive one complete WebSocket message from a client. Handles **frame fragmentation**
/// (assembles continuation frames until FIN), **control frames** (auto-PONG to PING, auto-close
/// echo on CLOSE), and **UTF-8 validation** for text frames. Caps reassembled message size at
/// 64 MiB (per WebSocket security best practice) — over-cap closes with status 0x03F1
/// "Message Too Big". Invalid UTF-8 in text frames closes with 0x03EF "Invalid Payload".
/// Returns the decoded message as rt_string, or empty string on connection close/error.
rt_string rt_ws_server_client_recv(void *tcp) {
    if (!tcp)
        return rt_string_from_bytes("", 0);

    while (rt_tcp_is_open(tcp)) {
        uint8_t fin = 0;
        uint8_t opcode = 0;
        uint8_t *data = NULL;
        size_t len = 0;
        if (!ws_server_recv_frame(tcp, &fin, &opcode, &data, &len))
            break;

        if (opcode == WS_OP_PING) {
            ws_server_send_frame(tcp, WS_OP_PONG, data, len);
            free(data);
            continue;
        }

        if (opcode == WS_OP_CLOSE) {
            // Send close response
            ws_server_send_frame(tcp, WS_OP_CLOSE, data, len);
            free(data);
            break;
        }

        if (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY || opcode == WS_OP_CONTINUATION) {
            uint8_t *message = NULL;
            size_t message_len = 0;
            uint8_t message_opcode = opcode;

            if (opcode == WS_OP_CONTINUATION) {
                free(data);
                ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
                break;
            }

            if (fin) {
                message = data;
                message_len = len;
            } else {
                message = data;
                message_len = len;
                while (1) {
                    uint8_t next_fin = 0;
                    uint8_t next_opcode = 0;
                    uint8_t *next_data = NULL;
                    size_t next_len = 0;
                    if (!ws_server_recv_frame(
                            tcp, &next_fin, &next_opcode, &next_data, &next_len)) {
                        free(message);
                        return rt_string_from_bytes("", 0);
                    }

                    if (next_opcode >= 0x08) {
                        if (next_opcode == WS_OP_PING)
                            ws_server_send_frame(tcp, WS_OP_PONG, next_data, next_len);
                        else if (next_opcode == WS_OP_CLOSE)
                            ws_server_send_frame(tcp, WS_OP_CLOSE, next_data, next_len);
                        free(next_data);
                        if (next_opcode == WS_OP_CLOSE) {
                            free(message);
                            return rt_string_from_bytes("", 0);
                        }
                        continue;
                    }

                    if (next_opcode != WS_OP_CONTINUATION) {
                        free(next_data);
                        free(message);
                        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
                        return rt_string_from_bytes("", 0);
                    }

                    if (message_len + next_len > 64u * 1024u * 1024u) {
                        free(next_data);
                        free(message);
                        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xF1", 2);
                        return rt_string_from_bytes("", 0);
                    }

                    uint8_t *grown = (uint8_t *)realloc(message, message_len + next_len);
                    if (!grown) {
                        free(next_data);
                        free(message);
                        return rt_string_from_bytes("", 0);
                    }
                    message = grown;
                    memcpy(message + message_len, next_data, next_len);
                    message_len += next_len;
                    free(next_data);

                    if (next_fin)
                        break;
                }
            }

            if (message_opcode == WS_OP_TEXT && !ws_is_valid_utf8(message, message_len)) {
                free(message);
                ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEF", 2);
                break;
            }

            rt_string result = rt_string_from_bytes((const char *)message, message_len);
            free(message);
            return result;
        }

        free(data);
    }

    return rt_string_from_bytes("", 0);
}

/// @brief Send a TEXT frame to a single client. On send failure, closes the connection (caller
/// can then drop the handle). Companion to `_client_recv` for the per-connection message loop.
void rt_ws_server_client_send(void *tcp, rt_string message) {
    if (!tcp)
        return;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;
    if (!ws_server_send_frame(tcp, WS_OP_TEXT, msg, len))
        rt_tcp_close(tcp);
}

/// @brief Send a polite WebSocket CLOSE frame (no payload) and tear down the TCP connection.
/// Use when terminating a single client without affecting the server's other connections.
void rt_ws_server_client_close(void *tcp) {
    if (!tcp)
        return;
    ws_server_send_frame(tcp, WS_OP_CLOSE, NULL, 0);
    rt_tcp_close(tcp);
}
