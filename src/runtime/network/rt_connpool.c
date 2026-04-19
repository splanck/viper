//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_connpool.c
// Purpose: Thread-safe TCP connection pooling for HTTP keep-alive.
// Key invariants:
//   - Connections keyed by "host:port" string.
//   - Internal mutex protects all pool operations.
//   - Idle connections closed after 60 seconds.
// Ownership/Lifetime:
//   - Pool objects are GC-managed. Finalizer closes all connections.
// Links: rt_connpool.h (API)
//
//===----------------------------------------------------------------------===//

#include "rt_connpool.h"

#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SOCKET conn_socket_t;
#define INVALID_CONN_SOCK INVALID_SOCKET
typedef CRITICAL_SECTION pool_mutex_t;
#define POOL_MUTEX_INIT(m) InitializeCriticalSection(m)
#define POOL_MUTEX_LOCK(m) EnterCriticalSection(m)
#define POOL_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define POOL_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
typedef int conn_socket_t;
#define INVALID_CONN_SOCK (-1)
typedef pthread_mutex_t pool_mutex_t;
#define POOL_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define POOL_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define POOL_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define POOL_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

#include "rt_trap.h"
extern conn_socket_t rt_tcp_socket_fd(void *obj);
extern int wait_socket(conn_socket_t sock, int timeout_ms, bool for_write);

//=============================================================================
// Internal Structures
//=============================================================================

#define POOL_MAX_ENTRIES 128
#define POOL_IDLE_TIMEOUT_SEC 60

typedef struct {
    void *tcp;        // TCP connection object
    char *key;        // "host:port" key
    time_t last_used; // When connection was returned to pool
    bool in_use;      // Currently checked out
} pooled_entry_t;

typedef struct {
    pooled_entry_t entries[POOL_MAX_ENTRIES];
    int count;
    int max_size;
    pool_mutex_t lock;
    bool lock_initialized;
} rt_connpool_impl;

//=============================================================================
// Helpers
//=============================================================================

static void make_key(const char *host, int port, char *buf, size_t buf_len) {
    if (host && strchr(host, ':') != NULL && host[0] != '[')
        snprintf(buf, buf_len, "[%s]:%d", host, port);
    else
        snprintf(buf, buf_len, "%s:%d", host ? host : "", port);
}

static void close_entry(pooled_entry_t *entry) {
    if (entry->tcp) {
        rt_tcp_close(entry->tcp);
        if (rt_obj_release_check0(entry->tcp))
            rt_obj_free(entry->tcp);
        entry->tcp = NULL;
    }
    free(entry->key);
    entry->key = NULL;
    entry->in_use = false;
}

/// @brief Probe a pooled TCP connection to see if the peer has silently closed it.
/// @details A connection sitting idle in the pool can be closed by the peer
///          (server timeout, NAT tear-down, etc.) without our side noticing
///          until the next write fails. Detecting that *before* handing the
///          connection to a caller avoids spurious request failures.
///
///          The probe uses a non-blocking `select` + `MSG_PEEK` to look at
///          whatever bytes (if any) are sitting in the receive buffer:
///          - `wait_socket(..., 0, ...)` with a zero timeout returns >0 if
///            the socket has readable data (peer wrote, *or* peer closed —
///            both produce a readable signal).
///          - If nothing's readable, the connection is healthy (idle but
///            still open).
///          - If something *is* readable, peek one byte without consuming it:
///            * `peek > 0` — peer wrote real bytes (unexpected on an idle
///              pooled connection but technically the connection is alive).
///            * `peek == 0` — orderly close from the peer; the connection
///              is dead.
///            * `peek < 0` with EAGAIN/EWOULDBLOCK — race between select
///              and peek; treat as healthy.
///            * `peek < 0` with any other errno — connection is broken.
/// @param tcp Pooled TCP connection object.
/// @return 1 if the connection is healthy and reusable, 0 if it should be
///         closed and removed from the pool.
static int tcp_connection_healthy(void *tcp) {
    if (!tcp || !rt_tcp_is_open(tcp))
        return 0;

    conn_socket_t sock = rt_tcp_socket_fd(tcp);
    if (sock == INVALID_CONN_SOCK)
        return 0;

    int ready = wait_socket(sock, 0, false);
    if (ready <= 0)
        return 1;

    uint8_t byte = 0;
    int peeked = recv(sock, (char *)&byte, 1, MSG_PEEK);
    if (peeked > 0)
        return 1;
    if (peeked == 0)
        return 0;
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK ? 1 : 0;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK ? 1 : 0;
#endif
}

//=============================================================================
// Finalizer
//=============================================================================

/// @brief GC finalizer: close every pooled TCP connection (releasing each entry's key + tcp ref)
/// and destroy the pool's mutex.
static void rt_connpool_finalize(void *obj) {
    if (!obj)
        return;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;
    for (int i = 0; i < pool->count; i++)
        close_entry(&pool->entries[i]);
    if (pool->lock_initialized)
        POOL_MUTEX_DESTROY(&pool->lock);
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Construct a TCP connection pool. `max_size` is clamped to `[1, POOL_MAX_ENTRIES]`.
/// Pooled connections are keyed by `host:port`. Returns a GC-managed handle.
void *rt_connpool_new(int64_t max_size) {
    rt_connpool_impl *pool =
        (rt_connpool_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_connpool_impl));
    if (!pool)
        rt_trap("ConnectionPool: memory allocation failed");
    memset(pool, 0, sizeof(*pool));
    pool->max_size =
        (int)(max_size > 0 && max_size < POOL_MAX_ENTRIES ? max_size : POOL_MAX_ENTRIES);
    POOL_MUTEX_INIT(&pool->lock);
    pool->lock_initialized = true;
    rt_obj_set_finalizer(pool, rt_connpool_finalize);
    return pool;
}

