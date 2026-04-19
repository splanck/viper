//===----------------------------------------------------------------------===//

#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_https_server.c
// Purpose: TLS-backed HTTP/1.1 server with routing and request/response objects.
// Key invariants:
//   - Accept loop runs in a dedicated thread.
//   - Each connection is dispatched to the worker pool for request handling.
//   - Sequential HTTP/1.1 keep-alive requests are supported when framing permits.
//   - Routes matched via embedded HttpRouter.
// Ownership/Lifetime:
//   - Server object GC-managed. Stop() or finalizer stops accept loop.
//   - Per-request ServerReq/ServerRes are stack-allocated (not GC).
// Links: rt_https_server.h (API), rt_http_router.h (routing), rt_tls.c
//
//===----------------------------------------------------------------------===//

#include "rt_https_server.h"
#include "rt_http2.h"
#include "rt_http_router.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threadpool.h"
#include "rt_tls_server_internal.h"

#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
typedef CRITICAL_SECTION https_server_mutex_t;
#define HTTPS_SERVER_MUTEX_INIT(m) InitializeCriticalSection(m)
#define HTTPS_SERVER_MUTEX_LOCK(m) EnterCriticalSection(m)
#define HTTPS_SERVER_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define HTTPS_SERVER_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
typedef pthread_mutex_t https_server_mutex_t;
#define HTTPS_SERVER_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define HTTPS_SERVER_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define HTTPS_SERVER_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define HTTPS_SERVER_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

#include "rt_trap.h"
extern void rt_trap_net(const char *msg, int err_code);

#if defined(__GNUC__) || defined(__clang__)
#define HTTPS_MAYBE_UNUSED __attribute__((unused))
#else
#define HTTPS_MAYBE_UNUSED
#endif

//=============================================================================
// Internal Structures
//=============================================================================

#define HTTP_REQ_MAX_LINE 8192
#define HTTP_REQ_MAX_HEADERS 100
#define HTTP_REQ_MAX_BODY (16 * 1024 * 1024) // 16 MB
#define HTTP_REQ_MAX_ENCODED_BODY (HTTP_REQ_MAX_BODY + 65536)

typedef struct {
    char *method;
    char *path;
    char *query;
    char *body;
    size_t body_len;
    int http_major;
    int http_minor;
    void *headers; // Map
    void *params;  // Map (from router)
} server_req_t;

typedef struct {
    int status_code;
    void *headers; // Map
    char *body;
    size_t body_len;
    bool sent;
} server_res_t;

typedef struct {
    char *tag;
} route_entry_t;

typedef struct {
    char *tag;
    rt_http_server_handler_dispatch_fn dispatch;
    void *ctx;
    rt_http_server_handler_cleanup_fn cleanup;
} handler_binding_t;

typedef struct {
    void *router;     // HttpRouter
    void *tcp_server; // TcpServer
    void *worker_pool;
    rt_tls_server_ctx_t *tls_ctx;
    char *cert_file;
    char *key_file;
    int64_t port;
    bool running;
    route_entry_t *routes;
    int route_count;
    int route_cap;
    handler_binding_t *bindings;
    int binding_count;
    int binding_cap;
#ifdef _WIN32
    HANDLE accept_thread;
#else
    pthread_t accept_thread;
#endif
    bool thread_started;
    void **active_conns;
    int active_conn_count;
    int active_conn_cap;
    https_server_mutex_t state_lock;
    bool state_lock_initialized;
} rt_http_server_impl;

static void free_server_req(server_req_t *req);
static void build_route_response(rt_http_server_impl *server, server_req_t *req, server_res_t *res);
static void free_route_entries(rt_http_server_impl *server);
static void free_handler_bindings(rt_http_server_impl *server);
static int contains_crlf(const char *text);
static int is_server_managed_header_name(const char *name);
static int response_forces_close(const server_res_t *res);

static void https_server_state_lock(rt_http_server_impl *server) {
    if (server && server->state_lock_initialized)
        HTTPS_SERVER_MUTEX_LOCK(&server->state_lock);
}

static void https_server_state_unlock(rt_http_server_impl *server) {
    if (server && server->state_lock_initialized)
        HTTPS_SERVER_MUTEX_UNLOCK(&server->state_lock);
}

static int https_server_is_running(rt_http_server_impl *server) {
    int running = 0;
    if (!server)
        return 0;
    https_server_state_lock(server);
    running = server->running ? 1 : 0;
    https_server_state_unlock(server);
    return running;
}

static int https_server_register_active_conn(rt_http_server_impl *server, void *conn) {
    int ok = 1;
    if (!server || !conn)
        return 0;
    https_server_state_lock(server);
    for (int i = 0; i < server->active_conn_count; i++) {
        if (server->active_conns[i] == conn) {
            https_server_state_unlock(server);
            return 1;
        }
    }
    if (server->active_conn_count == server->active_conn_cap) {
        int new_cap = server->active_conn_cap > 0 ? server->active_conn_cap * 2 : 8;
        void **grown = (void **)realloc(server->active_conns, (size_t)new_cap * sizeof(void *));
        if (!grown) {
            ok = 0;
        } else {
            server->active_conns = grown;
            server->active_conn_cap = new_cap;
        }
    }
    if (ok)
        server->active_conns[server->active_conn_count++] = conn;
    https_server_state_unlock(server);
    return ok;
}

static void https_server_unregister_active_conn(rt_http_server_impl *server, void *conn) {
    if (!server || !conn)
        return;
    https_server_state_lock(server);
    for (int i = 0; i < server->active_conn_count; i++) {
        if (server->active_conns[i] == conn) {
            server->active_conn_count--;
            server->active_conns[i] = server->active_conns[server->active_conn_count];
            server->active_conns[server->active_conn_count] = NULL;
            break;
        }
    }
    https_server_state_unlock(server);
}

static void **https_server_snapshot_active_conns(rt_http_server_impl *server, int *count_out) {
    void **snapshot = NULL;
    int count = 0;
    if (count_out)
        *count_out = 0;
    if (!server)
        return NULL;
    https_server_state_lock(server);
    count = server->active_conn_count;
    if (count > 0) {
        snapshot = (void **)malloc((size_t)count * sizeof(void *));
        if (snapshot)
            memcpy(snapshot, server->active_conns, (size_t)count * sizeof(void *));
        else
            count = 0;
    }
    https_server_state_unlock(server);
    if (count_out)
        *count_out = count;
    return snapshot;
}

