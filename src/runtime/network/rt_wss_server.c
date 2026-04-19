//===----------------------------------------------------------------------===//

#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_wss_server.c
// Purpose: TLS-backed WebSocket server with broadcast and per-client messaging.
// Key invariants:
//   - Accept loop runs in background thread.
//   - Clients tracked in a fixed-size array protected by mutex.
//   - WebSocket framing reuses logic from rt_websocket.c over TLS transport.
// Ownership/Lifetime:
//   - Server GC-managed. Stop() or finalizer closes all clients.
// Links: rt_wss_server.h (API), rt_websocket.h (framing), rt_tls.c
//
//===----------------------------------------------------------------------===//

#include "rt_wss_server.h"
#include "rt_websocket.h"

#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_internal.h"
#include "rt_network_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_threadpool.h"
#include "rt_tls_server_internal.h"

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

#if defined(__GNUC__) || defined(__clang__)
#define WSS_MAYBE_UNUSED __attribute__((unused))
#else
#define WSS_MAYBE_UNUSED
#endif

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
    void *tcp; // TLS connection
    bool active;
} ws_client_t;

typedef struct {
    void *tcp_server;
    void *worker_pool;
    rt_tls_server_ctx_t *tls_ctx;
    char *cert_file;
    char *key_file;
    char *subprotocol;
    int64_t port;
    bool running;
    ws_client_t clients[WS_SERVER_MAX_CLIENTS];
    int client_count;
    ws_mutex_t lock;
    ws_mutex_t io_lock;
    bool lock_initialized;
    bool io_lock_initialized;
#ifdef _WIN32
    HANDLE accept_thread;
#else
    pthread_t accept_thread;
#endif
    bool thread_started;
} rt_ws_server_impl;

typedef struct {
    rt_ws_server_impl *server;
    int slot;
    void *tcp;
} ws_client_task_t;

typedef struct {
    int slot;
    void *tcp;
} ws_broadcast_target_t;

#include "rt_ws_shared.inc"

static int ws_tls_is_open(void *tcp) {
    return tcp && rt_tls_get_socket((rt_tls_session_t *)tcp) >= 0;
}

static void ws_release_tcp(void **tcp_ptr) {
    if (!tcp_ptr || !*tcp_ptr)
        return;
    rt_tls_close((rt_tls_session_t *)*tcp_ptr);
    *tcp_ptr = NULL;
}

static int ws_server_send_raw(void *tcp, const void *data, size_t len) {
    return rt_tls_send((rt_tls_session_t *)tcp, data, len) == (long)len;
}

static int ws_tls_recv_exact(void *tcp, uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        long got = rt_tls_recv((rt_tls_session_t *)tcp, buf + total, len - total);
        if (got <= 0)
            return 0;
        total += (size_t)got;
    }
    return 1;
}

static rt_string ws_tls_recv_line(void *tcp) {
    size_t cap = 128;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;

    while (1) {
        char ch = '\0';
        long got = rt_tls_recv((rt_tls_session_t *)tcp, &ch, 1);
        if (got <= 0) {
            free(buf);
            return NULL;
        }
        if (ch == '\n')
            break;
        if (ch != '\r') {
            if (len + 1 >= cap) {
                size_t next_cap = cap * 2;
                char *grown = (char *)realloc(buf, next_cap);
                if (!grown) {
                    free(buf);
                    return NULL;
                }
                buf = grown;
                cap = next_cap;
            }
            buf[len++] = ch;
        }
    }

    {
        rt_string line = rt_string_from_bytes(buf, len);
        free(buf);
        return line;
    }
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
    uint8_t header[2];
    uint8_t ext8[8];
    uint8_t mask[4];
    *data_out = NULL;
    *len_out = 0;

    if (!ws_tls_recv_exact(tcp, header, sizeof(header)))
        return 0;
    uint8_t first = header[0];
    uint8_t second = header[1];
    *fin_out = (first & WS_FIN) ? 1 : 0;
    *opcode_out = first & 0x0F;
    uint8_t masked = second & WS_MASK;
    size_t payload_len = second & 0x7F;

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
        if (!ws_tls_recv_exact(tcp, ext8, 2))
            return 0;
        payload_len = ((size_t)ext8[0] << 8) | ext8[1];
    } else if (payload_len == 127) {
        if (!ws_tls_recv_exact(tcp, ext8, 8))
            return 0;
        if (!ws_decode_u64_len(ext8, &payload_len)) {
            ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
            return 0;
        }
    }

    if (*opcode_out >= 0x08 && (!*fin_out || payload_len > 125)) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xEA", 2);
        return 0;
    }

    if (payload_len > 64 * 1024 * 1024) {
        ws_server_send_frame(tcp, WS_OP_CLOSE, "\x03\xF1", 2);
        return 0; // Too large
    }

    if (!ws_tls_recv_exact(tcp, mask, sizeof(mask)))
        return 0;

    // Read payload
    if (payload_len > 0) {
        *data_out = (uint8_t *)malloc(payload_len);
        if (!*data_out)
            return 0;
        if (!ws_tls_recv_exact(tcp, *data_out, payload_len)) {
            free(*data_out);
            *data_out = NULL;
            return 0;
        }

        // Unmask
        for (size_t i = 0; i < payload_len; i++)
            (*data_out)[i] ^= mask[i % 4];
        *len_out = payload_len;
    }

    return 1;
}

