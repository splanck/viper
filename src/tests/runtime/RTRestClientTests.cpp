//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Viper.Network.RestClient
//
//===----------------------------------------------------------------------===//

#include "rt_restclient.h"
#include "rt_netutils.h"
#include "rt_network.h"
#include "rt_string.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static void test_result(bool cond, const char *name) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

static std::atomic<bool> rest_server_ready{false};
static std::atomic<bool> rest_server_failed{false};
static std::atomic<int> rest_keepalive_accept_count{0};
static std::atomic<bool> rest_keepalive_reused{false};
static std::string rest_keepalive_connection_headers[2];

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

static void send_text(void *client, const char *text) {
    rt_tcp_send_str(client, rt_const_cstr(text));
}

static void wait_for_server() {
    for (int i = 0; i < 200 && !rest_server_ready.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

static void rest_keepalive_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        rest_server_failed = true;
        rest_server_ready = true;
        return;
    }

    rest_server_failed = false;
    rest_server_ready = true;
    rest_keepalive_accept_count = 0;
    rest_keepalive_reused = false;
    rest_keepalive_connection_headers[0].clear();
    rest_keepalive_connection_headers[1].clear();

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    rest_keepalive_accept_count = 1;
    rt_string request_line = rt_tcp_recv_line(client);
    rt_string_unref(request_line);
    rest_keepalive_connection_headers[0] = read_http_header_value(client, "Connection");
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: keep-alive\r\n"
              "\r\n"
              "ok");

    void *second_client = rt_tcp_server_accept_for(server, 750);
    if (second_client) {
        rest_keepalive_accept_count = 2;
        request_line = rt_tcp_recv_line(second_client);
        rt_string_unref(request_line);
        rest_keepalive_connection_headers[1] = read_http_header_value(second_client, "Connection");
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
    rest_keepalive_connection_headers[1] = read_http_header_value(client, "Connection");
    rest_keepalive_reused = true;
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: close\r\n"
              "\r\n"
              "ok");
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

//=============================================================================
// Creation Tests
//=============================================================================

static void test_new_client() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    test_result(client != NULL, "new_client: should create client");

    rt_string base = rt_restclient_base_url(client);
    test_result(strcmp(rt_string_cstr(base), "https://api.example.com") == 0,
                "new_client: should store base URL");
}

static void test_new_client_empty_url() {
    void *client = rt_restclient_new(rt_const_cstr(""));

    test_result(client != NULL, "new_client_empty: should create client with empty URL");

    rt_string base = rt_restclient_base_url(client);
    test_result(strlen(rt_string_cstr(base)) == 0, "new_client_empty: should have empty base URL");
}

static void test_new_client_null() {
    rt_string base = rt_restclient_base_url(NULL);
    test_result(strlen(rt_string_cstr(base)) == 0, "null_client: should return empty string");
}

//=============================================================================
// Header Configuration Tests
//=============================================================================

static void test_set_header() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting a header shouldn't crash
    rt_restclient_set_header(
        client, rt_const_cstr("X-Custom-Header"), rt_const_cstr("CustomValue"));

    test_result(true, "set_header: should set header without crash");
}

static void test_del_header() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_header(
        client, rt_const_cstr("X-Custom-Header"), rt_const_cstr("CustomValue"));

    // Deleting a header shouldn't crash
    rt_restclient_del_header(client, rt_const_cstr("X-Custom-Header"));

    test_result(true, "del_header: should delete header without crash");
}

static void test_null_client_headers() {
    // Operations on NULL client should be safe (no-op)
    rt_restclient_set_header(NULL, rt_const_cstr("Header"), rt_const_cstr("Value"));
    rt_restclient_del_header(NULL, rt_const_cstr("Header"));

    test_result(true, "null_client_headers: should handle NULL safely");
}

//=============================================================================
// Authentication Tests
//=============================================================================

static void test_set_auth_bearer() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting bearer auth shouldn't crash
    rt_restclient_set_auth_bearer(client, rt_const_cstr("my-token-12345"));

    test_result(true, "set_auth_bearer: should set bearer auth without crash");
}

static void test_set_auth_basic() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting basic auth shouldn't crash
    rt_restclient_set_auth_basic(client, rt_const_cstr("username"), rt_const_cstr("password"));

    test_result(true, "set_auth_basic: should set basic auth without crash");
}

static void test_clear_auth() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_auth_bearer(client, rt_const_cstr("token"));
    rt_restclient_clear_auth(client);

    test_result(true, "clear_auth: should clear auth without crash");
}

static void test_null_client_auth() {
    // Auth operations on NULL client should be safe
    rt_restclient_set_auth_bearer(NULL, rt_const_cstr("token"));
    rt_restclient_set_auth_basic(NULL, rt_const_cstr("user"), rt_const_cstr("pass"));
    rt_restclient_clear_auth(NULL);

    test_result(true, "null_client_auth: should handle NULL safely");
}

//=============================================================================
// Timeout Tests
//=============================================================================

static void test_set_timeout() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_timeout(client, 60000); // 60 seconds

    test_result(true, "set_timeout: should set timeout without crash");
}

static void test_set_timeout_null() {
    rt_restclient_set_timeout(NULL, 5000);

    test_result(true, "set_timeout_null: should handle NULL safely");
}

