//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTWebSocketTests.cpp
// Purpose: Validate WebSocket timeout support.
// Key invariants:
//   - recv_for returns NULL on timeout and connect_for respects timeout.
//   - HTTP header matching is ASCII case-insensitive and platform-neutral.
// Ownership/Lifetime:
//   - Local test servers own and close their runtime connections.
//   - Runtime WebSocket objects remain valid for each test scope.
// Links: src/runtime/network/rt_websocket.c
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_netutils.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_websocket.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static bool ascii_header_name_equals(const char *line, const char *name, size_t name_len) {
    if (!line || !name)
        return false;
    for (size_t i = 0; i < name_len; i++) {
        char a = line[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + ('a' - 'A'));
        if (a == '\0')
            return false;
        if (a != b)
            return false;
    }
    return true;
}

/// @brief Extract a header value from raw HTTP headers.
/// Writes at most max_len-1 characters to out and NUL-terminates. Returns true on success.
static bool extract_http_header(const char *headers, const char *name, char *out, size_t max_len) {
    const size_t name_len = name ? strlen(name) : 0;
    if (!headers || !name || name_len == 0 || !out || max_len == 0)
        return false;

    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2; // skip CRLF
        if (ascii_header_name_equals(p, name, name_len)) {
            const char *val = p + name_len;
            while (*val == ' ' || *val == '\t')
                val++;
            const char *end = val;
            while (*end && *end != '\r' && *end != '\n')
                end++;
            size_t len = (size_t)(end - val);
            if (len == 0 || len >= max_len)
                return false;
            memcpy(out, val, len);
            out[len] = '\0';
            return true;
        }
    }
    return false;
}

/// @brief Extract the value of "Sec-WebSocket-Key:" from raw HTTP headers buffer.
static bool extract_ws_key(const char *headers, char *out, size_t max_len) {
    return extract_http_header(headers, "Sec-WebSocket-Key:", out, max_len);
}

/// @brief Build and send a valid WebSocket 101 response using the client's key.
static void ws_send_handshake(void *client, const char *headers_buf) {
    char ws_key[128] = {0};
    char *accept = NULL;

    if (extract_ws_key(headers_buf, ws_key, sizeof(ws_key)))
        accept = rt_ws_compute_accept_key(ws_key);

    char response[512];
    if (accept) {
        snprintf(response,
                 sizeof(response),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n"
                 "\r\n",
                 accept);
        free(accept);
    } else {
        // Fallback (shouldn't happen in tests)
        snprintf(response,
                 sizeof(response),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "\r\n");
    }

    rt_string resp_str = rt_const_cstr(response);
    rt_tcp_send_str(client, resp_str);
}

static void ws_send_server_frame(void *client,
                                 uint8_t first_byte,
                                 const uint8_t *payload,
                                 size_t len) {
    assert(len < 126 && "test helper only supports small payloads");
    uint8_t header[2] = {first_byte, (uint8_t)len};
    rt_tcp_send_all_raw(client, header, 2);
    if (len > 0)
        rt_tcp_send_all_raw(client, payload, (int64_t)len);
}

//=============================================================================
// Minimal WebSocket server for testing
//=============================================================================

static std::atomic<bool> ws_server_ready{false};
static std::atomic<bool> ws_server_failed{false};
static std::string ws_last_host_header;
static std::string ws_last_subprotocol_header;

/// @brief Accept a TCP connection and perform a minimal WS handshake,
///        then sit idle (never send data) so recv_for can time out.
static void ws_silent_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;

    // Accept one client
    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    // Read the HTTP upgrade request (consume all of it)
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        // Check for end of headers
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    // Send a valid WebSocket handshake response with computed Sec-WebSocket-Accept
    buf[total] = '\0';
    ws_send_handshake(client, buf);

    // Now just wait - don't send any WebSocket frames
    // This allows the recv_for timeout test to work
    std::this_thread::sleep_for(std::chrono::seconds(3));

    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