/// @brief Perform server-side WebSocket upgrade handshake.
static int ws_server_handshake(void *tcp, const char *required_subprotocol) {
    // Read HTTP upgrade request
    rt_string line = ws_tls_recv_line(tcp);
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
        rt_string hdr = ws_tls_recv_line(tcp);
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
        !saw_version || !ws_origin_matches_expected(origin_header, host_header, "https", 443))
        return 0;
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

    ws_server_send_raw(tcp, response, (size_t)rlen);

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
    rt_wss_server_stop(s);
    rt_tls_server_ctx_free(s->tls_ctx);
    s->tls_ctx = NULL;
    free(s->cert_file);
    free(s->key_file);
    free(s->subprotocol);
    s->cert_file = NULL;
    s->key_file = NULL;
    s->subprotocol = NULL;
    if (s->worker_pool && rt_obj_release_check0(s->worker_pool))
        rt_obj_free(s->worker_pool);
    s->worker_pool = NULL;
    if (s->io_lock_initialized)
        WS_MUTEX_DESTROY(&s->io_lock);
    if (s->lock_initialized)
        WS_MUTEX_DESTROY(&s->lock);
}

static void ws_server_remove_client(rt_ws_server_impl *s, int slot, void *tcp) {
    if (!s || slot < 0)
        return;

    WS_MUTEX_LOCK(&s->io_lock);
    WS_MUTEX_LOCK(&s->lock);
    if (slot < s->client_count && s->clients[slot].tcp == tcp) {
        ws_release_tcp(&s->clients[slot].tcp);
        s->clients[slot].active = false;
    } else {
        ws_release_tcp(&tcp);
    }
    WS_MUTEX_UNLOCK(&s->lock);
    WS_MUTEX_UNLOCK(&s->io_lock);
}

static int ws_server_send_locked(rt_ws_server_impl *s, void *tcp, uint8_t opcode, const void *data, size_t len) {
    int ok = 0;
    if (!s || !tcp)
        return 0;
    WS_MUTEX_LOCK(&s->io_lock);
    ok = ws_server_send_frame(tcp, opcode, data, len);
    WS_MUTEX_UNLOCK(&s->io_lock);
    return ok;
}