static void https_server_interrupt_conn(void *conn) {
    int fd = rt_tls_get_socket((rt_tls_session_t *)conn);
    if (fd < 0)
        return;
#ifdef _WIN32
    shutdown((SOCKET)fd, SD_BOTH);
#else
    shutdown((socket_t)fd, SHUT_RDWR);
#endif
}

typedef enum {
    CHUNK_PARSE_OK = 0,
    CHUNK_PARSE_INCOMPLETE = 1,
    CHUNK_PARSE_INVALID = 2,
} chunk_parse_status_t;

/// @brief Heap-allocate a NUL-terminated copy of a C string.
///
/// Uses plain `malloc` rather than the GC-managed string pool because
/// the resulting buffer is owned by an internal route / binding entry
/// whose lifetime is tied to the server (not to a script value). NULL
/// input returns NULL; allocation failure returns NULL.
///
/// @param text Source NUL-terminated string, or NULL.
/// @return Newly allocated copy that callers must `free()`, or NULL.
static char *dup_cstr(const char *text) {
    if (!text)
        return NULL;
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

/// @brief Grow `server->routes` so it can hold at least `needed` entries.
///
/// Doubles the capacity geometrically (initial 8, then 16, 32, ...) up
/// to a hard cap of `INT32_MAX / 2` to avoid wraparound. New tail
/// entries are zero-initialized so callers can write into them
/// immediately. No-op when capacity already suffices.
///
/// @param server Server impl whose `routes`/`route_cap` is grown.
/// @param needed Minimum required capacity (`route_count + 1` at call sites).
/// @return `1` on success (capacity now satisfies `needed`); `0` on
///         allocation failure or overflow.
static int ensure_route_capacity(rt_http_server_impl *server, int needed) {
    if (needed <= server->route_cap)
        return 1;

    int new_cap = server->route_cap > 0 ? server->route_cap : 8;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }

    route_entry_t *routes =
        (route_entry_t *)realloc(server->routes, (size_t)new_cap * sizeof(route_entry_t));
    if (!routes)
        return 0;
    memset(routes + server->route_cap, 0, (size_t)(new_cap - server->route_cap) * sizeof(*routes));
    server->routes = routes;
    server->route_cap = new_cap;
    return 1;
}

/// @brief Grow `server->bindings` so it can hold at least `needed` entries.
///
/// Mirror of `ensure_route_capacity` for the handler-binding array. Same
/// geometric-growth strategy with INT32_MAX/2 overflow guard and zeroed
/// tail.
///
/// @param server Server impl whose `bindings`/`binding_cap` is grown.
/// @param needed Minimum required capacity.
/// @return `1` on success, `0` on allocation failure or overflow.
static int ensure_binding_capacity(rt_http_server_impl *server, int needed) {
    if (needed <= server->binding_cap)
        return 1;

    int new_cap = server->binding_cap > 0 ? server->binding_cap : 8;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }

    handler_binding_t *bindings =
        (handler_binding_t *)realloc(server->bindings, (size_t)new_cap * sizeof(handler_binding_t));
    if (!bindings)
        return 0;
    memset(bindings + server->binding_cap,
           0,
           (size_t)(new_cap - server->binding_cap) * sizeof(*bindings));
    server->bindings = bindings;
    server->binding_cap = new_cap;
    return 1;
}

/// @brief Append a new route entry tagged with `tag` and return a
///        writable pointer to it.
///
/// Reserves capacity via `ensure_route_capacity`, then `dup_cstr`s the
/// tag into the new slot and increments `route_count`. Caller fills in
/// the rest of the entry's fields (method, pattern). Returns NULL on
/// any allocation failure (capacity grow OR tag duplication).
///
/// @param server Server impl.
/// @param tag    Route tag (typically the route's display name); duplicated
///               into the entry. Lifetime tied to the server.
/// @return Pointer to the newly-allocated entry, or NULL on failure.
static route_entry_t *append_route_entry(rt_http_server_impl *server, const char *tag) {
    if (!server || !tag)
        return NULL;
    if (!ensure_route_capacity(server, server->route_count + 1))
        return NULL;

    route_entry_t *entry = &server->routes[server->route_count];
    entry->tag = dup_cstr(tag);
    if (!entry->tag)
        return NULL;

    server->route_count++;
    return entry;
}

/// @brief Linear-search the binding array for an entry with matching tag.
///
/// Bindings are typically few (one per handler), so a linear scan is
/// faster than maintaining a hash table for the cache-locality win.
/// NULL inputs return NULL.
///
/// @param server Server impl.
/// @param tag    Tag to match (case-sensitive `strcmp`).
/// @return Pointer to the matching entry, or NULL if not found.
static handler_binding_t *find_handler_binding(rt_http_server_impl *server, const char *tag) {
    if (!server || !tag)
        return NULL;

    for (int i = 0; i < server->binding_count; i++) {
        handler_binding_t *binding = &server->bindings[i];
        if (binding->tag && strcmp(binding->tag, tag) == 0)
            return binding;
    }
    return NULL;
}

/// @brief Release a binding's resources in place (without compacting
///        the array).
///
/// Calls the binding's `cleanup` (if present) on its `ctx` so the
/// handler implementation can release whatever it allocated, then frees
/// the heap-owned `tag` string and zeros the slot. Caller is responsible
/// for any array compaction or count adjustment.
///
/// @param binding Binding to release; NULL is a safe no-op.
static void release_handler_binding(handler_binding_t *binding) {
    if (!binding)
        return;
    if (binding->cleanup)
        binding->cleanup(binding->ctx);
    free(binding->tag);
    memset(binding, 0, sizeof(*binding));
}

/// @brief Install or replace the handler binding for `tag`.
///
/// If a binding already exists with `tag`, its cleanup is invoked
/// (only when the new context is a different pointer — re-binding the
/// same context shouldn't double-free) and the dispatch/ctx/cleanup are
/// overwritten in place. Otherwise a new binding is appended after
/// growing capacity. Tag is duplicated into the binding so callers can
/// reuse their tag buffer afterwards.
///
/// @param server   Server impl.
/// @param tag      Identifier for the binding (matched by `strcmp`).
/// @param dispatch Callback that handles requests; required.
/// @param ctx      Opaque pointer passed to `dispatch` and `cleanup`.
/// @param cleanup  Optional callback invoked with `ctx` when the binding
///                 is released or replaced. NULL when `ctx` doesn't
///                 require cleanup.
/// @return `1` on success, `0` on allocation failure or NULL inputs.
static int set_handler_binding(rt_http_server_impl *server,
                               const char *tag,
                               rt_http_server_handler_dispatch_fn dispatch,
                               void *ctx,
                               rt_http_server_handler_cleanup_fn cleanup) {
    if (!server || !tag || !dispatch)
        return 0;

    handler_binding_t *binding = find_handler_binding(server, tag);
    if (binding) {
        if (binding->cleanup && binding->ctx != ctx)
            binding->cleanup(binding->ctx);
        binding->dispatch = dispatch;
        binding->ctx = ctx;
        binding->cleanup = cleanup;
        return 1;
    }

    if (!ensure_binding_capacity(server, server->binding_count + 1))
        return 0;

    binding = &server->bindings[server->binding_count];
    binding->tag = dup_cstr(tag);
    if (!binding->tag)
        return 0;
    binding->dispatch = dispatch;
    binding->ctx = ctx;
    binding->cleanup = cleanup;
    server->binding_count++;
    return 1;
}

