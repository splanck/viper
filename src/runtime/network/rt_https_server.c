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
// HTTP Request Parsing
//=============================================================================

/// @brief Locate the next `\r\n` line terminator within a length-bounded
///        buffer.
///
/// Used to walk request lines and headers without requiring NUL
/// termination (the receive buffer holds raw network bytes).
///
/// @param buf Buffer start.
/// @param len Buffer length in bytes.
/// @return Pointer to the first `\r` of a `\r\n` pair, or NULL if no
///         terminator is present in `[buf, buf+len)`.
static const char *find_crlf(const char *buf, size_t len) {
    if (len < 2)
        return NULL;
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return buf + i;
    }
    return NULL;
}

/// @brief Locate the end of an HTTP request header section (`\r\n\r\n`).
///
/// HTTP headers are followed by an empty line; this returns a pointer
/// to the first byte *after* that empty-line terminator (i.e. where
/// the body begins, or the end of the buffer for header-only requests).
///
/// @param buf Buffer start.
/// @param len Buffer length in bytes.
/// @return Pointer one past the `\r\n\r\n` separator, or NULL if not yet
///         present (caller should keep reading).
static const char *find_header_end(const char *buf, size_t len) {
    if (len < 4)
        return NULL;
    for (size_t i = 3; i < len; i++) {
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n')
            return buf + i + 1;
    }
    return NULL;
}

/// @brief `memchr`-equivalent for length-bounded buffers (single-byte
///        needle).
///
/// Used by header parsing to find spaces, colons, etc. without the
/// `strchr` requirement of NUL termination.
///
/// @param buf    Buffer start.
/// @param len    Buffer length in bytes.
/// @param needle Byte to search for.
/// @return Pointer to first occurrence of `needle`, or NULL if not present.
static const char *find_char_bounded(const char *buf, size_t len, char needle) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == needle)
            return buf + i;
    }
    return NULL;
}

/// @brief Detect any CR or LF byte in a NUL-terminated string.
///
/// Header injection guard: caller-supplied header values are validated
/// with this helper before being concatenated into the wire response,
/// so a malicious value can't smuggle in `\r\nFake-Header: x\r\n`.
///
/// @param text Source string. NULL returns false.
/// @return Non-zero if any CR or LF byte is present.
static int contains_crlf(const char *text) {
    if (!text)
        return 0;
    for (; *text; text++) {
        if (*text == '\r' || *text == '\n')
            return 1;
    }
    return 0;
}

/// @brief Test whether a header name is one the server manages itself.
///
/// `Content-Length`, `Connection`, and `Transfer-Encoding` are computed
/// from the response body and connection state, so user code is not
/// allowed to set them via `Response.SetHeader`. Comparison is
/// case-insensitive (HTTP header names are case-insensitive per
/// RFC 7230 §3.2).
///
/// @param name Candidate header name; NULL returns 0.
/// @return Non-zero when the server owns this header.
static int is_server_managed_header_name(const char *name) {
    return name &&
           (strcasecmp(name, "Content-Length") == 0 || strcasecmp(name, "Connection") == 0 ||
            strcasecmp(name, "Transfer-Encoding") == 0);
}

/// @brief Parse a decimal `size_t` from a length-bounded substring.
///
/// Used for parsing the `Content-Length` header value. Skips leading
/// whitespace, accepts trailing whitespace, and rejects non-digit
/// content. Detects multiplication overflow before it can wrap.
///
/// @param text Buffer holding the candidate digits.
/// @param len  Buffer length.
/// @param out  Out-param receiving the parsed value on success.
/// @return `true` on successful parse, `false` on empty input,
///         non-digit content, or arithmetic overflow.
static bool parse_size_decimal(const char *text, size_t len, size_t *out) {
    size_t value = 0;
    size_t i = 0;

    while (i < len && (text[i] == ' ' || text[i] == '\t'))
        i++;
    if (i == len)
        return false;

    for (; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= '0' && c <= '9') {
            size_t digit = (size_t)(c - '0');
            if (value > (SIZE_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
            continue;
        }
        if (c == ' ' || c == '\t') {
            while (i < len && (text[i] == ' ' || text[i] == '\t'))
                i++;
            if (i != len)
                return false;
            *out = value;
            return true;
        }
        return false;
    }

    *out = value;
    return true;
}

/// @brief Return true if a comma-separated header value contains @p token case-insensitively.
static bool header_value_contains_token(
    const char *value, size_t value_len, const char *token, size_t token_len) {
    const char *p = value;
    const char *end = value + value_len;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == ','))
            p++;
        const char *token_start = p;
        while (p < end && *p != ',' && *p != ';')
            p++;
        const char *token_end = p;
        while (token_end > token_start && (token_end[-1] == ' ' || token_end[-1] == '\t'))
            token_end--;
        if ((size_t)(token_end - token_start) == token_len &&
            strncasecmp(token_start, token, token_len) == 0) {
            return true;
        }
        while (p < end && *p != ',')
            p++;
    }

    return false;
}

