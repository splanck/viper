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
#else
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

extern void rt_trap(const char *msg);
extern char *rt_ws_compute_accept_key(const char *key_cstr); // from rt_websocket.c

//=============================================================================
// Internal Structures
//=============================================================================

#define WS_SERVER_MAX_CLIENTS 128

// WebSocket opcodes (duplicated from rt_websocket.c to avoid dependency)
#define WS_OP_TEXT 0x01
#define WS_OP_BINARY 0x02
#define WS_OP_CLOSE 0x08
#define WS_FIN 0x80
#define WS_MASK 0x80

typedef struct
{
    void *tcp;       // TCP connection
    bool active;
} ws_client_t;

typedef struct
{
    void *tcp_server;
    int64_t port;
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

static int ws_header_has_upgrade_token(const char *value)
{
    const char *p = value;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if ((size_t)(end - start) == strlen("Upgrade") && strncasecmp(start, "Upgrade", 7) == 0)
            return 1;
    }
    return 0;
}

//=============================================================================
// WebSocket Framing Helpers
//=============================================================================

/// @brief Send a WebSocket frame over a raw TCP connection (server-side: no masking).
static int ws_server_send_frame(void *tcp, uint8_t opcode, const void *data, size_t len)
{
    uint8_t header[10];
    size_t header_len = 2;

    header[0] = WS_FIN | opcode;

    // Server frames are NOT masked (RFC 6455 §5.1)
    if (len < 126)
    {
        header[1] = (uint8_t)len;
    }
    else if (len < 65536)
    {
        header[1] = 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len);
        header_len = 4;
    }
    else
    {
        header[1] = 127;
        header[2] = 0; header[3] = 0; header[4] = 0; header[5] = 0;
        header[6] = (uint8_t)(len >> 24);
        header[7] = (uint8_t)(len >> 16);
        header[8] = (uint8_t)(len >> 8);
        header[9] = (uint8_t)(len);
        header_len = 10;
    }

    // Send header
    rt_tcp_send_all_raw(tcp, header, (int64_t)header_len);

    // Send data
    if (len > 0 && data)
        rt_tcp_send_all_raw(tcp, data, (int64_t)len);

    return 1;
}

/// @brief Read a WebSocket frame from a TCP connection (client frames are masked).
static int ws_server_recv_frame(void *tcp, uint8_t *opcode_out,
                                 uint8_t **data_out, size_t *len_out)
{
    *data_out = NULL;
    *len_out = 0;

    // Read 2-byte header
    void *hdr = rt_tcp_recv_exact(tcp, 2);
    if (!hdr) return 0;
    typedef struct { int64_t l; uint8_t *d; } bi;
    uint8_t *h = ((bi *)hdr)->d;
    *opcode_out = h[0] & 0x0F;
    uint8_t masked = h[1] & WS_MASK;
    size_t payload_len = h[1] & 0x7F;
    if (rt_obj_release_check0(hdr)) rt_obj_free(hdr);

    // Extended length
    if (payload_len == 126)
    {
        void *ext = rt_tcp_recv_exact(tcp, 2);
        if (!ext) return 0;
        uint8_t *e = ((bi *)ext)->d;
        payload_len = ((size_t)e[0] << 8) | e[1];
        if (rt_obj_release_check0(ext)) rt_obj_free(ext);
    }
    else if (payload_len == 127)
    {
        void *ext = rt_tcp_recv_exact(tcp, 8);
        if (!ext) return 0;
        uint8_t *e = ((bi *)ext)->d;
        payload_len = ((size_t)e[4] << 24) | ((size_t)e[5] << 16) |
                      ((size_t)e[6] << 8) | e[7];
        if (rt_obj_release_check0(ext)) rt_obj_free(ext);
    }

    if (payload_len > 64 * 1024 * 1024)
        return 0; // Too large

    // Read mask key if present
    uint8_t mask[4] = {0};
    if (masked)
    {
        void *m = rt_tcp_recv_exact(tcp, 4);
        if (!m) return 0;
        memcpy(mask, ((bi *)m)->d, 4);
        if (rt_obj_release_check0(m)) rt_obj_free(m);
    }

    // Read payload
    if (payload_len > 0)
    {
        void *payload = rt_tcp_recv_exact(tcp, (int64_t)payload_len);
        if (!payload) return 0;
        uint8_t *p = ((bi *)payload)->d;

        // Unmask
        if (masked)
        {
            for (size_t i = 0; i < payload_len; i++)
                p[i] ^= mask[i % 4];
        }

        *data_out = (uint8_t *)malloc(payload_len);
        if (*data_out)
        {
            memcpy(*data_out, p, payload_len);
            *len_out = payload_len;
        }
        if (rt_obj_release_check0(payload)) rt_obj_free(payload);
    }

    return 1;
}