/// @brief Accept a TCP connection and perform a minimal WS handshake,
///        then echo one message back.
static void ws_echo_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    // Read the HTTP upgrade request
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    // Send valid handshake response with computed Sec-WebSocket-Accept
    buf[total] = '\0';
    ws_send_handshake(client, buf);

    // Read one WebSocket frame from client (text message)
    // Frame format: [FIN+opcode] [MASK+len] [4-byte mask] [masked payload]
    void *hdr_bytes = rt_tcp_recv(client, 2);
    if (hdr_bytes && rt_bytes_len(hdr_bytes) == 2) {
        // We have the header - read mask key and payload
        uint8_t len_byte = rt_bytes_get(hdr_bytes, 1) & 0x7F;
        void *mask_bytes = rt_tcp_recv(client, 4);
        void *payload = rt_tcp_recv(client, len_byte);

        if (mask_bytes && payload) {
            // Unmask payload
            uint8_t mask[4];
            for (int i = 0; i < 4; i++)
                mask[i] = (uint8_t)rt_bytes_get(mask_bytes, i);

            // Build unmasked text
            char text[256];
            int64_t plen = rt_bytes_len(payload);
            if (plen > 255)
                plen = 255;
            for (int64_t i = 0; i < plen; i++)
                text[i] = (char)(rt_bytes_get(payload, i) ^ mask[i % 4]);
            text[plen] = '\0';

            // Send back as unmasked text frame
            uint8_t frame[256 + 2];
            frame[0] = 0x81; // FIN + TEXT
            frame[1] = (uint8_t)plen;
            memcpy(frame + 2, text, plen);

            void *frame_bytes = rt_bytes_new((int64_t)(2 + plen));
            for (int64_t i = 0; i < 2 + plen; i++)
                rt_bytes_set(frame_bytes, i, frame[i]);
            rt_tcp_send(client, frame_bytes);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

/// @brief Accept one client, capture the Host header from the handshake, then complete the upgrade.
static void ws_capture_handshake_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;
    ws_last_host_header.clear();
    ws_last_subprotocol_header.clear();

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    buf[total] = '\0';
    char host[256] = {0};
    char subprotocol[256] = {0};
    if (extract_http_header(buf, "Host:", host, sizeof(host)))
        ws_last_host_header = host;
    if (extract_http_header(buf, "Sec-WebSocket-Protocol:", subprotocol, sizeof(subprotocol)))
        ws_last_subprotocol_header = subprotocol;

    char ws_key[128] = {0};
    char *accept = NULL;
    if (extract_ws_key(buf, ws_key, sizeof(ws_key)))
        accept = rt_ws_compute_accept_key(ws_key);

    char response[768];
    int len = snprintf(response,
                       sizeof(response),
                       "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: %s\r\n",
                       accept ? accept : "");
    if (ws_last_subprotocol_header.size() > 0) {
        len += snprintf(response + len,
                        sizeof(response) - (size_t)len,
                        "Sec-WebSocket-Protocol: %s\r\n",
                        ws_last_subprotocol_header.c_str());
    }
    snprintf(response + len, sizeof(response) - (size_t)len, "\r\n");
    if (accept)
        free(accept);

    rt_tcp_send_str(client, rt_const_cstr(response));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void ws_fragment_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    buf[total] = '\0';
    ws_send_handshake(client, buf);

    static const uint8_t part1[] = {'h', 'e', 'l'};
    static const uint8_t part2[] = {'l', 'o'};
    ws_send_server_frame(client, 0x01, part1, sizeof(part1)); // text, FIN=0
    ws_send_server_frame(client, 0x80, part2, sizeof(part2)); // continuation, FIN=1

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void ws_invalid_utf8_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    buf[total] = '\0';
    ws_send_handshake(client, buf);

    static const uint8_t invalid_text[] = {0xC3, 0x28};
    ws_send_server_frame(client, 0x81, invalid_text, sizeof(invalid_text));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

//=============================================================================
// Tests — Sec-WebSocket-Accept key computation (CS-5)
//=============================================================================

/// @brief Test RFC 6455 §1.3 known-answer vector for Sec-WebSocket-Accept.
///
/// The RFC specifies an exact example:
///   Client key : "dGhlIHNhbXBsZSBub25jZQ=="
///   Expected   : "s3pPLMBiTxaQ9kYGzzhZRbK+xoo="
/// This pins the SHA-1 + base64 implementation against the standard.
static void test_ws_accept_key_rfc_example() {
    printf("\nTesting WebSocket accept key (RFC 6455 §1.3 vector):\n");

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    // NOTE: RFC 6455 §1.3 contains a known typo — it prints "xoo=" but the
    // mathematically correct base64 of SHA-1 bytes 0xc4, 0xea is "xOo=" (capital O).
    // Decoding "xoo=" gives 0xc6, 0x8a which contradicts the RFC's own stated
    // SHA-1 hex value. The correct expected value is "...xOo=" as implemented here.
    const char *expected_accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

    char *accept = rt_ws_compute_accept_key(client_key);

    test_result("accept key is not NULL", accept != NULL);
    if (accept) {
        test_result("RFC 6455 §1.3 accept key matches", strcmp(accept, expected_accept) == 0);
        free(accept);
    }
}

/// @brief Test that rt_ws_compute_accept_key is NULL-safe.
static void test_ws_accept_key_null_safe() {
    printf("\nTesting WebSocket accept key NULL safety:\n");

    char *result = rt_ws_compute_accept_key(NULL);
    test_result("compute_accept_key(NULL) returns NULL", result == NULL);
}

//=============================================================================
// Tests — timeout
//=============================================================================

/// @brief Test that recv_for returns NULL when timeout expires.
static void test_ws_recv_for_timeout() {
    printf("\nTesting WebSocket recv_for timeout:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_silent_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect to our silent WS server
    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    rt_string url = rt_const_cstr(url_buf);
    void *ws = rt_ws_connect(url);

    test_result("WebSocket connect succeeds", ws != nullptr);
    test_result("WebSocket is open", rt_ws_is_open(ws) == 1);

    // Try to receive with 100ms timeout - server sends nothing
    auto start = std::chrono::steady_clock::now();
    rt_string msg = rt_ws_recv_for(ws, 150);
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result("recv_for returns NULL on timeout", msg == NULL);
    test_result("recv_for timeout is respected (>=100ms)", elapsed >= 100);
    test_result("recv_for timeout is reasonable (<1000ms)", elapsed < 1000);

    rt_ws_close(ws);
    server.join();
}

/// @brief Test that recv_bytes_for returns NULL when timeout expires.
static void test_ws_recv_bytes_for_timeout() {
    printf("\nTesting WebSocket recv_bytes_for timeout:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_silent_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    rt_string url = rt_const_cstr(url_buf);
    void *ws = rt_ws_connect(url);

    test_result("WebSocket connect succeeds", ws != nullptr);

    // Try to receive bytes with 100ms timeout - server sends nothing
    auto start = std::chrono::steady_clock::now();
    void *data = rt_ws_recv_bytes_for(ws, 150);
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result("recv_bytes_for returns NULL on timeout", data == nullptr);
    test_result("recv_bytes_for timeout is respected (>=100ms)", elapsed >= 100);
    test_result("recv_bytes_for timeout is reasonable (<1000ms)", elapsed < 1000);

    rt_ws_close(ws);
    server.join();
}

/// @brief Test that connect_for works for successful fast connections.
static void test_ws_connect_for_success() {
    printf("\nTesting WebSocket connect_for (success case):\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_echo_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect with a generous timeout (should succeed quickly)
    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    rt_string url = rt_const_cstr(url_buf);

    auto start = std::chrono::steady_clock::now();
    void *ws = rt_ws_connect_for(url, 5000);
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result("connect_for succeeds to localhost", ws != nullptr);
    test_result("connect_for is fast to localhost (<2000ms)", elapsed < 2000);
    test_result("WebSocket is open after connect_for", rt_ws_is_open(ws) == 1);

    // Send a message and receive echo
    rt_ws_send(ws, rt_const_cstr("hello"));
    rt_string reply = rt_ws_recv_for(ws, 2000);
    if (reply) {
        const char *r = rt_string_cstr(reply);
        test_result("Echo reply is 'hello'", r != nullptr && strcmp(r, "hello") == 0);
    } else {
        test_result("Echo reply received", false);
    }

    rt_ws_close(ws);
    server.join();
}

static void test_ws_text_frames_preserve_embedded_nul() {
    printf("\nTesting WebSocket embedded NUL text frames:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_echo_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    void *ws = rt_ws_connect(rt_const_cstr(url_buf));
    test_result("WebSocket connect succeeds", ws != nullptr);

    const char payload[] = {'h', 'e', '\0', 'l', 'o'};
    rt_string msg = rt_string_from_bytes(payload, sizeof(payload));
    rt_ws_send(ws, msg);
    rt_string reply = rt_ws_recv_for(ws, 2000);
    test_result("Embedded NUL echo reply received", reply != nullptr);
    if (reply) {
        test_result("Embedded NUL echo length preserved",
                    rt_str_len(reply) == (int64_t)sizeof(payload));
        test_result("Embedded NUL echo bytes preserved",
                    memcmp(rt_string_cstr(reply), payload, sizeof(payload)) == 0);
    }

    rt_string_unref(msg);
    rt_ws_close(ws);
    server.join();
}

static void test_ws_connect_sends_canonical_host_header() {
    printf("\nTesting WebSocket Host header formatting:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;
    ws_last_host_header.clear();

    std::thread server(ws_capture_handshake_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/chat", port);
    void *ws = rt_ws_connect(rt_const_cstr(url_buf));
    test_result("WebSocket connect succeeds", ws != nullptr);
    if (ws)
        rt_ws_close(ws);

    server.join();
    test_result("WebSocket Host header includes explicit non-default port",
                ws_last_host_header == ("127.0.0.1:" + std::to_string(port)));
}

static void test_ws_connect_protocol_negotiates_subprotocol() {
    printf("\nTesting WebSocket subprotocol negotiation:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;
    ws_last_subprotocol_header.clear();

    std::thread server(ws_capture_handshake_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/chat", port);
    void *ws = rt_ws_connect_protocol(rt_const_cstr(url_buf), rt_const_cstr("chat.v1"));
    test_result("WebSocket connect with protocol succeeds", ws != nullptr);
    if (ws) {
        rt_string subprotocol = rt_ws_subprotocol(ws);
        test_result("WebSocket stores negotiated subprotocol",
                    strcmp(rt_string_cstr(subprotocol), "chat.v1") == 0);
        rt_string_unref(subprotocol);
        rt_ws_close(ws);
    }

    server.join();
    test_result("Client sent requested subprotocol header",
                ws_last_subprotocol_header == "chat.v1");
}

static void test_ws_fragmented_text_reassembly() {
    printf("\nTesting WebSocket fragmented text reassembly:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_fragment_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    void *ws = rt_ws_connect(rt_const_cstr(url_buf));
    test_result("WebSocket connect succeeds", ws != nullptr);

    rt_string msg = rt_ws_recv_for(ws, 2000);
    test_result("Fragmented message received", msg != nullptr);
    if (msg) {
        test_result("Fragmented message reassembled to 'hello'",
                    strcmp(rt_string_cstr(msg), "hello") == 0);
    }

    rt_ws_close(ws);
    server.join();
}

static void test_ws_invalid_utf8_closes_with_1007() {
    printf("\nTesting WebSocket invalid UTF-8 close handling:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;

    std::thread server(ws_invalid_utf8_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    void *ws = rt_ws_connect(rt_const_cstr(url_buf));
    test_result("WebSocket connect succeeds", ws != nullptr);

    rt_string msg = rt_ws_recv_for(ws, 2000);
    test_result("Invalid UTF-8 closes timed recv", msg == nullptr);
    test_result("Close code is 1007", rt_ws_close_code(ws) == 1007);
    test_result("Connection is closed", rt_ws_is_open(ws) == 0);

    server.join();
}

//=============================================================================
// Close handshake (VDOC-137)
//=============================================================================

static std::atomic<bool> ws_close_server_saw_close_frame{false};
static std::atomic<bool> ws_close_server_saw_eof{false};

/// @brief Accept one client, complete the upgrade, then participate in the
///        RFC 6455 closing handshake: read the client's close frame, echo a
///        close reply, and record whether the client's transport reaches EOF.
static void ws_close_handshake_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_failed = true;
        ws_server_ready = true;
        return;
    }

    ws_server_failed = false;
    ws_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    // Read the HTTP upgrade request and complete the handshake.
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c) {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }
    buf[total] = '\0';
    ws_send_handshake(client, buf);

    rt_tcp_set_recv_timeout(client, 5000);

    // Read the client's masked close frame: [0x88][0x82][mask:4][code:2].
    void *hdr = rt_tcp_recv(client, 2);
    if (hdr && rt_bytes_len(hdr) == 2 && (rt_bytes_get(hdr, 0) & 0x0F) == 0x08) {
        ws_close_server_saw_close_frame = true;
        int payload_len = (int)(rt_bytes_get(hdr, 1) & 0x7F);
        // TCP is a byte stream: a single recv is allowed to return only part
        // of the mask/payload. Consume the complete client frame before
        // waiting for EOF, otherwise a short read can be mistaken for a
        // transport that remained open under parallel ctest load.
        void *frame_body = rt_tcp_recv_exact(client, 4 + payload_len);
        if (frame_body && rt_obj_release_check0(frame_body))
            rt_obj_free(frame_body);

        // Echo the close reply (unmasked, code 1000).
        const uint8_t code[2] = {0x03, 0xE8};
        ws_send_server_frame(client, 0x88, code, 2);

        // The client must now close its transport deterministically: the
        // next read observes EOF (empty bytes) rather than blocking until
        // GC finalization.
        void *tail = rt_tcp_recv(client, 16);
        if (tail && rt_bytes_len(tail) == 0)
            ws_close_server_saw_eof = true;
        if (tail && rt_obj_release_check0(tail))
            rt_obj_free(tail);
    }

    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

/// @brief Close must complete the closing handshake and release the transport
///        promptly instead of deferring socket/TLS teardown to the finalizer.
static void test_ws_close_completes_handshake_and_releases_transport() {
    printf("\nTesting WebSocket close handshake + deterministic transport release:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }
    ws_server_ready = false;
    ws_server_failed = false;
    ws_close_server_saw_close_frame = false;
    ws_close_server_saw_eof = false;

    std::thread server(ws_close_handshake_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ws_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[64];
    snprintf(url_buf, sizeof(url_buf), "ws://127.0.0.1:%d/", port);
    void *ws = rt_ws_connect_for(rt_const_cstr(url_buf), 5000);
    test_result("WebSocket connect succeeds", ws != nullptr);

    auto start = std::chrono::steady_clock::now();
    rt_ws_close(ws);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    test_result("Close marks connection closed", rt_ws_is_open(ws) == 0);
    test_result("Close code is 1000", rt_ws_close_code(ws) == 1000);
    test_result("Close returns within its bounded wait (<3000ms)", elapsed < 3000);

    server.join();
    test_result("Server received the client close frame", ws_close_server_saw_close_frame);
    test_result("Server observed prompt transport EOF", ws_close_server_saw_eof);
}

/// @brief Test recv_for and recv_bytes_for with NULL object.
static void test_ws_null_object() {
    printf("\nTesting WebSocket timeout functions with NULL:\n");

    rt_string msg = rt_ws_recv_for(nullptr, 100);
    test_result("recv_for(NULL) returns NULL", msg == nullptr);

    void *data = rt_ws_recv_bytes_for(nullptr, 100);
    test_result("recv_bytes_for(NULL) returns NULL", data == nullptr);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== WebSocket Tests ===\n");

    // CS-5: Sec-WebSocket-Accept key computation
    test_ws_accept_key_rfc_example();
    test_ws_accept_key_null_safe();

    // Timeout tests
    test_ws_null_object();
    test_ws_recv_for_timeout();
    test_ws_recv_bytes_for_timeout();
    test_ws_connect_for_success();
    test_ws_text_frames_preserve_embedded_nul();
    test_ws_connect_sends_canonical_host_header();
    test_ws_connect_protocol_negotiates_subprotocol();
    test_ws_fragmented_text_reassembly();
    test_ws_invalid_utf8_closes_with_1007();
    test_ws_close_completes_handshake_and_releases_transport();

    printf("\nAll WebSocket tests passed.\n");
    return 0;
}
