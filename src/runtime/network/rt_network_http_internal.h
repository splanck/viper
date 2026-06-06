//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_network_http_internal.h
// Purpose: Internal helpers shared between the HTTP transport and higher-level
//          HTTP client wrappers.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Default per-request redirect cap / timeout (shared by client + wrappers).
#define HTTP_MAX_REDIRECTS 5
#define HTTP_DEFAULT_TIMEOUT_MS 30000

/// @brief Parsed URL structure.
typedef struct parsed_url {
    char *host;  // Allocated hostname
    int port;    // Port number (default 80 for http, 443 for https)
    char *path;  // Path including query string (allocated)
    int use_tls; // 1 for https, 0 for http
} parsed_url_t;

/// @brief HTTP header entry.
typedef struct http_header {
    char *name;
    char *value;
    struct http_header *next;
} http_header_t;

/// @brief HTTP request structure.
typedef struct rt_http_req {
    char *method;           // HTTP method
    parsed_url_t url;       // Parsed URL
    http_header_t *headers; // Linked list of headers
    uint8_t *body;          // Request body
    size_t body_len;        // Body length
    int timeout_ms;         // Timeout in milliseconds
    int tls_verify;         // 1 = verify peer certificate, 0 = allow insecure HTTPS
    int follow_redirects;   // Automatically follow redirect responses
    int max_redirects;      // Redirect limit for this request
    int accept_gzip;        // Advertise gzip support when no explicit Accept-Encoding is set
    int decode_gzip;        // Transparently decode gzip-encoded response bodies
    int keep_alive;         // Request connection reuse when paired with a pool
    void *connection_pool;  // Internal pool used by session-style HTTP clients
    int force_http1;        // Keep the transport on HTTP/1.1 even over TLS
} rt_http_req_t;

/// @brief HTTP response structure.
typedef struct rt_http_res {
    int status;        // HTTP status code
    char *status_text; // Status text (allocated)
    void *headers;     // Map of headers
    uint8_t *body;     // Response body
    size_t body_len;   // Body length
} rt_http_res_t;

#ifdef __cplusplus
extern "C" {
#endif

// HTTP client core (defined in rt_network_http.c, consumed by rt_network_http_api.c).
int http_method_is_token(const char *method);
int http_rt_string_has_embedded_nul(rt_string text);
void free_parsed_url(parsed_url_t *url);
int parse_url(const char *url_str, parsed_url_t *result);
void free_headers(http_header_t *headers);
void add_header(rt_http_req_t *req, const char *name, const char *value);
bool has_header(rt_http_req_t *req, const char *name);
void set_request_body_from_string(rt_http_req_t *req, rt_string body);
rt_http_res_t *do_http_request(rt_http_req_t *req, int redirects_remaining);
int do_http_download_request(rt_http_req_t *req, int redirects_remaining, FILE *out);

/// @brief Create an internal HTTP connection pool for keep-alive reuse.
void *rt_http_conn_pool_new(int64_t max_size);

/// @brief Drop every idle connection from an internal HTTP connection pool.
void rt_http_conn_pool_clear(void *pool);

/// @brief Toggle keep-alive / pooled transport on a request.
void *rt_http_req_set_keep_alive(void *obj, int8_t keep_alive);

/// @brief Attach an internal HTTP connection pool to a request.
void *rt_http_req_set_connection_pool(void *obj, void *pool);

/// @brief Return true when a cross-origin redirect should drop the header.
int8_t rt_http_header_is_sensitive_for_cross_origin_redirect(const char *name);

/// @brief Return true when a comma-separated HTTP header contains @p token.
int8_t rt_http_header_value_has_token(const char *value, const char *token);

/// @brief Compare two absolute URLs for same-origin equality.
int8_t rt_http_url_has_same_origin(rt_string lhs, rt_string rhs);

/// @brief Resolve a redirect Location value against the current absolute URL.
rt_string rt_http_resolve_redirect_url(rt_string current_url, rt_string location);

#ifdef __cplusplus
}
#endif
