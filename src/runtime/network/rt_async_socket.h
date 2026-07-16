//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_async_socket.h
// Purpose: Future wrapper that runs blocking socket work on a shared worker pool.
// Key invariants:
//   - Async operations submit work to a thread pool and return Futures.
//   - Each Future resolves with the result of the blocking operation.
//   - One fixed four-worker pool is shared by all AsyncSocket operations.
// Ownership/Lifetime:
//   - Returned Futures are GC-managed.
// Links: rt_network.h (blocking sockets), rt_threadpool.h (thread pool)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Asynchronously connect to host:port. Returns a Future that resolves to a Tcp object.
/// @brief Configure the shared worker-pool size (before the first async call).
void rt_async_socket_set_pool_size(int64_t size);

void *rt_async_connect(rt_string host, int64_t port);

/// @brief Asynchronously connect to host:port with an explicit timeout.
void *rt_async_connect_for(rt_string host, int64_t port, int64_t timeout_ms);

/// @brief Submit a send; the generic Future currently stores its count as a raw ABI scalar.
void *rt_async_send(void *tcp, void *data);

/// @brief Asynchronously receive data. Returns a Future[Bytes].
void *rt_async_recv(void *tcp, int64_t max_bytes);

/// @brief Asynchronously perform HTTP GET. Returns a Future[String].
void *rt_async_http_get(rt_string url);

/// @brief Asynchronously perform HTTP POST. Returns a Future[String].
void *rt_async_http_post(rt_string url, rt_string body);

#ifdef __cplusplus
}
#endif
