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

#ifdef __cplusplus
}
#endif
