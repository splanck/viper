//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_async_socket.c
// Purpose: Future wrapper that bridges blocking I/O through a fixed worker pool.
// Key invariants:
//   - Each async operation creates a Future, submits blocking work to the
//     thread pool, and resolves the Future when the operation completes.
//   - Uses one shared four-worker pool (lazy-initialized).
// Ownership/Lifetime:
//   - Returned Futures are GC-managed.
//   - Closure args are heap-allocated and freed by the worker.
// Links: rt_async_socket.h (API), rt_network.h (blocking sockets)
//
//===----------------------------------------------------------------------===//

#include "rt_async_socket.h"

#include "rt_box.h"
#include "system/rt_machine.h"

#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <setjmp.h>
#include <stdint.h>
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
extern void rt_promise_set_transferred(void *promise, void *value);
extern void rt_promise_set_error(void *promise, rt_string error);
extern void *rt_promise_get_future(void *promise);

//=============================================================================
// Default Thread Pool (lazy singleton)
//=============================================================================

static void *default_pool = NULL;

/// @brief Requested worker count for the shared pool (0 = CPU-scaled default).
/// Configurable via `AsyncSocket.SetPoolSize` BEFORE the first async call.
static int64_t requested_pool_size = 0;

/// @brief CPU-aware default pool size (2x cores, floor 8, cap 1024): each
///        blocking operation occupies a worker for its duration, so the old
///        fixed 4-worker pool let a handful of long reads starve the surface.
static int64_t async_default_pool_size(void) {
    int64_t size = requested_pool_size;
    if (size <= 0) {
        int64_t cores = rt_machine_cores();
        if (cores < 1)
            cores = 1;
        size = cores * 2;
        if (size < 8)
            size = 8;
    }
    if (size > 1024)
        size = 1024;
    return size;
}

#ifdef _WIN32
static LONG pool_init_state = 0;
#else
static pthread_once_t pool_once = PTHREAD_ONCE_INIT;

/// @brief POSIX `pthread_once` initializer for the shared worker pool.
static void init_default_pool(void) {
    default_pool = rt_threadpool_new(async_default_pool_size());
}
#endif

/// @brief Lazy-initialize and return the shared async-socket thread pool. POSIX uses
/// `pthread_once`; Win32 uses a 3-state Interlocked flag (0=uninit, 1=initializing, 2=ready)
/// with a busy-wait while another thread is initializing.
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
    default_pool = rt_threadpool_new(async_default_pool_size());
    InterlockedExchange(&pool_init_state, 2);
    return default_pool;
#else
    pthread_once(&pool_once, init_default_pool);
    return default_pool;
#endif
}

