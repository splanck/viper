//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_async_socket.c
// Purpose: Non-blocking socket wrapper that bridges blocking I/O with Futures.
// Key invariants:
//   - Each async operation creates a Future, submits blocking work to the
//     thread pool, and resolves the Future when the operation completes.
//   - Uses a shared default thread pool (lazy-initialized).
// Ownership/Lifetime:
//   - Returned Futures are GC-managed.
//   - Closure args are heap-allocated and freed by the worker.
// Links: rt_async_socket.h (API), rt_network.h (blocking sockets)
//
//===----------------------------------------------------------------------===//

#include "rt_async_socket.h"

#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

extern void rt_trap(const char *msg);

// Forward declarations for thread pool (from rt_threadpool.h)
extern void *rt_threadpool_new(int64_t size);
extern int8_t rt_threadpool_submit(void *pool, void *callback, void *arg);

// Forward declarations for promise/future (from rt_promise.h)
extern void *rt_promise_new(void);
extern void rt_promise_set(void *promise, void *value);
extern void rt_promise_set_error(void *promise, rt_string error);
extern void *rt_promise_get_future(void *promise);

//=============================================================================
// Default Thread Pool (lazy singleton)
//=============================================================================

static void *default_pool = NULL;
#ifdef _WIN32
static LONG pool_init_state = 0;
#else
static pthread_once_t pool_once = PTHREAD_ONCE_INIT;

static void init_default_pool(void) {
    default_pool = rt_threadpool_new(4);
}
#endif

static void *get_default_pool(void) {
#ifdef _WIN32
    if (pool_init_state == 2)
        return default_pool;
    LONG prev = InterlockedCompareExchange(&pool_init_state, 1, 0);
    if (prev == 2)
        return default_pool;
    if (prev == 1) {
        while (pool_init_state != 2)
            Sleep(0);
        return default_pool;
    }
    default_pool = rt_threadpool_new(4);
    InterlockedExchange(&pool_init_state, 2);
    return default_pool;
#else
    pthread_once(&pool_once, init_default_pool);
    return default_pool;
#endif
}