/// @brief Perform server-side WebSocket upgrade handshake.
static int ws_server_handshake(void *tcp)
{
    // Read HTTP upgrade request
    rt_string line = rt_tcp_recv_line(tcp);
    if (!line) return 0;
    const char *request_line = rt_string_cstr(line);
    if (!request_line || strncmp(request_line, "GET ", 4) != 0)
    {
        rt_string_unref(line);
        return 0;
    }
    rt_string_unref(line);

    char ws_key[128] = {0};
    int saw_upgrade = 0;
    int saw_connection = 0;
    int saw_version = 0;

    // Read headers
    while (1)
    {
        rt_string hdr = rt_tcp_recv_line(tcp);
        if (!hdr) return 0;
        const char *h = rt_string_cstr(hdr);
        if (!h || *h == '\0')
        {
            rt_string_unref(hdr);
            break;
        }
        // Look for Sec-WebSocket-Key
        if (strncasecmp(h, "Sec-WebSocket-Key:", 18) == 0)
        {
            const char *val = h + 18;
            while (*val == ' ') val++;
            strncpy(ws_key, val, sizeof(ws_key) - 1);
        }
        else if (strncasecmp(h, "Upgrade:", 8) == 0)
        {
            const char *val = h + 8;
            while (*val == ' ') val++;
            saw_upgrade = strcasecmp(val, "websocket") == 0;
        }
        else if (strncasecmp(h, "Connection:", 11) == 0)
        {
            const char *val = h + 11;
            while (*val == ' ') val++;
            saw_connection = ws_header_has_upgrade_token(val);
        }
        else if (strncasecmp(h, "Sec-WebSocket-Version:", 22) == 0)
        {
            const char *val = h + 22;
            while (*val == ' ') val++;
            saw_version = strcmp(val, "13") == 0;
        }
        rt_string_unref(hdr);
    }

    if (ws_key[0] == '\0' || !saw_upgrade || !saw_connection || !saw_version)
        return 0;

    // Compute accept key
    char *accept = rt_ws_compute_accept_key(ws_key);
    if (!accept)
        return 0;

    // Send upgrade response
    char response[512];
    int rlen = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    free(accept);

    rt_string resp = rt_string_from_bytes(response, (size_t)rlen);
    rt_tcp_send_str(tcp, resp);
    rt_string_unref(resp);

    return 1;
}

//=============================================================================
// Finalizer
//=============================================================================

static void rt_ws_server_finalize(void *obj)
{
    if (!obj) return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    s->running = false;
    for (int i = 0; i < s->client_count; i++)
    {
        if (s->clients[i].active && s->clients[i].tcp)
            rt_tcp_close(s->clients[i].tcp);
    }
    if (s->tcp_server)
        rt_tcp_server_close(s->tcp_server);
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

    while (s->running)
    {
        void *tcp = rt_tcp_server_accept_for(s->tcp_server, 1000);
        if (!tcp) continue;
        if (!s->running) { rt_tcp_close(tcp); break; }

        // Perform WebSocket handshake
        if (!ws_server_handshake(tcp))
        {
            rt_tcp_close(tcp);
            continue;
        }

        // Add client
        WS_MUTEX_LOCK(&s->lock);
        if (s->client_count < WS_SERVER_MAX_CLIENTS)
        {
            s->clients[s->client_count].tcp = tcp;
            s->clients[s->client_count].active = true;
            s->client_count++;
        }
        else
        {
            rt_tcp_close(tcp);
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

void *rt_ws_server_new(int64_t port)
{
    if (port < 1 || port > 65535)
        rt_trap("WsServer: invalid port");

    rt_ws_server_impl *s =
        (rt_ws_server_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ws_server_impl));
    if (!s) rt_trap("WsServer: memory allocation failed");
    memset(s, 0, sizeof(*s));
    rt_obj_set_finalizer(s, rt_ws_server_finalize);
    s->port = port;
    WS_MUTEX_INIT(&s->lock);
    s->lock_initialized = true;
    return s;
}

void rt_ws_server_start(void *obj)
{
    if (!obj) return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (s->running) return;

    s->tcp_server = rt_tcp_server_listen(s->port);
    s->running = true;

#ifdef _WIN32
    s->accept_thread = CreateThread(NULL, 0, ws_accept_loop, s, 0, NULL);
    s->thread_started = s->accept_thread != NULL;
#else
    s->thread_started = pthread_create(&s->accept_thread, NULL, ws_accept_loop, s) == 0;
#endif
}

void rt_ws_server_stop(void *obj)
{
    if (!obj) return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    s->running = false;

    if (s->tcp_server) { rt_tcp_server_close(s->tcp_server); s->tcp_server = NULL; }

    if (s->thread_started)
    {
#ifdef _WIN32
        WaitForSingleObject(s->accept_thread, 5000);
        CloseHandle(s->accept_thread);
#else
        pthread_join(s->accept_thread, NULL);
#endif
        s->thread_started = false;
    }

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++)
    {
        if (s->clients[i].active && s->clients[i].tcp)
            rt_tcp_close(s->clients[i].tcp);
        s->clients[i].active = false;
    }
    s->client_count = 0;
    WS_MUTEX_UNLOCK(&s->lock);
}

void rt_ws_server_broadcast(void *obj, rt_string message)
{
    if (!obj) return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++)
    {
        if (s->clients[i].active && s->clients[i].tcp && rt_tcp_is_open(s->clients[i].tcp))
            ws_server_send_frame(s->clients[i].tcp, WS_OP_TEXT, msg, len);
    }
    WS_MUTEX_UNLOCK(&s->lock);
}

