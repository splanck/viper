//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libws/src/websocket.cpp
// Purpose: WebSocket client library (RFC 6455) for ViperDOS.
//
//===----------------------------------------------------------------------===//

#include "../include/websocket.h"

#include <stdlib.h>
#include <string.h>

// Syscall wrappers (ViperDOS socket layer)
extern "C" {
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
}

// Socket syscalls
#define SYS_SOCKET_CREATE 0x50
#define SYS_SOCKET_CONNECT 0x51
#define SYS_SOCKET_SEND 0x52
#define SYS_SOCKET_RECV 0x53
#define SYS_SOCKET_CLOSE 0x54
#define SYS_DNS_RESOLVE 0x55
#define SYS_GETRANDOM 0xE4

// Simple base64 encoding for the WebSocket key
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, size_t in_len, char *out) {
    size_t i = 0, j = 0;
    while (i < in_len) {
        uint32_t a = i < in_len ? in[i++] : 0;
        uint32_t b = i < in_len ? in[i++] : 0;
        uint32_t c = i < in_len ? in[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > in_len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > in_len) ? '=' : b64_table[triple & 0x3F];
    }
    out[j] = '\0';
}

// Parse ws:// or wss:// URL
static int parse_url(const char *url, ws_conn_t *conn) {
    if (strncmp(url, "wss://", 6) == 0) {
        conn->use_tls = 1;
        conn->port = 443;
        url += 6;
    } else if (strncmp(url, "ws://", 5) == 0) {
        conn->use_tls = 0;
        conn->port = 80;
        url += 5;
    } else {
        return WS_ERROR;
    }

    // Extract host
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');
    size_t host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = static_cast<size_t>(colon - url);
        conn->port = static_cast<uint16_t>(atoi(colon + 1));
    } else if (slash) {
        host_len = static_cast<size_t>(slash - url);
    } else {
        host_len = strlen(url);
    }

    if (host_len >= sizeof(conn->host))
        host_len = sizeof(conn->host) - 1;
    memcpy(conn->host, url, host_len);
    conn->host[host_len] = '\0';

    // Extract path
    if (slash) {
        size_t path_len = strlen(slash);
        if (path_len >= sizeof(conn->path))
            path_len = sizeof(conn->path) - 1;
        memcpy(conn->path, slash, path_len);
        conn->path[path_len] = '\0';
    } else {
        conn->path[0] = '/';
        conn->path[1] = '\0';
    }

    return WS_OK;
}

// Send raw data via socket
static int ws_raw_send(ws_conn_t *conn, const void *data, size_t len) {
    long result = __syscall3(SYS_SOCKET_SEND, conn->socket_fd,
                             reinterpret_cast<long>(data), static_cast<long>(len));
    return result < 0 ? WS_ERROR : WS_OK;
}

// Receive raw data from socket
static int ws_raw_recv(ws_conn_t *conn, void *buf, size_t max_len) {
    long result = __syscall3(SYS_SOCKET_RECV, conn->socket_fd,
                             reinterpret_cast<long>(buf), static_cast<long>(max_len));
    return static_cast<int>(result);
}

// Build and send a WebSocket frame
static int ws_send_frame(ws_conn_t *conn, uint8_t opcode, const void *data, size_t len) {
    if (conn->state != WS_STATE_OPEN && opcode != WS_OPCODE_CLOSE)
        return WS_ERROR_CLOSED;

    // Frame header
    uint8_t header[14];
    size_t header_len = 2;

    header[0] = 0x80 | (opcode & 0x0F); // FIN + opcode
    header[1] = 0x80;                     // Mask bit set (client must mask)

    if (len < 126) {
        header[1] |= static_cast<uint8_t>(len);
    } else if (len <= 65535) {
        header[1] |= 126;
        header[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[3] = static_cast<uint8_t>(len & 0xFF);
        header_len = 4;
    } else {
        header[1] |= 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = static_cast<uint8_t>((len >> (56 - i * 8)) & 0xFF);
        }
        header_len = 10;
    }

    // Generate masking key
    uint8_t mask[4];
    __syscall2(SYS_GETRANDOM, reinterpret_cast<long>(mask), 4);
    memcpy(header + header_len, mask, 4);
    header_len += 4;

    // Send header
    int ret = ws_raw_send(conn, header, header_len);
    if (ret != WS_OK)
        return ret;

    // Send masked payload
    if (len > 0 && data) {
        uint8_t masked[WS_MAX_FRAME_SIZE];
        size_t send_len = len > WS_MAX_FRAME_SIZE ? WS_MAX_FRAME_SIZE : len;
        const uint8_t *src = reinterpret_cast<const uint8_t *>(data);
        for (size_t i = 0; i < send_len; i++) {
            masked[i] = src[i] ^ mask[i & 3];
        }
        ret = ws_raw_send(conn, masked, send_len);
    }

    return ret;
}