static void async_release_owned(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void async_fail_submit(void *promise, const char *message) {
    rt_string err = rt_string_from_bytes(message, strlen(message));
    rt_promise_set_error(promise, err);
    rt_string_unref(err);
    async_release_owned(promise);
}

//=============================================================================
// Async Connect
//=============================================================================

typedef struct {
    char *host;
    int64_t port;
    void *promise;
} connect_args_t;

static void async_connect_worker(void *arg) {
    connect_args_t *a = (connect_args_t *)arg;
    rt_string host = rt_string_from_bytes(a->host, strlen(a->host));
    void *tcp = rt_tcp_connect(host, a->port);
    rt_string_unref(host);
    rt_promise_set(a->promise, tcp);
    async_release_owned(a->promise);
    free(a->host);
    free(a);
}

/// @brief Async connect.
void *rt_async_connect(rt_string host, int64_t port) {
    const char *h = rt_string_cstr(host);
    if (!h)
        rt_trap("AsyncSocket: NULL host");

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    connect_args_t *args = (connect_args_t *)malloc(sizeof(connect_args_t));
    if (!args)
        rt_trap("AsyncSocket: OOM");
    args->host = strdup(h);
    args->port = port;
    args->promise = promise;
    if (!args->host) {
        free(args);
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }

    if (!rt_threadpool_submit(get_default_pool(), (void *)async_connect_worker, args)) {
        free(args->host);
        free(args);
        async_fail_submit(promise, "AsyncSocket: thread pool is shut down");
    }
    return future;
}

//=============================================================================
// Async Send
//=============================================================================

typedef struct {
    void *tcp;
    void *data;
    void *promise;
} send_args_t;

static void async_send_worker(void *arg) {
    send_args_t *a = (send_args_t *)arg;
    int64_t sent = rt_tcp_send(a->tcp, a->data);
    // Resolve with boxed integer (use pointer-sized value)
    rt_promise_set(a->promise, (void *)(intptr_t)sent);
    async_release_owned(a->data);
    async_release_owned(a->tcp);
    async_release_owned(a->promise);
    free(a);
}

/// @brief Async send.
void *rt_async_send(void *tcp, void *data) {
    if (!tcp || !data)
        rt_trap("AsyncSocket: NULL arg");

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    send_args_t *args = (send_args_t *)malloc(sizeof(send_args_t));
    if (!args)
        rt_trap("AsyncSocket: OOM");
    rt_obj_retain_maybe(tcp);
    rt_obj_retain_maybe(data);
    args->tcp = tcp;
    args->data = data;
    args->promise = promise;
    if (!rt_threadpool_submit(get_default_pool(), (void *)async_send_worker, args)) {
        async_release_owned(data);
        async_release_owned(tcp);
        free(args);
        async_fail_submit(promise, "AsyncSocket: thread pool is shut down");
    }
    return future;
}

//=============================================================================
// Async Recv
//=============================================================================

typedef struct {
    void *tcp;
    int64_t max_bytes;
    void *promise;
} recv_args_t;

static void async_recv_worker(void *arg) {
    recv_args_t *a = (recv_args_t *)arg;
    void *data = rt_tcp_recv(a->tcp, a->max_bytes);
    rt_promise_set(a->promise, data);
    async_release_owned(a->tcp);
    async_release_owned(a->promise);
    free(a);
}

/// @brief Async recv.
void *rt_async_recv(void *tcp, int64_t max_bytes) {
    if (!tcp)
        rt_trap("AsyncSocket: NULL tcp");

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    recv_args_t *args = (recv_args_t *)malloc(sizeof(recv_args_t));
    if (!args)
        rt_trap("AsyncSocket: OOM");
    rt_obj_retain_maybe(tcp);
    args->tcp = tcp;
    args->max_bytes = max_bytes;
    args->promise = promise;
    if (!rt_threadpool_submit(get_default_pool(), (void *)async_recv_worker, args)) {
        async_release_owned(tcp);
        free(args);
        async_fail_submit(promise, "AsyncSocket: thread pool is shut down");
    }
    return future;
}

//=============================================================================
// Async HTTP
//=============================================================================

typedef struct {
    char *url;
    char *body; // NULL for GET
    void *promise;
} http_args_t;

static void async_http_get_worker(void *arg) {
    http_args_t *a = (http_args_t *)arg;
    rt_string url = rt_string_from_bytes(a->url, strlen(a->url));
    rt_string result = rt_http_get(url);
    rt_string_unref(url);
    rt_promise_set(a->promise, (void *)result);
    async_release_owned(a->promise);
    free(a->url);
    free(a);
}

/// @brief Async http get.
void *rt_async_http_get(rt_string url) {
    const char *u = rt_string_cstr(url);
    if (!u)
        rt_trap("AsyncSocket: NULL URL");

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    http_args_t *args = (http_args_t *)malloc(sizeof(http_args_t));
    if (!args)
        rt_trap("AsyncSocket: OOM");
    args->url = strdup(u);
    args->body = NULL;
    args->promise = promise;
    if (!args->url) {
        free(args);
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }

    if (!rt_threadpool_submit(get_default_pool(), (void *)async_http_get_worker, args)) {
        free(args->url);
        free(args);
        async_fail_submit(promise, "AsyncSocket: thread pool is shut down");
    }
    return future;
}

static void async_http_post_worker(void *arg) {
    http_args_t *a = (http_args_t *)arg;
    rt_string url = rt_string_from_bytes(a->url, strlen(a->url));
    rt_string body =
        a->body ? rt_string_from_bytes(a->body, strlen(a->body)) : rt_string_from_bytes("", 0);
    rt_string result = rt_http_post(url, body);
    rt_string_unref(url);
    rt_string_unref(body);
    rt_promise_set(a->promise, (void *)result);
    async_release_owned(a->promise);
    free(a->url);
    free(a->body);
    free(a);
}

/// @brief Async http post.
void *rt_async_http_post(rt_string url, rt_string body) {
    const char *u = rt_string_cstr(url);
    const char *b = rt_string_cstr(body);
    if (!u)
        rt_trap("AsyncSocket: NULL URL");

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    http_args_t *args = (http_args_t *)malloc(sizeof(http_args_t));
    if (!args)
        rt_trap("AsyncSocket: OOM");
    args->url = strdup(u);
    args->body = b ? strdup(b) : NULL;
    args->promise = promise;
    if (!args->url || (b && !args->body)) {
        free(args->url);
        free(args->body);
        free(args);
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }

    if (!rt_threadpool_submit(get_default_pool(), (void *)async_http_post_worker, args)) {
        free(args->url);
        free(args->body);
        free(args);
        async_fail_submit(promise, "AsyncSocket: thread pool is shut down");
    }
    return future;
}
