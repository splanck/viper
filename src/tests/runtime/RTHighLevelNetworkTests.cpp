//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTHighLevelNetworkTests.cpp
// Purpose: Integration-style coverage for higher-level network APIs.
//
//===----------------------------------------------------------------------===//

#include "rt_netutils.h"
#include "rt_network.h"
#include "rt_smtp.h"
#include "rt_sse.h"
#include "rt_string.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static std::atomic<bool> api_server_ready{false};
static std::atomic<bool> api_server_failed{false};
static std::string smtp_captured_message;

static void send_text(void *client, const char *text) {
    rt_tcp_send_str(client, rt_const_cstr(text));
}

static void read_http_request_headers(void *client) {
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        rt_string_unref(line);
        if (done)
            break;
    }
}

static void sse_plain_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    read_http_request_headers(client);
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Connection: close\r\n"
              "\r\n"
              "event: greet\r\n"
              "id: 42\r\n"
              "data: hello\r\n"
              "\r\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void sse_chunked_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    read_http_request_headers(client);
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Transfer-Encoding: chunked\r\n"
              "Connection: close\r\n"
              "\r\n");
    // HTTP chunked encoding: each chunk is <hex-size>\r\n<data>\r\n
    // Chunk 1: 0x17 = 23 bytes of SSE event metadata + blank-line delimiter
    // Chunk 2: 0x18 = 24 bytes of SSE data fields + blank-line delimiter
    // Chunk 0: terminates the stream
    send_text(client, "17\r\nevent: multi\r\nid: 7\r\n\r\n\r\n");
    send_text(client, "18\r\ndata: one\r\ndata: two\r\n\r\n\r\n");
    send_text(client, "0\r\n\r\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void smtp_plain_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    smtp_captured_message.clear();
    send_text(client, "220 localhost ESMTP ready\r\n");

    rt_string line = rt_tcp_recv_line(client);
    const char *cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "EHLO ", 5) == 0);
    rt_string_unref(line);
    send_text(client, "250-localhost\r\n250 SIZE 1024\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "MAIL FROM:", 10) == 0);
    rt_string_unref(line);
    send_text(client, "250 OK\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "RCPT TO:", 8) == 0);
    rt_string_unref(line);
    send_text(client, "250 OK\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strcmp(cstr, "DATA") == 0);
    rt_string_unref(line);
    send_text(client, "354 End data with <CR><LF>.<CR><LF>\r\n");

    while (true) {
        line = rt_tcp_recv_line(client);
        cstr = rt_string_cstr(line);
        if (cstr && strcmp(cstr, ".") == 0) {
            rt_string_unref(line);
            break;
        }
        smtp_captured_message += cstr ? cstr : "";
        smtp_captured_message += '\n';
        rt_string_unref(line);
    }

    send_text(client, "250 Queued\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strcmp(cstr, "QUIT") == 0);
    rt_string_unref(line);
    send_text(client, "221 Bye\r\n");

    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void wait_for_server() {
    while (!api_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

static void test_sse_plain_event() {
    printf("\nTesting SseClient plain event stream:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_plain_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/events", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data = rt_sse_recv(sse);
    test_result("SSE plain event payload matches", strcmp(rt_string_cstr(data), "hello") == 0);

    rt_string event_type = rt_sse_last_event_type(sse);
    rt_string event_id = rt_sse_last_event_id(sse);
    test_result("SSE plain event type captured", strcmp(rt_string_cstr(event_type), "greet") == 0);
    test_result("SSE plain event id captured", strcmp(rt_string_cstr(event_id), "42") == 0);

    rt_sse_close(sse);
    server.join();
}

static void test_sse_chunked_event() {
    printf("\nTesting SseClient chunked event stream:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_chunked_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/chunked", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE chunked connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data = rt_sse_recv_for(sse, 2000);
    test_result("SSE chunked payload matches", strcmp(rt_string_cstr(data), "one\ntwo") == 0);

    rt_string event_type = rt_sse_last_event_type(sse);
    rt_string event_id = rt_sse_last_event_id(sse);
    test_result("SSE chunked event type captured",
                strcmp(rt_string_cstr(event_type), "multi") == 0);
    test_result("SSE chunked event id captured", strcmp(rt_string_cstr(event_id), "7") == 0);

    rt_sse_close(sse);
    server.join();
}

static void test_smtp_plain_send_sanitizes_and_dot_stuffs() {
    printf("\nTesting SmtpClient plain send path:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(smtp_plain_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *smtp = rt_smtp_new(rt_const_cstr("127.0.0.1"), port);
    int8_t ok = rt_smtp_send(smtp,
                             rt_const_cstr("sender@example.com"),
                             rt_const_cstr("dest@example.com"),
                             rt_const_cstr("Hello\r\nInjected: yes"),
                             rt_const_cstr(".leading line\nsecond line"));

    test_result("SMTP send succeeds", ok == 1);
    test_result("SMTP last error is empty",
                strcmp(rt_string_cstr(rt_smtp_last_error(smtp)), "") == 0);

    server.join();

    test_result("SMTP captured From header",
                smtp_captured_message.find("From: sender@example.com") != std::string::npos);
    test_result("SMTP captured To header",
                smtp_captured_message.find("To: dest@example.com") != std::string::npos);
    test_result("SMTP subject was sanitized",
                smtp_captured_message.find("Subject: Hello  Injected: yes") != std::string::npos);
    test_result("SMTP content type header present",
                smtp_captured_message.find("Content-Type: text/plain; charset=utf-8") !=
                    std::string::npos);
    test_result("SMTP body was dot-stuffed",
                smtp_captured_message.find("..leading line") != std::string::npos);
    test_result("SMTP body preserved following line",
                smtp_captured_message.find("second line") != std::string::npos);
}

int main() {
    test_sse_plain_event();
    test_sse_chunked_event();
    test_smtp_plain_send_sanitizes_and_dot_stuffs();

    printf("\nAll high-level network tests passed.\n");
    return 0;
}
