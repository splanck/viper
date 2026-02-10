//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTWebSocketTests.cpp
// Purpose: Validate WebSocket timeout support.
// Key invariants: recv_for returns NULL on timeout, connect_for respects timeout.
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_network.h"
#include "rt_string.h"
#include "rt_websocket.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Minimal WebSocket server for testing
//=============================================================================

static std::atomic<bool> ws_server_ready{false};

/// @brief Accept a TCP connection and perform a minimal WS handshake,
///        then sit idle (never send data) so recv_for can time out.
static void ws_silent_server_thread(int port)
{
    void *server = rt_tcp_server_listen(port);
    if (!server)
    {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_ready = true;
        return;
    }

    ws_server_ready = true;

    // Accept one client
    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client)
    {
        rt_tcp_server_close(server);
        return;
    }

    // Read the HTTP upgrade request (consume all of it)
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1)
    {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c)
        {
            buf[total] = c[0];
            total++;
        }
        // Check for end of headers
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    // Send a minimal valid WebSocket handshake response
    // The Sec-WebSocket-Accept is not validated by our client in this test context,
    // but we need to send "101 Switching Protocols" with proper headers.
    const char *response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "\r\n";
    rt_string resp_str = rt_const_cstr(response);
    rt_tcp_send_str(client, resp_str);

    // Now just wait - don't send any WebSocket frames
    // This allows the recv_for timeout test to work
    std::this_thread::sleep_for(std::chrono::seconds(3));

    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

/// @brief Accept a TCP connection and perform a minimal WS handshake,
///        then echo one message back.
static void ws_echo_server_thread(int port)
{
    void *server = rt_tcp_server_listen(port);
    if (!server)
    {
        printf("  WARNING: Could not create server on port %d\n", port);
        ws_server_ready = true;
        return;
    }

    ws_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client)
    {
        rt_tcp_server_close(server);
        return;
    }

    // Read the HTTP upgrade request
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1)
    {
        rt_string line = rt_tcp_recv_str(client, 1);
        if (!line)
            break;
        const char *c = rt_string_cstr(line);
        if (c)
        {
            buf[total] = c[0];
            total++;
        }
        if (total >= 4 && buf[total - 4] == '\r' && buf[total - 3] == '\n' &&
            buf[total - 2] == '\r' && buf[total - 1] == '\n')
            break;
    }

    // Send handshake response
    const char *response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "\r\n";
    rt_string resp_str = rt_const_cstr(response);
    rt_tcp_send_str(client, resp_str);

    // Read one WebSocket frame from client (text message)
    // Frame format: [FIN+opcode] [MASK+len] [4-byte mask] [masked payload]
    void *hdr_bytes = rt_tcp_recv(client, 2);
    if (hdr_bytes && rt_bytes_len(hdr_bytes) == 2)
    {
        // We have the header - read mask key and payload
        uint8_t len_byte = rt_bytes_get(hdr_bytes, 1) & 0x7F;
        void *mask_bytes = rt_tcp_recv(client, 4);
        void *payload = rt_tcp_recv(client, len_byte);

        if (mask_bytes && payload)
        {
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

//=============================================================================
// Tests
//=============================================================================

/// @brief Test that recv_for returns NULL when timeout expires.
static void test_ws_recv_for_timeout()
{
    printf("\nTesting WebSocket recv_for timeout:\n");

    const int port = 19920;
    ws_server_ready = false;

    std::thread server(ws_silent_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
static void test_ws_recv_bytes_for_timeout()
{
    printf("\nTesting WebSocket recv_bytes_for timeout:\n");

    const int port = 19921;
    ws_server_ready = false;

    std::thread server(ws_silent_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
static void test_ws_connect_for_success()
{
    printf("\nTesting WebSocket connect_for (success case):\n");

    const int port = 19922;
    ws_server_ready = false;

    std::thread server(ws_echo_server_thread, port);

    while (!ws_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    if (reply)
    {
        const char *r = rt_string_cstr(reply);
        test_result("Echo reply is 'hello'", r != nullptr && strcmp(r, "hello") == 0);
    }
    else
    {
        test_result("Echo reply received", false);
    }

    rt_ws_close(ws);
    server.join();
}

/// @brief Test recv_for and recv_bytes_for with NULL object.
static void test_ws_null_object()
{
    printf("\nTesting WebSocket timeout functions with NULL:\n");

    rt_string msg = rt_ws_recv_for(nullptr, 100);
    test_result("recv_for(NULL) returns NULL", msg == nullptr);

    void *data = rt_ws_recv_bytes_for(nullptr, 100);
    test_result("recv_bytes_for(NULL) returns NULL", data == nullptr);
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("=== WebSocket Timeout Tests ===\n");

    test_ws_null_object();
    test_ws_recv_for_timeout();
    test_ws_recv_bytes_for_timeout();
    test_ws_connect_for_success();

    printf("\nAll WebSocket timeout tests passed.\n");
    return 0;
}