/// @brief Acquire a TCP connection to `(host, port)`. Walk-the-pool flow:
///   1. Evict expired idle entries (`POOL_IDLE_TIMEOUT_SEC` since last_used).
///   2. Find an idle entry whose key matches; if its TCP is still healthy, reuse it.
///   3. If unhealthy, close + remove and keep searching.
///   4. If nothing matches, open a fresh TCP connection.
/// All steps run under the pool mutex except the actual `rt_tcp_connect` (which can block).
void *rt_connpool_acquire(void *obj, rt_string host, int64_t port) {
    if (!obj)
        rt_trap("ConnectionPool: NULL pool");

    rt_connpool_impl *pool = (rt_connpool_impl *)obj;
    const char *host_str = rt_string_cstr(host);
    if (!host_str || port < 1 || port > 65535)
        rt_trap("ConnectionPool: invalid host or port");

    char key[300];
    make_key(host_str, (int)port, key, sizeof(key));

    POOL_MUTEX_LOCK(&pool->lock);

    // Evict expired idle connections
    time_t now = time(NULL);
    for (int i = 0; i < pool->count; i++) {
        if (!pool->entries[i].in_use &&
            difftime(now, pool->entries[i].last_used) > POOL_IDLE_TIMEOUT_SEC) {
            close_entry(&pool->entries[i]);
            // Swap with last
            pool->entries[i] = pool->entries[pool->count - 1];
            memset(&pool->entries[pool->count - 1], 0, sizeof(pooled_entry_t));
            pool->count--;
            i--;
        }
    }

    // Look for an idle connection with matching key
    for (int i = 0; i < pool->count; i++) {
        if (!pool->entries[i].in_use && pool->entries[i].key &&
            strcmp(pool->entries[i].key, key) == 0) {
            // Check if still open
            if (tcp_connection_healthy(pool->entries[i].tcp)) {
                pool->entries[i].in_use = true;
                void *tcp = pool->entries[i].tcp;
                POOL_MUTEX_UNLOCK(&pool->lock);
                return tcp;
            } else {
                // Connection died; remove it
                close_entry(&pool->entries[i]);
                pool->entries[i] = pool->entries[pool->count - 1];
                memset(&pool->entries[pool->count - 1], 0, sizeof(pooled_entry_t));
                pool->count--;
                i--;
            }
        }
    }

    POOL_MUTEX_UNLOCK(&pool->lock);

    // No pooled connection available; create new one
    void *tcp = rt_tcp_connect(host, port);
    return tcp;
}

/// @brief Release the connpool.
void rt_connpool_release(void *obj, void *conn) {
    if (!obj || !conn)
        return;

    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    if (!tcp_connection_healthy(conn)) {
        rt_tcp_close(conn);
        if (rt_obj_release_check0(conn))
            rt_obj_free(conn);
        return;
    }

    POOL_MUTEX_LOCK(&pool->lock);

    // Find the entry for this connection (if it was tracked)
    for (int i = 0; i < pool->count; i++) {
        if (pool->entries[i].tcp == conn) {
            pool->entries[i].in_use = false;
            pool->entries[i].last_used = time(NULL);
            POOL_MUTEX_UNLOCK(&pool->lock);
            return;
        }
    }

    // Not tracked — add it if there's space
    if (pool->count < pool->max_size) {
        pooled_entry_t *entry = &pool->entries[pool->count++];
        entry->tcp = conn;
        // Build key from connection properties
        rt_string h = rt_tcp_host(conn);
        int64_t p = rt_tcp_port(conn);
        char key[300];
        make_key(rt_string_cstr(h), (int)p, key, sizeof(key));
        entry->key = strdup(key);
        entry->last_used = time(NULL);
        entry->in_use = false;
        POOL_MUTEX_UNLOCK(&pool->lock);
        return;
    }

    POOL_MUTEX_UNLOCK(&pool->lock);

    // Pool full — close the connection
    rt_tcp_close(conn);
    if (rt_obj_release_check0(conn))
        rt_obj_free(conn);
}

/// @brief Remove all entries from the connpool.
void rt_connpool_clear(void *obj) {
    if (!obj)
        return;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    for (int i = 0; i < pool->count; i++)
        close_entry(&pool->entries[i]);
    pool->count = 0;
    POOL_MUTEX_UNLOCK(&pool->lock);
}

/// @brief Return the size of the connpool.
int64_t rt_connpool_size(void *obj) {
    if (!obj)
        return 0;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    int64_t n = pool->count;
    POOL_MUTEX_UNLOCK(&pool->lock);
    return n;
}

/// @brief Available the connpool.
int64_t rt_connpool_available(void *obj) {
    if (!obj)
        return 0;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    int64_t n = 0;
    for (int i = 0; i < pool->count; i++) {
        if (!pool->entries[i].in_use)
            n++;
    }
    POOL_MUTEX_UNLOCK(&pool->lock);
    return n;
}