void rt_ws_server_broadcast_bytes(void *obj, void *data)
{
    if (!obj || !data) return;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    typedef struct { int64_t l; uint8_t *d; } bi;
    int64_t len = ((bi *)data)->l;
    uint8_t *ptr = ((bi *)data)->d;

    WS_MUTEX_LOCK(&s->lock);
    for (int i = 0; i < s->client_count; i++)
    {
        if (s->clients[i].active && s->clients[i].tcp && rt_tcp_is_open(s->clients[i].tcp))
            ws_server_send_frame(s->clients[i].tcp, WS_OP_BINARY, ptr, (size_t)len);
    }
    WS_MUTEX_UNLOCK(&s->lock);
}

int64_t rt_ws_server_client_count(void *obj)
{
    if (!obj) return 0;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    WS_MUTEX_LOCK(&s->lock);
    int64_t count = 0;
    for (int i = 0; i < s->client_count; i++)
        if (s->clients[i].active) count++;
    WS_MUTEX_UNLOCK(&s->lock);
    return count;
}

int64_t rt_ws_server_port(void *obj)
{
    if (!obj) return 0;
    return ((rt_ws_server_impl *)obj)->port;
}

int8_t rt_ws_server_is_running(void *obj)
{
    if (!obj) return 0;
    return ((rt_ws_server_impl *)obj)->running ? 1 : 0;
}

void *rt_ws_server_accept(void *obj)
{
    if (!obj) return NULL;
    rt_ws_server_impl *s = (rt_ws_server_impl *)obj;
    if (!s->tcp_server) return NULL;

    void *tcp = rt_tcp_server_accept(s->tcp_server);
    if (!tcp) return NULL;

    if (!ws_server_handshake(tcp))
    {
        rt_tcp_close(tcp);
        return NULL;
    }

    return tcp;
}

rt_string rt_ws_server_client_recv(void *tcp)
{
    if (!tcp) return rt_string_from_bytes("", 0);

    while (rt_tcp_is_open(tcp))
    {
        uint8_t opcode;
        uint8_t *data = NULL;
        size_t len;
        if (!ws_server_recv_frame(tcp, &opcode, &data, &len))
            break;

        if (opcode == WS_OP_CLOSE)
        {
            // Send close response
            ws_server_send_frame(tcp, WS_OP_CLOSE, data, len);
            free(data);
            break;
        }

        if (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY)
        {
            rt_string result = rt_string_from_bytes((const char *)data, len);
            free(data);
            return result;
        }

        free(data);
    }

    return rt_string_from_bytes("", 0);
}

void rt_ws_server_client_send(void *tcp, rt_string message)
{
    if (!tcp) return;
    const char *msg = rt_string_cstr(message);
    size_t len = msg ? strlen(msg) : 0;
    ws_server_send_frame(tcp, WS_OP_TEXT, msg, len);
}

void rt_ws_server_client_close(void *tcp)
{
    if (!tcp) return;
    ws_server_send_frame(tcp, WS_OP_CLOSE, NULL, 0);
    rt_tcp_close(tcp);
}
