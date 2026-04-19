//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_connpool.h
// Purpose: Thread-safe plain-TCP connection pooling for HTTP keep-alive.
// Key invariants:
//   - Connections are keyed by host:port.
//   - Pool is thread-safe (internal mutex).
//   - Idle connections are evicted after max_idle_sec.
// Ownership/Lifetime:
//   - Pool objects are GC-managed via rt_obj_set_finalizer.
// Links: rt_network.h (TCP)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new connection pool.
/// @param max_size Maximum number of pooled connections.
void *rt_connpool_new(int64_t max_size);

/// @brief Acquire a plain TCP connection from the pool (or create new).
void *rt_connpool_acquire(void *pool, rt_string host, int64_t port);

/// @brief Return a connection to the pool for reuse.
void rt_connpool_release(void *pool, void *conn);

/// @brief Close and remove all pooled connections.
void rt_connpool_clear(void *pool);

/// @brief Get number of connections currently in the pool.
int64_t rt_connpool_size(void *pool);

/// @brief Get number of idle (available) connections.
int64_t rt_connpool_available(void *pool);

#ifdef __cplusplus
}
#endif