/// @brief Free every route entry and the surrounding array.
///
/// Walks `server->routes`, frees each entry's tag string, frees the
/// array itself, and zeros the count/capacity/pointer triplet so the
/// server can be safely re-initialized. Idempotent — safe to call when
/// no routes have been registered.
///
/// @param server Server impl.
static void free_route_entries(rt_http_server_impl *server) {
    if (!server || !server->routes)
        return;
    for (int i = 0; i < server->route_count; i++)
        free(server->routes[i].tag);
    free(server->routes);
    server->routes = NULL;
    server->route_count = 0;
    server->route_cap = 0;
}

/// @brief Free every handler binding and the surrounding array.
///
/// Mirror of `free_route_entries`: invokes per-binding `release_handler_
/// binding` (which runs `cleanup` callbacks), frees the array, and zeros
/// the count/capacity/pointer triplet.
///
/// @param server Server impl.
static void free_handler_bindings(rt_http_server_impl *server) {
    if (!server || !server->bindings)
        return;
    for (int i = 0; i < server->binding_count; i++)
        release_handler_binding(&server->bindings[i]);
    free(server->bindings);
    server->bindings = NULL;
    server->binding_count = 0;
    server->binding_cap = 0;
}

/// @brief Trampoline that adapts the C-API `rt_http_server_handler_fn`
///        to the dispatch callback signature.
///
/// Bindings can be installed in two flavors: a "native" handler taking
/// `(req, res)` directly, or a script-bridge dispatch taking
/// `(ctx, req, res)`. The native path stores its function pointer in
/// `ctx` and uses this trampoline as the dispatch slot so the call site
/// is uniform. NULL `ctx` is a safe no-op (skipped, no crash).
///
/// @param ctx Native handler function pointer (cast back to `rt_http_
///            server_handler_fn`).
/// @param req Request object handle, opaque to this layer.
/// @param res Response object handle, opaque to this layer.
static void native_handler_dispatch(void *ctx, void *req, void *res) {
    if (!ctx)
        return;
    ((rt_http_server_handler_fn)ctx)(req, res);
}

//=============================================================================
// Finalizer
//=============================================================================

/// @brief GC finalizer for `HttpServer` instances.
///
/// Called by the GC sweep when the last reference to an `HttpServer`
/// drops. Tears down every owned resource in the right order:
///   1. Stop the accept loop / worker pool so no thread can touch the
///      server's data while we free it.
///   2. Free the route + binding arrays (releases their cleanup
///      callbacks, then their tag strings).
///   3. Release the script-side router and worker_pool object refs (each
///      one is itself a GC handle; we take its refcount to zero before
///      calling `rt_obj_free`).
///
/// Safe to call with NULL `obj` (no-op) and idempotent if invoked twice.
///
/// @param obj `rt_http_server_impl *` cast to `void *`.
static void rt_http_server_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    rt_https_server_stop(server);
    free_route_entries(server);
    free_handler_bindings(server);
    if (server->router && rt_obj_release_check0(server->router))
        rt_obj_free(server->router);
    server->router = NULL;
    if (server->worker_pool && rt_obj_release_check0(server->worker_pool))
        rt_obj_free(server->worker_pool);
    server->worker_pool = NULL;
    rt_tls_server_ctx_free(server->tls_ctx);
    server->tls_ctx = NULL;
    free(server->cert_file);
    free(server->key_file);
    server->cert_file = NULL;
    server->key_file = NULL;
    free(server->active_conns);
    server->active_conns = NULL;
    server->active_conn_count = 0;
    server->active_conn_cap = 0;
    if (server->state_lock_initialized) {
        HTTPS_SERVER_MUTEX_DESTROY(&server->state_lock);
        server->state_lock_initialized = false;
    }
}

//=============================================================================
// Shared HTTP request/response helpers
//=============================================================================

#include "rt_http_server_shared.inc"

/// @brief Free every owned field of a parsed `server_req_t`.
///
/// Releases the heap-owned `method`, `path`, `query`, and `body`
/// strings (allocated during `parse_http_request`), then drops the
/// refcount on the GC-managed `headers` and `params` Map objects.
/// Caller is responsible for freeing the `req` storage itself if it
/// was heap-allocated.
///
/// @param req Request struct to release. Members may be NULL (skipped).
static void free_server_req(server_req_t *req) {
    free(req->method);
    free(req->path);
    free(req->query);
    free(req->body);
    if (req->headers && rt_obj_release_check0(req->headers))
        rt_obj_free(req->headers);
    if (req->params && rt_obj_release_check0(req->params))
        rt_obj_free(req->params);
}

/// @brief Test-only entry point for the HTTP request parser.
///
/// Wraps `parse_http_request` so unit tests can exercise the parser
/// without needing a real socket. Out-parameters receive freshly
/// `strdup`'d copies of the parsed fields so the test can call the
/// req-free path independently. Caller owns each returned string.
///
/// @param raw           Raw request bytes (e.g. `"GET / HTTP/1.1\r\n..."`).
/// @param raw_len       Length of `raw`.
/// @param method_out    Receives method string (e.g. `"GET"`), or NULL.
/// @param path_out      Receives path string (e.g. `"/api/users"`), or NULL.
/// @param body_out      Receives body bytes (NUL-terminated copy), or NULL.
/// @param body_len_out  Receives body length in bytes, or NULL.
/// @return `1` on successful parse, `0` on malformed request.
static HTTPS_MAYBE_UNUSED int rt_https_server_test_parse_request(const char *raw,
                                                                 size_t raw_len,
                                                                 char **method_out,
                                                                 char **path_out,
                                                                 char **body_out,
                                                                 size_t *body_len_out) {
    server_req_t req;
    if (!parse_http_request(raw, raw_len, &req))
        return 0;

    if (method_out)
        *method_out = req.method ? strdup(req.method) : NULL;
    if (path_out)
        *path_out = req.path ? strdup(req.path) : NULL;
    if (body_out)
        *body_out = req.body ? strdup(req.body) : NULL;
    if (body_len_out)
        *body_len_out = req.body_len;

    free_server_req(&req);
    return 1;
}

