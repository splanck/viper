//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTNetworkRuntimeTests.c
// Purpose: Regression tests for networking runtime hardening fixes.
//
//===----------------------------------------------------------------------===//

#include "rt_http_server.h"
#include "rt_object.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                          \
    } while (0)

extern int rt_http_server_test_parse_request(const char *raw,
                                             size_t raw_len,
                                             char **method_out,
                                             char **path_out,
                                             char **body_out,
                                             size_t *body_len_out);
extern char *rt_http_server_test_build_response(int status_code,
                                                const char *body,
                                                size_t body_len,
                                                const char **header_names,
                                                const char **header_values,
                                                size_t header_count,
                                                size_t *out_len);
extern int rt_http_parse_url_for_test(
    const char *url_str, char **host_out, int *port_out, char **path_out, int *use_tls_out);
extern int rt_ws_parse_url_for_test(
    const char *url, int *is_secure, char **host, int *port, char **path);
extern int rt_ws_validate_handshake_response_for_test(const char *response, const char *key_copy);
extern char *rt_ws_compute_accept_key(const char *key_cstr);

static char captured_method[32];
static char captured_path[64];
static char captured_query[32];
static char captured_header[32];
static char captured_param[32];
static char captured_body[64];

static void copy_rt_string(char *dst, size_t cap, rt_string value) {
    const char *cstr = value ? rt_string_cstr(value) : NULL;
    if (!dst || cap == 0)
        return;
    if (!cstr) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", cstr);
}

static void reset_captured_request_state(void) {
    captured_method[0] = '\0';
    captured_path[0] = '\0';
    captured_query[0] = '\0';
    captured_header[0] = '\0';
    captured_param[0] = '\0';
    captured_body[0] = '\0';
}

static void native_http_handler(void *req_obj, void *res_obj) {
    rt_string method = rt_server_req_method(req_obj);
    rt_string path = rt_server_req_path(req_obj);
    rt_string query = rt_server_req_query(req_obj, rt_const_cstr("q"));
    rt_string header = rt_server_req_header(req_obj, rt_const_cstr("X-Test"));
    rt_string param = rt_server_req_param(req_obj, rt_const_cstr("id"));
    rt_string body = rt_server_req_body(req_obj);

    copy_rt_string(captured_method, sizeof(captured_method), method);
    copy_rt_string(captured_path, sizeof(captured_path), path);
    copy_rt_string(captured_query, sizeof(captured_query), query);
    copy_rt_string(captured_header, sizeof(captured_header), header);
    copy_rt_string(captured_param, sizeof(captured_param), param);
    copy_rt_string(captured_body, sizeof(captured_body), body);

    rt_string_unref(method);
    rt_string_unref(path);
    rt_string_unref(query);
    rt_string_unref(header);
    rt_string_unref(param);
    rt_string_unref(body);

    rt_server_res_status(res_obj, 201);
    rt_server_res_header(res_obj, rt_const_cstr("X-Handled"), rt_const_cstr("yes"));
    rt_server_res_send(res_obj, rt_const_cstr("native-ok"));
}

static void test_http_server_parses_exact_body(void) {
    const char raw[] = "POST /submit?q=1 HTTP/1.1\r\n"
                       "Host: example.test\r\n"
                       "Content-Length: 5\r\n"
                       "\r\n"
                       "hello";
    char *method = NULL;
    char *path = NULL;
    char *body = NULL;
    size_t body_len = 0;

    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), &method, &path, &body, &body_len) ==
           1);
    ASSERT(method && strcmp(method, "POST") == 0);
    ASSERT(path && strcmp(path, "/submit") == 0);
    ASSERT(body && strcmp(body, "hello") == 0);
    ASSERT(body_len == 5);

    free(method);
    free(path);
    free(body);
}

