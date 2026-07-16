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
//   - Pool objects are GC-managed. Tracked entries hold their own retained
//     reference; Acquire returns a caller-owned (retained) handle.
//   - Clear/finalize close idle entries only; checked-out handles are
//     detached, never closed out from under their holder.
// Links: rt_connpool.h (API)
//
//===----------------------------------------------------------------------===//

#include "rt_connpool.h"

#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_time.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define POOL_IDLE_TIMEOUT_MS (60LL * 1000LL)

typedef struct {
    void *tcp;            // TCP connection object
    char *key;            // "host:port" key
    int64_t last_used_ms; // Monotonic ms tick when connection was returned
    bool in_use;          // Currently checked out
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

static int connpool_has_embedded_nul(const char *data, size_t len) {
    return data && memchr(data, '\0', len) != NULL;
}

static size_t connpool_host_len_or_zero(rt_string host) {
    int64_t len = host ? rt_str_len(host) : -1;
    if (len <= 0 || (uint64_t)len > (uint64_t)SIZE_MAX)
        return 0;
    return (size_t)len;
}

/// @brief Read the monotonic millisecond clock used for idle-age bookkeeping.
/// @details Connection reuse decisions must not depend on wall-clock time:
///          NTP adjustments, daylight-saving changes, or manual clock edits
///          should not make idle sockets live forever or expire immediately.
/// @return Monotonic milliseconds from the runtime clock's unspecified epoch.
static int64_t connpool_now_ms(void) {
    return rt_clock_ticks_us() / 1000;
}

/// @brief Return whether an idle entry has exceeded the pool timeout.
/// @details Backward clock deltas are clamped to zero defensively even though
///          the source is monotonic.
/// @param entry Entry being considered for eviction.
/// @param now_ms Current monotonic tick in milliseconds.
/// @return True when the entry is idle long enough to close.
static bool connpool_entry_is_idle_expired(const pooled_entry_t *entry, int64_t now_ms) {
    if (!entry || entry->in_use)
        return false;
    int64_t age_ms = now_ms >= entry->last_used_ms ? now_ms - entry->last_used_ms : 0;
    return age_ms > POOL_IDLE_TIMEOUT_MS;
}

/// @brief Format a "host:port" cache key, bracketing bare IPv6 literals.
/// @details An unbracketed colon in @p host indicates an IPv6 address that
///          would otherwise collide with the port colon, so we wrap it as
///          @c [::1]:443. Already-bracketed hosts and plain hostnames flow
///          through unchanged.
static bool make_key(const char *host, int port, char *buf, size_t buf_len) {
    int written;
    if (!host || port < 1 || port > 65535 || !buf || buf_len == 0)
        return false;
    if (strchr(host, ':') != NULL && host[0] != '[')
        written = snprintf(buf, buf_len, "[%s]:%d", host, port);
    else
        written = snprintf(buf, buf_len, "%s:%d", host, port);
    return written >= 0 && (size_t)written < buf_len;
}

/// @brief Close a TCP handle and drop the pool's reference.
/// @details Used in every code path where the pool is shedding a connection
///          (eviction, health-drop, capacity overflow). The @c rt_tcp_close
///          call is idempotent so an already-closed handle is fine.
static void close_tcp_connection(void *tcp) {
    if (!tcp)
        return;
    rt_tcp_close(tcp);
    if (rt_obj_release_check0(tcp))
        rt_obj_free(tcp);
}

/// @brief Tear down a pooled entry's TCP handle and free its key.
/// @details Leaves the entry struct itself zeroed so the slot can be
///          repurposed by the next `track_connection` call or the
///          subsequent compaction in @ref remove_entry_at.
static void close_entry(pooled_entry_t *entry) {
    if (entry->tcp) {
        close_tcp_connection(entry->tcp);
        entry->tcp = NULL;
    }
    free(entry->key);
    entry->key = NULL;
    entry->in_use = false;
}

/// @brief Remove the entry at @p index using swap-with-last for O(1) deletion.
/// @details Order within the @c entries array is not meaningful — slots are
///          looked up linearly by `(key, in_use)` — so the cheap swap is
///          preferred over a memmove. The vacated tail slot is zeroed.
static void remove_entry_at(rt_connpool_impl *pool, int index) {
    if (!pool || index < 0 || index >= pool->count)
        return;
    close_entry(&pool->entries[index]);
    pool->entries[index] = pool->entries[pool->count - 1];
    memset(&pool->entries[pool->count - 1], 0, sizeof(pooled_entry_t));
    pool->count--;
}

/// @brief Insert a new entry recording @p tcp under @p key.
/// @details Used by both `acquire` (when opening a fresh connection that
///          should immediately appear as checked-out) and `release` (when
///          a caller returns an unknown connection that the pool may
///          want to keep). The pool RETAINS its own reference on success,
///          so a tracked entry stays valid even after every caller drops
///          theirs. Refuses to grow past @c max_size and copies @p key
///          into heap storage so the caller's buffer can be
///          stack-allocated.
/// @return True when the entry was appended; false on capacity or OOM.
static bool track_connection(
    rt_connpool_impl *pool, void *tcp, const char *key, bool in_use, int64_t last_used_ms) {
    if (!pool || !tcp || !key || pool->count >= pool->max_size)
        return false;

    char *key_copy = strdup(key);
    if (!key_copy)
        return false;

    pooled_entry_t *entry = &pool->entries[pool->count++];
    memset(entry, 0, sizeof(*entry));
    rt_obj_retain_maybe(tcp); // pool's own reference, dropped when untracked
    entry->tcp = tcp;
    entry->key = key_copy;
    entry->last_used_ms = last_used_ms;
    entry->in_use = in_use;
    return true;
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
///            * `peek > 0` — stale protocol bytes are queued; reusing the
///              socket would cross-contaminate the next request, so it is
///              rejected and closed.
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
        return 0; // pending unread bytes: stale protocol data, do not reuse
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

static void detach_entry(pooled_entry_t *entry);

/// @brief GC finalizer: close idle pooled connections, detach checked-out
/// ones (their holders keep working handles), and destroy the pool's mutex.
static void rt_connpool_finalize(void *obj) {
    if (!obj)
        return;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;
    for (int i = 0; i < pool->count; i++) {
        if (pool->entries[i].in_use)
            detach_entry(&pool->entries[i]);
        else
            close_entry(&pool->entries[i]);
    }
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
    if (!pool) {
        rt_trap("ConnectionPool: memory allocation failed");
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));
    if (max_size < 1)
        max_size = 1;
    if (max_size > POOL_MAX_ENTRIES)
        max_size = POOL_MAX_ENTRIES;
    pool->max_size = (int)max_size;
    POOL_MUTEX_INIT(&pool->lock);
    pool->lock_initialized = true;
    rt_obj_set_finalizer(pool, rt_connpool_finalize);
    return pool;
}

/// @brief Acquire a TCP connection to `(host, port)`. Walk-the-pool flow:
///   1. Evict expired idle entries (`POOL_IDLE_TIMEOUT_MS` since last_used_ms).
///   2. Find an idle entry whose key matches; if its TCP is still healthy, reuse it.
///   3. If unhealthy, close + remove and keep searching.
///   4. If nothing matches, open a fresh TCP connection and immediately track
///      it as checked out when pool capacity allows.
/// All steps run under the pool mutex except the actual `rt_tcp_connect` (which can block).
void *rt_connpool_acquire(void *obj, rt_string host, int64_t port) {
    if (!obj) {
        rt_trap("ConnectionPool: NULL pool");
        return NULL;
    }

    rt_connpool_impl *pool = (rt_connpool_impl *)obj;
    const char *host_str = rt_string_cstr(host);
    size_t host_len = connpool_host_len_or_zero(host);
    if (!host_str || host_len == 0 || connpool_has_embedded_nul(host_str, host_len) || port < 1 ||
        port > 65535) {
        rt_trap("ConnectionPool: invalid host or port");
        return NULL;
    }

    char key[300];
    if (!make_key(host_str, (int)port, key, sizeof(key))) {
        rt_trap("ConnectionPool: host is too long");
        return NULL;
    }

    POOL_MUTEX_LOCK(&pool->lock);

    // Evict expired idle connections
    int64_t now_ms = connpool_now_ms();
    for (int i = 0; i < pool->count; i++) {
        if (connpool_entry_is_idle_expired(&pool->entries[i], now_ms)) {
            remove_entry_at(pool, i);
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
                rt_obj_retain_maybe(tcp); // caller's owned reference
                POOL_MUTEX_UNLOCK(&pool->lock);
                return tcp;
            } else {
                // Connection died; remove it
                remove_entry_at(pool, i);
                i--;
            }
        }
    }

    POOL_MUTEX_UNLOCK(&pool->lock);

    // No pooled connection available; create new one
    void *tcp = rt_tcp_connect(host, port);
    POOL_MUTEX_LOCK(&pool->lock);
    (void)track_connection(pool, tcp, key, true, 0);
    POOL_MUTEX_UNLOCK(&pool->lock);
    return tcp;
}

