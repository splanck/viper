//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_websocket.c
// Purpose: WebSocket client implementing RFC 6455.
// Structure: [vptr | socket_fd | tls_session | url | is_open | close_code | ...]
//
// Protocol overview:
// - Opening handshake: HTTP Upgrade request with Sec-WebSocket-Key
// - Data transfer: Framed messages (text/binary) with masking
// - Closing handshake: Close frame exchange
//
//===----------------------------------------------------------------------===//

#include "rt_websocket.h"

#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_object.h"
#include "rt_random.h"
#include "rt_string.h"
#include "rt_tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration from rt_io.c
extern void rt_trap(const char *msg);

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
typedef int socklen_t;
#else
// Unix and ViperDOS: use BSD socket APIs.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// WebSocket opcodes
#define WS_OP_CONTINUATION 0x00
#define WS_OP_TEXT 0x01
#define WS_OP_BINARY 0x02
#define WS_OP_CLOSE 0x08
#define WS_OP_PING 0x09
#define WS_OP_PONG 0x0A

// Frame flags
#define WS_FIN 0x80
#define WS_MASK 0x80

// Close codes
#define WS_CLOSE_NORMAL 1000
#define WS_CLOSE_GOING_AWAY 1001
#define WS_CLOSE_PROTOCOL_ERROR 1002
#define WS_CLOSE_UNSUPPORTED 1003
#define WS_CLOSE_NO_STATUS 1005
#define WS_CLOSE_ABNORMAL 1006

/// @brief WebSocket connection implementation.
typedef struct rt_ws_impl
{
    void **vptr;             ///< Vtable pointer placeholder.
    int socket_fd;           ///< TCP socket file descriptor.
    rt_tls_session_t *tls;   ///< TLS session (NULL for ws://).
    char *url;               ///< Original connection URL.
    int8_t is_open;          ///< Connection state.
    int64_t close_code;      ///< Close status code.
    char *close_reason;      ///< Close reason string.
    uint8_t *recv_buffer;    ///< Buffer for receiving frames.
    size_t recv_buffer_size; ///< Size of receive buffer.
    size_t recv_buffer_len;  ///< Bytes currently in buffer.
} rt_ws_impl;