//=============================================================================
// HTTP Response Building
//=============================================================================

/// @brief Test-only entry point for the HTTP response builder.
///
/// Mirror of `rt_http_server_test_parse_request`: lets unit tests
/// construct a wire-format response from C input arrays without going
/// through the script-side `Response` object. Builds a temporary
/// `server_res_t`, populates it with the status code, body, and
/// headers, then delegates to the shared `build_response` formatter.
///
/// @param status_code   HTTP status code (e.g. 200, 404).
/// @param body          Body bytes (may be NULL when `body_len == 0`).
/// @param body_len      Body length in bytes.
/// @param header_names  Array of `header_count` header name strings.
/// @param header_values Array of `header_count` header value strings,
///                      paired with `header_names` by index.
/// @param header_count  Number of header pairs.
/// @param out_len       Out-param receiving the formatted response
///                      length in bytes.
/// @return Newly malloc'd response buffer (caller frees), or NULL on
///         allocation failure.
static HTTPS_MAYBE_UNUSED char *rt_https_server_test_build_response(int status_code,
                                                                    const char *body,
                                                                    size_t body_len,
                                                                    const char **header_names,
                                                                    const char **header_values,
                                                                    size_t header_count,
                                                                    size_t *out_len) {
    char *body_copy = NULL;
    server_res_t res;
    memset(&res, 0, sizeof(res));
    res.status_code = status_code;
    if (body && body_len > 0) {
        body_copy = (char *)malloc(body_len);
        if (!body_copy)
            return NULL;
        memcpy(body_copy, body, body_len);
    }
    res.body = body_copy;
    res.body_len = body_copy ? body_len : 0;
    res.headers = rt_map_new();
    if (!res.headers) {
        free(body_copy);
        return NULL;
    }

    for (size_t i = 0; i < header_count; i++) {
        rt_string key = rt_string_from_bytes(header_names[i], strlen(header_names[i]));
        rt_string value = rt_string_from_bytes(header_values[i], strlen(header_values[i]));
        rt_map_set(res.headers, key, (void *)value);
        rt_string_unref(key);
        rt_string_unref(value);
    }

    char *resp = build_response(&res, 0, out_len);
    if (res.headers && rt_obj_release_check0(res.headers))
        rt_obj_free(res.headers);
    free(body_copy);
    return resp;
}

//=============================================================================
// Request Handler
//=============================================================================

static int https_conn_is_open(rt_tls_session_t *tls) {
    return tls && rt_tls_get_socket(tls) >= 0;
}

static void https_conn_set_recv_timeout(rt_tls_session_t *tls, int timeout_ms) {
    int fd = tls ? rt_tls_get_socket(tls) : -1;
    if (fd >= 0)
        set_socket_timeout((socket_t)fd, timeout_ms, true);
}

static void https_conn_set_send_timeout(rt_tls_session_t *tls, int timeout_ms) {
    int fd = tls ? rt_tls_get_socket(tls) : -1;
    if (fd >= 0)
        set_socket_timeout((socket_t)fd, timeout_ms, false);
}

static long https_conn_recv(rt_tls_session_t *tls, uint8_t *buf, size_t len) {
    return rt_tls_recv(tls, buf, len);
}

static int https_conn_send_all(rt_tls_session_t *tls, const void *buf, size_t len) {
    return rt_tls_send(tls, buf, len) == (long)len;
}

static long https_http2_read(void *ctx, uint8_t *buf, size_t len) {
    return https_conn_recv((rt_tls_session_t *)ctx, buf, len);
}

static int https_http2_write(void *ctx, const uint8_t *buf, size_t len) {
    return https_conn_send_all((rt_tls_session_t *)ctx, buf, len);
}

static int http2_request_to_server_req(const rt_http2_request_t *src, server_req_t *dst) {
    char *path_copy = NULL;
    char *qmark = NULL;
    if (!src || !dst || !src->method || !src->path || src->path[0] != '/')
        return 0;

    memset(dst, 0, sizeof(*dst));
    dst->method = strdup(src->method);
    path_copy = strdup(src->path);
    if (!dst->method || !path_copy)
        goto fail;

    qmark = strchr(path_copy, '?');
    if (qmark) {
        *qmark = '\0';
        dst->query = strdup(qmark + 1);
        if (!dst->query)
            goto fail;
    }

    dst->path = strdup(path_copy);
    if (!dst->path)
        goto fail;
    dst->http_major = 2;
    dst->http_minor = 0;
    dst->headers = rt_map_new();
    if (!dst->headers)
        goto fail;
    for (const rt_http2_header_t *it = src->headers; it; it = it->next) {
        rt_string key = NULL;
        rt_string value = NULL;
        if (!it->name || !it->value)
            continue;
        key = rt_string_from_bytes(it->name, strlen(it->name));
        value = rt_string_from_bytes(it->value, strlen(it->value));
        if (!key || !value) {
            if (key)
                rt_string_unref(key);
            if (value)
                rt_string_unref(value);
            goto fail;
        }
        rt_map_set(dst->headers, key, value);
        rt_string_unref(key);
        rt_string_unref(value);
    }
    if (src->body_len > 0) {
        dst->body = (char *)malloc(src->body_len + 1);
        if (!dst->body)
            goto fail;
        memcpy(dst->body, src->body, src->body_len);
        dst->body[src->body_len] = '\0';
        dst->body_len = src->body_len;
    }
    free(path_copy);
    return 1;

fail:
    free(path_copy);
    free_server_req(dst);
    memset(dst, 0, sizeof(*dst));
    return 0;
}

static int server_headers_to_http2(const server_res_t *src, rt_http2_header_t **headers_out) {
    rt_http2_header_t *headers = NULL;
    if (!headers_out)
        return 0;
    *headers_out = NULL;
    if (src && src->headers) {
        void *keys = rt_map_keys(src->headers);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *val = rt_map_get(src->headers, key);
            const char *k = rt_string_cstr(key);
            const char *v = val ? rt_string_cstr((rt_string)val) : NULL;
            if (k && v && !contains_crlf(k) && !contains_crlf(v) &&
                !is_server_managed_header_name(k) &&
                !rt_http2_header_append_copy(&headers, k, v)) {
                rt_http2_headers_free(headers);
                if (rt_obj_release_check0(keys))
                    rt_obj_free(keys);
                return 0;
            }
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }
    if (src && src->body) {
        char len_buf[32];
        snprintf(len_buf, sizeof(len_buf), "%zu", src->body_len);
        if (!rt_http2_header_append_copy(&headers, "content-length", len_buf)) {
            rt_http2_headers_free(headers);
            return 0;
        }
    }
    *headers_out = headers;
    return 1;
}