/// @brief Return a single connection to the pool, or close it.
/// @details Locates the entry by pointer identity under the pool mutex.
///          - Tracked + healthy: cleared `in_use` and stamped with the
///            monotonic clock so the idle-timeout sweep can later reclaim it.
///          - Tracked + unhealthy: removed via @ref remove_entry_at so
///            the slot doesn't keep a dead fd around.
///          - Untracked + healthy: adopted into a free slot if any.
///          - Untracked + unhealthy or pool full: closed and dropped.
void rt_connpool_release(void *obj, void *conn) {
    if (!obj || !conn)
        return;

    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);

    // Find the entry for this connection (if it was tracked)
    for (int i = 0; i < pool->count; i++) {
        if (pool->entries[i].tcp == conn) {
            if (!tcp_connection_healthy(conn)) {
                remove_entry_at(pool, i);
                POOL_MUTEX_UNLOCK(&pool->lock);
                return;
            }
            pool->entries[i].in_use = false;
            pool->entries[i].last_used_ms = connpool_now_ms();
            POOL_MUTEX_UNLOCK(&pool->lock);
            return;
        }
    }

    if (!tcp_connection_healthy(conn)) {
        POOL_MUTEX_UNLOCK(&pool->lock);
        // Untracked handle: the pool holds no reference of its own, so only
        // close the transport — the caller's (borrowed) reference stays theirs.
        rt_tcp_close(conn);
        return;
    }

    // Not tracked — add it if there's space
    if (pool->count < pool->max_size) {
        // Build key from connection properties
        rt_string h = rt_tcp_host(conn);
        int64_t p = rt_tcp_port(conn);
        char key[300];
        const char *host_str = rt_string_cstr(h);
        size_t host_len = connpool_host_len_or_zero(h);
        bool valid_key = host_str && host_len > 0 &&
                         !connpool_has_embedded_nul(host_str, host_len) && p >= 1 && p <= 65535 &&
                         make_key(host_str, (int)p, key, sizeof(key));
        if (valid_key && track_connection(pool, conn, key, false, connpool_now_ms())) {
            rt_string_unref(h);
            POOL_MUTEX_UNLOCK(&pool->lock);
            return;
        }
        rt_string_unref(h);
    }

    POOL_MUTEX_UNLOCK(&pool->lock);

    // Pool full or bookkeeping allocation failed — close the transport but
    // leave the caller's borrowed reference untouched.
    rt_tcp_close(conn);
}

