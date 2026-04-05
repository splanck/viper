//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_server.c
// Purpose: Threaded HTTP/1.1 server with routing and request/response objects.
// Key invariants:
//   - Accept loop runs in a dedicated thread.
//   - Each request dispatched to thread pool for concurrent handling.
//   - Connection: close after each request (no keep-alive in v1).
//   - Routes matched via embedded HttpRouter.
// Ownership/Lifetime:
//   - Server object GC-managed. Stop() or finalizer stops accept loop.
//   - Per-request ServerReq/ServerRes are stack-allocated (not GC).
// Links: rt_http_server.h (API), rt_http_router.h (routing)
//
//===----------------------------------------------------------------------===//

#include "rt_http_server.h"
#include "rt_http_router.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threadpool.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "rt_trap.h"
extern void rt_trap_net(const char *msg, int err_code);

//=============================================================================
// Internal Structures
//=============================================================================

#define HTTP_REQ_MAX_LINE 8192
#define HTTP_REQ_MAX_HEADERS 100
#define HTTP_REQ_MAX_BODY (16 * 1024 * 1024) // 16 MB

typedef struct {
    char *method;
    char *path;
    char *query;
    char *body;
    size_t body_len;
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
} rt_http_server_impl;

static void free_server_req(server_req_t *req);
static void build_route_response(rt_http_server_impl *server, server_req_t *req, server_res_t *res);
static void free_route_entries(rt_http_server_impl *server);
static void free_handler_bindings(rt_http_server_impl *server);

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

static void release_handler_binding(handler_binding_t *binding) {
    if (!binding)
        return;
    if (binding->cleanup)
        binding->cleanup(binding->ctx);
    free(binding->tag);
    memset(binding, 0, sizeof(*binding));
}

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

static void native_handler_dispatch(void *ctx, void *req, void *res) {
    if (!ctx)
        return;
    ((rt_http_server_handler_fn)ctx)(req, res);
}

//=============================================================================
// Finalizer
//=============================================================================

static void rt_http_server_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    rt_http_server_stop(server);
    free_route_entries(server);
    free_handler_bindings(server);
    if (server->router && rt_obj_release_check0(server->router))
        rt_obj_free(server->router);
    server->router = NULL;
    if (server->worker_pool && rt_obj_release_check0(server->worker_pool))
        rt_obj_free(server->worker_pool);
    server->worker_pool = NULL;
}

//=============================================================================
// HTTP Request Parsing
//=============================================================================

static const char *find_crlf(const char *buf, size_t len) {
    if (len < 2)
        return NULL;
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return buf + i;
    }
    return NULL;
}

static const char *find_header_end(const char *buf, size_t len) {
    if (len < 4)
        return NULL;
    for (size_t i = 3; i < len; i++) {
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n')
            return buf + i + 1;
    }
    return NULL;
}

static const char *find_char_bounded(const char *buf, size_t len, char needle) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == needle)
            return buf + i;
    }
    return NULL;
}

static int contains_crlf(const char *text) {
    if (!text)
        return 0;
    for (; *text; text++) {
        if (*text == '\r' || *text == '\n')
            return 1;
    }
    return 0;
}