// Public API

int ws_connect(const char *url, ws_conn_t *conn) {
    if (!url || !conn)
        return WS_ERROR;

    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = -1;
    conn->tls_session = -1;
    conn->state = WS_STATE_DISCONNECTED;

    // Parse URL
    if (parse_url(url, conn) != WS_OK)
        return WS_ERROR;

    conn->state = WS_STATE_CONNECTING;

    // Resolve hostname
    uint32_t ip_addr = 0;
    long dns_ret = __syscall2(SYS_DNS_RESOLVE, reinterpret_cast<long>(conn->host),
                              reinterpret_cast<long>(&ip_addr));
    if (dns_ret < 0) {
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_CONNECT;
    }

    // Create socket
    long sock = __syscall2(SYS_SOCKET_CREATE, 0, 0);
    if (sock < 0) {
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_CONNECT;
    }
    conn->socket_fd = static_cast<int>(sock);

    // Connect
    uint32_t connect_addr = (ip_addr & 0xFF) << 24 |
                            ((ip_addr >> 8) & 0xFF) << 16 |
                            ((ip_addr >> 16) & 0xFF) << 8 |
                            ((ip_addr >> 24) & 0xFF);
    long conn_ret = __syscall3(SYS_SOCKET_CONNECT, conn->socket_fd,
                               connect_addr, conn->port);
    if (conn_ret < 0) {
        __syscall2(SYS_SOCKET_CLOSE, conn->socket_fd, 0);
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_CONNECT;
    }

    // Generate WebSocket key
    uint8_t key_bytes[16];
    __syscall2(SYS_GETRANDOM, reinterpret_cast<long>(key_bytes), 16);
    char ws_key[25];
    base64_encode(key_bytes, 16, ws_key);

    // Build HTTP upgrade request
    char request[1024];
    int req_len = 0;
    auto append = [&](const char *s) {
        while (*s && req_len < 1023) {
            request[req_len++] = *s++;
        }
    };

    append("GET ");
    append(conn->path);
    append(" HTTP/1.1\r\nHost: ");
    append(conn->host);
    append("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
    append("Sec-WebSocket-Key: ");
    append(ws_key);
    append("\r\nSec-WebSocket-Version: 13\r\n\r\n");
    request[req_len] = '\0';

    // Send upgrade request
    if (ws_raw_send(conn, request, static_cast<size_t>(req_len)) != WS_OK) {
        __syscall2(SYS_SOCKET_CLOSE, conn->socket_fd, 0);
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_HANDSHAKE;
    }

    // Read response
    char response[2048];
    int resp_len = ws_raw_recv(conn, response, sizeof(response) - 1);
    if (resp_len <= 0) {
        __syscall2(SYS_SOCKET_CLOSE, conn->socket_fd, 0);
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_HANDSHAKE;
    }
    response[resp_len] = '\0';

    // Check for 101 Switching Protocols
    if (strstr(response, "101") == nullptr) {
        __syscall2(SYS_SOCKET_CLOSE, conn->socket_fd, 0);
        conn->state = WS_STATE_DISCONNECTED;
        return WS_ERROR_HANDSHAKE;
    }

    conn->state = WS_STATE_OPEN;
    return WS_OK;
}

int ws_send_text(ws_conn_t *conn, const char *text) {
    if (!conn || !text)
        return WS_ERROR;
    return ws_send_frame(conn, WS_OPCODE_TEXT, text, strlen(text));
}

int ws_send_binary(ws_conn_t *conn, const void *data, size_t len) {
    if (!conn || !data)
        return WS_ERROR;
    return ws_send_frame(conn, WS_OPCODE_BINARY, data, len);
}

int ws_send_ping(ws_conn_t *conn) {
    if (!conn)
        return WS_ERROR;
    return ws_send_frame(conn, WS_OPCODE_PING, nullptr, 0);
}

int ws_recv(ws_conn_t *conn, ws_frame_t *frame, int timeout_ms) {
    if (!conn || !frame)
        return WS_ERROR;
    if (conn->state != WS_STATE_OPEN)
        return WS_ERROR_CLOSED;

    (void)timeout_ms; // Timeout not yet supported

    // Read frame header
    uint8_t hdr[2];
    int n = ws_raw_recv(conn, hdr, 2);
    if (n < 2)
        return WS_ERROR_PROTOCOL;

    frame->fin = (hdr[0] >> 7) & 1;
    frame->opcode = hdr[0] & 0x0F;
    bool has_mask = (hdr[1] >> 7) & 1;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (ws_raw_recv(conn, ext, 2) < 2)
            return WS_ERROR_PROTOCOL;
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (ws_raw_recv(conn, ext, 8) < 8)
            return WS_ERROR_PROTOCOL;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    if (payload_len > WS_MAX_FRAME_SIZE)
        return WS_ERROR_PROTOCOL;

    // Read masking key (server shouldn't mask, but handle it)
    uint8_t mask[4] = {0, 0, 0, 0};
    if (has_mask) {
        if (ws_raw_recv(conn, mask, 4) < 4)
            return WS_ERROR_PROTOCOL;
    }

    // Read payload
    if (payload_len > 0) {
        frame->data = malloc(payload_len + 1);
        if (!frame->data)
            return WS_ERROR_MEMORY;

        size_t total = 0;
        while (total < payload_len) {
            int r = ws_raw_recv(conn, reinterpret_cast<uint8_t *>(frame->data) + total,
                                static_cast<size_t>(payload_len - total));
            if (r <= 0) {
                free(frame->data);
                frame->data = nullptr;
                return WS_ERROR_PROTOCOL;
            }
            total += static_cast<size_t>(r);
        }

        // Unmask if needed
        if (has_mask) {
            uint8_t *p = reinterpret_cast<uint8_t *>(frame->data);
            for (size_t i = 0; i < payload_len; i++) {
                p[i] ^= mask[i & 3];
            }
        }

        // NUL-terminate for text convenience
        reinterpret_cast<uint8_t *>(frame->data)[payload_len] = 0;
    } else {
        frame->data = nullptr;
    }

    frame->data_len = static_cast<size_t>(payload_len);

    // Handle control frames
    if (frame->opcode == WS_OPCODE_PING) {
        // Auto-respond with pong
        ws_send_frame(conn, WS_OPCODE_PONG, frame->data, frame->data_len);
    } else if (frame->opcode == WS_OPCODE_CLOSE) {
        conn->state = WS_STATE_CLOSED;
        // Echo close frame
        ws_send_frame(conn, WS_OPCODE_CLOSE, frame->data,
                      frame->data_len > 2 ? 2 : frame->data_len);
    }

    return WS_OK;
}

int ws_close(ws_conn_t *conn, uint16_t status_code) {
    if (!conn)
        return WS_ERROR;

    if (conn->state == WS_STATE_OPEN) {
        conn->state = WS_STATE_CLOSING;
        uint8_t close_data[2];
        close_data[0] = static_cast<uint8_t>((status_code >> 8) & 0xFF);
        close_data[1] = static_cast<uint8_t>(status_code & 0xFF);
        ws_send_frame(conn, WS_OPCODE_CLOSE, close_data, 2);
    }

    if (conn->socket_fd >= 0) {
        __syscall2(SYS_SOCKET_CLOSE, conn->socket_fd, 0);
        conn->socket_fd = -1;
    }

    conn->state = WS_STATE_CLOSED;
    return WS_OK;
}

void ws_frame_free(ws_frame_t *frame) {
    if (frame && frame->data) {
        free(frame->data);
        frame->data = nullptr;
        frame->data_len = 0;
    }
}

ws_state_t ws_get_state(const ws_conn_t *conn) {
    if (!conn)
        return WS_STATE_DISCONNECTED;
    return conn->state;
}