/// @brief Parse an HTTP chunk-size line (hex digits with optional extensions after ';').
static bool parse_chunk_size_line(const char *line, size_t len, size_t *chunk_size_out) {
    size_t value = 0;
    size_t i = 0;
    bool saw_digit = false;

    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    for (; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        unsigned digit = 0;
        if (c == ';')
            break;
        if (c >= '0' && c <= '9')
            digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned)(c - 'A' + 10);
        else if (c == ' ' || c == '\t') {
            while (i < len && (line[i] == ' ' || line[i] == '\t'))
                i++;
            if (i < len && line[i] != ';')
                return false;
            break;
        } else {
            return false;
        }

        if (value > (SIZE_MAX - digit) / 16)
            return false;
        value = value * 16 + digit;
        saw_digit = true;
    }

    if (!saw_digit || value > HTTP_REQ_MAX_BODY)
        return false;
    *chunk_size_out = value;
    return true;
}

/// @brief Decode a chunked request body.
/// @return parse status; on success returns decoded bytes and consumed encoded length if requested.
static chunk_parse_status_t decode_chunked_body(const char *encoded,
                                                size_t encoded_len,
                                                char **decoded_out,
                                                size_t *decoded_len_out,
                                                size_t *consumed_len_out) {
    size_t pos = 0;
    size_t decoded_len = 0;
    size_t decoded_cap = 0;
    char *decoded = NULL;

    while (1) {
        const char *line_end = find_crlf(encoded + pos, encoded_len - pos);
        size_t chunk_size = 0;
        if (!line_end) {
            free(decoded);
            return CHUNK_PARSE_INCOMPLETE;
        }
        if (!parse_chunk_size_line(encoded + pos, (size_t)(line_end - (encoded + pos)), &chunk_size)) {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }

        pos = (size_t)((line_end - encoded) + 2);
        if (chunk_size > HTTP_REQ_MAX_BODY || decoded_len + chunk_size > HTTP_REQ_MAX_BODY) {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }
        if (chunk_size == 0) {
            while (1) {
                const char *trailer_end = find_crlf(encoded + pos, encoded_len - pos);
                if (!trailer_end) {
                    free(decoded);
                    return CHUNK_PARSE_INCOMPLETE;
                }
                if (trailer_end == encoded + pos) {
                    pos += 2;
                    if (decoded_out) {
                        if (!decoded) {
                            decoded = (char *)malloc(1);
                            if (!decoded)
                                return CHUNK_PARSE_INVALID;
                        }
                        decoded[decoded_len] = '\0';
                        *decoded_out = decoded;
                    }
                    if (decoded_len_out)
                        *decoded_len_out = decoded_len;
                    if (consumed_len_out)
                        *consumed_len_out = pos;
                    return CHUNK_PARSE_OK;
                }
                pos = (size_t)((trailer_end - encoded) + 2);
            }
        }
        if (encoded_len - pos < chunk_size + 2) {
            free(decoded);
            return CHUNK_PARSE_INCOMPLETE;
        }

        if (decoded_out && chunk_size > 0) {
            if (decoded_len + chunk_size + 1 > decoded_cap) {
                size_t next_cap = decoded_cap ? decoded_cap : 256;
                while (next_cap < decoded_len + chunk_size + 1)
                    next_cap *= 2;
                char *grown = (char *)realloc(decoded, next_cap);
                if (!grown) {
                    free(decoded);
                    return CHUNK_PARSE_INVALID;
                }
                decoded = grown;
                decoded_cap = next_cap;
            }
            memcpy(decoded + decoded_len, encoded + pos, chunk_size);
        }
        decoded_len += chunk_size;
        pos += chunk_size;

        if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }
        pos += 2;
    }
}

