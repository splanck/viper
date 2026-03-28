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

void *rt_http_client_new(void);
void *rt_http_client_get(void *client, rt_string url);
void *rt_http_client_post(void *client, rt_string url, rt_string body);
void *rt_http_client_put(void *client, rt_string url, rt_string body);
void *rt_http_client_delete(void *client, rt_string url);
void rt_http_client_set_header(void *client, rt_string name, rt_string value);
void rt_http_client_set_timeout(void *client, int64_t timeout_ms);
void rt_http_client_set_max_redirects(void *client, int64_t max);
int8_t rt_http_client_get_follow_redirects(void *client);
void rt_http_client_set_follow_redirects(void *client, int8_t follow);
void rt_http_client_set_cookie(void *client, rt_string domain, rt_string name, rt_string value);
void *rt_http_client_get_cookies(void *client, rt_string domain);

#ifdef __cplusplus
}
#endif
