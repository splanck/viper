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
#include "rt_bytes.h"
#include "rt_http_server.h"
#include "rt_http_client.h"
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
static std::atomic<int> http_keepalive_accept_count{0};
static std::atomic<bool> http_keepalive_reused{false};
static std::string smtp_captured_message;
static std::string http_cookie_headers[3];
static std::string http_keepalive_connection_headers[2];
static std::string sse_resume_header;

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
        if (a != b)
            return false;
    }
    return true;
}

static void send_text(void *client, const char *text) {
    rt_tcp_send_str(client, rt_const_cstr(text));
}

static void http_server_keepalive_handler(void *req_obj, void *res_obj) {
    (void)req_obj;
    rt_server_res_send(res_obj, rt_const_cstr("ok"));
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

static std::string read_http_cookie_header(void *client) {
    std::string cookie_header;
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        if (cstr && ascii_header_name_equals(cstr, "Cookie", 6) && cstr[6] == ':') {
            const char *value = cstr + 7;
            while (*value == ' ' || *value == '\t')
                value++;
            cookie_header = value;
        }
        rt_string_unref(line);
        if (done)
            break;
    }
    return cookie_header;
}

static std::string read_http_header_value(void *client, const char *name) {
    std::string header_value;
    const size_t name_len = strlen(name);
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        if (cstr && ascii_header_name_equals(cstr, name, name_len) && cstr[name_len] == ':') {
            const char *value = cstr + name_len + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            header_value = value;
        }
        rt_string_unref(line);
        if (done)
            break;
    }
    return header_value;
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

static void sse_resume_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    sse_resume_header.clear();

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
              "retry: 1\r\n"
              "id: 1\r\n"
              "data: first\r\n"
              "\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rt_tcp_close(client);

    client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }
    sse_resume_header = read_http_header_value(client, "Last-Event-ID");
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Connection: close\r\n"
              "\r\n"
              "id: 2\r\n"
              "data: second\r\n"
              "\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

static void http_cookie_scope_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    http_cookie_headers[0].clear();
    http_cookie_headers[1].clear();
    http_cookie_headers[2].clear();

    for (int i = 0; i < 3; i++) {
        void *client = rt_tcp_server_accept_for(server, 5000);
        if (!client)
            break;

        rt_string request_line = rt_tcp_recv_line(client);
        rt_string_unref(request_line);
        http_cookie_headers[i] = read_http_cookie_header(client);

        if (i == 0) {
            send_text(client,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 2\r\n"
                      "Set-Cookie: appToken=alpha; Path=/app\r\n"
                      "Set-Cookie: rootToken=beta; Path=/\r\n"
                      "Set-Cookie: secureToken=secret; Path=/app; Secure\r\n"
                      "\r\nok");
        } else if (i == 1) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\napp");
        } else {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nother");
        }

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
}

static void http_keepalive_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    http_keepalive_accept_count = 0;
    http_keepalive_reused = false;
    http_keepalive_connection_headers[0].clear();
    http_keepalive_connection_headers[1].clear();

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    http_keepalive_accept_count = 1;
    rt_string request_line = rt_tcp_recv_line(client);
    rt_string_unref(request_line);
    http_keepalive_connection_headers[0] = read_http_header_value(client, "Connection");
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: keep-alive\r\n"
              "\r\n"
              "ok");

    void *second_client = rt_tcp_server_accept_for(server, 750);
    if (second_client) {
        http_keepalive_accept_count = 2;
        request_line = rt_tcp_recv_line(second_client);
        rt_string_unref(request_line);
        http_keepalive_connection_headers[1] = read_http_header_value(second_client, "Connection");
        send_text(second_client,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Length: 2\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "ok");
        rt_tcp_close(second_client);
        rt_tcp_close(client);
        rt_tcp_server_close(server);
        return;
    }

    request_line = rt_tcp_recv_line(client);
    rt_string_unref(request_line);
    http_keepalive_connection_headers[1] = read_http_header_value(client, "Connection");
    http_keepalive_reused = true;
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: close\r\n"
              "\r\n"
              "ok");
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