static void ws_client_task_run(void *arg) {
    ws_client_task_t *task = (ws_client_task_t *)arg;
    rt_ws_server_impl *s = task ? task->server : NULL;
    void *tcp = task ? task->tcp : NULL;
    int slot = task ? task->slot : -1;

    free(task);
    if (!s || !tcp || slot < 0)
        return;

    while (s->running && ws_tls_is_open(tcp)) {
        uint8_t fin = 0;
        uint8_t opcode = 0;
        uint8_t *data = NULL;
        size_t len = 0;

        if (!ws_server_recv_frame(tcp, &fin, &opcode, &data, &len))
            break;

        if (opcode == WS_OP_PING) {
            if (!ws_server_send_locked(s, tcp, WS_OP_PONG, data, len)) {
                free(data);
                break;
            }
            free(data);
            continue;
        }

        if (opcode == WS_OP_PONG) {
            free(data);
            continue;
        }

        if (opcode == WS_OP_CLOSE) {
            ws_server_send_locked(s, tcp, WS_OP_CLOSE, data, len);
            free(data);
            break;
        }

        if (opcode == WS_OP_CONTINUATION) {
            free(data);
            ws_server_send_locked(s, tcp, WS_OP_CLOSE, "\x03\xEA", 2);
            break;
        }

        if (opcode == WS_OP_TEXT && fin && !ws_is_valid_utf8(data, len)) {
            free(data);
            ws_server_send_locked(s, tcp, WS_OP_CLOSE, "\x03\xEF", 2);
            break;
        }

        free(data);

        if (!fin) {
            while (s->running && ws_tls_is_open(tcp)) {
                uint8_t next_fin = 0;
                uint8_t next_opcode = 0;
                uint8_t *next_data = NULL;
                size_t next_len = 0;

                if (!ws_server_recv_frame(tcp, &next_fin, &next_opcode, &next_data, &next_len))
                    goto done;

                if (next_opcode == WS_OP_PING) {
                    int ok = ws_server_send_locked(s, tcp, WS_OP_PONG, next_data, next_len);
                    free(next_data);
                    if (!ok)
                        goto done;
                    continue;
                }

                if (next_opcode == WS_OP_PONG) {
                    free(next_data);
                    continue;
                }

                if (next_opcode == WS_OP_CLOSE) {
                    ws_server_send_locked(s, tcp, WS_OP_CLOSE, next_data, next_len);
                    free(next_data);
                    goto done;
                }

                if (next_opcode != WS_OP_CONTINUATION) {
                    free(next_data);
                    ws_server_send_locked(s, tcp, WS_OP_CLOSE, "\x03\xEA", 2);
                    goto done;
                }

                free(next_data);
                if (next_fin)
                    break;
            }
        }
    }

done:
    ws_server_remove_client(s, slot, tcp);
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
            rt_tcp_close(tcp);
            if (rt_obj_release_check0(tcp))
                rt_obj_free(tcp);
            break;
        }

        rt_tls_session_t *tls = rt_tls_server_accept_socket((int)rt_tcp_socket_fd(tcp), s->tls_ctx);
        rt_tcp_detach_socket(tcp);
        if (rt_obj_release_check0(tcp))
            rt_obj_free(tcp);
        if (!tls)
            continue;

        // Perform WebSocket handshake
        if (!ws_server_handshake(tls, s->subprotocol)) {
            ws_release_tcp((void **)&tls);
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
            s->clients[slot].tcp = tls;
            s->clients[slot].active = true;
        } else {
            ws_release_tcp((void **)&tls);
        }
        WS_MUTEX_UNLOCK(&s->lock);

        if (slot >= 0) {
            ws_client_task_t *task = (ws_client_task_t *)malloc(sizeof(*task));
            if (!task) {
                ws_server_remove_client(s, slot, tls);
                continue;
            }
            task->server = s;
            task->slot = slot;
            task->tcp = tls;
            if (!s->worker_pool ||
                !rt_threadpool_submit(s->worker_pool, (void *)ws_client_task_run, task)) {
                free(task);
                ws_server_remove_client(s, slot, tls);
            }
        }
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
void *rt_wss_server_new(int64_t port, rt_string cert_file, rt_string key_file) {
    if (port < 1 || port > 65535)
        rt_trap("WssServer: invalid port");

    rt_ws_server_impl *s =
        (rt_ws_server_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ws_server_impl));
    if (!s)
        rt_trap("WssServer: memory allocation failed");
    memset(s, 0, sizeof(*s));
    rt_obj_set_finalizer(s, rt_ws_server_finalize);
    s->port = port;
    s->worker_pool = rt_threadpool_new(8);
    s->cert_file = strdup(rt_string_cstr(cert_file));
    s->key_file = strdup(rt_string_cstr(key_file));
    if (!s->cert_file || !s->key_file)
        rt_trap("WssServer: failed to copy certificate paths");
    {
        rt_tls_server_config_t tls_cfg;
        rt_tls_server_config_init(&tls_cfg);
        tls_cfg.cert_file = s->cert_file;
        tls_cfg.key_file = s->key_file;
        tls_cfg.alpn_protocol = "http/1.1";
        s->tls_ctx = rt_tls_server_ctx_new(&tls_cfg);
        if (!s->tls_ctx)
            rt_trap(rt_tls_server_last_error());
    }
    WS_MUTEX_INIT(&s->lock);
    s->lock_initialized = true;
    WS_MUTEX_INIT(&s->io_lock);
    s->io_lock_initialized = true;
    return s;
}