/// @brief Scan a raw HTTP header block to extract `Content-Length` and
///        `Transfer-Encoding: chunked` framing hints.
///
/// Used during request reception to decide whether more body bytes are
/// expected and where the body ends. Stops at the empty line marking
/// the end of headers. Duplicate `Content-Length` headers are rejected
/// per RFC 7230 §3.3.2 (the spec requires a single value to avoid
/// request-smuggling ambiguity).
///
/// @param headers            Header block start (after the request line).
/// @param headers_len        Header block length in bytes.
/// @param content_length_out Out-param receiving parsed content length
///                           (`0` when absent).
/// @param chunked_out        Optional out-param set to `true` when
///                           `Transfer-Encoding: chunked` was seen.
/// @return `true` on successful parse, `false` on malformed headers
///         (truncated lines, duplicate Content-Length, bad numeric value).
static bool parse_content_length_header_block(const char *headers,
                                              size_t headers_len,
                                              size_t *content_length_out,
                                              bool *chunked_out) {
    const char *p = headers;
    const char *end = headers + headers_len;
    size_t content_length = 0;
    bool saw_content_length = false;
    bool saw_chunked = false;

    while (p < end) {
        const char *line_end = find_crlf(p, (size_t)(end - p));
        if (!line_end)
            return false;
        if (line_end == p)
            break;

        const char *colon = find_char_bounded(p, (size_t)(line_end - p), ':');
        if (colon) {
            size_t name_len = (size_t)(colon - p);
            if (name_len == strlen("Content-Length") &&
                strncasecmp(p, "Content-Length", name_len) == 0) {
                const char *value = colon + 1;
                while (value < line_end && (*value == ' ' || *value == '\t'))
                    value++;
                if (saw_content_length)
                    return false;
                if (!parse_size_decimal(value, (size_t)(line_end - value), &content_length))
                    return false;
                saw_content_length = true;
            } else if (name_len == strlen("Transfer-Encoding") &&
                       strncasecmp(p, "Transfer-Encoding", name_len) == 0) {
                const char *value = colon + 1;
                while (value < line_end && (*value == ' ' || *value == '\t'))
                    value++;
                if (header_value_contains_token(value, (size_t)(line_end - value), "chunked", 7))
                    saw_chunked = true;
            }
        }

        p = line_end + 2;
    }

    if (saw_chunked && saw_content_length)
        return false;
    *content_length_out = saw_content_length ? content_length : 0;
    if (chunked_out)
        *chunked_out = saw_chunked;
    return true;
}

