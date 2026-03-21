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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION pool_mutex_t;
#define POOL_MUTEX_INIT(m) InitializeCriticalSection(m)
#define POOL_MUTEX_LOCK(m) EnterCriticalSection(m)
#define POOL_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define POOL_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t pool_mutex_t;
#define POOL_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define POOL_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define POOL_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define POOL_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structures
//=============================================================================

#define POOL_MAX_ENTRIES 128
#define POOL_IDLE_TIMEOUT_SEC 60

typedef struct
{
    void *tcp;          // TCP connection object
    char *key;          // "host:port" key
    time_t last_used;   // When connection was returned to pool
    bool in_use;        // Currently checked out
} pooled_entry_t;

typedef struct
{
    pooled_entry_t entries[POOL_MAX_ENTRIES];
    int count;
    int max_size;
    pool_mutex_t lock;
    bool lock_initialized;
} rt_connpool_impl;

//=============================================================================
// Helpers
//=============================================================================

static void make_key(const char *host, int port, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s:%d", host, port);
}

static void close_entry(pooled_entry_t *entry)
{
    if (entry->tcp)
    {
        rt_tcp_close(entry->tcp);
        entry->tcp = NULL;
    }
    free(entry->key);
    entry->key = NULL;
    entry->in_use = false;
}

//=============================================================================
// Finalizer
//=============================================================================

static void rt_connpool_finalize(void *obj)
{
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

void *rt_connpool_new(int64_t max_size)
{
    rt_connpool_impl *pool =
        (rt_connpool_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_connpool_impl));
    if (!pool)
        rt_trap("ConnectionPool: memory allocation failed");
    memset(pool, 0, sizeof(*pool));
    pool->max_size = (int)(max_size > 0 && max_size < POOL_MAX_ENTRIES ? max_size : POOL_MAX_ENTRIES);
    POOL_MUTEX_INIT(&pool->lock);
    pool->lock_initialized = true;
    rt_obj_set_finalizer(pool, rt_connpool_finalize);
    return pool;
}

void *rt_connpool_acquire(void *obj, rt_string host, int64_t port)
{
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
    for (int i = 0; i < pool->count; i++)
    {
        if (!pool->entries[i].in_use &&
            difftime(now, pool->entries[i].last_used) > POOL_IDLE_TIMEOUT_SEC)
        {
            close_entry(&pool->entries[i]);
            // Swap with last
            pool->entries[i] = pool->entries[pool->count - 1];
            memset(&pool->entries[pool->count - 1], 0, sizeof(pooled_entry_t));
            pool->count--;
            i--;
        }
    }

    // Look for an idle connection with matching key
    for (int i = 0; i < pool->count; i++)
    {
        if (!pool->entries[i].in_use && pool->entries[i].key &&
            strcmp(pool->entries[i].key, key) == 0)
        {
            // Check if still open
            if (rt_tcp_is_open(pool->entries[i].tcp))
            {
                pool->entries[i].in_use = true;
                void *tcp = pool->entries[i].tcp;
                POOL_MUTEX_UNLOCK(&pool->lock);
                return tcp;
            }
            else
            {
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

void rt_connpool_release(void *obj, void *conn)
{
    if (!obj || !conn)
        return;

    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    if (!rt_tcp_is_open(conn))
    {
        rt_tcp_close(conn);
        return;
    }

    POOL_MUTEX_LOCK(&pool->lock);

    // Find the entry for this connection (if it was tracked)
    for (int i = 0; i < pool->count; i++)
    {
        if (pool->entries[i].tcp == conn)
        {
            pool->entries[i].in_use = false;
            pool->entries[i].last_used = time(NULL);
            POOL_MUTEX_UNLOCK(&pool->lock);
            return;
        }
    }

    // Not tracked — add it if there's space
    if (pool->count < pool->max_size)
    {
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
}

void rt_connpool_clear(void *obj)
{
    if (!obj)
        return;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    for (int i = 0; i < pool->count; i++)
        close_entry(&pool->entries[i]);
    pool->count = 0;
    POOL_MUTEX_UNLOCK(&pool->lock);
}

int64_t rt_connpool_size(void *obj)
{
    if (!obj)
        return 0;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    int64_t n = pool->count;
    POOL_MUTEX_UNLOCK(&pool->lock);
    return n;
}

int64_t rt_connpool_available(void *obj)
{
    if (!obj)
        return 0;
    rt_connpool_impl *pool = (rt_connpool_impl *)obj;

    POOL_MUTEX_LOCK(&pool->lock);
    int64_t n = 0;
    for (int i = 0; i < pool->count; i++)
    {
        if (!pool->entries[i].in_use)
            n++;
    }
    POOL_MUTEX_UNLOCK(&pool->lock);
    return n;
}
