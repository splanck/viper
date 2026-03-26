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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
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

static void test_http_server_parses_exact_body(void)
{
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

static void test_http_server_rejects_invalid_content_length(void)
{
    const char raw[] = "POST /x HTTP/1.1\r\n"
                       "Content-Length: -1\r\n"
                       "\r\n";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_rejects_truncated_body(void)
{
    const char raw[] = "POST /x HTTP/1.1\r\n"
                       "Content-Length: 5\r\n"
                       "\r\n"
                       "hi";
    ASSERT(rt_http_server_test_parse_request(raw, strlen(raw), NULL, NULL, NULL, NULL) == 0);
}

static void test_http_server_builds_large_header_block(void)
{
    enum
    {
        header_count = 40
    };

    const char *body = "{\"ok\":true}";
    const char *header_names[header_count];
    const char *header_values[header_count];
    char name_storage[header_count][24];
    char value_storage[header_count][40];

    for (int i = 0; i < header_count; i++)
    {
        snprintf(name_storage[i], sizeof(name_storage[i]), "X-Test-%02d", i);
        snprintf(value_storage[i], sizeof(value_storage[i]), "value-%02d-abcdefghijklmnop", i);
        header_names[i] = name_storage[i];
        header_values[i] = value_storage[i];
    }

    size_t resp_len = 0;
    char *resp = rt_http_server_test_build_response(
        200, body, strlen(body), header_names, header_values, header_count, &resp_len);
    ASSERT(resp != NULL);
    if (resp)
    {
        ASSERT(resp_len > strlen(body));
        ASSERT(strstr(resp, "X-Test-39: value-39-abcdefghijklmnop\r\n") != NULL);
        ASSERT(strstr(resp, "Content-Length: 11\r\n") != NULL);
        ASSERT(resp_len >= strlen(body));
        ASSERT(memcmp(resp + resp_len - strlen(body), body, strlen(body)) == 0);
    }
    free(resp);
}

static void test_http_parse_url_accepts_ipv6_literal(void)
{
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

static void test_ws_parse_url_accepts_ipv6_literal(void)
{
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

static void test_ws_handshake_validation_accepts_valid_response(void)
{
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

static void test_ws_handshake_validation_rejects_spurious_101(void)
{
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

int main(void)
{
    test_http_server_parses_exact_body();
    test_http_server_rejects_invalid_content_length();
    test_http_server_rejects_truncated_body();
    test_http_server_builds_large_header_block();
    test_http_parse_url_accepts_ipv6_literal();
    test_ws_parse_url_accepts_ipv6_literal();
    test_ws_handshake_validation_accepts_valid_response();
    test_ws_handshake_validation_rejects_spurious_101();

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
