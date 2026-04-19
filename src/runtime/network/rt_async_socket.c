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

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "rt_trap.h"

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

/// @brief POSIX `pthread_once` initializer for the shared 4-thread pool.
static void init_default_pool(void) {
    default_pool = rt_threadpool_new(4);
}
#endif

/// @brief Lazy-initialize and return the shared async-socket thread pool. POSIX uses
/// `pthread_once`; Win32 uses a 3-state Interlocked flag (0=uninit, 1=initializing, 2=ready)
/// with a busy-wait while another thread is initializing. Pool is 4 threads — enough for
/// concurrent socket operations without overwhelming the system on small machines.
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

/// @brief Drop one reference; free if it was the last. Used in worker cleanup and error paths.
static void async_release_owned(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Resolve `promise` as Err with `message` and release the promise reference.
/// Used on the unhappy path when the thread pool refuses the work item.
static void async_fail_submit(void *promise, const char *message) {
    rt_string err = rt_string_from_bytes(message, strlen(message));
    rt_promise_set_error(promise, err);
    rt_string_unref(err);
    async_release_owned(promise);
}

static void async_promise_error_copy(void *promise, const char *msg, const char *fallback) {
    const char *text = msg ? msg : fallback;
    if (!text) {
        rt_promise_set_error(promise, rt_const_cstr("Unknown error"));
        return;
    }
    rt_string copy = rt_string_from_bytes(text, strlen(text));
    rt_promise_set_error(promise, copy);
    rt_string_unref(copy);
}

static void async_promise_error_from_trap(void *promise, const char *fallback) {
    async_promise_error_copy(promise, rt_trap_get_error(), fallback);
}

//=============================================================================
// Async Connect
//=============================================================================

typedef struct {
    char *host;
    int64_t port;
    void *promise;
} connect_args_t;

/// @brief Pool-thread body for `rt_async_connect`. Performs the blocking `rt_tcp_connect`,
/// resolves the promise with the resulting TCP handle, releases the promise reference, and frees
/// the heap-owned host string + args struct.
static void async_connect_worker(void *arg) {
    connect_args_t *a = (connect_args_t *)arg;
    rt_string host = NULL;
    jmp_buf recovery;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        host = rt_string_from_bytes(a->host, strlen(a->host));
        void *tcp = rt_tcp_connect(host, a->port);
        rt_string_unref(host);
        host = NULL;
        rt_promise_set(a->promise, tcp);
    } else {
        if (host)
            rt_string_unref(host);
        async_promise_error_from_trap(a->promise, "AsyncSocket: connect failed");
    }
    rt_trap_clear_recovery();

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

/// @brief Pool-thread body for `rt_async_send`. Performs the blocking send and resolves the
/// promise with the byte count (boxed as a pointer-sized integer for the void* ABI).
static void async_send_worker(void *arg) {
    send_args_t *a = (send_args_t *)arg;
    jmp_buf recovery;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        int64_t sent = rt_tcp_send(a->tcp, a->data);
        rt_promise_set(a->promise, (void *)(intptr_t)sent);
    } else {
        async_promise_error_from_trap(a->promise, "AsyncSocket: send failed");
    }
    rt_trap_clear_recovery();

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

/// @brief Pool-thread body for `rt_async_recv`. Performs the blocking recv and resolves the
/// promise with the resulting Bytes object.
static void async_recv_worker(void *arg) {
    recv_args_t *a = (recv_args_t *)arg;
    jmp_buf recovery;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        void *data = rt_tcp_recv(a->tcp, a->max_bytes);
        rt_promise_set(a->promise, data);
    } else {
        async_promise_error_from_trap(a->promise, "AsyncSocket: recv failed");
    }
    rt_trap_clear_recovery();

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

/// @brief Pool-thread body for `rt_async_http_get`. Performs the blocking one-shot HTTP GET and
/// resolves the promise with the response body string.
static void async_http_get_worker(void *arg) {
    http_args_t *a = (http_args_t *)arg;
    rt_string url = NULL;
    jmp_buf recovery;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        rt_string result;
        url = rt_string_from_bytes(a->url, strlen(a->url));
        result = rt_http_get(url);
        rt_string_unref(url);
        url = NULL;
        rt_promise_set(a->promise, (void *)result);
    } else {
        if (url)
            rt_string_unref(url);
        async_promise_error_from_trap(a->promise, "AsyncSocket: HTTP GET failed");
    }
    rt_trap_clear_recovery();

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

/// @brief Pool-thread body for `rt_async_http_post`. Performs the blocking POST and resolves the
/// promise with the response body. Empty body is sent as "" rather than NULL.
static void async_http_post_worker(void *arg) {
    http_args_t *a = (http_args_t *)arg;
    rt_string url = NULL;
    rt_string body = NULL;
    jmp_buf recovery;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        rt_string result;
        url = rt_string_from_bytes(a->url, strlen(a->url));
        body = a->body ? rt_string_from_bytes(a->body, strlen(a->body)) : rt_string_from_bytes("", 0);
        result = rt_http_post(url, body);
        rt_string_unref(url);
        rt_string_unref(body);
        url = NULL;
        body = NULL;
        rt_promise_set(a->promise, (void *)result);
    } else {
        if (url)
            rt_string_unref(url);
        if (body)
            rt_string_unref(body);
        async_promise_error_from_trap(a->promise, "AsyncSocket: HTTP POST failed");
    }
    rt_trap_clear_recovery();

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
