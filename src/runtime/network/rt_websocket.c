//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_websocket.c
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

// Forward declarations (defined in rt_io.c).
#include "rt_trap.h"
extern void rt_trap_net(const char *msg, int err_code);

#include "rt_error.h"

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

// SIGPIPE suppression (same approach as rt_network.c).
#if defined(__linux__) || defined(__viperdos__)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/// @brief Suppress SIGPIPE on writes to a closed peer.
///
/// macOS uses the per-socket `SO_NOSIGPIPE` option; Linux uses
/// per-call `MSG_NOSIGNAL` (set via `SEND_FLAGS`); other platforms
/// have nothing to do.
static void suppress_sigpipe(int sock) {
#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    (void)sock;
}

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
#define WS_CLOSE_INVALID_DATA 1007
#define WS_CLOSE_MESSAGE_TOO_BIG 1009

// Maximum total size for reassembled fragmented messages (64 MB)
#define WS_MAX_REASSEMBLY_SIZE (64u * 1024u * 1024u)

/// @brief WebSocket connection implementation.
typedef struct rt_ws_impl {
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

/// @brief True if `host` is an IPv6 literal that must be wrapped in `[…]` for URL/Host.
static int host_needs_brackets(const char *host) {
    return host && strchr(host, ':') != NULL && host[0] != '[';
}

/// @brief Validate a WebSocket frame opcode against RFC 6455 §5.2.
///
/// Only the six defined opcodes (continuation, text, binary, close,
/// ping, pong) are legal — reserved opcodes 0x3-0x7 and 0xB-0xF
/// must trigger a protocol-error close.
/// @return 1 if `opcode` is a defined WS opcode, 0 otherwise.
static int ws_is_valid_opcode(uint8_t opcode) {
    switch (opcode) {
        case WS_OP_CONTINUATION:
        case WS_OP_TEXT:
        case WS_OP_BINARY:
        case WS_OP_CLOSE:
        case WS_OP_PING:
        case WS_OP_PONG:
            return 1;
        default:
            return 0;
    }
}

/// @brief Encode `len` as a big-endian 8-byte payload-length field.
///
/// Used for WebSocket frames whose payload is >= 65536 bytes
/// (RFC 6455 §5.2 — the 7+64 length encoding).
static void ws_encode_u64_len(uint8_t out[8], size_t len) {
    uint64_t value = (uint64_t)len;
    for (int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(value & 0xFFu);
        value >>= 8;
    }
}

/// @brief Decode an 8-byte big-endian length field into a host `size_t`.
///
/// Rejects values that exceed `SIZE_MAX` so we never silently
/// truncate on 32-bit platforms.
/// @return 1 on success, 0 if the value would overflow `size_t`.
static int ws_decode_u64_len(const uint8_t in[8], size_t *len_out) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value = (value << 8) | in[i];
    if (value > (uint64_t)SIZE_MAX)
        return 0;
    *len_out = (size_t)value;
    return 1;
}

