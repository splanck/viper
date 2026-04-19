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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