/// @brief Start listening: bind the TCP server, mark `running=true`, and spawn the accept loop
/// on a dedicated thread (Win32 `CreateThread` or POSIX `pthread_create`). Idempotent — calling
/// while already running is a no-op.
void rt_wss_server_start(void *obj) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (s->running)
        return;

    s->tcp_server = rt_tcp_server_listen(s->port);
    if (!s->tcp_server)
        rt_trap("WssServer: failed to bind listener");
    s->port = rt_tcp_server_port(s->tcp_server);
    if (!s->worker_pool)
        s->worker_pool = rt_threadpool_new(8);
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
        rt_trap("WssServer: failed to start accept thread");
    }
}

/// @brief Stop the server: set `running=false`, close the TCP listener (which unblocks
/// `accept_for`), join the accept thread (5s wait on Win32), then close every active client
/// connection under the mutex. Designed to be safely called from any thread.
void rt_wss_server_stop(void *obj) {
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

    WS_MUTEX_LOCK(&s->io_lock);
    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp)
            ws_release_tcp(&s->clients[i].tcp);
        s->clients[i].active = false;
    }
    s->client_count = 0;
    WS_MUTEX_UNLOCK(&s->lock);
    WS_MUTEX_UNLOCK(&s->io_lock);

    if (s->worker_pool)
        rt_threadpool_wait(s->worker_pool);
}

void rt_wss_server_set_subprotocol(void *obj, rt_string subprotocol) {
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    const char *protocol = subprotocol ? rt_string_cstr(subprotocol) : NULL;
    char *copy = NULL;

    if (!s)
        return;
    if (s->running)
        rt_trap("WssServer.SetSubprotocol: cannot change subprotocol while server is running");
    if (protocol && *protocol) {
        if (!ws_token_is_valid(protocol))
            rt_trap("WssServer.SetSubprotocol: invalid subprotocol token");
        copy = strdup(protocol);
        if (!copy)
            rt_trap("WssServer.SetSubprotocol: memory allocation failed");
    }

    free(s->subprotocol);
    s->subprotocol = copy;
}

rt_string rt_wss_server_subprotocol(void *obj) {
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (!s || !s->subprotocol)
        return rt_str_empty();
    return rt_string_from_bytes(s->subprotocol, strlen(s->subprotocol));
}

/// @brief Send a TEXT frame to every connected client. Clients whose send fails are marked
/// inactive and their TCP handles released — handles dead-client cleanup as a side effect.
/// Holds the client-list mutex during the entire broadcast (caller blocks on long sends).
void rt_wss_server_broadcast(void *obj, rt_string message) {
    if (!obj)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;
    ws_broadcast_target_t targets[WS_SERVER_MAX_CLIENTS];
    int target_count = 0;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp)
            targets[target_count++] = (ws_broadcast_target_t){i, s->clients[i].tcp};
    }
    WS_MUTEX_UNLOCK(&s->lock);

    for (int i = 0; i < target_count; i++) {
        int should_send = 0;
        int ok = 1;
        WS_MUTEX_LOCK(&s->io_lock);
        WS_MUTEX_LOCK(&s->lock);
        if (targets[i].slot < s->client_count && s->clients[targets[i].slot].active &&
            s->clients[targets[i].slot].tcp == targets[i].tcp && ws_tls_is_open(targets[i].tcp)) {
            should_send = 1;
        }
        WS_MUTEX_UNLOCK(&s->lock);

        if (should_send)
            ok = ws_server_send_frame(targets[i].tcp, WS_OP_TEXT, msg, len);

        WS_MUTEX_LOCK(&s->lock);
        if (!ok && targets[i].slot < s->client_count &&
            s->clients[targets[i].slot].tcp == targets[i].tcp) {
            ws_release_tcp(&s->clients[targets[i].slot].tcp);
            s->clients[targets[i].slot].active = false;
        }
        WS_MUTEX_UNLOCK(&s->lock);
        WS_MUTEX_UNLOCK(&s->io_lock);
    }
}