static void handle_connection_http2(rt_http_server_impl *server, rt_tls_session_t *tls) {
    rt_http2_io_t io;
    rt_http2_conn_t *h2 = NULL;

    io.ctx = tls;
    io.read = https_http2_read;
    io.write = https_http2_write;
    h2 = rt_http2_server_new(&io);
    if (!h2) {
        rt_tls_close(tls);
        return;
    }

    if (!https_server_register_active_conn(server, tls)) {
        rt_http2_conn_free(h2);
        rt_tls_close(tls);
        return;
    }
    https_conn_set_recv_timeout(tls, 30000);
    https_conn_set_send_timeout(tls, 30000);

    while (https_conn_is_open(tls) && https_server_is_running(server) && rt_http2_conn_is_usable(h2)) {
        rt_http2_request_t h2req;
        server_req_t req;
        server_res_t res;
        rt_http2_header_t *resp_headers = NULL;
        int close_after = 0;

        memset(&h2req, 0, sizeof(h2req));
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));

        if (!rt_http2_server_receive_request(h2, HTTP_REQ_MAX_BODY, &h2req))
            break;
        if (!http2_request_to_server_req(&h2req, &req)) {
            (void)rt_http2_server_send_response(h2, h2req.stream_id, 400, NULL, NULL, 0);
            rt_http2_request_free(&h2req);
            break;
        }

        build_route_response(server, &req, &res);
        if (!server_headers_to_http2(&res, &resp_headers) ||
            !rt_http2_server_send_response(
                h2, h2req.stream_id, res.status_code, resp_headers, (uint8_t *)res.body, res.body_len)) {
            rt_http2_headers_free(resp_headers);
            free(res.body);
            if (res.headers && rt_obj_release_check0(res.headers))
                rt_obj_free(res.headers);
            free_server_req(&req);
            rt_http2_request_free(&h2req);
            break;
        }

        close_after = response_forces_close(&res);
        rt_http2_headers_free(resp_headers);
        free(res.body);
        if (res.headers && rt_obj_release_check0(res.headers))
            rt_obj_free(res.headers);
        free_server_req(&req);
        rt_http2_request_free(&h2req);
        if (close_after)
            break;
    }

    https_server_unregister_active_conn(server, tls);
    rt_http2_conn_free(h2);
    rt_tls_close(tls);
}

/// @brief Handle a single accepted client connection end-to-end.
///
/// Reads the request (with a 30-second receive timeout to bound idle
/// keepalives), parses headers, validates the framing
/// (`Content-Length` or `Transfer-Encoding: chunked`; oversize bodies are
/// rejected at `HTTP_REQ_MAX_BODY`), parses the request, dispatches
/// through the route table via `build_route_response`, formats the
/// response via `build_response`, sends it on the socket, and closes
/// the connection. Caller (the accept loop or worker pool) owns the
/// `tcp` handle's lifetime up to entering this function; we close it
/// before returning.
///
/// On any allocation failure or malformed request, sends a minimal
/// `400 Bad Request` and closes the connection.
///
/// @param server Server impl owning the route table and bindings.
/// @param tcp    Accepted TLS connection handle (we close it).
static void handle_connection(rt_http_server_impl *server, void *tcp) {
    rt_tls_session_t *tls = (rt_tls_session_t *)tcp;
    if (strcmp(rt_tls_get_negotiated_alpn(tls), "h2") == 0) {
        handle_connection_http2(server, tls);
        return;
    }
    if (!https_server_register_active_conn(server, tls)) {
        rt_tls_close(tls);
        return;
    }
    https_conn_set_recv_timeout(tls, 30000);
    https_conn_set_send_timeout(tls, 30000);
    while (https_conn_is_open(tls) && https_server_is_running(server)) {
        size_t buf_cap = 65536;
        char *buf = (char *)malloc(buf_cap);
        if (!buf)
            break;

        size_t buf_len = 0;
        bool headers_done = false;
        size_t content_length = 0;
        size_t header_end_pos = 0;
        bool chunked = false;
        bool bad_request = false;

        while (buf_len < buf_cap && https_conn_is_open(tls)) {
            long data_len = https_conn_recv(tls, (uint8_t *)buf + buf_len, buf_cap - buf_len);
            if (data_len <= 0) {
                break;
            }
            buf_len += (size_t)data_len;

            if (!headers_done) {
                const char *header_end = find_header_end(buf, buf_len);
                if (header_end) {
                    headers_done = true;
                    header_end_pos = (size_t)(header_end - buf);
                    if (!parse_content_length_header_block(
                            buf, header_end_pos, &content_length, &chunked) ||
                        content_length > HTTP_REQ_MAX_BODY) {
                        bad_request = true;
                        break;
                    }
                }
            }

            if (bad_request)
                break;
            if (headers_done) {
                if (chunked) {
                    size_t consumed_len = 0;
                    chunk_parse_status_t status =
                        decode_chunked_body(buf + header_end_pos,
                                            buf_len - header_end_pos,
                                            NULL,
                                            NULL,
                                            &consumed_len);
                    if (status == CHUNK_PARSE_OK)
                        break;
                    if (status == CHUNK_PARSE_INVALID) {
                        bad_request = true;
                        break;
                    }
                    if (buf_cap < header_end_pos + HTTP_REQ_MAX_ENCODED_BODY) {
                        size_t next_cap = buf_cap * 2;
                        size_t max_cap = header_end_pos + HTTP_REQ_MAX_ENCODED_BODY + 1;
                        if (next_cap > max_cap)
                            next_cap = max_cap;
                        if (next_cap > buf_cap) {
                            char *new_buf = (char *)realloc(buf, next_cap);
                            if (!new_buf)
                                break;
                            buf = new_buf;
                            buf_cap = next_cap;
                        }
                    }
                } else {
                    if (content_length == 0 || buf_len >= header_end_pos + content_length)
                        break;
                    if (header_end_pos + content_length > buf_cap) {
                        buf_cap = header_end_pos + content_length + 1;
                        char *new_buf = (char *)realloc(buf, buf_cap);
                        if (!new_buf)
                            break;
                        buf = new_buf;
                    }
                }
            }
        }

        if (buf_len == 0) {
            free(buf);
            break;
        }

        if (buf_len < buf_cap)
            buf[buf_len] = '\0';
        else
            buf[buf_cap - 1] = '\0';

        server_req_t req;
        if (bad_request || !parse_http_request(buf, buf_len, &req)) {
            const char *bad =
                "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            https_conn_send_all(tls, bad, strlen(bad));
            free(buf);
            break;
        }
        free(buf);

        server_res_t res;
        memset(&res, 0, sizeof(res));
        build_route_response(server, &req, &res);

        int keep_alive = request_allows_keep_alive(&req) && !response_forces_close(&res);
        size_t resp_len = 0;
        char *resp = build_response(&res, keep_alive, &resp_len);
        if (resp) {
            https_conn_send_all(tls, resp, resp_len);
            free(resp);
        } else {
            keep_alive = 0;
        }

        free(res.body);
        if (res.headers && rt_obj_release_check0(res.headers))
            rt_obj_free(res.headers);
        free_server_req(&req);

        if (!keep_alive)
            break;
    }
    https_server_unregister_active_conn(server, tls);
    rt_tls_close(tls);
}