/// @brief Validate a byte buffer as well-formed UTF-8 per RFC 3629.
///
/// Required by RFC 6455 §8.1 — text frames whose payload is not
/// valid UTF-8 must trigger a 1007 close. Walks each codepoint and
/// rejects:
///   - over-long encodings (e.g. C0/C1, E0 with a < 0xA0 byte)
///   - surrogate halves (ED A0..BF)
///   - codepoints above U+10FFFF (F4 above 0x8F, F5..FF entirely)
///   - truncated multi-byte sequences
/// @return 1 if all `len` bytes form valid UTF-8, 0 otherwise.
static int ws_is_valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];
        if (c <= 0x7F)
            continue;

        if (c >= 0xC2 && c <= 0xDF) {
            if (i >= len || (data[i] & 0xC0) != 0x80)
                return 0;
            i++;
            continue;
        }

        if (c == 0xE0) {
            if (i + 1 >= len || data[i] < 0xA0 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c >= 0xE1 && c <= 0xEC) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c == 0xED) {
            if (i + 1 >= len || data[i] < 0x80 || data[i] > 0x9F || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c >= 0xEE && c <= 0xEF) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c == 0xF0) {
            if (i + 2 >= len || data[i] < 0x90 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        if (c >= 0xF1 && c <= 0xF3) {
            if (i + 2 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        if (c == 0xF4) {
            if (i + 2 >= len || data[i] < 0x80 || data[i] > 0x8F || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        return 0;
    }
    return 1;
}

/// @brief Minimal SHA-1 (RFC 3174) for Sec-WebSocket-Accept validation (RFC 6455 §4.1).
/// SHA-1 is acceptable here: it is used as a protocol-mandated HMAC-like check,
/// not for general cryptographic security.
static void ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
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

    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)padded[chunk + j * 4] << 24) |
                   ((uint32_t)padded[chunk + j * 4 + 1] << 16) |
                   ((uint32_t)padded[chunk + j * 4 + 2] << 8) |
                   ((uint32_t)padded[chunk + j * 4 + 3]);
        }
        for (int j = 16; j < 80; j++) {
            uint32_t t = w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16];
            w[j] = (t << 1) | (t >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (j < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (j < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
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

    for (int i = 0; i < 4; i++) {
        digest[i] = (uint8_t)(h0 >> (24 - i * 8));
        digest[4 + i] = (uint8_t)(h1 >> (24 - i * 8));
        digest[8 + i] = (uint8_t)(h2 >> (24 - i * 8));
        digest[12 + i] = (uint8_t)(h3 >> (24 - i * 8));
        digest[16 + i] = (uint8_t)(h4 >> (24 - i * 8));
    }
}

/// @brief Generate a random WebSocket key (16 random bytes, base64 encoded).
static rt_string generate_ws_key(void) {
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
char *rt_ws_compute_accept_key(const char *key_cstr) {
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
static int parse_ws_url(const char *url, int *is_secure, char **host, int *port, char **path) {
    *is_secure = 0;
    *host = NULL;
    *port = 80;
    *path = NULL;

    if (strncmp(url, "wss://", 6) == 0) {
        *is_secure = 1;
        *port = 443;
        url += 6;
    } else if (strncmp(url, "ws://", 5) == 0) {
        url += 5;
    } else {
        return 0; // Invalid scheme
    }

    const char *host_end = url;
    if (*url == '[') {
        const char *bracket_end = strchr(url + 1, ']');
        if (!bracket_end)
            return 0;
        size_t host_len = (size_t)(bracket_end - (url + 1));
        if (host_len == 0)
            return 0;
        *host = malloc(host_len + 1);
        if (!*host)
            return 0;
        memcpy(*host, url + 1, host_len);
        (*host)[host_len] = '\0';
        host_end = bracket_end + 1;
    } else {
        while (*host_end && *host_end != ':' && *host_end != '/')
            host_end++;

        size_t host_len = (size_t)(host_end - url);
        *host = malloc(host_len + 1);
        if (!*host)
            return 0;
        memcpy(*host, url, host_len);
        (*host)[host_len] = '\0';
    }

    if (*host_end && *host_end != ':' && *host_end != '/') {
        free(*host);
        *host = NULL;
        return 0;
    }

    // Check for port
    if (*host_end == ':') {
        char *endptr = NULL;
        long port_val = strtol(host_end + 1, &endptr, 10);
        if (endptr == host_end + 1 || port_val < 1 || port_val > 65535) {
            free(*host);
            *host = NULL;
            return 0;
        }
        *port = (int)port_val;
        host_end = endptr;
        while (*host_end && *host_end != '/')
            host_end++;
    }

    // Path
    if (*host_end == '/') {
        *path = strdup(host_end);
    } else {
        *path = strdup("/");
    }

    if (!*path) {
        free(*host);
        *host = NULL;
        return 0;
    }

    return 1;
}

/// @brief Test hook exposing the otherwise-static `parse_ws_url`.
/// Lets unit tests probe URL-parsing edge cases without going
/// through `rt_ws_connect`. Output strings are heap-allocated and
/// must be freed by the caller.
int rt_ws_parse_url_for_test(const char *url, int *is_secure, char **host, int *port, char **path) {
    return parse_ws_url(url, is_secure, host, port, path);
}

/// @brief Send data over connection (handles TLS vs plain TCP).
static long ws_send_partial(rt_ws_impl *ws, const void *data, size_t len) {
    if (ws->tls) {
        return rt_tls_send(ws->tls, data, len);
    } else {
        return send(ws->socket_fd, data, (int)len, SEND_FLAGS);
    }
}

/// @brief Send `len` bytes, looping on partial sends until done or failed.
///
/// Wraps `ws_send_partial` with a retry loop so callers don't have
/// to handle short writes from TCP/TLS. A non-positive return from
/// any partial send aborts the whole transfer.
/// @return 1 if all bytes sent, 0 on failure.
static int ws_send_all(rt_ws_impl *ws, const void *data, size_t len) {
    const uint8_t *ptr = (const uint8_t *)data;
    size_t total = 0;
    while (total < len) {
        long sent = ws_send_partial(ws, ptr + total, len - total);
        if (sent <= 0)
            return 0;
        total += (size_t)sent;
    }
    return 1;
}

/// @brief Receive data from connection (handles TLS vs plain TCP).
static long ws_recv(rt_ws_impl *ws, void *buffer, size_t len) {
    if (ws->tls) {
        return rt_tls_recv(ws->tls, buffer, len);
    } else {
        return recv(ws->socket_fd, buffer, (int)len, 0);
    }
}

/// @brief Wait for socket to become readable or writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
static int ws_wait_socket(int fd, int timeout_ms, int for_write) {
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
static void ws_set_nonblocking(int fd, int nonblocking) {
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
static void ws_set_socket_timeout(int fd, int timeout_ms, int is_recv) {
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
static void ws_clear_socket_timeout(int fd, int is_recv) {
    ws_set_socket_timeout(fd, 0, is_recv);
}

/// @brief Case-insensitive ASCII compare of two `len`-byte regions.
///
/// Used for HTTP header name/value matching during the WebSocket
/// handshake. ASCII-only — does not lowercase non-ASCII bytes (a
/// deliberate narrow contract since headers are 7-bit ASCII per HTTP).
/// @return 1 if the regions match case-insensitively, 0 otherwise.
static int ws_ascii_ieq_n(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb)
            return 0;
    }
    return 1;
}

/// @brief Test whether a comma-separated header `value` contains `token`.
///
/// Splits on `,`, trims surrounding whitespace from each element,
/// and case-insensitively compares against `token`. Used to confirm
/// `Connection: Upgrade` (which may appear as `Connection:
/// keep-alive, Upgrade`).
/// @return 1 if token is present, 0 otherwise.
static int ws_header_has_token(const char *value, size_t len, const char *token) {
    size_t token_len = strlen(token);
    size_t i = 0;
    while (i < len) {
        while (i < len && (value[i] == ' ' || value[i] == '\t' || value[i] == ','))
            i++;
        size_t start = i;
        while (i < len && value[i] != ',')
            i++;
        size_t end = i;
        while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
            end--;
        if (end - start == token_len && ws_ascii_ieq_n(value + start, token, token_len))
            return 1;
    }
    return 0;
}

/// @brief Validate the server's WebSocket upgrade response (test-visible).
///
/// Per RFC 6455 §4.1 a successful handshake requires:
///   - Status line `HTTP/1.1 101 …`
///   - `Upgrade: websocket` header
///   - `Connection` header containing the `Upgrade` token
///   - `Sec-WebSocket-Accept` header equal to
///     `base64(SHA1(key_copy + WS_MAGIC))` where `WS_MAGIC` is
///     `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`.
/// Made non-static to allow direct unit testing of the parser.
/// @return 1 on a valid response, 0 on any mismatch / malformed header.
int rt_ws_validate_handshake_response_for_test(const char *response, const char *key_copy) {
    if (!response || !key_copy)
        return 0;

    const char *line_end = strstr(response, "\r\n");
    if (!line_end)
        return 0;
    if ((size_t)(line_end - response) < strlen("HTTP/1.1 101") ||
        strncmp(response, "HTTP/1.1 101", strlen("HTTP/1.1 101")) != 0)
        return 0;

    int upgrade_ok = 0;
    int connection_ok = 0;
    const char *accept_hdr = NULL;
    const char *p = line_end + 2;
    while (*p) {
        const char *next = strstr(p, "\r\n");
        if (!next)
            return 0;
        if (next == p)
            break;

        const char *colon = strchr(p, ':');
        if (colon && colon < next) {
            const char *value = colon + 1;
            while (value < next && (*value == ' ' || *value == '\t'))
                value++;
            size_t name_len = (size_t)(colon - p);
            size_t value_len = (size_t)(next - value);
            if (name_len == strlen("Upgrade") && ws_ascii_ieq_n(p, "Upgrade", name_len))
                upgrade_ok = (value_len == strlen("websocket") &&
                              ws_ascii_ieq_n(value, "websocket", value_len));
            else if (name_len == strlen("Connection") && ws_ascii_ieq_n(p, "Connection", name_len))
                connection_ok = ws_header_has_token(value, value_len, "Upgrade");
            else if (name_len == strlen("Sec-WebSocket-Accept") &&
                     ws_ascii_ieq_n(p, "Sec-WebSocket-Accept", name_len))
                accept_hdr = value;
        }

        p = next + 2;
    }

    if (!upgrade_ok || !connection_ok || !accept_hdr)
        return 0;

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

    void *digest_bytes = rt_bytes_new(20);
    for (int i = 0; i < 20; i++)
        rt_bytes_set(digest_bytes, i, sha1_digest[i]);
    rt_string expected_str = rt_bytes_to_base64(digest_bytes);
    if (rt_obj_release_check0(digest_bytes))
        rt_obj_free(digest_bytes);
    if (!expected_str)
        return 0;

    const char *expected_cstr = rt_string_cstr(expected_str);
    size_t expected_len = expected_cstr ? strlen(expected_cstr) : 0;
    int accept_ok = 0;
    if (expected_cstr && strncmp(accept_hdr, expected_cstr, expected_len) == 0 &&
        (accept_hdr[expected_len] == '\r' || accept_hdr[expected_len] == '\n' ||
         accept_hdr[expected_len] == '\0'))
        accept_ok = 1;

    rt_string_unref(expected_str);
    return accept_ok;
}

/// @brief Create TCP connection to host:port with optional timeout.
static int create_tcp_socket(const char *host, int port, int64_t timeout_ms) {
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (p = res; p; p = p->ai_next) {
        fd = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        suppress_sigpipe(fd);

        if (timeout_ms > 0) {
            // Non-blocking connect with timeout
            ws_set_nonblocking(fd, 1);

            int rc = connect(fd, p->ai_addr, (int)p->ai_addrlen);
            if (rc == 0) {
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
                if (ready > 0) {
                    // Check if connect succeeded
                    int so_error = 0;
                    socklen_t len = sizeof(so_error);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                    if (so_error == 0) {
                        ws_set_nonblocking(fd, 0);
                        break;
                    }
                }
            }

            ws_set_nonblocking(fd, 0);
            close(fd);
            fd = -1;
        } else {
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
static int ws_handshake(rt_ws_impl *ws, const char *host, int port, const char *path) {
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
                           "Host: %s%s%s:%d\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n",
                           path,
                           host_needs_brackets(host) ? "[" : "",
                           host,
                           host_needs_brackets(host) ? "]" : "",
                           port,
                           key_cstr);

    rt_string_unref(ws_key);

    // Send request
    if (req_len <= 0 || (size_t)req_len >= sizeof(request))
        return 0;
    if (!ws_send_all(ws, request, (size_t)req_len))
        return 0;

    // Receive response headers
    char response[4096];
    int total = 0;
    while (total < (int)sizeof(response) - 1) {
        long n = ws_recv(ws, response + total, 1);
        if (n <= 0)
            return 0;
        total++;

        // Check for end of headers
        if (total >= 4 && memcmp(response + total - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    response[total] = '\0';

    if (!rt_ws_validate_handshake_response_for_test(response, key_copy))
        return 0;
    return 1;
}

/// @brief Send a WebSocket frame.
static int ws_send_frame(rt_ws_impl *ws, uint8_t opcode, const void *data, size_t len) {
    uint8_t header[14];
    size_t header_len = 2;

    // FIN + opcode
    header[0] = WS_FIN | opcode;

    // Mask + length
    if (len < 126) {
        header[1] = WS_MASK | (uint8_t)len;
    } else if (len < 65536) {
        header[1] = WS_MASK | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len);
        header_len = 4;
    } else {
        header[1] = WS_MASK | 127;
        ws_encode_u64_len(header + 2, len);
        header_len = 10;
    }

    // Generate masking key using CSPRNG (RFC 6455 §5.3 requires unpredictability)
    uint8_t mask[4];
    rt_crypto_random_bytes(mask, sizeof(mask));
    memcpy(header + header_len, mask, 4);
    header_len += 4;

    // Send header
    if (!ws_send_all(ws, header, header_len))
        return 0;

    // Mask and send data
    if (len > 0) {
        const uint8_t *src = (const uint8_t *)data;
        uint8_t chunk[4096];
        size_t offset = 0;
        while (offset < len) {
            size_t part = len - offset;
            if (part > sizeof(chunk))
                part = sizeof(chunk);
            for (size_t i = 0; i < part; i++)
                chunk[i] = src[offset + i] ^ mask[(offset + i) & 3];
            if (!ws_send_all(ws, chunk, part))
                return 0;
            offset += part;
        }
    }

    return 1;
}

/// @brief Read exactly n bytes from connection.
static int ws_recv_exact(rt_ws_impl *ws, void *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
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
    rt_ws_impl *ws, uint8_t *fin_out, uint8_t *opcode_out, uint8_t **data_out, size_t *len_out) {
    uint8_t header[2];
    if (!ws_recv_exact(ws, header, 2))
        return 0;

    if ((header[0] & 0x70) != 0) {
        ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
        return 0;
    }

    *fin_out = (header[0] & WS_FIN) ? 1 : 0; // H-11: expose FIN bit for reassembly
    *opcode_out = header[0] & 0x0F;
    uint8_t masked = header[1] & WS_MASK;
    size_t payload_len = header[1] & 0x7F;

    if (!ws_is_valid_opcode(*opcode_out)) {
        ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
        return 0;
    }

    // M-9: RFC 6455 §5.1 — client MUST close connection if server sends a masked frame.
    if (masked) {
        ws->is_open = 0;
        ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
        return 0;
    }

    // Extended payload length
    if (payload_len == 126) {
        uint8_t ext[2];
        if (!ws_recv_exact(ws, ext, 2))
            return 0;
        payload_len = ((size_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!ws_recv_exact(ws, ext, 8))
            return 0;
        if (!ws_decode_u64_len(ext, &payload_len)) {
            ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
            return 0;
        }
    }

    if (*opcode_out >= 0x08 && (!*fin_out || payload_len > 125)) {
        ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
        return 0;
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
    if (payload_len > 0) {
        *data_out = malloc(payload_len);
        if (!*data_out)
            return 0;
        if (!ws_recv_exact(ws, *data_out, payload_len)) {
            free(*data_out);
            *data_out = NULL;
            return 0;
        }
    }

    return 1;
}

/// @brief Forward declaration for the control-frame handler (defined below).
static void ws_handle_control(rt_ws_impl *ws, uint8_t opcode, uint8_t *data, size_t len);

/// @brief Read a complete WebSocket message, reassembling fragments.
///
/// Loops over `ws_recv_frame`, transparently handling:
///   - Control frames (ping/pong/close) — dispatched to
///     `ws_handle_control` and not returned to the caller.
///   - Continuation frames — concatenated into a growing buffer.
///   - First data frame with `fin=1` — returned immediately.
///   - Reassembly cap (`WS_MAX_REASSEMBLY_SIZE`, 64 MB) — closes
///     with code 1009 (message too big).
///   - Out-of-order frames (e.g. continuation without an opener)
///     — closes with code 1002 (protocol error).
///   - Text payloads — final UTF-8 validation; closes with 1007
///     (invalid data) on failure.
///
/// On success `*data_out` is a heap allocation owned by the caller
/// (must be `free`d) and `*opcode_out` is `WS_OP_TEXT` or `WS_OP_BINARY`.
/// @return 1 on a complete message, 0 on close/error.
static int ws_recv_message(rt_ws_impl *ws,
                           uint8_t **data_out,
                           size_t *len_out,
                           uint8_t *opcode_out) {
    uint8_t *frag_buf = NULL;
    size_t frag_len = 0;
    uint8_t frag_opcode = 0;
    int frag_active = 0;

    *data_out = NULL;
    *len_out = 0;
    *opcode_out = 0;

    while (ws->is_open) {
        uint8_t fin = 0;
        uint8_t opcode = 0;
        uint8_t *data = NULL;
        size_t len = 0;

        if (!ws_recv_frame(ws, &fin, &opcode, &data, &len)) {
            free(frag_buf);
            if (ws->close_code == 0)
                ws->close_code = WS_CLOSE_ABNORMAL;
            ws->is_open = 0;
            return 0;
        }

        if (opcode >= 0x08) {
            ws_handle_control(ws, opcode, data, len);
            free(data);
            continue;
        }

        if (opcode == WS_OP_CONTINUATION) {
            if (!frag_active) {
                free(data);
                free(frag_buf);
                ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
                ws->is_open = 0;
                return 0;
            }
        } else if (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY) {
            if (frag_active) {
                free(data);
                free(frag_buf);
                ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
                ws->is_open = 0;
                return 0;
            }

            if (fin) {
                if (opcode == WS_OP_TEXT && !ws_is_valid_utf8(data, len)) {
                    free(data);
                    rt_ws_close_with(ws, WS_CLOSE_INVALID_DATA, rt_str_empty());
                    return 0;
                }
                *data_out = data;
                *len_out = len;
                *opcode_out = opcode;
                return 1;
            }

            frag_active = 1;
            frag_opcode = opcode;
        } else {
            free(data);
            free(frag_buf);
            ws->close_code = WS_CLOSE_PROTOCOL_ERROR;
            ws->is_open = 0;
            return 0;
        }

        if (len > 0) {
            if (frag_len + len > WS_MAX_REASSEMBLY_SIZE) {
                free(data);
                free(frag_buf);
                rt_ws_close_with(ws, WS_CLOSE_MESSAGE_TOO_BIG, rt_str_empty());
                return 0;
            }

            uint8_t *new_buf = (uint8_t *)realloc(frag_buf, frag_len + len);
            if (!new_buf) {
                free(data);
                free(frag_buf);
                ws->is_open = 0;
                return 0;
            }
            frag_buf = new_buf;
            memcpy(frag_buf + frag_len, data, len);
            frag_len += len;
        }
        free(data);

        if (fin) {
            if (frag_opcode == WS_OP_TEXT && !ws_is_valid_utf8(frag_buf, frag_len)) {
                free(frag_buf);
                rt_ws_close_with(ws, WS_CLOSE_INVALID_DATA, rt_str_empty());
                return 0;
            }
            *data_out = frag_buf;
            *len_out = frag_len;
            *opcode_out = frag_opcode;
            return 1;
        }
    }

    free(frag_buf);
    return 0;
}

/// @brief Handle control frames (ping, pong, close).
static void ws_handle_control(rt_ws_impl *ws, uint8_t opcode, uint8_t *data, size_t len) {
    switch (opcode) {
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
            if (len >= 2) {
                ws->close_code = ((int64_t)data[0] << 8) | data[1];
                if (len > 2) {
                    ws->close_reason = malloc(len - 1);
                    if (ws->close_reason) {
                        memcpy(ws->close_reason, data + 2, len - 2);
                        ws->close_reason[len - 2] = '\0';
                    }
                }
            } else {
                ws->close_code = WS_CLOSE_NO_STATUS;
            }
            // Send close response
            ws_send_frame(ws, WS_OP_CLOSE, data, len);
            break;
    }
}

/// @brief Finalizer for WebSocket connections.
static void rt_ws_finalize(void *obj) {
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (ws->tls) {
        rt_tls_close(ws->tls);
        ws->tls = NULL;
        ws->socket_fd = -1;
    } else if (ws->socket_fd >= 0) {
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

/// @brief Connect to a WebSocket URL with a 30-second default timeout.
/// @see rt_ws_connect_for
void *rt_ws_connect(rt_string url) {
    return rt_ws_connect_for(url, 30000); // 30 second default timeout
}

/// @brief Connect to a WebSocket URL (`ws://` or `wss://`) with explicit timeout.
///
/// Resolves the host, opens a TCP (or TLS for `wss://`) connection,
/// and performs the WebSocket handshake (RFC 6455 §4): generates a
/// random 16-byte key, sends an HTTP/1.1 Upgrade request, and
/// validates the server's `Sec-WebSocket-Accept` against the
/// expected `base64(SHA1(key + WS_MAGIC))`. On success returns a
/// GC-managed `rt_ws_impl` with `is_open == 1`.
/// @throws Err_InvalidUrl on bad URL,
///         generic trap on connect/handshake failure or NULL URL.
void *rt_ws_connect_for(rt_string url, int64_t timeout_ms) {
    const char *url_cstr = rt_string_cstr(url);
    if (!url_cstr) {
        rt_trap("WebSocket: NULL URL");
        return NULL;
    }

    int is_secure;
    char *host = NULL;
    int port;
    char *path = NULL;

    if (!parse_ws_url(url_cstr, &is_secure, &host, &port, &path)) {
        rt_trap_net("WebSocket: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    // Create connection object
    rt_ws_impl *ws = (rt_ws_impl *)rt_obj_new_i64(0, sizeof(rt_ws_impl));
    if (!ws) {
        free(host);
        free(path);
        rt_trap("WebSocket: memory allocation failed");
        return NULL;
    }

    ws->vptr = NULL;
    ws->socket_fd = -1;
    ws->tls = NULL;
    ws->url = strdup(url_cstr);
    if (!ws->url) {
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
    if (ws->socket_fd < 0) {
        free(host);
        free(path);
        if (rt_obj_release_check0(ws))
            rt_obj_free(ws);
        rt_trap_net("WebSocket: connection failed", Err_NetworkError);
        return NULL;
    }

    // Set socket-level recv/send timeout for handshake phase
    if (timeout_ms > 0) {
        ws_set_socket_timeout(ws->socket_fd, (int)timeout_ms, 1);
        ws_set_socket_timeout(ws->socket_fd, (int)timeout_ms, 0);
    }

    // TLS handshake if secure
    if (is_secure) {
        rt_tls_config_t config;
        rt_tls_config_init(&config);
        config.hostname = host;
        config.alpn_protocol = "http/1.1";

        ws->tls = rt_tls_new(ws->socket_fd, &config);
        if (!ws->tls) {
            const char *detail = rt_tls_last_error();
            char msg[512];
            free(host);
            free(path);
            if (rt_obj_release_check0(ws))
                rt_obj_free(ws);
            if (detail && *detail) {
                snprintf(msg, sizeof(msg), "WebSocket: TLS setup failed: %s", detail);
                rt_trap_net(msg, Err_TlsError);
            }
            rt_trap_net("WebSocket: TLS setup failed", Err_TlsError);
            return NULL;
        }

        if (rt_tls_handshake(ws->tls) != RT_TLS_OK) {
            const char *detail = rt_tls_get_error(ws->tls);
            char msg[512];
            free(host);
            free(path);
            if (rt_obj_release_check0(ws))
                rt_obj_free(ws);
            if (detail && *detail) {
                snprintf(msg, sizeof(msg), "WebSocket: TLS handshake failed: %s", detail);
                rt_trap_net(msg, Err_TlsError);
            }
            rt_trap_net("WebSocket: TLS handshake failed", Err_TlsError);
            return NULL;
        }
    }

    // WebSocket handshake
    if (!ws_handshake(ws, host, port, path)) {
        free(host);
        free(path);
        if (rt_obj_release_check0(ws))
            rt_obj_free(ws);
        rt_trap_net("WebSocket: handshake failed", Err_ProtocolError);
        return NULL;
    }

    free(host);
    free(path);

    // Clear socket timeouts now that handshake is complete
    if (timeout_ms > 0) {
        ws_clear_socket_timeout(ws->socket_fd, 1);
        ws_clear_socket_timeout(ws->socket_fd, 0);
    }

    ws->is_open = 1;
    return ws;
}

/// @brief The URL the connection was opened with. Empty string for NULL/closed-with-no-url.
rt_string rt_ws_url(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;
    if (!ws->url)
        return rt_str_empty();
    return rt_string_from_bytes(ws->url, strlen(ws->url));
}

/// @brief True (1) if the connection is still established, false (0) once closed/error.
int8_t rt_ws_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_ws_impl *ws = obj;
    return ws->is_open;
}

/// @brief WebSocket close code (1000-4999 per RFC 6455 §7.4). 0 if still open.
int64_t rt_ws_close_code(void *obj) {
    if (!obj)
        return 0;
    rt_ws_impl *ws = obj;
    return ws->close_code;
}

/// @brief Optional close reason text supplied by the peer. Empty string when none.
rt_string rt_ws_close_reason(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;
    if (!ws->close_reason)
        return rt_str_empty();
    return rt_string_from_bytes(ws->close_reason, strlen(ws->close_reason));
}

/// @brief Send a text frame. Sends the string's bytes as UTF-8 (no validation).
/// @throws Err_ConnectionClosed if `is_open == 0`,
///         Err_NetworkError if the underlying send fails.
void rt_ws_send(void *obj, rt_string text) {
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open) {
        rt_trap_net("WebSocket: connection is closed", Err_ConnectionClosed);
        return;
    }

    const char *cstr = rt_string_cstr(text);
    size_t len = cstr ? strlen(cstr) : 0;

    if (!ws_send_frame(ws, WS_OP_TEXT, cstr, len)) {
        ws->is_open = 0;
        rt_trap_net("WebSocket: send failed", Err_NetworkError);
    }
}

/// @brief Send a binary frame containing the bytes of `data`.
/// @throws Err_ConnectionClosed if closed, Err_NetworkError on send failure.
void rt_ws_send_bytes(void *obj, void *data) {
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open) {
        rt_trap_net("WebSocket: connection is closed", Err_ConnectionClosed);
        return;
    }

    int64_t len = rt_bytes_len(data);
    uint8_t *buffer = malloc(len);
    if (!buffer && len > 0) {
        rt_trap("WebSocket: memory allocation failed");
        return;
    }

    for (int64_t i = 0; i < len; i++)
        buffer[i] = (uint8_t)rt_bytes_get(data, i);

    if (!ws_send_frame(ws, WS_OP_BINARY, buffer, len)) {
        free(buffer);
        ws->is_open = 0;
        rt_trap_net("WebSocket: send failed", Err_NetworkError);
        return;
    }

    free(buffer);
}

/// @brief Send an empty PING control frame to keep the connection alive.
///
/// Silently no-ops on a closed connection. The peer's PONG (if any)
/// is consumed transparently inside `ws_recv_message`.
void rt_ws_ping(void *obj) {
    if (!obj)
        return;
    rt_ws_impl *ws = obj;
    if (!ws->is_open)
        return;

    ws_send_frame(ws, WS_OP_PING, NULL, 0);
}

/// @brief Block until a complete message arrives and return it as a string.
/// Returns the empty string on close/error. Both text and binary
/// payloads are decoded as UTF-8 (binary may produce mojibake).
rt_string rt_ws_recv(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_ws_impl *ws = obj;
    uint8_t *message = NULL;
    size_t message_len = 0;
    uint8_t opcode = 0;
    if (!ws_recv_message(ws, &message, &message_len, &opcode))
        return rt_str_empty();
    rt_string result = rt_string_from_bytes((const char *)message, message_len);
    free(message);
    (void)opcode;
    return result;
}

/// @brief Receive a message with an upper bound on wait time (returns NULL on timeout).
///
/// Probes the TLS read buffer first because `select()` only sees
/// the raw socket — already-decrypted bytes wouldn't show up.
/// Then waits on `select` for `timeout_ms` and pumps a recv if data arrives.
/// @return The string message, or NULL if the connection isn't
///         open, the timeout fires, or the recv fails.
rt_string rt_ws_recv_for(void *obj, int64_t timeout_ms) {
    if (!obj)
        return NULL;
    rt_ws_impl *ws = (rt_ws_impl *)obj;
    if (!ws->is_open)
        return NULL;

    // Wait for data to arrive with timeout.
    // Check TLS buffer first — select() on the raw socket won't see data that
    // has already been decrypted and buffered by the TLS layer.
    if (timeout_ms > 0) {
        if (ws->tls && rt_tls_has_buffered_data(ws->tls)) {
            // Data already available in TLS buffer — skip select()
        } else {
            int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
            if (ready <= 0)
                return NULL; // Timeout or error
        }
    }

    return rt_ws_recv(obj);
}

/// @brief Block for a complete message and return its raw bytes.
/// Returns an empty Bytes object on close/error.
void *rt_ws_recv_bytes(void *obj) {
    if (!obj)
        return rt_bytes_new(0);
    rt_ws_impl *ws = obj;
    uint8_t *message = NULL;
    size_t message_len = 0;
    uint8_t opcode = 0;
    if (!ws_recv_message(ws, &message, &message_len, &opcode))
        return rt_bytes_new(0);
    void *result = rt_bytes_new((int64_t)message_len);
    for (size_t i = 0; i < message_len; i++)
        rt_bytes_set(result, (int64_t)i, message[i]);
    free(message);
    (void)opcode;
    return result;
}

/// @brief Receive a binary message with a timeout (NULL on timeout).
/// @see rt_ws_recv_for
void *rt_ws_recv_bytes_for(void *obj, int64_t timeout_ms) {
    if (!obj)
        return NULL;
    rt_ws_impl *ws = (rt_ws_impl *)obj;
    if (!ws->is_open)
        return NULL;

    // Wait for data to arrive with timeout.
    // Check TLS buffer first — select() on the raw socket won't see data that
    // has already been decrypted and buffered by the TLS layer.
    if (timeout_ms > 0) {
        if (ws->tls && rt_tls_has_buffered_data(ws->tls)) {
            // Data already available in TLS buffer — skip select()
        } else {
            int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
            if (ready <= 0)
                return NULL; // Timeout or error
        }
    }

    return rt_ws_recv_bytes(obj);
}

/// @brief Send a normal close (code 1000) and mark the connection closed.
/// @see rt_ws_close_with
void rt_ws_close(void *obj) {
    rt_ws_close_with(obj, WS_CLOSE_NORMAL, rt_str_empty());
}

/// @brief Send a close frame with a specific code and optional reason text.
///
/// Builds a close payload of `[code:u16-be][reason]`, hands it to
/// `ws_send_frame`, and clears `is_open`. Silently no-ops on NULL
/// or already-closed connections. The TCP/TLS layer is left to be
/// torn down by the GC finalizer (`rt_ws_finalize`).
void rt_ws_close_with(void *obj, int64_t code, rt_string reason) {
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
    if (payload) {
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