/// @brief Minimal SHA-1 (RFC 3174) for Sec-WebSocket-Accept validation (RFC 6455 §4.1).
/// SHA-1 is acceptable here: it is used as a protocol-mandated HMAC-like check,
/// not for general cryptographic security.
static void ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20])
{
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u, h4 = 0xC3D2E1F0u;

    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    uint8_t *padded = (uint8_t *)calloc(padded_len, 1);
    if (!padded)
        return;
    memcpy(padded, data, len);
    padded[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        padded[padded_len - 8 + i] = (uint8_t)(bit_len >> (56 - i * 8));

    for (size_t chunk = 0; chunk < padded_len; chunk += 64)
    {
        uint32_t w[80];
        for (int j = 0; j < 16; j++)
        {
            w[j] = ((uint32_t)padded[chunk + j * 4] << 24) |
                   ((uint32_t)padded[chunk + j * 4 + 1] << 16) |
                   ((uint32_t)padded[chunk + j * 4 + 2] << 8) |
                   ((uint32_t)padded[chunk + j * 4 + 3]);
        }
        for (int j = 16; j < 80; j++)
        {
            uint32_t t = w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16];
            w[j] = (t << 1) | (t >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++)
        {
            uint32_t f, k;
            if (j < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            }
            else if (j < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            }
            else if (j < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[j];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    free(padded);

    for (int i = 0; i < 4; i++)
    {
        digest[i] = (uint8_t)(h0 >> (24 - i * 8));
        digest[4 + i] = (uint8_t)(h1 >> (24 - i * 8));
        digest[8 + i] = (uint8_t)(h2 >> (24 - i * 8));
        digest[12 + i] = (uint8_t)(h3 >> (24 - i * 8));
        digest[16 + i] = (uint8_t)(h4 >> (24 - i * 8));
    }
}

/// @brief Generate a random WebSocket key (16 random bytes, base64 encoded).
static rt_string generate_ws_key(void)
{
    // Generate 16 cryptographically-random bytes (RFC 6455 §4.1 requires unpredictability)
    uint8_t raw[16];
    rt_crypto_random_bytes(raw, sizeof(raw));

    void *bytes = rt_bytes_new(16);
    for (int i = 0; i < 16; i++)
        rt_bytes_set(bytes, i, raw[i]);

    // Encode to base64
    rt_string result = rt_bytes_to_base64(bytes);
    if (rt_obj_release_check0(bytes))
        rt_obj_free(bytes);
    return result;
}

/// @brief Compute the Sec-WebSocket-Accept header value for a given key.
///
/// Returns Base64(SHA1(key + WS_MAGIC)) as a malloc'd C string.
/// The caller is responsible for freeing the returned string.
/// Returns NULL on allocation failure.
char *rt_ws_compute_accept_key(const char *key_cstr)
{
    if (!key_cstr)
        return NULL;

    static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    size_t key_len = strlen(key_cstr);
    size_t magic_len = sizeof(WS_MAGIC) - 1;

    char *concat = (char *)malloc(key_len + magic_len + 1);
    if (!concat)
        return NULL;

    memcpy(concat, key_cstr, key_len);
    memcpy(concat + key_len, WS_MAGIC, magic_len);
    concat[key_len + magic_len] = '\0';

    uint8_t sha1_digest[20];
    ws_sha1((const uint8_t *)concat, key_len + magic_len, sha1_digest);
    free(concat);

    // Base64-encode using rt_bytes_to_base64
    void *digest_bytes = rt_bytes_new(20);
    if (!digest_bytes)
        return NULL;

    for (int i = 0; i < 20; i++)
        rt_bytes_set(digest_bytes, i, sha1_digest[i]);

    rt_string accept_str = rt_bytes_to_base64(digest_bytes);
    if (rt_obj_release_check0(digest_bytes))
        rt_obj_free(digest_bytes);

    if (!accept_str)
        return NULL;

    const char *accept_cstr = rt_string_cstr(accept_str);
    char *result = accept_cstr ? strdup(accept_cstr) : NULL;
    rt_string_unref(accept_str);
    return result;
}

/// @brief Parse URL into components.
/// @return 1 on success, 0 on failure.
static int parse_ws_url(const char *url, int *is_secure, char **host, int *port, char **path)
{
    *is_secure = 0;
    *host = NULL;
    *port = 80;
    *path = NULL;

    if (strncmp(url, "wss://", 6) == 0)
    {
        *is_secure = 1;
        *port = 443;
        url += 6;
    }
    else if (strncmp(url, "ws://", 5) == 0)
    {
        url += 5;
    }
    else
    {
        return 0; // Invalid scheme
    }

    // Find host end
    const char *host_end = url;
    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    size_t host_len = host_end - url;
    *host = malloc(host_len + 1);
    if (!*host)
        return 0;
    memcpy(*host, url, host_len);
    (*host)[host_len] = '\0';

    // Check for port
    if (*host_end == ':')
    {
        *port = atoi(host_end + 1);
        while (*host_end && *host_end != '/')
            host_end++;
    }

    // Path
    if (*host_end == '/')
    {
        *path = strdup(host_end);
    }
    else
    {
        *path = strdup("/");
    }

    if (!*path)
    {
        free(*host);
        *host = NULL;
        return 0;
    }

    return 1;
}

/// @brief Send data over connection (handles TLS vs plain TCP).
static long ws_send(rt_ws_impl *ws, const void *data, size_t len)
{
    if (ws->tls)
    {
        return rt_tls_send(ws->tls, data, len);
    }
    else
    {
        return send(ws->socket_fd, data, len, 0);
    }
}

/// @brief Receive data from connection (handles TLS vs plain TCP).
static long ws_recv(rt_ws_impl *ws, void *buffer, size_t len)
{
    if (ws->tls)
    {
        return rt_tls_recv(ws->tls, buffer, len);
    }
    else
    {
        return recv(ws->socket_fd, buffer, len, 0);
    }
}

/// @brief Wait for socket to become readable or writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
static int ws_wait_socket(int fd, int timeout_ms, int for_write)
{
#if 0 // removed: ViperDOS now provides select() via libc
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((unsigned)fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result;
    if (for_write)
        result = select(fd + 1, NULL, &fds, NULL, &tv);
    else
        result = select(fd + 1, &fds, NULL, NULL, &tv);

    return result;
#endif
}

/// @brief Set socket to non-blocking mode.
static void ws_set_nonblocking(int fd, int nonblocking)
{
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    // Unix and ViperDOS: use fcntl.
    int flags = fcntl(fd, F_GETFL, 0);
    if (nonblocking)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

/// @brief Set socket receive/send timeout.
static void ws_set_socket_timeout(int fd, int timeout_ms, int is_recv)
{
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(fd, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    // Unix and ViperDOS: use setsockopt with timeval.
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/// @brief Clear socket timeout (set to 0 = no timeout).
static void ws_clear_socket_timeout(int fd, int is_recv)
{
    ws_set_socket_timeout(fd, 0, is_recv);
}

/// @brief Create TCP connection to host:port with optional timeout.
static int create_tcp_socket(const char *host, int port, int64_t timeout_ms)
{
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;

        if (timeout_ms > 0)
        {
            // Non-blocking connect with timeout
            ws_set_nonblocking(fd, 1);

            int rc = connect(fd, p->ai_addr, (int)p->ai_addrlen);
            if (rc == 0)
            {
                // Connected immediately
                ws_set_nonblocking(fd, 0);
                break;
            }

#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
#else
            int err = errno;
            if (err == EINPROGRESS)
#endif
            {
                int ready = ws_wait_socket(fd, (int)timeout_ms, 1);
                if (ready > 0)
                {
                    // Check if connect succeeded
                    int so_error = 0;
                    socklen_t len = sizeof(so_error);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                    if (so_error == 0)
                    {
                        ws_set_nonblocking(fd, 0);
                        break;
                    }
                }
            }

            ws_set_nonblocking(fd, 0);
            close(fd);
            fd = -1;
        }
        else
        {
            // Blocking connect (no timeout)
            if (connect(fd, p->ai_addr, (int)p->ai_addrlen) == 0)
                break;

            close(fd);
            fd = -1;
        }
    }

    freeaddrinfo(res);
    return fd;
}

/// @brief Perform WebSocket handshake.
static int ws_handshake(rt_ws_impl *ws, const char *host, int port, const char *path)
{
    // Generate key and keep a copy for accept validation
    rt_string ws_key = generate_ws_key();
    const char *key_cstr = rt_string_cstr(ws_key);

    char key_copy[64] = {0};
    if (key_cstr)
        strncpy(key_copy, key_cstr, sizeof(key_copy) - 1);

    // Build handshake request
    char request[2048];
    int req_len = snprintf(request,
                           sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n",
                           path,
                           host,
                           port,
                           key_cstr);

    rt_string_unref(ws_key);

    // Send request
    if (ws_send(ws, request, req_len) != req_len)
        return 0;

    // Receive response headers
    char response[4096];
    int total = 0;
    while (total < (int)sizeof(response) - 1)
    {
        long n = ws_recv(ws, response + total, 1);
        if (n <= 0)
            return 0;
        total++;

        // Check for end of headers
        if (total >= 4 && memcmp(response + total - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    response[total] = '\0';

    // Check for 101 Switching Protocols
    if (strstr(response, "101") == NULL)
        return 0;

    // Check for Upgrade: websocket (case-insensitive on value)
    if (strstr(response, "Upgrade: websocket") == NULL &&
        strstr(response, "upgrade: websocket") == NULL)
        return 0;

    // Validate Sec-WebSocket-Accept (RFC 6455 §4.1)
    // Expected = Base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
    static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    size_t key_len = strlen(key_copy);
    size_t magic_len = sizeof(WS_MAGIC) - 1;
    char *concat = (char *)malloc(key_len + magic_len + 1);
    if (!concat)
        return 0;
    memcpy(concat, key_copy, key_len);
    memcpy(concat + key_len, WS_MAGIC, magic_len);
    concat[key_len + magic_len] = '\0';

    uint8_t sha1_digest[20];
    ws_sha1((const uint8_t *)concat, key_len + magic_len, sha1_digest);
    free(concat);

    // Base64-encode the SHA-1 digest
    void *digest_bytes = rt_bytes_new(20);
    for (int i = 0; i < 20; i++)
        rt_bytes_set(digest_bytes, i, sha1_digest[i]);
    rt_string expected_str = rt_bytes_to_base64(digest_bytes);
    if (rt_obj_release_check0(digest_bytes))
        rt_obj_free(digest_bytes);

    // Find Sec-WebSocket-Accept header in the response (case-insensitive search
    // via two common capitalizations; RFC 7230 says headers are case-insensitive)
    const char *accept_hdr = NULL;
    const char *found = strstr(response, "Sec-WebSocket-Accept:");
    if (!found)
        found = strstr(response, "sec-websocket-accept:");
    if (found)
    {
        accept_hdr = found + 21; // skip "Sec-WebSocket-Accept:"
        while (*accept_hdr == ' ')
            accept_hdr++;
    }

    int accept_ok = 0;
    if (accept_hdr && expected_str)
    {
        const char *expected_cstr = rt_string_cstr(expected_str);
        if (expected_cstr)
        {
            size_t elen = strlen(expected_cstr);
            accept_ok = (strncmp(accept_hdr, expected_cstr, elen) == 0 &&
                         (accept_hdr[elen] == '\r' || accept_hdr[elen] == '\n' ||
                          accept_hdr[elen] == '\0'));
        }
    }

    if (expected_str)
        rt_string_unref(expected_str);

    return accept_ok;
}

/// @brief Send a WebSocket frame.
static int ws_send_frame(rt_ws_impl *ws, uint8_t opcode, const void *data, size_t len)
{
    uint8_t header[14];
    size_t header_len = 2;

    // FIN + opcode
    header[0] = WS_FIN | opcode;

    // Mask + length
    if (len < 126)
    {
        header[1] = WS_MASK | (uint8_t)len;
    }
    else if (len < 65536)
    {
        header[1] = WS_MASK | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len);
        header_len = 4;
    }
    else
    {
        header[1] = WS_MASK | 127;
        header[2] = 0;
        header[3] = 0;
        header[4] = 0;
        header[5] = 0;
        header[6] = (uint8_t)(len >> 24);
        header[7] = (uint8_t)(len >> 16);
        header[8] = (uint8_t)(len >> 8);
        header[9] = (uint8_t)(len);
        header_len = 10;
    }

    // Generate masking key using CSPRNG (RFC 6455 §5.3 requires unpredictability)
    uint8_t mask[4];
    rt_crypto_random_bytes(mask, sizeof(mask));
    memcpy(header + header_len, mask, 4);
    header_len += 4;

    // Send header
    if (ws_send(ws, header, header_len) != (long)header_len)
        return 0;

    // Mask and send data
    if (len > 0)
    {
        uint8_t *masked = malloc(len);
        if (!masked)
            return 0;
        const uint8_t *src = data;
        for (size_t i = 0; i < len; i++)
            masked[i] = src[i] ^ mask[i % 4];
        long sent = ws_send(ws, masked, len);
        free(masked);
        if (sent != (long)len)
            return 0;
    }

    return 1;
}

/// @brief Read exactly n bytes from connection.
static int ws_recv_exact(rt_ws_impl *ws, void *buffer, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        long n = ws_recv(ws, (uint8_t *)buffer + total, len - total);
        if (n <= 0)
            return 0;
        total += n;
    }
    return 1;
}

/// @brief Receive a WebSocket frame.
/// @param fin_out Receives 1 if this is the final fragment (FIN bit set).
/// @param opcode_out Receives the frame opcode.
/// @param data_out Receives the payload (caller frees).
/// @param len_out Receives the payload length.
/// @return 1 on success, 0 on error.
static int ws_recv_frame(
    rt_ws_impl *ws, uint8_t *fin_out, uint8_t *opcode_out, uint8_t **data_out, size_t *len_out)
{
    uint8_t header[2];
    if (!ws_recv_exact(ws, header, 2))
        return 0;

    *fin_out = (header[0] & WS_FIN) ? 1 : 0; // H-11: expose FIN bit for reassembly
    *opcode_out = header[0] & 0x0F;
    uint8_t masked = header[1] & WS_MASK;
    size_t payload_len = header[1] & 0x7F;

    // M-9: RFC 6455 §5.1 — client MUST close connection if server sends a masked frame.
    if (masked)
    {
        ws->is_open = 0;
        ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
        return 0;
    }

    // Extended payload length
    if (payload_len == 126)
    {
        uint8_t ext[2];
        if (!ws_recv_exact(ws, ext, 2))
            return 0;
        payload_len = ((size_t)ext[0] << 8) | ext[1];
    }
    else if (payload_len == 127)
    {
        uint8_t ext[8];
        if (!ws_recv_exact(ws, ext, 8))
            return 0;
        payload_len =
            ((size_t)ext[4] << 24) | ((size_t)ext[5] << 16) | ((size_t)ext[6] << 8) | ext[7];
    }

    /* Reject server-controlled allocation larger than 64 MB (S-10 fix).
       This prevents a malicious server from causing malloc(huge). */
#define WS_MAX_PAYLOAD (64u * 1024u * 1024u)
    if (payload_len > WS_MAX_PAYLOAD)
        return 0;
#undef WS_MAX_PAYLOAD

    // Payload
    *data_out = NULL;
    *len_out = payload_len;
    if (payload_len > 0)
    {
        *data_out = malloc(payload_len);
        if (!*data_out)
            return 0;
        if (!ws_recv_exact(ws, *data_out, payload_len))
        {
            free(*data_out);
            *data_out = NULL;
            return 0;
        }
    }

    return 1;
}

/// @brief Handle control frames (ping, pong, close).
static void ws_handle_control(rt_ws_impl *ws, uint8_t opcode, uint8_t *data, size_t len)
{
    switch (opcode)
    {
        case WS_OP_PING:
            // Respond with pong
            ws_send_frame(ws, WS_OP_PONG, data, len);
            break;

        case WS_OP_PONG:
            // Ignore pongs
            break;

        case WS_OP_CLOSE:
            // Parse close code and reason
            ws->is_open = 0;
            if (len >= 2)
            {
                ws->close_code = ((int64_t)data[0] << 8) | data[1];
                if (len > 2)
                {
                    ws->close_reason = malloc(len - 1);
                    if (ws->close_reason)
                    {
                        memcpy(ws->close_reason, data + 2, len - 2);
                        ws->close_reason[len - 2] = '\0';
                    }
                }
            }
            else
            {
                ws->close_code = WS_CLOSE_NO_STATUS;
            }
            // Send close response
            ws_send_frame(ws, WS_OP_CLOSE, data, len);
            break;
    }
}

/// @brief Finalizer for WebSocket connections.
static void rt_ws_finalize(void *obj)
{
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (ws->tls)
    {
        rt_tls_close(ws->tls);
        ws->tls = NULL;
    }
    if (ws->socket_fd >= 0)
    {
        close(ws->socket_fd);
        ws->socket_fd = -1;
    }
    free(ws->url);
    free(ws->close_reason);
    free(ws->recv_buffer);
    ws->url = NULL;
    ws->close_reason = NULL;
    ws->recv_buffer = NULL;
}

void *rt_ws_connect(rt_string url)
{
    return rt_ws_connect_for(url, 30000); // 30 second default timeout
}

void *rt_ws_connect_for(rt_string url, int64_t timeout_ms)
{
    const char *url_cstr = rt_string_cstr(url);
    if (!url_cstr)
    {
        rt_trap("WebSocket: NULL URL");
        return NULL;
    }

    int is_secure;
    char *host = NULL;
    int port;
    char *path = NULL;

    if (!parse_ws_url(url_cstr, &is_secure, &host, &port, &path))
    {
        rt_trap("WebSocket: invalid URL");
        return NULL;
    }

    // Create connection object
    rt_ws_impl *ws = (rt_ws_impl *)rt_obj_new_i64(0, sizeof(rt_ws_impl));
    if (!ws)
    {
        free(host);
        free(path);
        rt_trap("WebSocket: memory allocation failed");
        return NULL;
    }

    ws->vptr = NULL;
    ws->socket_fd = -1;
    ws->tls = NULL;
    ws->url = strdup(url_cstr);
    if (!ws->url)
    {
        free(host);
        free(path);
        rt_obj_free(ws);
        return NULL;
    }
    ws->is_open = 0;
    ws->close_code = 0;
    ws->close_reason = NULL;
    ws->recv_buffer = NULL;
    ws->recv_buffer_size = 0;
    ws->recv_buffer_len = 0;

    rt_obj_set_finalizer(ws, rt_ws_finalize);

    // Connect TCP (with timeout)
    ws->socket_fd = create_tcp_socket(host, port, timeout_ms);
    if (ws->socket_fd < 0)
    {
        free(host);
        free(path);
        if (rt_obj_release_check0(ws))
            rt_obj_free(ws);
        rt_trap("WebSocket: connection failed");
        return NULL;
    }

    // Set socket-level recv/send timeout for handshake phase
    if (timeout_ms > 0)
    {
        ws_set_socket_timeout(ws->socket_fd, (int)timeout_ms, 1);
        ws_set_socket_timeout(ws->socket_fd, (int)timeout_ms, 0);
    }

    // TLS handshake if secure
    if (is_secure)
    {
        rt_tls_config_t config;
        rt_tls_config_init(&config);
        config.hostname = host;

        ws->tls = rt_tls_new(ws->socket_fd, &config);
        if (!ws->tls)
        {
            free(host);
            free(path);
            if (rt_obj_release_check0(ws))
                rt_obj_free(ws);
            rt_trap("WebSocket: TLS setup failed");
            return NULL;
        }

        if (rt_tls_handshake(ws->tls) != RT_TLS_OK)
        {
            free(host);
            free(path);
            if (rt_obj_release_check0(ws))
                rt_obj_free(ws);
            rt_trap("WebSocket: TLS handshake failed");
            return NULL;
        }
    }

    // WebSocket handshake
    if (!ws_handshake(ws, host, port, path))
    {
        free(host);
        free(path);
        if (rt_obj_release_check0(ws))
            rt_obj_free(ws);
        rt_trap("WebSocket: handshake failed");
        return NULL;
    }

    free(host);
    free(path);

    // Clear socket timeouts now that handshake is complete
    if (timeout_ms > 0)
    {
        ws_clear_socket_timeout(ws->socket_fd, 1);
        ws_clear_socket_timeout(ws->socket_fd, 0);
    }

    ws->is_open = 1;
    return ws;
}

rt_string rt_ws_url(void *obj)
{
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;
    if (!ws->url)
        return rt_str_empty();
    return rt_string_from_bytes(ws->url, strlen(ws->url));
}

int8_t rt_ws_is_open(void *obj)
{
    if (!obj)
        return 0;
    rt_ws_impl *ws = obj;
    return ws->is_open;
}

int64_t rt_ws_close_code(void *obj)
{
    if (!obj)
        return 0;
    rt_ws_impl *ws = obj;
    return ws->close_code;
}

rt_string rt_ws_close_reason(void *obj)
{
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;
    if (!ws->close_reason)
        return rt_str_empty();
    return rt_string_from_bytes(ws->close_reason, strlen(ws->close_reason));
}

void rt_ws_send(void *obj, rt_string text)
{
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open)
    {
        rt_trap("WebSocket: connection is closed");
        return;
    }

    const char *cstr = rt_string_cstr(text);
    size_t len = cstr ? strlen(cstr) : 0;

    if (!ws_send_frame(ws, WS_OP_TEXT, cstr, len))
    {
        ws->is_open = 0;
        rt_trap("WebSocket: send failed");
    }
}

void rt_ws_send_bytes(void *obj, void *data)
{
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open)
    {
        rt_trap("WebSocket: connection is closed");
        return;
    }

    int64_t len = rt_bytes_len(data);
    uint8_t *buffer = malloc(len);
    if (!buffer && len > 0)
    {
        rt_trap("WebSocket: memory allocation failed");
        return;
    }

    for (int64_t i = 0; i < len; i++)
        buffer[i] = (uint8_t)rt_bytes_get(data, i);

    if (!ws_send_frame(ws, WS_OP_BINARY, buffer, len))
    {
        free(buffer);
        ws->is_open = 0;
        rt_trap("WebSocket: send failed");
        return;
    }

    free(buffer);
}

void rt_ws_ping(void *obj)
{
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open)
        return;

    ws_send_frame(ws, WS_OP_PING, NULL, 0);
}

rt_string rt_ws_recv(void *obj)
{
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;

    // H-11: Fragmentation reassembly buffer (RFC 6455 §5.4)
    uint8_t *frag_buf = NULL;
    size_t frag_len = 0;
    uint8_t frag_opcode = 0; // opcode of the first fragment

    while (ws->is_open)
    {
        uint8_t fin, opcode;
        uint8_t *data = NULL;
        size_t len;

        if (!ws_recv_frame(ws, &fin, &opcode, &data, &len))
        {
            free(frag_buf);
            ws->is_open = 0;
            ws->close_code = WS_CLOSE_ABNORMAL;
            return rt_str_empty();
        }

        // Control frames may arrive in the middle of fragmented messages (RFC 6455 §5.5)
        // and must not disturb the fragmentation state.
        if (opcode >= 0x08)
        {
            ws_handle_control(ws, opcode, data, len);
            free(data);
            continue;
        }

        // First fragment or unfragmented message
        if (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY)
        {
            free(frag_buf); // discard any incomplete previous message
            frag_buf = NULL;
            frag_len = 0;
            frag_opcode = opcode;
        }
        else if (opcode != WS_OP_CONTINUATION)
        {
            free(data);
            continue; // skip unknown opcodes
        }

        // Accumulate fragment payload
        if (len > 0)
        {
            uint8_t *new_buf = (uint8_t *)realloc(frag_buf, frag_len + len);
            if (!new_buf)
            {
                free(data);
                free(frag_buf);
                ws->is_open = 0;
                return rt_str_empty();
            }
            frag_buf = new_buf;
            memcpy(frag_buf + frag_len, data, len);
            frag_len += len;
        }
        free(data);

        if (fin)
        {
            // Final fragment: deliver the complete message
            rt_string result = rt_string_from_bytes((const char *)frag_buf, frag_len);
            free(frag_buf);
            (void)frag_opcode; // opcode available for future text/binary distinction
            return result;
        }
        // FIN=0: continue accumulating continuation frames
    }

    free(frag_buf);
    return rt_str_empty();
}

rt_string rt_ws_recv_for(void *obj, int64_t timeout_ms)
{
    if (!obj)
        return NULL;
    rt_ws_impl *ws = (rt_ws_impl *)obj;
    if (!ws->is_open)
        return NULL;

    // Wait for data to arrive with timeout
    if (timeout_ms > 0)
    {
        int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
        if (ready <= 0)
            return NULL; // Timeout or error
    }

    return rt_ws_recv(obj);
}

void *rt_ws_recv_bytes(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);
    rt_ws_impl *ws = obj;

    // H-11: Fragmentation reassembly buffer (RFC 6455 §5.4)
    uint8_t *frag_buf = NULL;
    size_t frag_len = 0;

    while (ws->is_open)
    {
        uint8_t fin, opcode;
        uint8_t *data = NULL;
        size_t len;

        if (!ws_recv_frame(ws, &fin, &opcode, &data, &len))
        {
            free(frag_buf);
            ws->is_open = 0;
            ws->close_code = WS_CLOSE_ABNORMAL;
            return rt_bytes_new(0);
        }

        // Control frames may interleave within fragmented messages
        if (opcode >= 0x08)
        {
            ws_handle_control(ws, opcode, data, len);
            free(data);
            continue;
        }

        if (opcode == WS_OP_BINARY || opcode == WS_OP_TEXT)
        {
            free(frag_buf);
            frag_buf = NULL;
            frag_len = 0;
        }
        else if (opcode != WS_OP_CONTINUATION)
        {
            free(data);
            continue;
        }

        // Accumulate fragment payload
        if (len > 0)
        {
            uint8_t *new_buf = (uint8_t *)realloc(frag_buf, frag_len + len);
            if (!new_buf)
            {
                free(data);
                free(frag_buf);
                ws->is_open = 0;
                return rt_bytes_new(0);
            }
            frag_buf = new_buf;
            memcpy(frag_buf + frag_len, data, len);
            frag_len += len;
        }
        free(data);

        if (fin)
        {
            // Final fragment: deliver complete message as Bytes object
            void *result = rt_bytes_new((int64_t)frag_len);
            for (size_t i = 0; i < frag_len; i++)
                rt_bytes_set(result, (int64_t)i, frag_buf[i]);
            free(frag_buf);
            return result;
        }
    }

    free(frag_buf);
    return rt_bytes_new(0);
}

void *rt_ws_recv_bytes_for(void *obj, int64_t timeout_ms)
{
    if (!obj)
        return NULL;
    rt_ws_impl *ws = (rt_ws_impl *)obj;
    if (!ws->is_open)
        return NULL;

    // Wait for data to arrive with timeout
    if (timeout_ms > 0)
    {
        int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
        if (ready <= 0)
            return NULL; // Timeout or error
    }

    return rt_ws_recv_bytes(obj);
}

void rt_ws_close(void *obj)
{
    rt_ws_close_with(obj, WS_CLOSE_NORMAL, rt_str_empty());
}

void rt_ws_close_with(void *obj, int64_t code, rt_string reason)
{
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open)
        return;

    const char *reason_cstr = rt_string_cstr(reason);
    size_t reason_len = reason_cstr ? strlen(reason_cstr) : 0;

    // Build close payload
    size_t payload_len = 2 + reason_len;
    uint8_t *payload = malloc(payload_len);
    if (payload)
    {
        payload[0] = (uint8_t)(code >> 8);
        payload[1] = (uint8_t)(code);
        if (reason_len > 0)
            memcpy(payload + 2, reason_cstr, reason_len);

        ws_send_frame(ws, WS_OP_CLOSE, payload, payload_len);
        free(payload);
    }

    ws->is_open = 0;
    ws->close_code = code;
}