typedef struct {
    rt_http_server_impl *server;
    void *tcp;
} http_conn_task_t;

/// @brief Worker-pool task wrapper around `handle_connection`.
///
/// The accept loop allocates an `http_conn_task_t` (server + tcp pair)
/// and submits it to the worker pool; the worker calls this function,
/// which forwards to `handle_connection` and frees the task struct
/// afterwards. The TCP handle's lifetime is owned by `handle_connection`,
/// not the task.
///
/// @param arg `http_conn_task_t *` cast to `void *`.
static void handle_connection_task(void *arg) {
    http_conn_task_t *task = (http_conn_task_t *)arg;
    handle_connection(task->server, task->tcp);
    free(task);
}

/// @brief Replace the response body with a JSON error object.
///
/// Used when the route lookup fails (404) or the handler crashes (500)
/// to produce a structured error response instead of the bare status
/// line. The body is `{"error":"<message>"}`; the `Content-Type` header
/// is set (creating the headers map if needed) so clients see the JSON
/// MIME type.
///
/// Idempotent: clears any pre-existing body before writing the new one.
/// Allocation failures result in a status-code-only response (no body).
///
/// @param res         Response struct to overwrite.
/// @param status_code HTTP status code to set.
/// @param message     Human-readable error text; embedded into the JSON
///                    body literally (no escaping). NULL means no body.
static void set_json_error_response(server_res_t *res, int status_code, const char *message) {
    if (!res)
        return;

    res->status_code = status_code;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;

    if (message) {
        size_t cap = strlen(message) + 32;
        char *json = (char *)malloc(cap);
        if (json) {
            int written = snprintf(json, cap, "{\"error\":\"%s\"}", message);
            if (written >= 0) {
                res->body = json;
                res->body_len = (size_t)written;
            } else {
                free(json);
            }
        }
    }

    if (!res->headers)
        res->headers = rt_map_new();
    if (res->headers) {
        rt_string ct_key = rt_const_cstr("Content-Type");
        rt_string ct_val = rt_const_cstr("application/json");
        rt_map_set(res->headers, ct_key, (void *)ct_val);
    }
}

/// @brief Dispatch a parsed request to its registered handler and
///        populate the response.
///
/// Looks up the route via `rt_http_router_match` (which also extracts
/// path parameters), records the params on the request, then resolves
/// the handler binding via `find_handler_binding` and calls its
/// dispatch callback. On lookup failure produces a JSON error response
/// (500 for missing handler, 404 for no matching route).
///
/// Defaults the response status to 200 if the handler didn't set one.
///
/// @param server Server impl owning the routes/bindings.
/// @param req    Parsed request; `params` is populated on success.
/// @param res    Response struct to populate (status, headers, body).
static void build_route_response(rt_http_server_impl *server,
                                 server_req_t *req,
                                 server_res_t *res) {
    rt_string method_str = rt_string_from_bytes(req->method, strlen(req->method));
    rt_string path_str = rt_string_from_bytes(req->path, strlen(req->path));
    void *match = rt_http_router_match(server->router, method_str, path_str);
    rt_string_unref(method_str);
    rt_string_unref(path_str);

    res->headers = rt_map_new();

    if (match) {
        req->params = match; // Transfer ownership
        res->status_code = 200;

        int64_t route_idx = rt_route_match_index(match);
        if (route_idx < 0 || route_idx >= server->route_count) {
            set_json_error_response(res, 500, "Route metadata missing");
            return;
        }

        const char *tag = server->routes[route_idx].tag;
        handler_binding_t *binding = find_handler_binding(server, tag);
        if (!binding || !binding->dispatch) {
            set_json_error_response(res, 500, "Handler not registered");
            return;
        }

        binding->dispatch(binding->ctx, req, res);
        if (res->status_code <= 0)
            res->status_code = 200;
    } else {
        set_json_error_response(res, 404, "Not Found");
    }
}

//=============================================================================
// Accept Loop
//=============================================================================

