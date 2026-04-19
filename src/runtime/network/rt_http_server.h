//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_server.h
// Purpose: Threaded HTTP/1.1 server with routing and request/response objects.
// Key invariants:
//   - Each incoming connection handled in a thread pool worker.
//   - Routes are matched via HttpRouter; unmatched routes return 404.
//   - HTTP/1.0 and HTTP/1.1 request framing / keep-alive semantics are enforced.
// Ownership/Lifetime:
//   - Server objects are GC-managed via rt_obj_set_finalizer.
//   - ServerReq/ServerRes are created per-request and GC-managed.
// Links: rt_http_router.h (routing), rt_network.h (TCP)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// HttpServer
//=========================================================================

/// @brief Create a new HTTP server on the given port.
void *rt_http_server_new(int64_t port);

/// @brief Add a GET route with a handler tag.
void rt_http_server_get(void *server, rt_string pattern, rt_string handler_tag);

/// @brief Add a POST route.
void rt_http_server_post(void *server, rt_string pattern, rt_string handler_tag);

/// @brief Add a PUT route.
void rt_http_server_put(void *server, rt_string pattern, rt_string handler_tag);

/// @brief Add a DELETE route.
void rt_http_server_del(void *server, rt_string pattern, rt_string handler_tag);

/// @brief Native handler signature used by bound route callbacks.
typedef void (*rt_http_server_handler_fn)(void *req, void *res);

/// @brief Dispatch signature used for VM/bytecode-backed handlers.
typedef void (*rt_http_server_handler_dispatch_fn)(void *ctx, void *req, void *res);

/// @brief Optional cleanup callback for a dispatch context.
typedef void (*rt_http_server_handler_cleanup_fn)(void *ctx);

/// @brief Bind a handler tag to a native function pointer.
void rt_http_server_bind_handler(void *server, rt_string handler_tag, void *entry);

/// @brief Bind a handler tag to a dispatcher with persistent context.
void rt_http_server_bind_handler_dispatch(
    void *server, rt_string handler_tag, void *dispatch, void *ctx, void *cleanup);

/// @brief Start accepting connections (blocks on accept loop in background thread).
void rt_http_server_start(void *server);

/// @brief Stop the server gracefully.
void rt_http_server_stop(void *server);

/// @brief Get the listening port.
int64_t rt_http_server_port(void *server);

/// @brief Check if server is running.
int8_t rt_http_server_is_running(void *server);

/// @brief Process one request synchronously (for testing).
void *rt_http_server_process_request(void *server, rt_string raw_request);

//=========================================================================
// ServerReq — Request object for handlers
//=========================================================================

rt_string rt_server_req_method(void *req);
rt_string rt_server_req_path(void *req);
rt_string rt_server_req_body(void *req);
rt_string rt_server_req_header(void *req, rt_string name);
rt_string rt_server_req_param(void *req, rt_string name);
rt_string rt_server_req_query(void *req, rt_string name);

//=========================================================================
// ServerRes — Response object for handlers
//=========================================================================

void *rt_server_res_status(void *res, int64_t code);
void *rt_server_res_header(void *res, rt_string name, rt_string value);
void rt_server_res_send(void *res, rt_string body);
void rt_server_res_json(void *res, rt_string json_str);

#ifdef __cplusplus
}
#endif