static void test_http_server_rejects_invalid_http_version(void) {
    const char raw[] = "GET / HTTP/2.0\r\n"
                       "Host: example.test\r\n"
                       "\r\n";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_rejects_invalid_content_length(void) {
    const char raw[] = "POST /x HTTP/1.1\r\n"
                       "Content-Length: -1\r\n"
                       "\r\n";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_rejects_truncated_body(void) {
    const char raw[] = "POST /x HTTP/1.1\r\n"
                       "Content-Length: 5\r\n"
                       "\r\n"
                       "hi";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_rejects_duplicate_content_length(void) {
    const char raw[] = "POST /x HTTP/1.1\r\n"
                       "Content-Length: 3\r\n"
                       "Content-Length: 3\r\n"
                       "\r\n"
                       "hey";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_parses_chunked_request_body(void) {
    const char raw[] = "POST /stream HTTP/1.1\r\n"
                       "Transfer-Encoding: chunked\r\n"
                       "\r\n"
                       "5\r\nhello\r\n0\r\n\r\n";
    char *method = NULL;
    char *path = NULL;
    char *body = NULL;
    size_t body_len = 0;

    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), &method, &path, &body, &body_len) ==
           1);
    ASSERT(method && strcmp(method, "POST") == 0);
    ASSERT(path && strcmp(path, "/stream") == 0);
    ASSERT(body && strcmp(body, "hello") == 0);
    ASSERT(body_len == 5);

    free(method);
    free(path);
    free(body);
}

static void test_http_server_builds_large_header_block(void) {
    enum { header_count = 40 };

    const char *body = "{\"ok\":true}";
    const char *header_names[header_count];
    const char *header_values[header_count];
    char name_storage[header_count][24];
    char value_storage[header_count][40];

    for (int i = 0; i < header_count; i++) {
        snprintf(name_storage[i], sizeof(name_storage[i]), "X-Test-%02d", i);
        snprintf(value_storage[i], sizeof(value_storage[i]), "value-%02d-abcdefghijklmnop", i);
        header_names[i] = name_storage[i];
        header_values[i] = value_storage[i];
    }

    size_t resp_len = 0;
    char *resp = rt_http_server_test_build_response(
        200, body, strlen(body), header_names, header_values, header_count, &resp_len);
    ASSERT(resp != NULL);
    if (resp) {
        ASSERT(resp_len > strlen(body));
        ASSERT(strstr(resp, "X-Test-39: value-39-abcdefghijklmnop\r\n") != NULL);
        ASSERT(strstr(resp, "Content-Length: 11\r\n") != NULL);
        ASSERT(resp_len >= strlen(body));
        ASSERT(memcmp(resp + resp_len - strlen(body), body, strlen(body)) == 0);
    }
    free(resp);
}

static void test_http_server_response_filters_managed_and_injected_headers(void) {
    const char *header_names[] = {"Connection", "X-Good", "X-Bad"};
    const char *header_values[] = {"keep-alive", "ok", "evil\r\nX-Injected: yes"};
    size_t resp_len = 0;
    char *resp =
        rt_http_server_test_build_response(200, "{}", 2, header_names, header_values, 3, &resp_len);
    ASSERT(resp != NULL);
    if (resp) {
        ASSERT(strstr(resp, "Connection: keep-alive\r\n") == NULL);
        ASSERT(strstr(resp, "X-Good: ok\r\n") != NULL);
        ASSERT(strstr(resp, "X-Injected: yes\r\n") == NULL);
    }
    free(resp);
}

static void test_http_server_executes_bound_native_handler(void) {
    reset_captured_request_state();

    void *server = rt_http_server_new(8080);
    rt_http_server_post(server, rt_const_cstr("/users/:id"), rt_const_cstr("handle_user"));
    rt_http_server_bind_handler(server, rt_const_cstr("handle_user"), (void *)&native_http_handler);

    rt_string raw = rt_const_cstr("POST /users/42?q=search HTTP/1.1\r\n"
                                  "Host: example.test\r\n"
                                  "X-Test: abc\r\n"
                                  "Content-Length: 5\r\n"
                                  "\r\n"
                                  "hello");
    rt_string response = (rt_string)rt_http_server_process_request(server, raw);
    const char *response_cstr = rt_string_cstr(response);

    ASSERT(response_cstr != NULL);
    ASSERT(strstr(response_cstr, "HTTP/1.1 201 Created\r\n") != NULL);
    ASSERT(strstr(response_cstr, "X-Handled: yes\r\n") != NULL);
    ASSERT(strstr(response_cstr, "\r\n\r\nnative-ok") != NULL);
    ASSERT(strcmp(captured_method, "POST") == 0);
    ASSERT(strcmp(captured_path, "/users/42") == 0);
    ASSERT(strcmp(captured_query, "search") == 0);
    ASSERT(strcmp(captured_header, "abc") == 0);
    ASSERT(strcmp(captured_param, "42") == 0);
    ASSERT(strcmp(captured_body, "hello") == 0);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
}

static void test_http_server_executes_bound_native_handler_for_chunked_body(void) {
    reset_captured_request_state();

    void *server = rt_http_server_new(8082);
    rt_http_server_post(server, rt_const_cstr("/stream/:id"), rt_const_cstr("handle_stream"));
    rt_http_server_bind_handler(
        server, rt_const_cstr("handle_stream"), (void *)&native_http_handler);

    rt_string raw = rt_const_cstr("POST /stream/7?q=resume HTTP/1.1\r\n"
                                  "Host: example.test\r\n"
                                  "X-Test: xyz\r\n"
                                  "Transfer-Encoding: chunked\r\n"
                                  "\r\n"
                                  "2\r\nhe\r\n"
                                  "3\r\nllo\r\n"
                                  "0\r\n\r\n");
    rt_string response = (rt_string)rt_http_server_process_request(server, raw);
    const char *response_cstr = rt_string_cstr(response);

    ASSERT(response_cstr != NULL);
    ASSERT(strstr(response_cstr, "HTTP/1.1 201 Created\r\n") != NULL);
    ASSERT(strstr(response_cstr, "\r\n\r\nnative-ok") != NULL);
    ASSERT(strcmp(captured_method, "POST") == 0);
    ASSERT(strcmp(captured_path, "/stream/7") == 0);
    ASSERT(strcmp(captured_query, "resume") == 0);
    ASSERT(strcmp(captured_header, "xyz") == 0);
    ASSERT(strcmp(captured_param, "7") == 0);
    ASSERT(strcmp(captured_body, "hello") == 0);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
}

static void test_http_server_http10_defaults_to_close(void) {
    void *server = rt_http_server_new(8083);
    rt_http_server_get(server, rt_const_cstr("/ping"), rt_const_cstr("handle_ping"));
    rt_http_server_bind_handler(server, rt_const_cstr("handle_ping"), (void *)&native_http_handler);

    rt_string raw = rt_const_cstr("GET /ping HTTP/1.0\r\n"
                                  "Host: example.test\r\n"
                                  "\r\n");
    rt_string response = (rt_string)rt_http_server_process_request(server, raw);
    const char *response_cstr = rt_string_cstr(response);

    ASSERT(response_cstr != NULL);
    ASSERT(strstr(response_cstr, "Connection: close\r\n") != NULL);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
}

static void test_http_server_http10_keepalive_opt_in(void) {
    void *server = rt_http_server_new(8084);
    rt_http_server_get(server, rt_const_cstr("/ping"), rt_const_cstr("handle_ping"));
    rt_http_server_bind_handler(server, rt_const_cstr("handle_ping"), (void *)&native_http_handler);

    rt_string raw = rt_const_cstr("GET /ping HTTP/1.0\r\n"
                                  "Host: example.test\r\n"
                                  "Connection: keep-alive\r\n"
                                  "\r\n");
    rt_string response = (rt_string)rt_http_server_process_request(server, raw);
    const char *response_cstr = rt_string_cstr(response);

    ASSERT(response_cstr != NULL);
    ASSERT(strstr(response_cstr, "Connection: keep-alive\r\n") != NULL);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
}

static void test_http_server_reports_missing_handler_binding(void) {
    void *server = rt_http_server_new(8081);
    rt_http_server_get(server, rt_const_cstr("/missing"), rt_const_cstr("missing_handler"));

    rt_string raw = rt_const_cstr("GET /missing HTTP/1.1\r\nHost: example.test\r\n\r\n");
    rt_string response = (rt_string)rt_http_server_process_request(server, raw);
    const char *response_cstr = rt_string_cstr(response);

    ASSERT(response_cstr != NULL);
    ASSERT(strstr(response_cstr, "HTTP/1.1 500 Internal Server Error\r\n") != NULL);
    ASSERT(strstr(response_cstr, "{\"error\":\"Handler not registered\"}") != NULL);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
}

static void test_http_parse_url_accepts_ipv6_literal(void) {
    char *host = NULL;
    char *path = NULL;
    int port = 0;
    int use_tls = 0;

    ASSERT(rt_http_parse_url_for_test(
               "https://[2001:db8::1]:8443/api?q=1", &host, &port, &path, &use_tls) == 1);
    ASSERT(host && strcmp(host, "2001:db8::1") == 0);
    ASSERT(port == 8443);
    ASSERT(path && strcmp(path, "/api?q=1") == 0);
    ASSERT(use_tls == 1);

    free(host);
    free(path);
}

static void test_http_parse_url_rejects_crlf_injection(void) {
    char *host = NULL;
    char *path = NULL;
    int port = 0;
    int use_tls = 0;
    ASSERT(rt_http_parse_url_for_test("http://example.test/\r\nX-Evil: yes",
                                      &host,
                                      &port,
                                      &path,
                                      &use_tls) == 0);
    free(host);
    free(path);
}

static void test_ws_parse_url_accepts_ipv6_literal(void) {
    int secure = 0;
    int port = 0;
    char *host = NULL;
    char *path = NULL;

    ASSERT(rt_ws_parse_url_for_test("ws://[::1]:9000/chat", &secure, &host, &port, &path) == 1);
    ASSERT(secure == 0);
    ASSERT(host && strcmp(host, "::1") == 0);
    ASSERT(port == 9000);
    ASSERT(path && strcmp(path, "/chat") == 0);

    free(host);
    free(path);
}

static void test_ws_handshake_validation_accepts_valid_response(void) {
    static const char key[] = "dGhlIHNhbXBsZSBub25jZQ==";
    char *accept = rt_ws_compute_accept_key(key);
    ASSERT(accept != NULL);
    if (!accept)
        return;

    char response[512];
    snprintf(response,
             sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: keep-alive, Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n"
             "\r\n",
             accept);

    ASSERT(rt_ws_validate_handshake_response_for_test(response, key) == 1);
    free(accept);
}

static void test_ws_handshake_validation_rejects_spurious_101(void) {
    static const char key[] = "dGhlIHNhbXBsZSBub25jZQ==";
    char *accept = rt_ws_compute_accept_key(key);
    ASSERT(accept != NULL);
    if (!accept)
        return;

    char response[512];
    snprintf(response,
             sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "X-Debug: 101 here\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n"
             "\r\n",
             accept);

    ASSERT(rt_ws_validate_handshake_response_for_test(response, key) == 0);
    free(accept);
}

int main(void) {
    test_http_server_parses_exact_body();
    test_http_server_rejects_invalid_http_version();
    test_http_server_rejects_invalid_content_length();
    test_http_server_rejects_truncated_body();
    test_http_server_rejects_duplicate_content_length();
    test_http_server_parses_chunked_request_body();
    test_http_server_builds_large_header_block();
    test_http_server_response_filters_managed_and_injected_headers();
    test_http_server_executes_bound_native_handler();
    test_http_server_executes_bound_native_handler_for_chunked_body();
    test_http_server_http10_defaults_to_close();
    test_http_server_http10_keepalive_opt_in();
    test_http_server_reports_missing_handler_binding();
    test_http_parse_url_accepts_ipv6_literal();
    test_http_parse_url_rejects_crlf_injection();
    test_ws_parse_url_accepts_ipv6_literal();
    test_ws_handshake_validation_accepts_valid_response();
    test_ws_handshake_validation_rejects_spurious_101();

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