/// @brief Parse an HTTP request from raw text.
static bool parse_http_request(const char *raw, size_t raw_len, server_req_t *req) {
    memset(req, 0, sizeof(*req));

    // Find end of request line
    const char *line_end = find_crlf(raw, raw_len);
    if (!line_end)
        return false;

    // Parse method
    const char *p = raw;
    const char *space = find_char_bounded(p, (size_t)(line_end - p), ' ');
    if (!space || space > line_end)
        return false;

    size_t method_len = (size_t)(space - p);
    req->method = (char *)malloc(method_len + 1);
    if (!req->method)
        return false;
    memcpy(req->method, p, method_len);
    req->method[method_len] = '\0';

    // Parse path (and query string)
    p = space + 1;
    space = find_char_bounded(p, (size_t)(line_end - p), ' ');
    if (!space || space > line_end)
        goto fail;

    size_t uri_len = (size_t)(space - p);
    char *uri = (char *)malloc(uri_len + 1);
    if (!uri)
        goto fail;
    memcpy(uri, p, uri_len);
    uri[uri_len] = '\0';

    // Split path and query
    char *qmark = strchr(uri, '?');
    if (qmark) {
        *qmark = '\0';
        req->query = strdup(qmark + 1);
        if (!req->query) {
            free(uri);
            goto fail;
        }
    }
    req->path = strdup(uri);
    free(uri);
    if (!req->path)
        goto fail;

    p = space + 1;
    if ((size_t)(line_end - p) != strlen("HTTP/1.1") || strncmp(p, "HTTP/1.", 7) != 0 ||
        (p[7] != '0' && p[7] != '1')) {
        goto fail;
    }
    req->http_major = 1;
    req->http_minor = p[7] - '0';

    // Parse headers
    req->headers = rt_map_new();
    if (!req->headers)
        goto fail;
    p = line_end + 2;
    const char *headers_begin = p;
    size_t content_length = 0;
    const char *body_start = NULL;

    int header_count = 0;
    while (p < raw + raw_len && header_count < HTTP_REQ_MAX_HEADERS) {
        const char *next_end = find_crlf(p, (size_t)(raw + raw_len - p));
        if (!next_end)
            goto fail;
        if (next_end == p) {
            body_start = next_end + 2;
            break; // Empty line = end of headers
        }

        const char *colon = find_char_bounded(p, (size_t)(next_end - p), ':');
        if (colon && colon < next_end) {
            size_t name_len = (size_t)(colon - p);
            char *name = (char *)malloc(name_len + 1);
            if (name) {
                memcpy(name, p, name_len);
                name[name_len] = '\0';
                // Lowercase for case-insensitive lookup
                for (char *c = name; *c; c++)
                    if (*c >= 'A' && *c <= 'Z')
                        *c += 32;

                const char *val = colon + 1;
                while (val < next_end && (*val == ' ' || *val == '\t'))
                    val++;
                size_t val_len = (size_t)(next_end - val);

                rt_string key = rt_string_from_bytes(name, name_len);
                rt_string value = rt_string_from_bytes(val, val_len);
                rt_map_set(req->headers, key, (void *)value);
                rt_string_unref(key);
                rt_string_unref(value);
                free(name);
            }
        }

        p = next_end + 2;
        header_count++;
    }
    if (!body_start && header_count >= HTTP_REQ_MAX_HEADERS)
        goto fail;
    if (!body_start)
        body_start = p;

    bool chunked = false;
    size_t headers_len = body_start > headers_begin ? (size_t)(body_start - headers_begin - 2) : 0;
    if (!parse_content_length_header_block(headers_begin, headers_len, &content_length, &chunked))
        goto fail;

    // Parse body
    if (chunked) {
        size_t available = (size_t)(raw + raw_len - body_start);
        size_t consumed_len = 0;
        char *decoded_body = NULL;
        if (decode_chunked_body(
                body_start, available, &decoded_body, &req->body_len, &consumed_len) != CHUNK_PARSE_OK)
            goto fail;
        req->body = decoded_body;
    } else if (content_length > HTTP_REQ_MAX_BODY)
        goto fail;
    else if (content_length > 0) {
        size_t available = (size_t)(raw + raw_len - body_start);
        if (available < content_length)
            goto fail;
        req->body = (char *)malloc(content_length + 1);
        if (req->body) {
            memcpy(req->body, body_start, content_length);
            req->body[content_length] = '\0';
            req->body_len = content_length;
        } else {
            goto fail;
        }
    }

    return true;

fail:
    free_server_req(req);
    memset(req, 0, sizeof(*req));
    return false;
}

static const char *map_get_lower_header_cstr(void *headers, const char *lower_name) {
    if (!headers || !lower_name)
        return NULL;
    rt_string key = rt_const_cstr(lower_name);
    void *val = rt_map_get(headers, key);
    if (!val)
        return NULL;
    return rt_string_cstr((rt_string)val);
}

static int request_allows_keep_alive(const server_req_t *req) {
    const char *connection =
        req ? map_get_lower_header_cstr(req->headers, "connection") : NULL;
    if (!req)
        return 0;
    if (req->http_major > 1 || (req->http_major == 1 && req->http_minor >= 1)) {
        if (!connection)
            return 1;
        return !header_value_contains_token(connection, strlen(connection), "close", 5);
    }
    if (!connection)
        return 0;
    return header_value_contains_token(connection, strlen(connection), "keep-alive", 10);
}

static int response_forces_close(const server_res_t *res) {
    const char *connection =
        res ? map_get_lower_header_cstr(res->headers, "connection") : NULL;
    if (!connection)
        return 0;
    return header_value_contains_token(connection, strlen(connection), "close", 5);
}

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