/// @brief Configure the shared pool's worker count (the executor model is a
///        bounded blocking-worker pool). Must be called before the first
///        async operation; traps once the pool exists.
void rt_async_socket_set_pool_size(int64_t size) {
    if (size < 1 || size > 1024) {
        rt_trap("AsyncSocket.SetPoolSize: size must be 1..1024");
        return;
    }
    if (default_pool) {
        rt_trap("AsyncSocket.SetPoolSize: pool already created; set the size "
                "before the first async operation");
        return;
    }
    requested_pool_size = size;
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

/// @brief Create a Future that is already resolved as an error.
/// @details Used by public async entry points when validation fails before a
///          worker item can be built. Returning a failed Future preserves the
///          asynchronous API contract while preventing invalid handles or
///          strings from reaching the worker pool after a recoverable trap.
/// @param message Error text to store in the returned Future.
/// @return A Future object resolved with @p message, or NULL if the promise
///         allocation itself fails.
static void *async_failed_future(const char *message) {
    void *promise = rt_promise_new();
    if (!promise)
        return NULL;
    void *future = rt_promise_get_future(promise);
    async_fail_submit(promise, message);
    return future;
}

/// @brief Resolve `promise` as Err with `msg` (or `fallback` if NULL), copying the message string.
/// @details The copy step matters because the caller's `msg` pointer may
///          live in thread-local trap storage that gets clobbered by the
///          next trap. Copying into a fresh `rt_string` decouples the
///          promise's error message from any external buffer.
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

/// @brief Resolve `promise` as Err using the most recent trap message on this thread.
/// @details Used inside worker thread bodies after a trap was caught via
///          `setjmp` recovery. The trap message lives in thread-local
///          storage; this helper copies it into the promise so the
///          caller can read it after the worker exits.
static void async_promise_error_from_trap(void *promise, const char *fallback) {
    async_promise_error_copy(promise, rt_trap_get_error(), fallback);
}

static size_t async_string_len_or_trap(rt_string str, const char *null_msg) {
    if (!str) {
        rt_trap(null_msg);
        return 0;
    }
    const char *data = rt_string_cstr(str);
    int64_t len = rt_str_len(str);
    if (!data || len < 0) {
        rt_trap(null_msg);
        return 0;
    }
    if ((uint64_t)len > (uint64_t)SIZE_MAX) {
        rt_trap("AsyncSocket: string is too large");
        return 0;
    }
    return (size_t)len;
}

static int async_has_embedded_nul(const char *data, size_t len) {
    return data && memchr(data, '\0', len) != NULL;
}

static char *async_copy_bytes(const char *data, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    if (len > 0)
        memcpy(copy, data, len);
    copy[len] = '\0';
    return copy;
}

//=============================================================================
// Async Connect
//=============================================================================

typedef struct {
    char *host;
    size_t host_len;
    int64_t port;
    int64_t timeout_ms;
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
        host = rt_string_from_bytes(a->host, a->host_len);
        if (!host)
            rt_trap("AsyncSocket: memory allocation failed");
        void *tcp = rt_tcp_connect_for(host, a->port, a->timeout_ms);
        rt_string_unref(host);
        host = NULL;
        // Transfer the worker's freshly produced reference to the Future so
        // consumption releases the only reference (VDOC-157).
        rt_promise_set_transferred(a->promise, tcp);
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

/// @brief Submit a blocking TCP connect to the worker pool with an explicit timeout.
/// @details Submits the blocking `rt_tcp_connect_for(host, port, timeout_ms)` to the
///          shared 4-thread pool and returns immediately with a future
///          the caller can `Await` or chain on. Heap-copies the host
///          string so the worker can use it safely after the calling
///          frame returns. On thread-pool refusal (pool shut down),
///          resolves the promise as Err synchronously rather than
///          leaking the request.
void *rt_async_connect_for(rt_string host, int64_t port, int64_t timeout_ms) {
    size_t host_len = async_string_len_or_trap(host, "AsyncSocket: NULL host");
    const char *h = host_len > 0 ? rt_string_cstr(host) : NULL;
    if (host_len == 0 || async_has_embedded_nul(h, host_len)) {
        rt_trap("AsyncSocket: invalid host");
        return async_failed_future("AsyncSocket: invalid host");
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    connect_args_t *args = (connect_args_t *)malloc(sizeof(connect_args_t));
    if (!args) {
        rt_trap("AsyncSocket: OOM");
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }
    args->host = async_copy_bytes(h, host_len);
    args->host_len = host_len;
    args->port = port;
    args->timeout_ms = timeout_ms;
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

/// @brief Submit a TCP connect using the default TCP connect timeout.
void *rt_async_connect(rt_string host, int64_t port) {
    return rt_async_connect_for(host, port, 30000);
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
        // Box the count: the Future payload must be a real runtime object,
        // not a raw pointer-cast integer (VDOC-158). Consumers unbox with
        // Zanna.Core.Box, matching Async.Run's boxed-result convention.
        rt_promise_set_transferred(a->promise, rt_box_i64(sent));
    } else {
        async_promise_error_from_trap(a->promise, "AsyncSocket: send failed");
    }
    rt_trap_clear_recovery();

    async_release_owned(a->data);
    async_release_owned(a->tcp);
    async_release_owned(a->promise);
    free(a);
}

/// @brief Submit a blocking TCP send and return a generic Future result.
/// @details Retains both the TCP handle and the data object before
///          submission so the worker can read them after the caller's
///          frame returns. On thread-pool refusal the promise is
///          resolved synchronously as Err and the held references are
///          dropped before returning.
void *rt_async_send(void *tcp, void *data) {
    if (!tcp || !data) {
        rt_trap("AsyncSocket: NULL arg");
        return async_failed_future("AsyncSocket: NULL arg");
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    send_args_t *args = (send_args_t *)malloc(sizeof(send_args_t));
    if (!args) {
        rt_trap("AsyncSocket: OOM");
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }
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
        rt_promise_set_transferred(a->promise, data);
    } else {
        async_promise_error_from_trap(a->promise, "AsyncSocket: recv failed");
    }
    rt_trap_clear_recovery();

    async_release_owned(a->tcp);
    async_release_owned(a->promise);
    free(a);
}

/// @brief Submit a blocking TCP receive and return a Future[Bytes].
/// @details Retains the TCP handle so the worker thread can hold a
///          stable reference. The blocking `rt_tcp_recv` runs on the
///          pool thread and the promise is resolved with the resulting
///          Bytes payload (or an error message via the trap recovery).
void *rt_async_recv(void *tcp, int64_t max_bytes) {
    if (!tcp) {
        rt_trap("AsyncSocket: NULL tcp");
        return async_failed_future("AsyncSocket: NULL tcp");
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    recv_args_t *args = (recv_args_t *)malloc(sizeof(recv_args_t));
    if (!args) {
        rt_trap("AsyncSocket: OOM");
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }
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
    size_t url_len;
    char *body; // NULL for GET
    size_t body_len;
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
        url = rt_string_from_bytes(a->url, a->url_len);
        if (!url)
            rt_trap("AsyncSocket: memory allocation failed");
        result = rt_http_get(url);
        rt_string_unref(url);
        url = NULL;
        rt_promise_set_transferred(a->promise, (void *)result);
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

/// @brief Initiate an HTTP GET and return a Future[String] for the response body.
/// @details Heap-copies the URL via @c strdup so the worker owns a stable
///          C-string. The pool thread reconstitutes the URL as an
///          @c rt_string before calling the blocking @c rt_http_get.
void *rt_async_http_get(rt_string url) {
    const char *u = rt_string_cstr(url);
    size_t url_len = async_string_len_or_trap(url, "AsyncSocket: NULL URL");
    if (url_len == 0 || async_has_embedded_nul(u, url_len)) {
        rt_trap("AsyncSocket: invalid URL");
        return async_failed_future("AsyncSocket: invalid URL");
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    http_args_t *args = (http_args_t *)malloc(sizeof(http_args_t));
    if (!args) {
        rt_trap("AsyncSocket: OOM");
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }
    args->url = async_copy_bytes(u, url_len);
    args->url_len = url_len;
    args->body = NULL;
    args->body_len = 0;
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
        url = rt_string_from_bytes(a->url, a->url_len);
        body = rt_string_from_bytes(a->body ? a->body : "", a->body_len);
        if (!url || !body)
            rt_trap("AsyncSocket: memory allocation failed");
        result = rt_http_post(url, body);
        rt_string_unref(url);
        rt_string_unref(body);
        url = NULL;
        body = NULL;
        rt_promise_set_transferred(a->promise, (void *)result);
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

/// @brief Initiate an HTTP POST and return a Future[String] for the response body.
/// @details URL and body are both heap-copied so the worker can outlive
///          the calling frame. A NULL body is normalised to an empty
///          string before the blocking @c rt_http_post call so callers
///          don't have to special-case the no-body case.
void *rt_async_http_post(rt_string url, rt_string body) {
    const char *u = rt_string_cstr(url);
    const char *b = body ? rt_string_cstr(body) : NULL;
    size_t url_len = async_string_len_or_trap(url, "AsyncSocket: NULL URL");
    size_t body_len = body ? async_string_len_or_trap(body, "AsyncSocket: NULL body") : 0;
    if (url_len == 0 || async_has_embedded_nul(u, url_len)) {
        rt_trap("AsyncSocket: invalid URL");
        return async_failed_future("AsyncSocket: invalid URL");
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    http_args_t *args = (http_args_t *)malloc(sizeof(http_args_t));
    if (!args) {
        rt_trap("AsyncSocket: OOM");
        async_fail_submit(promise, "AsyncSocket: OOM");
        return future;
    }
    args->url = async_copy_bytes(u, url_len);
    args->url_len = url_len;
    args->body = body ? async_copy_bytes(b ? b : "", body_len) : NULL;
    args->body_len = body_len;
    args->promise = promise;
    if (!args->url || (body && !args->body)) {
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