/// @brief Background thread entry point: accept connections forever
///        and dispatch each to the worker pool.
///
/// Loops on `rt_tcp_server_accept_for` with a 1-second timeout so the
/// thread can wake periodically to check `server->running` (the stop
/// signal). For each accepted TCP connection, allocates a small
/// `http_conn_task_t` and submits `handle_connection_task` to the
/// worker pool. If allocation or submission fails, the connection is
/// closed immediately.
///
/// The platform-specific signature differs (`DWORD WINAPI` on Windows
/// vs `void *` on POSIX) but the body is identical.
///
/// @param arg `rt_http_server_impl *` cast to `void *`.
/// @return 0 / NULL on normal shutdown.
#ifdef _WIN32
static DWORD WINAPI accept_loop(LPVOID arg)
#else
static void *accept_loop(void *arg)
#endif
{
    rt_http_server_impl *server = (rt_http_server_impl *)arg;

    while (https_server_is_running(server)) {
        void *tcp = rt_tcp_server_accept_for(server->tcp_server, 1000);
        if (!tcp)
            continue; // Timeout — check running flag

        if (!https_server_is_running(server)) {
            rt_tcp_close(tcp);
            if (rt_obj_release_check0(tcp))
                rt_obj_free(tcp);
            break;
        }

        rt_tls_session_t *tls = rt_tls_server_accept_socket((int)rt_tcp_socket_fd(tcp), server->tls_ctx);
        rt_tcp_detach_socket(tcp);
        if (rt_obj_release_check0(tcp))
            rt_obj_free(tcp);
        if (!tls)
            continue;

        http_conn_task_t *task = (http_conn_task_t *)malloc(sizeof(http_conn_task_t));
        if (!task) {
            rt_tls_close(tls);
            continue;
        }

        task->server = server;
        task->tcp = tls;

        if (!server->worker_pool ||
            !rt_threadpool_submit(server->worker_pool, (void *)handle_connection_task, task)) {
            free(task);
            rt_tls_close(tls);
        }
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

//=============================================================================
// Public API
//=============================================================================

/// @brief `HttpServer.New(port)` — create a new HTTP server bound to
///        the given TCP port.
///
/// Allocates a GC-managed server impl, attaches the finalizer (which
/// will tear down routes / worker pool / accept loop on collection),
/// constructs the route trie and an 8-thread worker pool, and stores
/// the requested port for later use by `Start`. The TCP listener is
/// not actually opened until `Start`; this constructor only validates
/// inputs and reserves resources.
///
/// Traps on invalid port (`<1` or `>65535`) or allocation failure.
///
/// @param port TCP port number, 1..65535.
/// @return GC-managed `HttpServer` handle.
void *rt_https_server_new(int64_t port, rt_string cert_file, rt_string key_file) {
    if (port < 0 || port > 65535)
        rt_trap("HttpsServer: invalid port");

    rt_http_server_impl *server =
        (rt_http_server_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_server_impl));
    if (!server)
        rt_trap("HttpsServer: memory allocation failed");
    memset(server, 0, sizeof(*server));
    rt_obj_set_finalizer(server, rt_http_server_finalize);

    server->port = port;
    server->router = rt_http_router_new();
    server->worker_pool = rt_threadpool_new(8);
    server->running = false;
    HTTPS_SERVER_MUTEX_INIT(&server->state_lock);
    server->state_lock_initialized = true;
    server->cert_file = dup_cstr(rt_string_cstr(cert_file));
    server->key_file = dup_cstr(rt_string_cstr(key_file));
    if (!server->cert_file || !server->key_file)
        rt_trap("HttpsServer: failed to copy certificate paths");
    {
        rt_tls_server_config_t tls_cfg;
        rt_tls_server_config_init(&tls_cfg);
        tls_cfg.cert_file = server->cert_file;
        tls_cfg.key_file = server->key_file;
        tls_cfg.alpn_protocol = "h2,http/1.1";
        server->tls_ctx = rt_tls_server_ctx_new(&tls_cfg);
        if (!server->tls_ctx)
            rt_trap(rt_tls_server_last_error());
    }

    return server;
}

/// @brief Helper used by every `Get`/`Post`/`Put`/`Delete` registrar.
///
/// Forwards `pattern` to the router via the method-specific `adder`
/// (one of `rt_http_router_{get,post,put,delete}`) and records the
/// `handler_tag` in the server's route entry so subsequent
/// `bind_handler` calls can resolve the binding by tag.
///
/// Traps on invalid handler tag or route-add failure.
///
/// @param obj         Server impl, opaque.
/// @param pattern     Route URL pattern (e.g. `"/users/:id"`).
/// @param handler_tag Tag string used to associate the route with a
///                    handler bound later.
/// @param adder       Method-specific router-add function pointer.
static void add_route_binding(void *obj,
                              rt_string pattern,
                              rt_string handler_tag,
                              void *(*adder)(void *, rt_string)) {
    if (!obj || !adder)
        return;

    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    if (https_server_is_running(server))
        rt_trap("HttpsServer: cannot add routes while running");
    adder(server->router, pattern);

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag || !append_route_entry(server, tag))
        rt_trap("HttpsServer: failed to register route");
}

/// @brief `HttpServer.Get(pattern, handler_tag)` — register a GET route.
/// @param obj         HttpServer handle.
/// @param pattern     URL pattern.
/// @param handler_tag Handler tag string (resolved by `BindHandler`).
void rt_https_server_get(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_get);
}

/// @brief `HttpServer.Post(pattern, handler_tag)` — register a POST route.
void rt_https_server_post(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_post);
}

/// @brief `HttpServer.Put(pattern, handler_tag)` — register a PUT route.
void rt_https_server_put(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_put);
}

/// @brief `HttpServer.Delete(pattern, handler_tag)` — register a DELETE route.
void rt_https_server_del(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_delete);
}

/// @brief `HttpServer.BindHandler(tag, native_fn)` — bind a native C
///        callback to a previously-registered route tag.
///
/// `entry` should be a function pointer with signature
/// `void(rt_http_server_handler_fn)(req, res)`. Stored via the shared
/// `set_handler_binding` infrastructure with `native_handler_dispatch`
/// as the trampoline so dispatch can route through the same code path
/// as script-bridged handlers.
///
/// @param obj         HttpServer handle.
/// @param handler_tag Tag string matching one previously passed to
///                    `Get`/`Post`/etc.
/// @param entry       Native handler function pointer cast to `void *`.
void rt_https_server_bind_handler(void *obj, rt_string handler_tag, void *entry) {
    if (!obj || !entry)
        return;

    if (https_server_is_running((rt_http_server_impl *)obj))
        rt_trap("HttpsServer: cannot bind handlers while running");

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag)
        return;

    if (!set_handler_binding(
            (rt_http_server_impl *)obj, tag, native_handler_dispatch, entry, NULL)) {
        rt_trap("HttpsServer: failed to bind handler");
    }
}

/// @brief Script-bridge variant of `BindHandler` that takes an explicit
///        dispatch function, opaque context, and optional cleanup.
///
/// Used by Zia/BASIC frontends to install a closure-style handler:
/// `dispatch` is the trampoline that invokes the script handler;
/// `ctx` carries the script's environment (closure captures, this-
/// pointer, etc.); `cleanup` is invoked when the binding is replaced
/// or the server is finalized.
///
/// @param obj         HttpServer handle.
/// @param handler_tag Handler tag string.
/// @param dispatch    Dispatch function `void(*)(ctx, req, res)`.
/// @param ctx         Opaque context passed to dispatch.
/// @param cleanup     Optional cleanup `void(*)(ctx)`, or NULL.
void rt_https_server_bind_handler_dispatch(
    void *obj, rt_string handler_tag, void *dispatch, void *ctx, void *cleanup) {
    if (!obj || !dispatch)
        return;

    if (https_server_is_running((rt_http_server_impl *)obj))
        rt_trap("HttpsServer: cannot bind handlers while running");

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag)
        return;

    if (!set_handler_binding((rt_http_server_impl *)obj,
                             tag,
                             (rt_http_server_handler_dispatch_fn)dispatch,
                             ctx,
                             (rt_http_server_handler_cleanup_fn)cleanup)) {
        rt_trap("HttpsServer: failed to bind handler");
    }
}