static void test_keepalive_defaults() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));
    test_result(rt_restclient_get_keep_alive(client) == 1,
                "keepalive_default: should default to enabled");
}

static void test_keepalive_toggle_and_pool_resize() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));
    rt_restclient_set_keep_alive(client, 0);
    test_result(rt_restclient_get_keep_alive(client) == 0,
                "keepalive_toggle: should disable keepalive");
    rt_restclient_set_pool_size(client, 2);
    rt_restclient_set_keep_alive(client, 1);
    test_result(rt_restclient_get_keep_alive(client) == 1,
                "keepalive_toggle: should re-enable keepalive");
}

//=============================================================================
// Status Tests (without actual HTTP)
//=============================================================================

static void test_last_status_initial() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    int64_t status = rt_restclient_last_status(client);
    test_result(status == 0, "last_status_initial: should be 0 initially");
}

static void test_last_response_initial() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    void *response = rt_restclient_last_response(client);
    test_result(response == NULL, "last_response_initial: should be NULL initially");
}

static void test_last_ok_initial() {
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    int8_t ok = rt_restclient_last_ok(client);
    test_result(ok == 0, "last_ok_initial: should be false initially");
}

static void test_last_status_null() {
    int64_t status = rt_restclient_last_status(NULL);
    test_result(status == 0, "last_status_null: should return 0 for NULL");
}

static void test_last_response_null() {
    void *response = rt_restclient_last_response(NULL);
    test_result(response == NULL, "last_response_null: should return NULL for NULL");
}

static void test_last_ok_null() {
    int8_t ok = rt_restclient_last_ok(NULL);
    test_result(ok == 0, "last_ok_null: should return false for NULL");
}

//=============================================================================
// Lifecycle / Finalizer Tests
//=============================================================================

/// @brief Regression test for RC-1: finalizer must be registered for every client.
///
/// Creating many clients exercises the allocation path and ensures no crash
/// occurs from missing or double-registered finalizers. Under ASAN, any
/// finalizer bug surfaces as a leak or invalid-free report.
static void test_restclient_many_instances() {
    const int COUNT = 100;

    for (int i = 0; i < COUNT; i++) {
        void *c = rt_restclient_new(rt_const_cstr("https://api.example.com"));
        test_result(c != NULL, "many_instances: client created");

        // Exercise all post-init paths that the finalizer must clean up
        rt_restclient_set_header(c, rt_const_cstr("X-Index"), rt_const_cstr("val"));
        rt_restclient_set_auth_bearer(c, rt_const_cstr("tok"));
        rt_restclient_set_timeout(c, 5000);

        // Verify state is coherent
        rt_string base = rt_restclient_base_url(c);
        test_result(strcmp(rt_string_cstr(base), "https://api.example.com") == 0,
                    "many_instances: base URL preserved");
        test_result(rt_restclient_last_status(c) == 0, "many_instances: last_status initially 0");
    }

    test_result(true, "many_instances: all 100 instances created without crash");
}

static void test_restclient_keepalive_reuse() {
    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("SKIP: restclient_keepalive_reuse (local bind unavailable)\n");
        return;
    }

    rest_server_ready = false;
    rest_server_failed = false;
    std::thread server(rest_keepalive_server_thread, port);
    wait_for_server();
    if (rest_server_failed) {
        server.join();
        printf("SKIP: restclient_keepalive_reuse (local bind unavailable)\n");
        return;
    }

    char base_url[128];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    void *client = rt_restclient_new(rt_const_cstr(base_url));
    test_result(rt_restclient_get_keep_alive(client) == 1,
                "rest_keepalive: keepalive should default on");

    void *res1 = rt_restclient_get(client, rt_const_cstr("items"));
    test_result(rt_http_res_status(res1) == 200,
                "rest_keepalive: first request should succeed");

    void *res2 = rt_restclient_get(client, rt_const_cstr("items"));
    test_result(rt_http_res_status(res2) == 200,
                "rest_keepalive: second request should succeed");

    server.join();

    test_result(rest_keepalive_reused.load(),
                "rest_keepalive: requests should reuse one socket");
    test_result(rest_keepalive_accept_count.load() == 1,
                "rest_keepalive: second accept should not be needed");
    test_result(rest_keepalive_connection_headers[0].find("keep-alive") != std::string::npos,
                "rest_keepalive: first request should advertise keepalive");
    test_result(rest_keepalive_connection_headers[1].find("keep-alive") != std::string::npos,
                "rest_keepalive: second request should advertise keepalive");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    // Creation tests
    test_new_client();
    test_new_client_empty_url();
    test_new_client_null();

    // Header tests
    test_set_header();
    test_del_header();
    test_null_client_headers();

    // Auth tests
    test_set_auth_bearer();
    test_set_auth_basic();
    test_clear_auth();
    test_null_client_auth();

    // Timeout tests
    test_set_timeout();
    test_set_timeout_null();
    test_keepalive_defaults();
    test_keepalive_toggle_and_pool_resize();

    // Status tests
    test_last_status_initial();
    test_last_response_initial();
    test_last_ok_initial();
    test_last_status_null();
    test_last_response_null();
    test_last_ok_null();

    // Lifecycle / finalizer regression (RC-1)
    test_restclient_many_instances();
    test_restclient_keepalive_reuse();

    printf("All RestClient tests passed!\n");
    return 0;
}