/// @brief Map an HTTP status code to its canonical reason-phrase.
///
/// Covers the codes the HttpServer runtime currently emits (the common
/// 2xx, 3xx, 4xx, 5xx subset). Falls back to `"Unknown"` for codes the
/// table doesn't list — the response still goes out, just with a
/// non-standard reason phrase. Matches RFC 7231 §6.1 wording.
///
/// @param code HTTP status code.
/// @return Pointer to a static string literal with the reason phrase.
static const char *status_text_for_code(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

/// @brief Build HTTP response string.
static char *build_response(server_res_t *res, int keep_alive, size_t *out_len) {
    size_t body_len = res->body_len;
    size_t cap = (size_t)snprintf(
        NULL, 0, "HTTP/1.1 %d %s\r\n", res->status_code, status_text_for_code(res->status_code));
    cap += (size_t)snprintf(NULL, 0, "Content-Length: %zu\r\n", body_len);
    cap += strlen(keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");

    if (res->headers) {
        void *keys = rt_map_keys(res->headers);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *val = rt_map_get(res->headers, key);
            if (val) {
                const char *k = rt_string_cstr(key);
                const char *v = rt_string_cstr((rt_string)val);
                if (k && v && !contains_crlf(k) && !contains_crlf(v) &&
                    !is_server_managed_header_name(k)) {
                    size_t k_len = strlen(k);
                    size_t v_len = strlen(v);
                    if (cap > SIZE_MAX - (k_len + v_len + 4)) {
                        if (rt_obj_release_check0(keys))
                            rt_obj_free(keys);
                        return NULL;
                    }
                    cap += k_len + v_len + 4;
                }
            }
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }
    if (cap > SIZE_MAX - (body_len + 3))
        return NULL;
    cap += body_len + 2 + 1;

    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;

    char *cursor = buf;
    size_t remaining = cap;
#define APPEND_FORMAT(...)                                                                         \
    do {                                                                                           \
        int written = snprintf(cursor, remaining, __VA_ARGS__);                                    \
        if (written < 0 || (size_t)written >= remaining) {                                         \
            free(buf);                                                                             \
            return NULL;                                                                           \
        }                                                                                          \
        cursor += written;                                                                         \
        remaining -= (size_t)written;                                                              \
    } while (0)

    APPEND_FORMAT("HTTP/1.1 %d %s\r\n", res->status_code, status_text_for_code(res->status_code));
    APPEND_FORMAT("Content-Length: %zu\r\n", body_len);
    APPEND_FORMAT("%s", keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");

    if (res->headers) {
        void *keys = rt_map_keys(res->headers);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *val = rt_map_get(res->headers, key);
            if (val) {
                const char *k = rt_string_cstr(key);
                const char *v = rt_string_cstr((rt_string)val);
                if (k && v && !contains_crlf(k) && !contains_crlf(v) &&
                    !is_server_managed_header_name(k)) {
                    int written = snprintf(cursor, remaining, "%s: %s\r\n", k, v);
                    if (written < 0 || (size_t)written >= remaining) {
                        if (rt_obj_release_check0(keys))
                            rt_obj_free(keys);
                        free(buf);
                        return NULL;
                    }
                    cursor += written;
                    remaining -= (size_t)written;
                }
            }
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }

    APPEND_FORMAT("%s", "\r\n");

    // Body
    if (res->body && body_len > 0) {
        if (body_len > remaining) {
            free(buf);
            return NULL;
        }
        memcpy(cursor, res->body, body_len);
        cursor += body_len;
        remaining -= body_len;
    }

    *out_len = (size_t)(cursor - buf);
#undef APPEND_FORMAT
    return buf;
}

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
        tls_cfg.alpn_protocol = "http/1.1";
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

/// @brief `ServerReq.Method` — return the HTTP method as a string.
///
/// Returns "GET", "POST", "PUT", "DELETE", etc. as parsed from the
/// request line. Returns "" for NULL receiver or a malformed request.
/// The result is a fresh string the caller owns.
///
/// @param obj ServerReq handle.
/// @return Owned `rt_string` containing the method, never NULL.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_method_unused(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->method ? rt_string_from_bytes(req->method, strlen(req->method))
                       : rt_string_from_bytes("", 0);
}

/// @brief `ServerReq.Path` — return the request path (without query string).
///
/// The query string is stripped during request parsing and stored
/// separately (accessible via `Query`). Returns "" for NULL receiver
/// or a request with no path (defensive only — the parser rejects
/// such inputs upstream).
///
/// @param obj ServerReq handle.
/// @return Owned `rt_string` containing the path, never NULL.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_path_unused(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->path ? rt_string_from_bytes(req->path, strlen(req->path))
                     : rt_string_from_bytes("", 0);
}

/// @brief `ServerReq.Body` — return the raw request body.
///
/// The body is exactly what came over the wire after the headers — no
/// content-decoding, no charset normalization. Length is taken from the
/// parsed Content-Length, so binary bodies with embedded NULs are
/// preserved. Returns "" if there is no body or the receiver is NULL.
///
/// @param obj ServerReq handle.
/// @return Owned `rt_string` containing the body bytes, never NULL.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_body_unused(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->body ? rt_string_from_bytes(req->body, req->body_len) : rt_string_from_bytes("", 0);
}