/// @brief Binary-frame variant of `_broadcast`. `data` is interpreted as a `(int64 length, uint8*)`
/// pair (the runtime's Bytes layout); same dead-client cleanup as the text variant.
void rt_wss_server_broadcast_bytes(void *obj, void *data) {
    if (!obj || !data)
        return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;

    typedef struct {
        int64_t l;
        uint8_t *d;
    } bi;

    int64_t len = ((bi *)data)->l;
    uint8_t *ptr = ((bi *)data)->d;

    ws_broadcast_target_t targets[WS_SERVER_MAX_CLIENTS];
    int target_count = 0;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i].active && s->clients[i].tcp)
            targets[target_count++] = (ws_broadcast_target_t){i, s->clients[i].tcp};
    }
    WS_MUTEX_UNLOCK(&s->lock);

    for (int i = 0; i < target_count; i++) {
        int should_send = 0;
        int ok = 1;
        WS_MUTEX_LOCK(&s->io_lock);
        WS_MUTEX_LOCK(&s->lock);
        if (targets[i].slot < s->client_count && s->clients[targets[i].slot].active &&
            s->clients[targets[i].slot].tcp == targets[i].tcp && ws_tls_is_open(targets[i].tcp)) {
            should_send = 1;
        }
        WS_MUTEX_UNLOCK(&s->lock);

        if (should_send)
            ok = ws_server_send_frame(targets[i].tcp, WS_OP_BINARY, ptr, (size_t)len);

        WS_MUTEX_LOCK(&s->lock);
        if (!ok && targets[i].slot < s->client_count &&
            s->clients[targets[i].slot].tcp == targets[i].tcp) {
            ws_release_tcp(&s->clients[targets[i].slot].tcp);
            s->clients[targets[i].slot].active = false;
        }
        WS_MUTEX_UNLOCK(&s->lock);
        WS_MUTEX_UNLOCK(&s->io_lock);
    }
}

/// @brief Count active clients (only counts slots flagged active — slots from disconnected
/// clients are skipped). Reads under the mutex for a consistent snapshot.
int64_t rt_wss_server_client_count(void *obj) {
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
int64_t rt_wss_server_port(void *obj) {
    if (!obj)
        return 0;
    return ((rt_ws_server_impl *)obj)->port;
}

/// @brief Returns 1 between successful `_start` and `_stop`; 0 otherwise.
int8_t rt_wss_server_is_running(void *obj) {
    if (!obj)
        return 0;
    return ((rt_ws_server_impl *)obj)->running ? 1 : 0;
}

/// @brief **Synchronous** accept (alternative to the background `_start` thread). Blocks until
/// a client connects, performs the WebSocket handshake, returns the per-client TCP handle.
/// Returns NULL on accept failure or handshake rejection. Use this when the application owns
/// the accept loop instead of relying on the background broadcast model.
static WSS_MAYBE_UNUSED void *rt_wss_server_accept(void *obj) {
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
static WSS_MAYBE_UNUSED rt_string rt_wss_server_client_recv(void *tcp) {
    if (!tcp)
        return rt_string_from_bytes("", 0);

    while (ws_tls_is_open(tcp)) {
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
static WSS_MAYBE_UNUSED void rt_wss_server_client_send(void *tcp, rt_string message) {
    if (!tcp)
        return;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;
    if (!ws_server_send_frame(tcp, WS_OP_TEXT, msg, len))
        rt_tls_close((rt_tls_session_t *)tcp);
}

/// @brief Send a polite WebSocket CLOSE frame (no payload) and tear down the TCP connection.
/// Use when terminating a single client without affecting the server's other connections.
static WSS_MAYBE_UNUSED void rt_wss_server_client_close(void *tcp) {
    if (!tcp)
        return;
    ws_server_send_frame(tcp, WS_OP_CLOSE, NULL, 0);
    rt_tls_close((rt_tls_session_t *)tcp);
}