static int is_server_managed_header_name(const char *name) {
    return name &&
           (strcasecmp(name, "Content-Length") == 0 || strcasecmp(name, "Connection") == 0 ||
            strcasecmp(name, "Transfer-Encoding") == 0);
}

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
                if (strncasecmp(value, "chunked", 7) == 0)
                    saw_chunked = true;
            }
        }

        p = line_end + 2;
    }

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
    if (!parse_content_length_header_block(headers_begin, headers_len, &content_length, &chunked) ||
        chunked)
        goto fail;

    // Parse body
    if (content_length > HTTP_REQ_MAX_BODY)
        goto fail;
    if (content_length > 0) {
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

int rt_http_server_test_parse_request(const char *raw,
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
static char *build_response(server_res_t *res, size_t *out_len) {
    size_t body_len = res->body_len;
    size_t cap = (size_t)snprintf(
        NULL, 0, "HTTP/1.1 %d %s\r\n", res->status_code, status_text_for_code(res->status_code));
    cap += (size_t)snprintf(NULL, 0, "Content-Length: %zu\r\n", body_len);
    cap += strlen("Connection: close\r\n");

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
    APPEND_FORMAT("%s", "Connection: close\r\n");

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

char *rt_http_server_test_build_response(int status_code,
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

    char *resp = build_response(&res, out_len);
    if (res.headers && rt_obj_release_check0(res.headers))
        rt_obj_free(res.headers);
    free(body_copy);
    return resp;
}

//=============================================================================
// Request Handler
//=============================================================================

static void handle_connection(rt_http_server_impl *server, void *tcp) {
    // Read request (up to 64KB headers + body)
    size_t buf_cap = 65536;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) {
        rt_tcp_close(tcp);
        return;
    }

    size_t buf_len = 0;
    bool headers_done = false;
    size_t content_length = 0;
    size_t header_end_pos = 0;
    bool bad_request = false;

    // Set a 30-second timeout for reading
    rt_tcp_set_recv_timeout(tcp, 30000);

    while (buf_len < buf_cap && rt_tcp_is_open(tcp)) {
        void *data = rt_tcp_recv(tcp, (int64_t)(buf_cap - buf_len));
        int64_t data_len = rt_bytes_len(data);
        if (data_len <= 0) {
            if (data && rt_obj_release_check0(data))
                rt_obj_free(data);
            break;
        }

        typedef struct {
            int64_t len;
            uint8_t *d;
        } bi;

        uint8_t *ptr = ((bi *)data)->d;
        memcpy(buf + buf_len, ptr, (size_t)data_len);
        buf_len += (size_t)data_len;
        if (data && rt_obj_release_check0(data))
            rt_obj_free(data);

        // Check if headers are complete
        if (!headers_done) {
            const char *header_end = find_header_end(buf, buf_len);
            if (header_end) {
                headers_done = true;
                header_end_pos = (size_t)(header_end - buf);
                bool chunked = false;
                if (!parse_content_length_header_block(
                        buf, header_end_pos, &content_length, &chunked) ||
                    chunked || content_length > HTTP_REQ_MAX_BODY) {
                    bad_request = true;
                    break;
                }
            }
        }

        // Check if we have the full body
        if (bad_request)
            break;
        if (headers_done) {
            if (content_length == 0 || buf_len >= header_end_pos + content_length)
                break;
            // Grow buffer for large bodies
            if (header_end_pos + content_length > buf_cap) {
                buf_cap = header_end_pos + content_length + 1;
                char *new_buf = (char *)realloc(buf, buf_cap);
                if (!new_buf)
                    break;
                buf = new_buf;
            }
        }
    }

    if (buf_len == 0) {
        free(buf);
        rt_tcp_close(tcp);
        return;
    }

    if (buf_len < buf_cap)
        buf[buf_len] = '\0';
    else
        buf[buf_cap - 1] = '\0';

    // Parse request
    server_req_t req;
    if (bad_request || !parse_http_request(buf, buf_len, &req)) {
        // Send 400 Bad Request
        const char *bad =
            "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        rt_string bad_str = rt_const_cstr(bad);
        rt_tcp_send_str(tcp, bad_str);
        free(buf);
        rt_tcp_close(tcp);
        return;
    }
    free(buf);

    server_res_t res;
    memset(&res, 0, sizeof(res));
    build_route_response(server, &req, &res);

    // Send response
    size_t resp_len;
    char *resp = build_response(&res, &resp_len);
    if (resp) {
        rt_tcp_send_all_raw(tcp, resp, (int64_t)resp_len);
        free(resp);
    }

    // Cleanup
    free(res.body);
    if (res.headers && rt_obj_release_check0(res.headers))
        rt_obj_free(res.headers);
    free_server_req(&req);
    rt_tcp_close(tcp);
}

typedef struct {
    rt_http_server_impl *server;
    void *tcp;
} http_conn_task_t;

static void handle_connection_task(void *arg) {
    http_conn_task_t *task = (http_conn_task_t *)arg;
    handle_connection(task->server, task->tcp);
    free(task);
}

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

#ifdef _WIN32
static DWORD WINAPI accept_loop(LPVOID arg)
#else
static void *accept_loop(void *arg)
#endif
{
    rt_http_server_impl *server = (rt_http_server_impl *)arg;

    while (server->running) {
        void *tcp = rt_tcp_server_accept_for(server->tcp_server, 1000);
        if (!tcp)
            continue; // Timeout — check running flag

        if (!server->running) {
            rt_tcp_close(tcp);
            break;
        }

        http_conn_task_t *task = (http_conn_task_t *)malloc(sizeof(http_conn_task_t));
        if (!task) {
            rt_tcp_close(tcp);
            continue;
        }

        task->server = server;
        task->tcp = tcp;

        if (!server->worker_pool ||
            !rt_threadpool_submit(server->worker_pool, (void *)handle_connection_task, task)) {
            free(task);
            rt_tcp_close(tcp);
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

void *rt_http_server_new(int64_t port) {
    if (port < 1 || port > 65535)
        rt_trap("HttpServer: invalid port");

    rt_http_server_impl *server =
        (rt_http_server_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_server_impl));
    if (!server)
        rt_trap("HttpServer: memory allocation failed");
    memset(server, 0, sizeof(*server));
    rt_obj_set_finalizer(server, rt_http_server_finalize);

    server->port = port;
    server->router = rt_http_router_new();
    server->worker_pool = rt_threadpool_new(8);
    server->running = false;

    return server;
}

static void add_route_binding(void *obj,
                              rt_string pattern,
                              rt_string handler_tag,
                              void *(*adder)(void *, rt_string)) {
    if (!obj || !adder)
        return;

    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    adder(server->router, pattern);

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag || !append_route_entry(server, tag))
        rt_trap("HttpServer: failed to register route");
}

void rt_http_server_get(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_get);
}

void rt_http_server_post(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_post);
}

void rt_http_server_put(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_put);
}

