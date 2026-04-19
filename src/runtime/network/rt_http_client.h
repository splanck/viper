//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_client.h
// Purpose: Session-based HTTP client with cookie jar and auto-redirect.
// Key invariants:
//   - Cookies persist across requests to the same domain.
//   - Redirects followed automatically (configurable max).
//   - Default headers applied to every request.
// Ownership/Lifetime:
//   - Client objects are GC-managed.
// Links: rt_network_http.c (underlying HTTP)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new HTTP client session with empty cookie jar and default settings.
void *rt_http_client_new(void);
/// @brief Issue a GET request and return the HttpResponse handle.
void *rt_http_client_get(void *client, rt_string url);
/// @brief Issue a POST with the given body (Content-Type defaults to application/octet-stream).
void *rt_http_client_post(void *client, rt_string url, rt_string body);
/// @brief Issue a PUT with the given body.
void *rt_http_client_put(void *client, rt_string url, rt_string body);
/// @brief Issue a DELETE request.
void *rt_http_client_delete(void *client, rt_string url);
/// @brief Set a default header sent on every request from this client.
void rt_http_client_set_header(void *client, rt_string name, rt_string value);
/// @brief Set the per-request timeout in milliseconds (0 = no timeout).
void rt_http_client_set_timeout(void *client, int64_t timeout_ms);
/// @brief True if the client reuses keep-alive connections.
int8_t rt_http_client_get_keep_alive(void *client);
/// @brief Enable or disable keep-alive connection reuse.
void rt_http_client_set_keep_alive(void *client, int8_t keep_alive);
/// @brief Resize the internal keep-alive connection pool.
void rt_http_client_set_pool_size(void *client, int64_t max_size);
/// @brief Cap the number of automatic redirects followed (default 5; 0 disables).
void rt_http_client_set_max_redirects(void *client, int64_t max);
/// @brief True if the client follows 3xx redirects automatically.
int8_t rt_http_client_get_follow_redirects(void *client);
/// @brief Toggle automatic 3xx redirect following.
void rt_http_client_set_follow_redirects(void *client, int8_t follow);
/// @brief Manually inject a cookie into the jar for @p domain.
void rt_http_client_set_cookie(void *client, rt_string domain, rt_string name, rt_string value);
/// @brief Get all cookies the jar holds for @p domain (returns a Map[string, string]).
void *rt_http_client_get_cookies(void *client, rt_string domain);

#ifdef __cplusplus
}
#endif