/// @brief `ServerReq.Header(name)` — case-insensitive header lookup.
///
/// HTTP header names are case-insensitive per RFC 7230 §3.2, so the
/// lookup name is lowercased before consulting the parsed header map
/// (which stores keys in lowercase form). Allocates a transient lower
/// buffer for the lookup; returns "" for missing headers, NULL receiver,
/// NULL name, or allocation failure. The returned string is a fresh
/// copy so the caller can outlive the request object.
///
/// @param obj  ServerReq handle.
/// @param name Header name (any case).
/// @return Owned `rt_string` with header value, or "" if not present.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_header_unused(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    if (!req->headers)
        return rt_string_from_bytes("", 0);

    const char *name_cstr = rt_string_cstr(name);
    if (!name_cstr)
        return rt_string_from_bytes("", 0);

    size_t len = strlen(name_cstr);
    char *lower = (char *)malloc(len + 1);
    if (!lower)
        return rt_string_from_bytes("", 0);
    for (size_t i = 0; i <= len; i++) {
        char c = name_cstr[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }

    rt_string key = rt_string_from_bytes(lower, len);
    free(lower);
    void *val = rt_map_get(req->headers, key);
    rt_string_unref(key);
    if (!val)
        return rt_string_from_bytes("", 0);

    rt_string header = (rt_string)val;
    const char *header_cstr = rt_string_cstr(header);
    return header_cstr ? rt_string_from_bytes(header_cstr, strlen(header_cstr))
                       : rt_string_from_bytes("", 0);
}

/// @brief `ServerReq.Param(name)` — read a captured route parameter.
///
/// Route parameters are the placeholders in route patterns like
/// `/users/:id` — at match time the segment is captured and stored on
/// the request's `params` table. Returns "" if the parameter wasn't
/// captured for this request, the receiver is NULL, or the params
/// table is missing.
///
/// @param obj  ServerReq handle.
/// @param name Parameter name (without the leading `:`).
/// @return Owned `rt_string` with the captured value, or "" if absent.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_param_unused(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    if (!req->params)
        return rt_string_from_bytes("", 0);
    rt_string value = rt_route_match_param(req->params, name);
    const char *value_cstr = rt_string_cstr(value);
    return value_cstr ? rt_string_from_bytes(value_cstr, strlen(value_cstr))
                      : rt_string_from_bytes("", 0);
}