static void test_sse_resume_after_disconnect() {
    printf("\nTesting SseClient reconnect and resume:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_resume_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/resume", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE resume connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data1 = rt_sse_recv(sse);
    test_result("SSE first event payload matches", strcmp(rt_string_cstr(data1), "first") == 0);
    rt_string first_id = rt_sse_last_event_id(sse);
    test_result("SSE first event id captured", strcmp(rt_string_cstr(first_id), "1") == 0);

    rt_string data2 = rt_sse_recv(sse);
    test_result("SSE reconnect receives next event", strcmp(rt_string_cstr(data2), "second") == 0);
    rt_string second_id = rt_sse_last_event_id(sse);
    test_result("SSE second event id captured", strcmp(rt_string_cstr(second_id), "2") == 0);
    test_result("SSE reconnect sent Last-Event-ID header", sse_resume_header == "1");

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

static void test_http_client_cookie_scope() {
    printf("\nTesting HttpClient cookie scope:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(http_cookie_scope_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    void *client = rt_http_client_new();

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/set", port);
    void *res1 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient initial cookie response succeeds", rt_http_res_status(res1) == 200);

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/app/data", port);
    void *res2 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient matching-path request succeeds", rt_http_res_status(res2) == 200);

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/other", port);
    void *res3 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient non-matching-path request succeeds", rt_http_res_status(res3) == 200);

    server.join();

    test_result("Path cookie sent on matching path",
                http_cookie_headers[1].find("appToken=alpha") != std::string::npos);
    test_result("Root cookie sent on matching path",
                http_cookie_headers[1].find("rootToken=beta") != std::string::npos);
    test_result("Secure cookie withheld on plain HTTP",
                http_cookie_headers[1].find("secureToken=secret") == std::string::npos);
    test_result("Path cookie withheld outside its path",
                http_cookie_headers[2].find("appToken=alpha") == std::string::npos);
    test_result("Root cookie still sent outside the path",
                http_cookie_headers[2].find("rootToken=beta") != std::string::npos);
}

static void test_http_client_keepalive_reuse() {
    printf("\nTesting HttpClient keep-alive reuse:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(http_keepalive_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/reuse", port);

    void *client = rt_http_client_new();
    test_result("HttpClient keep-alive defaults on", rt_http_client_get_keep_alive(client) == 1);

    void *res1 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient first keep-alive request succeeds", rt_http_res_status(res1) == 200);

    void *res2 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient second keep-alive request succeeds", rt_http_res_status(res2) == 200);

    server.join();

    test_result("HttpClient reused a single accepted socket", http_keepalive_reused.load());
    test_result("HttpClient keep-alive did not require a second accept",
                http_keepalive_accept_count.load() == 1);
    test_result("HttpClient first request advertises keep-alive",
                http_keepalive_connection_headers[0].find("keep-alive") != std::string::npos);
    test_result("HttpClient second request advertises keep-alive",
                http_keepalive_connection_headers[1].find("keep-alive") != std::string::npos);
}

static void test_http_server_keepalive_response() {
    printf("\nTesting HttpServer keep-alive responses:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *server = rt_http_server_new(port);
    rt_http_server_get(server, rt_const_cstr("/ping"), rt_const_cstr("ping"));
    rt_http_server_bind_handler(server, rt_const_cstr("ping"), (void *)&http_server_keepalive_handler);
    rt_http_server_start(server);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    void *client = rt_tcp_connect(rt_const_cstr("127.0.0.1"), port);

    send_text(client,
              "GET /ping HTTP/1.1\r\n"
              "Host: 127.0.0.1\r\n"
              "Connection: keep-alive\r\n"
              "\r\n");
    rt_string status1 = rt_tcp_recv_line(client);
    test_result("HttpServer first response status is 200",
                strcmp(rt_string_cstr(status1), "HTTP/1.1 200 OK") == 0);
    rt_string_unref(status1);
    std::string connection1 = read_http_header_value(client, "Connection");
    rt_string body1_bytes = nullptr;
    void *body1 = rt_tcp_recv_exact(client, 2);
    body1_bytes = rt_bytes_to_str(body1);
    test_result("HttpServer first response body matches", strcmp(rt_string_cstr(body1_bytes), "ok") == 0);
    rt_string_unref(body1_bytes);

    send_text(client,
              "GET /ping HTTP/1.1\r\n"
              "Host: 127.0.0.1\r\n"
              "Connection: close\r\n"
              "\r\n");
    rt_string status2 = rt_tcp_recv_line(client);
    test_result("HttpServer second response status is 200",
                strcmp(rt_string_cstr(status2), "HTTP/1.1 200 OK") == 0);
    rt_string_unref(status2);
    std::string connection2 = read_http_header_value(client, "Connection");
    void *body2 = rt_tcp_recv_exact(client, 2);
    rt_string body2_str = rt_bytes_to_str(body2);
    test_result("HttpServer second response body matches", strcmp(rt_string_cstr(body2_str), "ok") == 0);
    rt_string_unref(body2_str);

    rt_http_server_stop(server);
    rt_tcp_close(client);

    test_result("HttpServer honors keep-alive on the first response",
                connection1.find("keep-alive") != std::string::npos);
    test_result("HttpServer closes when the request asks for close",
                connection2.find("close") != std::string::npos);
}

int main() {
    test_sse_plain_event();
    test_sse_chunked_event();
    test_sse_resume_after_disconnect();
    test_http_client_cookie_scope();
    test_http_client_keepalive_reuse();
    test_http_server_keepalive_response();
    test_smtp_plain_send_sanitizes_and_dot_stuffs();

    printf("\nAll high-level network tests passed.\n");
    return 0;
}