/// @brief Detach an entry without closing its transport: free the key and
///        drop the pool's reference, leaving the connection usable by
///        whoever checked it out.
static void detach_entry(pooled_entry_t *entry) {
    if (entry->tcp) {
        if (rt_obj_release_check0(entry->tcp))
            rt_obj_free(entry->tcp);
        entry->tcp = NULL;
    }
    free(entry->key);
    entry->key = NULL;
    entry->in_use = false;
}

/// @brief Drop every pooled entry and reset the entry count to zero.
/// @details Holds the pool mutex across the entire sweep so a concurrent
///          `acquire`/`release` cannot observe a partially-cleared pool.
///          Idle entries are closed; CHECKED-OUT entries are detached
///          without closing so a handle currently held by an `Acquire`
///          caller keeps working — it simply is no longer tracked (a later
///          `Release` re-adopts or closes it).
void rt_connpool_clear(void *obj) {
    if (!obj)
        return;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    for (int i = 0; i < pool->count; i++) {
        if (pool->entries[i].in_use)
            detach_entry(&pool->entries[i]);
        else
            close_entry(&pool->entries[i]);
    }
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

/// @brief Count entries that are tracked but not currently checked out.
/// @details Useful for sizing decisions — when this returns 0 the next
///          `acquire` will either reuse an in-use entry (impossible — the
///          pool only reuses idle entries) or open a fresh connection.
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