/// @brief `ServerReq.Query(name)` — read a query-string value.
///
/// Linear-scans the raw query string for `name=value` pairs separated
/// by `&`. The matched value is URL-decoded (percent-escapes and `+`
/// → space) before being returned. Only the first occurrence of a
/// duplicated name is returned. Returns "" if the parameter is absent,
/// the receiver/name is NULL, or no query string was present.
///
/// @param obj  ServerReq handle.
/// @param name Query-parameter name.
/// @return Owned `rt_string` with the URL-decoded value, or "" if absent.
static HTTPS_MAYBE_UNUSED rt_string https_server_req_query_unused(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    if (!req->query)
        return rt_string_from_bytes("", 0);

    const char *n = rt_string_cstr(name);
    if (!n)
        return rt_string_from_bytes("", 0);

    // Simple query string parsing: find name=value
    size_t nlen = strlen(n);
    const char *p = req->query;
    while (p && *p) {
        if (strncmp(p, n, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            const char *amp = strchr(val, '&');
            size_t vlen = amp ? (size_t)(amp - val) : strlen(val);
            rt_string raw = rt_string_from_bytes(val, vlen);
            rt_string decoded = rt_url_decode(raw);
            rt_string_unref(raw);
            return decoded;
        }
        const char *amp = strchr(p, '&');
        p = amp ? amp + 1 : NULL;
    }

    return rt_string_from_bytes("", 0);
}

//=============================================================================
// ServerRes Accessors
//=============================================================================

/// @brief `ServerRes.Status(code)` — set the HTTP status code.
///
/// Stores `code` on the response builder. No range validation: callers
/// can set non-standard codes (e.g., 418). Returns the receiver to
/// support chained builder syntax: `res.Status(404).Send("Not Found")`.
/// NULL receiver is a no-op that returns NULL.
///
/// @param obj  ServerRes handle.
/// @param code HTTP status code (e.g., 200, 404, 500).
/// @return The same `obj` for chaining.
static HTTPS_MAYBE_UNUSED void *https_server_res_status_unused(void *obj, int64_t code) {
    if (!obj)
        return obj;
    server_res_t *res = (server_res_t *)obj;
    res->status_code = (int)code;
    return obj;
}

/// @brief `ServerRes.Header(name, value)` — set a custom response header.
///
/// Adds an entry to the response header map (creating it lazily on
/// first call). Silently rejects:
///   - NULL or non-c-string name/value (defensive),
///   - any name or value containing CR or LF (header injection guard),
///   - "server-managed" header names like Content-Length,
///     Transfer-Encoding, and Connection that the framework owns.
/// Returns the receiver for chaining. NULL receiver is a no-op.
///
/// @param obj   ServerRes handle.
/// @param name  Header name.
/// @param value Header value.
/// @return The same `obj` for chaining.
static HTTPS_MAYBE_UNUSED void *https_server_res_header_unused(
    void *obj, rt_string name, rt_string value) {
    if (!obj)
        return obj;
    server_res_t *res = (server_res_t *)obj;
    if (!res->headers)
        res->headers = rt_map_new();
    const char *name_cstr = rt_string_cstr(name);
    const char *value_cstr = rt_string_cstr(value);
    if (!name_cstr || !value_cstr || contains_crlf(name_cstr) || contains_crlf(value_cstr) ||
        is_server_managed_header_name(name_cstr))
        return obj;
    rt_map_set(res->headers, name, (void *)value);
    return obj;
}

/// @brief `ServerRes.Send(body)` — finalize the response with a text body.
///
/// Replaces any previously buffered body, captures the new body via
/// `strdup` (so the response outlives the source string), and marks
/// the response as `sent` so the dispatch loop knows to flush rather
/// than fall through to the default 404. NULL receiver is a no-op;
/// NULL body collapses to a zero-length response.
///
/// @param obj  ServerRes handle.
/// @param body Body bytes to send (may be NULL → empty body).
static HTTPS_MAYBE_UNUSED void https_server_res_send_unused(void *obj, rt_string body) {
    if (!obj)
        return;
    server_res_t *res = (server_res_t *)obj;
    const char *b = rt_string_cstr(body);
    free(res->body);
    res->body = b ? strdup(b) : NULL;
    res->body_len = b ? strlen(b) : 0;
    res->sent = true;
}

/// @brief `ServerRes.Json(jsonStr)` — finalize as a JSON response.
///
/// Convenience wrapper that sets `Content-Type: application/json`
/// and delegates the body capture to `rt_server_res_send`. Does *not*
/// validate that `json_str` is well-formed JSON — that's the caller's
/// responsibility. NULL receiver is a no-op.
///
/// @param obj      ServerRes handle.
/// @param json_str Pre-serialized JSON body.
static HTTPS_MAYBE_UNUSED void https_server_res_json_unused(void *obj, rt_string json_str) {
    if (!obj)
        return;
    server_res_t *res = (server_res_t *)obj;
    if (!res->headers)
        res->headers = rt_map_new();
    rt_string ct_key = rt_const_cstr("Content-Type");
    rt_string ct_val = rt_const_cstr("application/json");
    rt_map_set(res->headers, ct_key, (void *)ct_val);
    https_server_res_send_unused(obj, json_str);
}

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