void rt_http_server_del(void *obj, rt_string pattern, rt_string handler_tag) {
    add_route_binding(obj, pattern, handler_tag, rt_http_router_delete);
}

void rt_http_server_bind_handler(void *obj, rt_string handler_tag, void *entry) {
    if (!obj || !entry)
        return;

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag)
        return;

    if (!set_handler_binding(
            (rt_http_server_impl *)obj, tag, native_handler_dispatch, entry, NULL)) {
        rt_trap("HttpServer: failed to bind handler");
    }
}

void rt_http_server_bind_handler_dispatch(
    void *obj, rt_string handler_tag, void *dispatch, void *ctx, void *cleanup) {
    if (!obj || !dispatch)
        return;

    const char *tag = rt_string_cstr(handler_tag);
    if (!tag)
        return;

    if (!set_handler_binding((rt_http_server_impl *)obj,
                             tag,
                             (rt_http_server_handler_dispatch_fn)dispatch,
                             ctx,
                             (rt_http_server_handler_cleanup_fn)cleanup)) {
        rt_trap("HttpServer: failed to bind handler");
    }
}

void rt_http_server_start(void *obj) {
    if (!obj)
        rt_trap("HttpServer: NULL server");

    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    if (server->running)
        return;

    // Create TCP server
    server->tcp_server = rt_tcp_server_listen(server->port);
    if (!server->worker_pool)
        server->worker_pool = rt_threadpool_new(8);
    server->running = true;

    // Start accept loop in background thread
#ifdef _WIN32
    server->accept_thread = CreateThread(NULL, 0, accept_loop, server, 0, NULL);
    server->thread_started = server->accept_thread != NULL;
#else
    server->thread_started = pthread_create(&server->accept_thread, NULL, accept_loop, server) == 0;
#endif
}

void rt_http_server_stop(void *obj) {
    if (!obj)
        return;
    rt_http_server_impl *server = (rt_http_server_impl *)obj;
    server->running = false;

    if (server->tcp_server) {
        rt_tcp_server_close(server->tcp_server);
        server->tcp_server = NULL;
    }

    if (server->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(server->accept_thread, 5000);
        CloseHandle(server->accept_thread);
#else
        pthread_join(server->accept_thread, NULL);
#endif
        server->thread_started = false;
    }

    if (server->worker_pool)
        rt_threadpool_wait(server->worker_pool);
}

int64_t rt_http_server_port(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_server_impl *)obj)->port;
}

int8_t rt_http_server_is_running(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_server_impl *)obj)->running ? 1 : 0;
}

//=============================================================================
// ServerReq Accessors
//=============================================================================

rt_string rt_server_req_method(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->method ? rt_string_from_bytes(req->method, strlen(req->method))
                       : rt_string_from_bytes("", 0);
}

rt_string rt_server_req_path(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->path ? rt_string_from_bytes(req->path, strlen(req->path))
                     : rt_string_from_bytes("", 0);
}

rt_string rt_server_req_body(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    server_req_t *req = (server_req_t *)obj;
    return req->body ? rt_string_from_bytes(req->body, req->body_len) : rt_string_from_bytes("", 0);
}

rt_string rt_server_req_header(void *obj, rt_string name) {
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

rt_string rt_server_req_param(void *obj, rt_string name) {
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

rt_string rt_server_req_query(void *obj, rt_string name) {
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

void *rt_server_res_status(void *obj, int64_t code) {
    if (!obj)
        return obj;
    server_res_t *res = (server_res_t *)obj;
    res->status_code = (int)code;
    return obj;
}

void *rt_server_res_header(void *obj, rt_string name, rt_string value) {
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

void rt_server_res_send(void *obj, rt_string body) {
    if (!obj)
        return;
    server_res_t *res = (server_res_t *)obj;
    const char *b = rt_string_cstr(body);
    free(res->body);
    res->body = b ? strdup(b) : NULL;
    res->body_len = b ? strlen(b) : 0;
    res->sent = true;
}

void rt_server_res_json(void *obj, rt_string json_str) {
    if (!obj)
        return;
    server_res_t *res = (server_res_t *)obj;
    if (!res->headers)
        res->headers = rt_map_new();
    rt_string ct_key = rt_const_cstr("Content-Type");
    rt_string ct_val = rt_const_cstr("application/json");
    rt_map_set(res->headers, ct_key, (void *)ct_val);
    rt_server_res_send(obj, json_str);
}

void *rt_http_server_process_request(void *obj, rt_string raw_request) {
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

    size_t resp_len = 0;
    char *resp = build_response(&res, &resp_len);
    rt_string result = resp ? rt_string_from_bytes(resp, resp_len) : rt_string_from_bytes("", 0);

    free(resp);
    free(res.body);
    if (res.headers && rt_obj_release_check0(res.headers))
        rt_obj_free(res.headers);
    free_server_req(&req);
    return result;
}