/// @brief `HttpServer.Start()` — open the listening socket and spin up
///        the accept loop on a background thread.
///
/// Idempotent: re-running on a server that's already running is a
/// silent no-op. Re-creates the worker pool if it was previously torn
/// down (e.g. by `Stop`). Traps on NULL receiver. The accept thread
/// terminates when `running` is cleared by `Stop`.
///
/// @param obj HttpServer handle.
void rt_https_server_start(void *obj) {
    if (!obj)
        rt_trap("HttpsServer: NULL server");

    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    if (https_server_is_running(server))
        return;

    // Create TCP server
    server->tcp_server = rt_tcp_server_listen(server->port);
    if (!server->tcp_server)
        rt_trap("HttpsServer: failed to bind listener");
    server->port = rt_tcp_server_port(server->tcp_server);
    if (!server->tls_ctx)
        rt_trap("HttpsServer: missing TLS context");
    if (!server->worker_pool)
        server->worker_pool = rt_threadpool_new(8);
    https_server_state_lock(server);
    server->running = true;
    https_server_state_unlock(server);

    // Start accept loop in background thread
#ifdef _WIN32
    server->accept_thread = CreateThread(NULL, 0, accept_loop, server, 0, NULL);
    server->thread_started = server->accept_thread != NULL;
#else
    server->thread_started = pthread_create(&server->accept_thread, NULL, accept_loop, server) == 0;
#endif
    if (!server->thread_started) {
        https_server_state_lock(server);
        server->running = false;
        https_server_state_unlock(server);
        if (server->tcp_server) {
            rt_tcp_server_close(server->tcp_server);
            if (rt_obj_release_check0(server->tcp_server))
                rt_obj_free(server->tcp_server);
            server->tcp_server = NULL;
        }
        rt_trap("HttpsServer: failed to start accept thread");
    }
}

/// @brief `HttpServer.Stop()` — tear down the listener and join the
///        accept thread.
///
/// Clears the running flag, closes the underlying TCP server (which
/// unblocks any in-progress `accept()` call), then waits up to 5s on
/// Windows (or unbounded on POSIX) for the accept thread to exit. Drains
/// any in-flight worker tasks before returning. NULL receiver is a
/// silent no-op. Safe to call repeatedly.
///
/// @param obj HttpServer handle.
void rt_https_server_stop(void *obj) {
    if (!obj)
        return;
    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    void *listener = NULL;
    int had_thread = 0;
#ifdef _WIN32
    HANDLE accept_thread = NULL;
#else
    pthread_t accept_thread;
#endif

    https_server_state_lock(server);
    server->running = false;
    listener = server->tcp_server;
    had_thread = server->thread_started ? 1 : 0;
    accept_thread = server->accept_thread;
    https_server_state_unlock(server);

    if (listener)
        rt_tcp_server_close(listener);

    {
        int conn_count = 0;
        void **active_conns = https_server_snapshot_active_conns(server, &conn_count);
        for (int i = 0; i < conn_count; i++)
            https_server_interrupt_conn(active_conns[i]);
        free(active_conns);
    }

    if (had_thread) {
#ifdef _WIN32
        WaitForSingleObject(accept_thread, 5000);
        CloseHandle(accept_thread);
#else
        pthread_join(accept_thread, NULL);
#endif
    }

    https_server_state_lock(server);
    if (server->tcp_server == listener)
        server->tcp_server = NULL;
    if (had_thread)
        server->thread_started = false;
    https_server_state_unlock(server);

    if (listener && rt_obj_release_check0(listener))
        rt_obj_free(listener);

    if (server->worker_pool)
        rt_threadpool_wait(server->worker_pool);
}

/// @brief `HttpServer.Port` — return the TCP port the server is bound to.
///
/// Returns 0 if the receiver is NULL or the server was constructed with
/// no explicit port and never started. After a successful `Start`, this
/// reflects the actual kernel-assigned port (useful when the server was
/// constructed with port 0 to request an ephemeral port).
///
/// @param obj HttpServer handle.
/// @return Bound TCP port, or 0 if not bound / NULL receiver.
int64_t rt_https_server_port(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_server_impl *)obj)->port;
}

/// @brief `HttpServer.IsRunning` — whether the accept loop is active.
///
/// Returns the latched `running` flag set by `Start` and cleared by
/// `Stop`. This is a snapshot — there is a brief window during teardown
/// where the flag is false but the accept thread is still draining.
///
/// @param obj HttpServer handle.
/// @return 1 if running, 0 if stopped or NULL receiver.
int8_t rt_https_server_is_running(void *obj) {
    if (!obj)
        return 0;
    return https_server_is_running((rt_http_server_impl *)obj) ? 1 : 0;
}

//=============================================================================
// ServerReq Accessors
//=============================================================================

/// @brief Synchronous router entry-point — parse a raw request string
///        and return the wire-format response.
///
/// Bypasses the socket, accept loop, and worker pool — useful for unit
/// tests and embedding scenarios where the host owns the I/O. The
/// pipeline is identical to the live path: parse → route → execute →
/// build wire response. Returns a 400 response if parsing fails.
/// Releases all transient header/body/req state before returning.
///
/// @param obj         HttpServer handle.
/// @param raw_request Raw HTTP request (request line + headers + body).
/// @return Owned `rt_string` containing the full HTTP/1.1 response.
HTTPS_MAYBE_UNUSED void *rt_https_server_process_request(void *obj, rt_string raw_request) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    const char *raw = rt_string_cstr(raw_request);
    if (!raw)
        return rt_string_from_bytes("", 0);

    server_req_t req;
    server_res_t res;
    memset(&res, 0, sizeof(res));

    if (!parse_http_request(raw, strlen(raw), &req)) {
        return rt_string_from_bytes(
            "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            sizeof("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n") -
                1);
    }

    build_route_response((rt_http_server_impl *)obj, &req, &res);

    int keep_alive = request_allows_keep_alive(&req) && !response_forces_close(&res);
    size_t resp_len = 0;
    char *resp = build_response(&res, keep_alive, &resp_len);
    rt_string result = resp ? rt_string_from_bytes(resp, resp_len) : rt_string_from_bytes("", 0);

    free(resp);
    free(res.body);
    if (res.headers && rt_obj_release_check0(res.headers))
        rt_obj_free(res.headers);
    free_server_req(&req);
    return result;
}
