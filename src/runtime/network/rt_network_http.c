//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_network_http.c
/// @brief HTTP/URL helpers for Viper.Network HTTP APIs.
//
//===----------------------------------------------------------------------===//

#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif

#include "rt_network_http_internal.h"
#include "rt_network_internal.h"
#include "rt_tls.h"

#include "rt_box.h"
#include "rt_compress.h"
#include "rt_error.h"
#include "rt_map.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
typedef CRITICAL_SECTION http_pool_mutex_t;
#define HTTP_POOL_MUTEX_INIT(m) InitializeCriticalSection(m)
#define HTTP_POOL_MUTEX_LOCK(m) EnterCriticalSection(m)
#define HTTP_POOL_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define HTTP_POOL_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t http_pool_mutex_t;
#define HTTP_POOL_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define HTTP_POOL_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define HTTP_POOL_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define HTTP_POOL_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

#include "rt_trap.h"

/// @brief True if `host` is an IPv6 literal that needs `[…]` wrapping in a URL/Host header.
///
/// Detects bare IPv6 addresses (containing ':' but no leading '[')
/// per RFC 3986 §3.2.2 — these must be bracketed when embedded in a
/// URL or `Host:` header.
static bool host_needs_brackets(const char *host) {
    return host && strchr(host, ':') != NULL && host[0] != '[';
}

//=============================================================================
// HTTP Client Implementation
//=============================================================================

/// @brief Maximum number of redirects to follow.
#define HTTP_MAX_REDIRECTS 5

/// @brief Default timeout for HTTP requests (30 seconds).
#define HTTP_DEFAULT_TIMEOUT_MS 30000

/// @brief Initial buffer size for reading responses.
#define HTTP_BUFFER_SIZE 4096

/// @brief Maximum response body size (256 MB) — prevents decompression/server DoS (S-09 fix).
#define HTTP_MAX_BODY_SIZE (256u * 1024u * 1024u)

/// @brief Parsed URL structure.
typedef struct parsed_url {
    char *host;  // Allocated hostname
    int port;    // Port number (default 80 for http, 443 for https)
    char *path;  // Path including query string (allocated)
    int use_tls; // 1 for https, 0 for http
} parsed_url_t;

/// @brief HTTP header entry.
typedef struct http_header {
    char *name;
    char *value;
    struct http_header *next;
} http_header_t;

/// @brief HTTP connection context (TCP or TLS).
typedef struct http_conn {
    socket_t socket_fd;     // Connected socket (owned directly for HTTP, by TLS for HTTPS)
    rt_tls_session_t *tls;  // TLS session (for HTTPS)
    int use_tls;            // 1 if using TLS
    uint8_t read_buf[4096]; // Read buffer
    size_t read_buf_len;    // Bytes in buffer
    size_t read_buf_pos;    // Current position in buffer
    void *pool;             // Owning connection pool, if this lease came from / returns to one
    int pool_slot;          // Slot reserved inside @p pool while the lease is checked out
    int tls_verify;         // Verification mode used to establish this connection
    char pool_key[320];     // Stable host/port/TLS key for reuse
} http_conn_t;

/// @brief Initialize HTTP connection for TCP.
static void http_conn_init_tcp(http_conn_t *conn, socket_t socket_fd) {
    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = socket_fd;
    conn->tls = NULL;
    conn->use_tls = 0;
    conn->pool_slot = -1;
}

/// @brief Initialize HTTP connection for TLS.
static void http_conn_init_tls(http_conn_t *conn, rt_tls_session_t *tls) {
    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = tls ? (socket_t)rt_tls_get_socket(tls) : INVALID_SOCK;
    conn->tls = tls;
    conn->use_tls = 1;
    conn->pool_slot = -1;
}

/// @brief Send all data over HTTP connection.
static int http_conn_send(http_conn_t *conn, const uint8_t *data, size_t len) {
    size_t total_sent = 0;

    if (conn->use_tls) {
        while (total_sent < len) {
            long sent = rt_tls_send(conn->tls, data + total_sent, len - total_sent);
            if (sent <= 0)
                return -1;
            total_sent += (size_t)sent;
        }
    } else {
        while (total_sent < len) {
            int sent = send(conn->socket_fd,
                            (const char *)(data + total_sent),
                            (int)(len - total_sent > INT_MAX ? INT_MAX : len - total_sent),
                            SEND_FLAGS);
            if (sent == SOCK_ERROR)
                return -1;
            total_sent += (size_t)sent;
        }
    }
    return 0;
}

/// @brief Receive data from HTTP connection (buffered).
static long http_conn_recv(http_conn_t *conn, uint8_t *buf, size_t len) {
    size_t total = 0;

    // First, drain any buffered data
    while (total < len && conn->read_buf_pos < conn->read_buf_len) {
        buf[total++] = conn->read_buf[conn->read_buf_pos++];
    }

    if (total == len)
        return (long)total;

    // Need more data from network
    if (conn->use_tls) {
        long n = rt_tls_recv(conn->tls, buf + total, len - total);
        if (n > 0)
            total += n;
    } else {
        int n = recv(conn->socket_fd,
                     (char *)(buf + total),
                     (int)(len - total > INT_MAX ? INT_MAX : len - total),
                     0);
        if (n > 0)
            total += (size_t)n;
    }

    return (long)total;
}

/// @brief Receive exactly one byte from HTTP connection.
static int http_conn_recv_byte(http_conn_t *conn, uint8_t *byte) {
    // Check buffer first
    if (conn->read_buf_pos < conn->read_buf_len) {
        *byte = conn->read_buf[conn->read_buf_pos++];
        return 1;
    }

    // Refill buffer
    if (conn->use_tls) {
        long n = rt_tls_recv(conn->tls, conn->read_buf, sizeof(conn->read_buf));
        if (n <= 0)
            return 0;
        conn->read_buf_len = (size_t)n;
        conn->read_buf_pos = 0;
    } else {
        int n = recv(conn->socket_fd, (char *)conn->read_buf, (int)sizeof(conn->read_buf), 0);
        if (n <= 0)
            return 0;
        conn->read_buf_len = (size_t)n;
        conn->read_buf_pos = 0;
    }

    *byte = conn->read_buf[conn->read_buf_pos++];
    return 1;
}

/// @brief Close HTTP connection.
static void http_conn_close(http_conn_t *conn) {
    if (conn->use_tls && conn->tls) {
        rt_tls_close(conn->tls);
        conn->tls = NULL;
        conn->socket_fd = INVALID_SOCK;
    } else if (conn->socket_fd != INVALID_SOCK) {
        CLOSE_SOCKET(conn->socket_fd);
        conn->socket_fd = INVALID_SOCK;
    }
    conn->read_buf_len = 0;
    conn->read_buf_pos = 0;
    conn->pool = NULL;
    conn->pool_slot = -1;
    conn->tls_verify = 0;
    conn->pool_key[0] = '\0';
}

#define HTTP_CONN_POOL_MAX_ENTRIES 64
#define HTTP_CONN_POOL_IDLE_TIMEOUT_SEC 30

typedef struct http_conn_pool_entry {
    http_conn_t conn;
    char *key;
    time_t last_used;
    int in_use;
} http_conn_pool_entry_t;

typedef struct http_conn_pool {
    http_conn_pool_entry_t entries[HTTP_CONN_POOL_MAX_ENTRIES];
    int count;
    int max_size;
    http_pool_mutex_t lock;
    int lock_initialized;
} http_conn_pool_t;

static void http_make_pool_key(
    const char *host, int port, int use_tls, int tls_verify, char *buf, size_t buf_len) {
    snprintf(buf,
             buf_len,
             "%c%c|%s%s%s|%d",
             use_tls ? 's' : 'p',
             tls_verify ? 'v' : 'i',
             host_needs_brackets(host) ? "[" : "",
             host ? host : "",
             host_needs_brackets(host) ? "]" : "",
             port);
}

static int http_conn_is_healthy(http_conn_t *conn) {
    if (!conn || conn->socket_fd == INVALID_SOCK)
        return 0;
    if (conn->use_tls && conn->tls && rt_tls_has_buffered_data(conn->tls))
        return 1;

    {
        int ready = wait_socket(conn->socket_fd, 0, false);
        if (ready <= 0)
            return 1;
    }

    {
        uint8_t byte = 0;
        int peeked = recv(conn->socket_fd, (char *)&byte, 1, MSG_PEEK);
        if (peeked > 0)
            return 1;
        if (peeked == 0)
            return 0;
    }

#ifdef _WIN32
    return GET_LAST_ERROR() == WSAEWOULDBLOCK ? 1 : 0;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK ? 1 : 0;
#endif
}

static void http_conn_pool_entry_reset(http_conn_pool_entry_t *entry) {
    if (!entry)
        return;
    http_conn_close(&entry->conn);
    free(entry->key);
    entry->key = NULL;
    entry->last_used = 0;
    entry->in_use = 0;
    memset(&entry->conn, 0, sizeof(entry->conn));
    entry->conn.socket_fd = INVALID_SOCK;
    entry->conn.pool_slot = -1;
}

static void http_conn_pool_trim_locked(http_conn_pool_t *pool) {
    while (pool->count > 0) {
        http_conn_pool_entry_t *tail = &pool->entries[pool->count - 1];
        if (tail->in_use || tail->key)
            break;
        memset(tail, 0, sizeof(*tail));
        tail->conn.socket_fd = INVALID_SOCK;
        tail->conn.pool_slot = -1;
        pool->count--;
    }
}

static void http_conn_pool_finalize(void *obj) {
    if (!obj)
        return;
    http_conn_pool_t *pool = (http_conn_pool_t *)obj;
    for (int i = 0; i < pool->count; i++)
        http_conn_pool_entry_reset(&pool->entries[i]);
    if (pool->lock_initialized)
        HTTP_POOL_MUTEX_DESTROY(&pool->lock);
}

void *rt_http_conn_pool_new(int64_t max_size) {
    http_conn_pool_t *pool =
        (http_conn_pool_t *)rt_obj_new_i64(0, (int64_t)sizeof(http_conn_pool_t));
    if (!pool)
        rt_trap("HTTP: connection pool allocation failed");
    memset(pool, 0, sizeof(*pool));
    pool->max_size = (int)(max_size > 0 && max_size < HTTP_CONN_POOL_MAX_ENTRIES ? max_size
                                                                                  : HTTP_CONN_POOL_MAX_ENTRIES);
    for (int i = 0; i < HTTP_CONN_POOL_MAX_ENTRIES; i++) {
        pool->entries[i].conn.socket_fd = INVALID_SOCK;
        pool->entries[i].conn.pool_slot = -1;
    }
    HTTP_POOL_MUTEX_INIT(&pool->lock);
    pool->lock_initialized = 1;
    rt_obj_set_finalizer(pool, http_conn_pool_finalize);
    return pool;
}

void rt_http_conn_pool_clear(void *obj) {
    if (!obj)
        return;
    http_conn_pool_t *pool = (http_conn_pool_t *)obj;
    HTTP_POOL_MUTEX_LOCK(&pool->lock);
    for (int i = 0; i < pool->count; i++)
        http_conn_pool_entry_reset(&pool->entries[i]);
    pool->count = 0;
    HTTP_POOL_MUTEX_UNLOCK(&pool->lock);
}

static void http_conn_pool_evict_idle_locked(http_conn_pool_t *pool, time_t now) {
    for (int i = 0; i < pool->count; i++) {
        http_conn_pool_entry_t *entry = &pool->entries[i];
        if (entry->in_use)
            continue;
        if (difftime(now, entry->last_used) <= HTTP_CONN_POOL_IDLE_TIMEOUT_SEC)
            continue;
        http_conn_pool_entry_reset(entry);
    }
    http_conn_pool_trim_locked(pool);
}

static int http_conn_pool_acquire(void *obj,
                                  const char *host,
                                  int port,
                                  int use_tls,
                                  int tls_verify,
                                  http_conn_t *out_conn) {
    if (!obj || !host || !out_conn)
        return 0;

    http_conn_pool_t *pool = (http_conn_pool_t *)obj;
    char key[sizeof(out_conn->pool_key)];
    http_make_pool_key(host, port, use_tls, tls_verify, key, sizeof(key));

    HTTP_POOL_MUTEX_LOCK(&pool->lock);
    http_conn_pool_evict_idle_locked(pool, time(NULL));

    for (int i = 0; i < pool->count; i++) {
        http_conn_pool_entry_t *entry = &pool->entries[i];
        if (entry->in_use || !entry->key || strcmp(entry->key, key) != 0)
            continue;
        if (!http_conn_is_healthy(&entry->conn)) {
            http_conn_pool_entry_reset(entry);
            http_conn_pool_trim_locked(pool);
            i--;
            continue;
        }

        entry->in_use = 1;
        *out_conn = entry->conn;
        memset(&entry->conn, 0, sizeof(entry->conn));
        entry->conn.socket_fd = INVALID_SOCK;
        entry->conn.pool_slot = -1;
        out_conn->pool = pool;
        out_conn->pool_slot = i;
        snprintf(out_conn->pool_key, sizeof(out_conn->pool_key), "%s", key);
        HTTP_POOL_MUTEX_UNLOCK(&pool->lock);
        return 1;
    }

    HTTP_POOL_MUTEX_UNLOCK(&pool->lock);
    return 0;
}

static void http_conn_pool_release(http_conn_t *conn, int reusable) {
    if (!conn)
        return;

    http_conn_pool_t *pool = (http_conn_pool_t *)conn->pool;
    if (!pool) {
        http_conn_close(conn);
        return;
    }

    if (!reusable || !http_conn_is_healthy(conn)) {
        http_conn_close(conn);
        HTTP_POOL_MUTEX_LOCK(&pool->lock);
        if (conn->pool_slot >= 0 && conn->pool_slot < pool->count) {
            http_conn_pool_entry_t *entry = &pool->entries[conn->pool_slot];
            if (entry->in_use) {
                http_conn_pool_entry_reset(entry);
            }
        }
        http_conn_pool_trim_locked(pool);
        HTTP_POOL_MUTEX_UNLOCK(&pool->lock);
        memset(conn, 0, sizeof(*conn));
        conn->socket_fd = INVALID_SOCK;
        conn->pool_slot = -1;
        return;
    }

    HTTP_POOL_MUTEX_LOCK(&pool->lock);

    if (conn->pool_slot >= 0 && conn->pool_slot < pool->count) {
        http_conn_pool_entry_t *entry = &pool->entries[conn->pool_slot];
        entry->conn = *conn;
        entry->key = entry->key ? entry->key : strdup(conn->pool_key);
        entry->last_used = time(NULL);
        entry->in_use = 0;
        entry->conn.pool = NULL;
        entry->conn.pool_slot = -1;
    } else {
        int slot = -1;
        for (int i = 0; i < pool->count; i++) {
            if (!pool->entries[i].in_use && !pool->entries[i].key) {
                slot = i;
                break;
            }
        }
        if (slot < 0 && pool->count < pool->max_size)
            slot = pool->count++;
        if (slot >= 0) {
            http_conn_pool_entry_t *entry = &pool->entries[slot];
            memset(entry, 0, sizeof(*entry));
            entry->conn = *conn;
            entry->key = strdup(conn->pool_key);
            entry->last_used = time(NULL);
            entry->in_use = 0;
            entry->conn.pool = NULL;
            entry->conn.pool_slot = -1;
            if (!entry->key) {
                http_conn_close(&entry->conn);
                memset(entry, 0, sizeof(*entry));
                entry->conn.socket_fd = INVALID_SOCK;
                entry->conn.pool_slot = -1;
                if (slot == pool->count - 1)
                    http_conn_pool_trim_locked(pool);
            }
        } else {
            http_conn_close(conn);
        }
    }

    HTTP_POOL_MUTEX_UNLOCK(&pool->lock);

    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = INVALID_SOCK;
    conn->pool_slot = -1;
}

/// @brief HTTP request structure.
typedef struct rt_http_req {
    char *method;           // HTTP method
    parsed_url_t url;       // Parsed URL
    http_header_t *headers; // Linked list of headers
    uint8_t *body;          // Request body
    size_t body_len;        // Body length
    int timeout_ms;         // Timeout in milliseconds
    int tls_verify;         // 1 = verify peer certificate, 0 = allow insecure HTTPS
    int follow_redirects;   // Automatically follow redirect responses
    int max_redirects;      // Redirect limit for this request
    int accept_gzip;        // Advertise gzip support when no explicit Accept-Encoding is set
    int decode_gzip;        // Transparently decode gzip-encoded response bodies
    int keep_alive;         // Request connection reuse when paired with a pool
    void *connection_pool;  // Internal pool used by session-style HTTP clients
} rt_http_req_t;

/// @brief HTTP response structure.
typedef struct rt_http_res {
    int status;        // HTTP status code
    char *status_text; // Status text (allocated)
    void *headers;     // Map of headers
    uint8_t *body;     // Response body
    size_t body_len;   // Body length
} rt_http_res_t;

static socket_t http_create_tcp_socket(const char *host, int port, int timeout_ms, int *err_code);

static int http_request_wants_pool(const rt_http_req_t *req) {
    return req && req->keep_alive && req->connection_pool;
}

static int http_open_connection(rt_http_req_t *req, http_conn_t *conn, int *err_out) {
    if (!req || !conn) {
        if (err_out)
            *err_out = Err_NetworkError;
        return 0;
    }

    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = INVALID_SOCK;
    conn->pool_slot = -1;
    conn->tls_verify = req->tls_verify ? 1 : 0;
    http_make_pool_key(req->url.host,
                       req->url.port,
                       req->url.use_tls,
                       conn->tls_verify,
                       conn->pool_key,
                       sizeof(conn->pool_key));

    if (http_request_wants_pool(req) &&
        http_conn_pool_acquire(req->connection_pool,
                               req->url.host,
                               req->url.port,
                               req->url.use_tls,
                               conn->tls_verify,
                               conn)) {
        if (req->timeout_ms > 0 && conn->socket_fd != INVALID_SOCK) {
            set_socket_timeout(conn->socket_fd, req->timeout_ms, true);
            set_socket_timeout(conn->socket_fd, req->timeout_ms, false);
        }
        return 1;
    }

    if (req->url.use_tls) {
        int connect_err = Err_NetworkError;
        socket_t sock =
            http_create_tcp_socket(req->url.host, req->url.port, req->timeout_ms, &connect_err);
        if (sock == INVALID_SOCK) {
            if (err_out)
                *err_out = connect_err;
            return 0;
        }
        if (req->timeout_ms > 0) {
            set_socket_timeout(sock, req->timeout_ms, true);
            set_socket_timeout(sock, req->timeout_ms, false);
        }

        rt_tls_config_t tls_config;
        rt_tls_config_init(&tls_config);
        tls_config.hostname = req->url.host;
        tls_config.alpn_protocol = "http/1.1";
        tls_config.verify_cert = conn->tls_verify;
        if (req->timeout_ms > 0)
            tls_config.timeout_ms = req->timeout_ms;

        rt_tls_session_t *tls = rt_tls_new((int)sock, &tls_config);
        if (!tls) {
            CLOSE_SOCKET(sock);
            if (err_out)
                *err_out = Err_TlsError;
            return 0;
        }
        if (rt_tls_handshake(tls) != RT_TLS_OK) {
            rt_tls_close(tls);
            if (err_out)
                *err_out = Err_TlsError;
            return 0;
        }
        http_conn_init_tls(conn, tls);
        conn->tls_verify = tls_config.verify_cert;
    } else {
        int connect_err = Err_NetworkError;
        socket_t sock =
            http_create_tcp_socket(req->url.host, req->url.port, req->timeout_ms, &connect_err);
        if (sock == INVALID_SOCK) {
            if (err_out)
                *err_out = connect_err;
            return 0;
        }
        if (req->timeout_ms > 0) {
            set_socket_timeout(sock, req->timeout_ms, true);
            set_socket_timeout(sock, req->timeout_ms, false);
        }
        http_conn_init_tcp(conn, sock);
        conn->tls_verify = req->tls_verify ? 1 : 0;
    }

    http_make_pool_key(req->url.host,
                       req->url.port,
                       req->url.use_tls,
                       conn->tls_verify,
                       conn->pool_key,
                       sizeof(conn->pool_key));
    if (http_request_wants_pool(req))
        conn->pool = req->connection_pool;
    return 1;
}

/// @brief Free parsed URL.
static void free_parsed_url(parsed_url_t *url) {
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    url->host = NULL;
    url->path = NULL;
}

/// @brief Parse URL into components.
/// @return 0 on success, -1 on error.
static int parse_url(const char *url_str, parsed_url_t *result) {
    memset(result, 0, sizeof(*result));
    result->port = 80;
    result->use_tls = 0;

    // Check for http:// or https:// prefix; reject any other scheme
    if (strncmp(url_str, "http://", 7) == 0) {
        url_str += 7;
        result->use_tls = 0;
        result->port = 80;
    } else if (strncmp(url_str, "https://", 8) == 0) {
        url_str += 8;
        result->use_tls = 1;
        result->port = 443;
    } else if (strstr(url_str, "://") != NULL) {
        // An unrecognized scheme (e.g. ftp://, ws://) — reject rather than
        // silently defaulting to HTTP on port 80.
        return -1;
    }

    const char *p = url_str;
    if (*p == '[') {
        const char *bracket_end = strchr(p + 1, ']');
        if (!bracket_end)
            return -1;
        size_t host_len = (size_t)(bracket_end - (p + 1));
        if (host_len == 0)
            return -1;
        result->host = (char *)malloc(host_len + 1);
        if (!result->host)
            return -1;
        memcpy(result->host, p + 1, host_len);
        result->host[host_len] = '\0';
        p = bracket_end + 1;
    } else {
        const char *host_end = p;
        while (*host_end && *host_end != ':' && *host_end != '/' && *host_end != '?' &&
               *host_end != '#')
            host_end++;

        size_t host_len = (size_t)(host_end - p);
        if (host_len == 0)
            return -1;

        result->host = (char *)malloc(host_len + 1);
        if (!result->host)
            return -1;
        memcpy(result->host, p, host_len);
        result->host[host_len] = '\0';
        p = host_end;
    }

    if (*p != '\0' && *p != ':' && *p != '/' && *p != '?' && *p != '#') {
        free_parsed_url(result);
        return -1;
    }

    // Parse port if present
    if (*p == ':') {
        p++;
        result->port = 0;
        while (*p >= '0' && *p <= '9') {
            result->port = result->port * 10 + (*p - '0');
            p++;
        }
        if (result->port <= 0 || result->port > 65535) {
            free_parsed_url(result);
            return -1;
        }
    }

    // Parse request-target path + query (fragments are never sent on the wire)
    if (*p == '/') {
        const char *path_end = p;
        while (*path_end && *path_end != '#')
            path_end++;
        size_t path_len = (size_t)(path_end - p);
        result->path = (char *)malloc(path_len + 1);
        if (!result->path) {
            free_parsed_url(result);
            return -1;
        }
        memcpy(result->path, p, path_len);
        result->path[path_len] = '\0';
    } else if (*p == '?') {
        const char *query_end = p;
        while (*query_end && *query_end != '#')
            query_end++;
        size_t query_len = (size_t)(query_end - p);
        result->path = (char *)malloc(query_len + 2);
        if (!result->path) {
            free_parsed_url(result);
            return -1;
        }
        result->path[0] = '/';
        memcpy(result->path + 1, p, query_len);
        result->path[query_len + 1] = '\0';
    } else {
        result->path = (char *)malloc(2);
        if (!result->path) {
            free_parsed_url(result);
            return -1;
        }
        result->path[0] = '/';
        result->path[1] = '\0';
    }

    return 0;
}

/// @brief Free header list.
static void free_headers(http_header_t *headers) {
    while (headers) {
        http_header_t *next = headers->next;
        free(headers->name);
        free(headers->value);
        free(headers);
        headers = next;
    }
}

/// @brief Enable TCP_NODELAY on a connected socket.
static void http_set_nodelay(socket_t sock) {
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

/// @brief Set or clear non-blocking mode for connect-with-timeout.
static bool http_set_nonblocking(socket_t sock, bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    int new_flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(sock, F_SETFL, new_flags) == 0;
#endif
}

/// @brief Connect a socket with an optional timeout in milliseconds.
static bool http_connect_socket_with_timeout(
    socket_t sock, const struct sockaddr *addr, socklen_t addrlen, int timeout_ms, int *err_out) {
    if (err_out)
        *err_out = 0;

    if (timeout_ms > 0) {
        if (!http_set_nonblocking(sock, true)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }

        int connect_result = connect(sock, addr, addrlen);
        if (connect_result == SOCK_ERROR) {
            int err = GET_LAST_ERROR();
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
#else
            if (err == EINPROGRESS)
#endif
            {
                int ready = wait_socket(sock, timeout_ms, true);
                if (ready <= 0) {
                    if (err_out)
                        *err_out = ready == 0 ? ETIMEDOUT : GET_LAST_ERROR();
                    http_set_nonblocking(sock, false);
                    return false;
                }

                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                if (so_error != 0) {
                    if (err_out)
                        *err_out = so_error;
                    http_set_nonblocking(sock, false);
                    return false;
                }
            } else {
                if (err_out)
                    *err_out = err;
                http_set_nonblocking(sock, false);
                return false;
            }
        }

        if (!http_set_nonblocking(sock, false)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }
        return true;
    }

    if (connect(sock, addr, addrlen) == SOCK_ERROR) {
        if (err_out)
            *err_out = GET_LAST_ERROR();
        return false;
    }

    return true;
}

/// @brief Open a raw TCP socket to host:port without trapping.
static socket_t http_create_tcp_socket(const char *host, int port, int timeout_ms, int *err_code) {
    if (err_code)
        *err_code = Err_NetworkError;

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        if (err_code)
            *err_code = Err_HostNotFound;
        return INVALID_SOCK;
    }

    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        socket_t candidate = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (candidate == INVALID_SOCK)
            continue;

        suppress_sigpipe(candidate);
        if (http_connect_socket_with_timeout(
                candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen, timeout_ms, &last_err)) {
            sock = candidate;
            break;
        }

        CLOSE_SOCKET(candidate);
    }
    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        if (err_code) {
            if (last_err == CONN_REFUSED)
                *err_code = Err_ConnectionRefused;
#ifdef _WIN32
            else if (last_err == WSAETIMEDOUT)
#else
            else if (last_err == ETIMEDOUT)
#endif
                *err_code = Err_Timeout;
            else
                *err_code = Err_NetworkError;
        }
        return INVALID_SOCK;
    }

    http_set_nodelay(sock);
    return sock;
}

/// @brief Format and raise a TLS failure with the underlying TLS diagnostic when available.
static void http_trap_tls_error(const char *prefix, const char *detail) {
    const char *tls_err = detail && *detail ? detail : rt_tls_last_error();
    if (tls_err && *tls_err) {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s: %s", prefix, tls_err);
        rt_trap_net(msg, Err_TlsError);
    }
    rt_trap_net(prefix, Err_TlsError);
}

/// @brief Parse a decimal Content-Length value strictly.
static int parse_content_length_strict(const char *text, size_t *out_len) {
    const unsigned char *p = (const unsigned char *)text;
    size_t value = 0;
    int saw_digit = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0' || *p == '-')
        return -1;

    while (*p) {
        if (isdigit(*p)) {
            unsigned digit = (unsigned)(*p - '0');
            if (value > (SIZE_MAX - digit) / 10)
                return -1;
            value = value * 10 + digit;
            saw_digit = 1;
            p++;
            continue;
        }
        break;
    }

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '\0' || !saw_digit)
        return -1;

    *out_len = value;
    return 0;
}

/// @brief Return 1 if the response is not allowed to carry a payload body.
static int response_has_no_body(const rt_http_req_t *req, int status) {
    if (strcmp(req->method, "HEAD") == 0)
        return 1;
    if ((status >= 100 && status < 200) || status == 204 || status == 304)
        return 1;
    return 0;
}

/// @brief Build the absolute URL string for the current parsed request target.
static char *build_absolute_url(const parsed_url_t *url) {
    const char *scheme = url->use_tls ? "https" : "http";
    int default_port = url->use_tls ? 443 : 80;
    size_t host_len = strlen(url->host);
    size_t path_len = strlen(url->path);
    int include_port = url->port != default_port;
    size_t out_len = strlen(scheme) + 3 + host_len + path_len + 16;
    if (host_needs_brackets(url->host))
        out_len += 2;

    char *full = (char *)malloc(out_len + 1);
    if (!full)
        return NULL;

    if (include_port) {
        snprintf(full,
                 out_len + 1,
                 host_needs_brackets(url->host) ? "%s://[%s]:%d%s" : "%s://%s:%d%s",
                 scheme,
                 url->host,
                 url->port,
                 url->path);
    } else {
        snprintf(full,
                 out_len + 1,
                 host_needs_brackets(url->host) ? "%s://[%s]%s" : "%s://%s%s",
                 scheme,
                 url->host,
                 url->path);
    }

    return full;
}

/// @brief Resolve an HTTP redirect target against the current URL and replace the request URL.
static int resolve_redirect_target(parsed_url_t *current, const char *location) {
    int ok = -1;
    char *current_full = NULL;
    rt_string current_rt = NULL;
    rt_string location_rt = NULL;
    void *base = NULL;
    void *resolved = NULL;
    rt_string resolved_full = NULL;
    parsed_url_t next = {0};

    if (!location || !*location)
        return -1;

    current_full = build_absolute_url(current);
    if (!current_full)
        return -1;

    current_rt = rt_string_from_bytes(current_full, strlen(current_full));
    location_rt = rt_string_from_bytes(location, strlen(location));
    base = rt_url_parse(current_rt);
    resolved = rt_url_resolve(base, location_rt);
    resolved_full = rt_url_full(resolved);

    if (parse_url(rt_string_cstr(resolved_full), &next) == 0) {
        free_parsed_url(current);
        *current = next;
        memset(&next, 0, sizeof(next));
        ok = 0;
    }

    if (resolved_full)
        rt_string_unref(resolved_full);
    if (resolved && rt_obj_release_check0(resolved))
        rt_obj_free(resolved);
    if (base && rt_obj_release_check0(base))
        rt_obj_free(base);
    if (location_rt)
        rt_string_unref(location_rt);
    if (current_rt)
        rt_string_unref(current_rt);
    free(current_full);
    free_parsed_url(&next);
    return ok;
}

/// @brief GC finalizer for an HttpReq object.
///
/// Releases the heap-owned method string, the parsed URL fields,
/// the linked-list of headers, and the body buffer. Safe on a
/// partially-built request because every helper either nulls or
/// initialises its target.
static void rt_http_req_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_req_t *req = (rt_http_req_t *)obj;
    free(req->method);
    req->method = NULL;
    free_parsed_url(&req->url);
    free_headers(req->headers);
    req->headers = NULL;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    req->timeout_ms = 0;
}

/// @brief GC finalizer for an HttpRes object.
///
/// Frees the status text, body buffer, and decrements the headers
/// map's refcount (releasing if it hits zero).
static void rt_http_res_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_res_t *res = (rt_http_res_t *)obj;
    free(res->status_text);
    res->status_text = NULL;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    if (res->headers && rt_obj_release_check0(res->headers))
        rt_obj_free(res->headers);
    res->headers = NULL;
    res->status = 0;
}

/// @brief Return true if the string contains CR or LF (HTTP injection guard).
static bool has_crlf(const char *s) {
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n')
            return true;
    }
    return false;
}

/// @brief Add header to request.
static void add_header(rt_http_req_t *req, const char *name, const char *value) {
    /* Reject headers that contain CR or LF — they would split the HTTP stream (S-08 fix) */
    if (!name || !value || has_crlf(name) || has_crlf(value))
        return;

    http_header_t *h = (http_header_t *)malloc(sizeof(http_header_t));
    if (!h)
        return;
    h->name = strdup(name);
    if (!h->name) {
        free(h);
        return;
    }
    h->value = strdup(value);
    if (!h->value) {
        free(h->name);
        free(h);
        return;
    }
    h->next = req->headers;
    req->headers = h;
}

/// @brief Check if header exists (case-insensitive).
static bool has_header(rt_http_req_t *req, const char *name) {
    for (http_header_t *h = req->headers; h; h = h->next) {
        if (strcasecmp(h->name, name) == 0)
            return true;
    }
    return false;
}

/// @brief Return true when a comma-separated HTTP header value contains @p token.
static bool header_value_has_token(const char *value, const char *token) {
    size_t token_len;
    if (!value || !token)
        return false;

    token_len = strlen(token);
    while (*value) {
        const char *segment_start;
        const char *segment_end;
        size_t segment_len;

        while (*value == ' ' || *value == '\t' || *value == ',')
            value++;
        if (*value == '\0')
            break;

        segment_start = value;
        while (*value && *value != ',' && *value != ';')
            value++;
        segment_end = value;
        while (segment_end > segment_start &&
               (segment_end[-1] == ' ' || segment_end[-1] == '\t')) {
            segment_end--;
        }
        segment_len = (size_t)(segment_end - segment_start);
        if (segment_len == token_len && strncasecmp(segment_start, token, token_len) == 0)
            return true;

        while (*value && *value != ',')
            value++;
    }

    return false;
}

/// @brief Fetch a boxed string header value from the lowercase response header map.
static rt_string get_header_value(void *headers_map, const char *name) {
    rt_string key = rt_const_cstr(name);
    void *boxed = rt_map_get(headers_map, key);
    if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
        return NULL;
    return rt_unbox_str(boxed);
}

/// @brief Replace or remove a lowercase string header entry on the response header map.
static void set_header_value(void *headers_map, const char *name, const char *value) {
    rt_string key = rt_const_cstr(name);
    if (!value) {
        rt_map_remove(headers_map, key);
        return;
    }

    rt_string value_str = rt_string_from_bytes(value, strlen(value));
    void *boxed = rt_box_str(value_str);
    rt_map_set(headers_map, key, boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);
    rt_string_unref(value_str);
}

/// @brief Update the response Content-Length to the decoded body size.
static void set_content_length_header(void *headers_map, size_t body_len) {
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", body_len);
    set_header_value(headers_map, "content-length", len_buf);
}

/// @brief Decode a gzip response body in-place when the response advertises Content-Encoding:gzip.
static int maybe_decode_gzip_body(
    const rt_http_req_t *req, void *headers_map, uint8_t **body_io, size_t *body_len_io) {
    uint8_t *body = body_io ? *body_io : NULL;
    size_t body_len = body_len_io ? *body_len_io : 0;
    rt_string content_encoding;
    void *encoded = NULL;
    void *decoded = NULL;
    uint8_t *decoded_body = NULL;
    size_t decoded_len;

    if (!req || !req->decode_gzip || !headers_map || !body || body_len == 0)
        return 1;

    content_encoding = get_header_value(headers_map, "content-encoding");
    if (!content_encoding || !header_value_has_token(rt_string_cstr(content_encoding), "gzip")) {
        if (content_encoding)
            rt_string_unref(content_encoding);
        return 1;
    }
    rt_string_unref(content_encoding);

    encoded = rt_bytes_new((int64_t)body_len);
    if (!encoded)
        return 0;
    memcpy(bytes_data(encoded), body, body_len);

    decoded = rt_compress_gunzip(encoded);
    if (encoded && rt_obj_release_check0(encoded))
        rt_obj_free(encoded);
    if (!decoded)
        return 0;

    decoded_len = (size_t)bytes_len(decoded);
    decoded_body = (uint8_t *)malloc(decoded_len > 0 ? decoded_len : 1);
    if (!decoded_body) {
        if (decoded && rt_obj_release_check0(decoded))
            rt_obj_free(decoded);
        return 0;
    }
    if (decoded_len > 0)
        memcpy(decoded_body, bytes_data(decoded), decoded_len);
    if (decoded && rt_obj_release_check0(decoded))
        rt_obj_free(decoded);

    free(body);
    *body_io = decoded_body;
    *body_len_io = decoded_len;
    set_header_value(headers_map, "content-encoding", NULL);
    set_content_length_header(headers_map, decoded_len);
    return 1;
}

/// @brief Build HTTP request string.
/// @return Allocated string, caller must free.
static char *build_request(rt_http_req_t *req) {
    int add_default_connection = !has_header(req, "Connection");
    int want_keep_alive = req->keep_alive ? 1 : 0;

    // Calculate total size
    size_t size =
        strlen(req->method) + 1 + strlen(req->url.path) + 11; // "METHOD PATH HTTP/1.1\r\n"

    // Host header
    char host_header[300];
    if (req->url.port != 80 && req->url.port != 443)
        snprintf(host_header,
                 sizeof(host_header),
                 host_needs_brackets(req->url.host) ? "Host: [%s]:%d\r\n" : "Host: %s:%d\r\n",
                 req->url.host,
                 req->url.port);
    else
        snprintf(host_header,
                 sizeof(host_header),
                 host_needs_brackets(req->url.host) ? "Host: [%s]\r\n" : "Host: %s\r\n",
                 req->url.host);
    size += strlen(host_header);

    // Content-Length if body
    char content_len_header[64] = "";
    if (req->body && req->body_len > 0) {
        snprintf(content_len_header,
                 sizeof(content_len_header),
                 "Content-Length: %zu\r\n",
                 req->body_len);
        size += strlen(content_len_header);
    }

    if (add_default_connection)
        size += want_keep_alive ? 24 : 19; // keep-alive / close

    if (req->accept_gzip && !has_header(req, "Accept-Encoding"))
        size += 23; // "Accept-Encoding: gzip\r\n"

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next) {
        size += strlen(h->name) + 2 + strlen(h->value) + 2; // "Name: Value\r\n"
    }

    size += 2; // Final CRLF
    size += 1; // Null terminator

    char *request = (char *)malloc(size);
    if (!request)
        return NULL;

    char *p = request;
    size_t remaining = size;
    int written;

#define SNPRINTF_OR_FAIL(fmt, ...)                                                                 \
    do {                                                                                           \
        written = snprintf(p, remaining, fmt, __VA_ARGS__);                                        \
        if (written < 0 || (size_t)written >= remaining) {                                         \
            free(request);                                                                         \
            return NULL;                                                                           \
        }                                                                                          \
        p += written;                                                                              \
        remaining -= (size_t)written;                                                              \
    } while (0)

    SNPRINTF_OR_FAIL("%s %s HTTP/1.1\r\n", req->method, req->url.path);
    SNPRINTF_OR_FAIL("%s", host_header);

    if (content_len_header[0])
        SNPRINTF_OR_FAIL("%s", content_len_header);

    if (add_default_connection)
        SNPRINTF_OR_FAIL("%s", want_keep_alive ? "Connection: keep-alive\r\n"
                                               : "Connection: close\r\n");
    if (req->accept_gzip && !has_header(req, "Accept-Encoding"))
        SNPRINTF_OR_FAIL("%s", "Accept-Encoding: gzip\r\n");

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next) {
        SNPRINTF_OR_FAIL("%s: %s\r\n", h->name, h->value);
    }

    SNPRINTF_OR_FAIL("%s", "\r\n");

#undef SNPRINTF_OR_FAIL

    if (remaining == 0) {
        free(request);
        return NULL;
    }
    *p = '\0';
    return request;
}

/// @brief Public test hook into the otherwise-static URL parser.
///
/// Lets unit tests verify URL parsing without going through the
/// HTTP request path. All output pointers are optional. Strings
/// returned via `host_out`/`path_out` are heap-allocated and must
/// be `free()`d by the caller.
/// @return 1 on success, 0 on parse failure (in which case no
///         outputs are written and no allocations leak).
int rt_http_parse_url_for_test(
    const char *url_str, char **host_out, int *port_out, char **path_out, int *use_tls_out) {
    parsed_url_t parsed;
    if (parse_url(url_str, &parsed) != 0)
        return 0;

    if (host_out)
        *host_out = parsed.host ? strdup(parsed.host) : NULL;
    if (port_out)
        *port_out = parsed.port;
    if (path_out)
        *path_out = parsed.path ? strdup(parsed.path) : NULL;
    if (use_tls_out)
        *use_tls_out = parsed.use_tls;

    free_parsed_url(&parsed);
    return 1;
}

/// @brief Read a line from connection (up to CRLF).
/// @return Allocated line without CRLF, or NULL on error.
static char *read_line_conn(http_conn_t *conn) {
    char *line = NULL;
    size_t len = 0;
    size_t cap = 256;
    line = (char *)malloc(cap);
    if (!line)
        return NULL;

    while (1) {
        uint8_t byte;
        if (http_conn_recv_byte(conn, &byte) == 0) {
            // Connection closed
            if (len == 0) {
                free(line);
                return NULL;
            }
            break;
        }

        char c = (char)byte;

        if (c == '\n') {
            // Remove trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len + 1 >= cap) {
            // Cap at 64 KB to prevent unbounded realloc from malicious servers
            if (cap >= 65536) {
                free(line);
                return NULL;
            }
            cap *= 2;
            if (cap > 65536)
                cap = 65536;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line) {
                free(line);
                return NULL;
            }
            line = new_line;
        }
        line[len++] = c;
    }

    line[len] = '\0';
    return line;
}

/// @brief Parse HTTP response status line.
/// @return Status code, or -1 on error.
static int parse_status_line(
    const char *line, int *http_minor_out, char **status_text_out) {
    // Format: HTTP/1.x STATUS_CODE STATUS_TEXT
    if (strncmp(line, "HTTP/1.", 7) != 0)
        return -1;

    const char *p = line + 7;
    // Skip version digit
    if (*p != '0' && *p != '1')
        return -1;
    if (http_minor_out)
        *http_minor_out = *p - '0';
    p++;

    // Skip space
    if (*p != ' ')
        return -1;
    p++;

    // Parse status code
    int status = 0;
    while (*p >= '0' && *p <= '9') {
        status = status * 10 + (*p - '0');
        p++;
    }

    if (status < 100 || status > 599)
        return -1;

    // Skip space and get status text
    if (*p == ' ')
        p++;

    if (status_text_out) {
        *status_text_out = strdup(p);
        if (!*status_text_out)
            return -1;
    }

    return status;
}

/// @brief Parse header line into name and value.
static void parse_header_line(const char *line, void *headers_map) {
    const char *colon = strchr(line, ':');
    if (!colon)
        return;

    size_t name_len = colon - line;
    char *name = (char *)malloc(name_len + 1);
    if (!name)
        return;
    memcpy(name, line, name_len);
    name[name_len] = '\0';

    // Skip colon and whitespace
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
        value++;

    // Convert name to lowercase for case-insensitive lookup
    for (char *p = name; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p = *p + ('a' - 'A');
    }

    rt_string name_str = rt_string_from_bytes(name, strlen(name));
    rt_string value_str = rt_string_from_bytes(value, strlen(value));

    void *existing = rt_map_get(headers_map, name_str);
    if (existing && rt_box_type(existing) == RT_BOX_STR) {
        rt_string existing_str = rt_unbox_str(existing);
        const char *existing_cstr = rt_string_cstr(existing_str);
        const char *value_cstr = rt_string_cstr(value_str);
        const char *sep = strcmp(name, "set-cookie") == 0 ? "\n" : ", ";
        size_t existing_len = existing_cstr ? strlen(existing_cstr) : 0;
        size_t value_len = value_cstr ? strlen(value_cstr) : 0;
        size_t sep_len = strlen(sep);
        char *joined = (char *)malloc(existing_len + sep_len + value_len + 1);
        if (joined) {
            memcpy(joined, existing_cstr ? existing_cstr : "", existing_len);
            memcpy(joined + existing_len, sep, sep_len);
            memcpy(joined + existing_len + sep_len, value_cstr ? value_cstr : "", value_len);
            joined[existing_len + sep_len + value_len] = '\0';
            rt_string merged = rt_string_from_bytes(joined, existing_len + sep_len + value_len);
            rt_string_unref(value_str);
            value_str = merged;
            free(joined);
        }
        rt_string_unref(existing_str);
    }

    void *boxed = rt_box_str(value_str);
    rt_map_set(headers_map, name_str, boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);
    rt_string_unref(value_str);
    rt_string_unref(name_str);
    free(name);
}

/// @brief Read the status line and headers from an HTTP response.
/// @return 1 on success, 0 on malformed/short response or allocation failure.
static int read_response_head(http_conn_t *conn,
                              int *status_out,
                              int *http_minor_out,
                              char **status_text_out,
                              void **headers_map_out,
                              char **redirect_location_out) {
    char *status_line = NULL;
    char *status_text = NULL;
    void *headers_map = NULL;
    char *redirect_location = NULL;
    int status = -1;
    int header_count = 0;

    if (status_out)
        *status_out = -1;
    if (http_minor_out)
        *http_minor_out = 1;
    if (status_text_out)
        *status_text_out = NULL;
    if (headers_map_out)
        *headers_map_out = NULL;
    if (redirect_location_out)
        *redirect_location_out = NULL;

    status_line = read_line_conn(conn);
    if (!status_line)
        return 0;

    status = parse_status_line(status_line, http_minor_out, &status_text);
    free(status_line);
    if (status < 0)
        goto fail;

    headers_map = rt_map_new();
    if (!headers_map)
        goto fail;

    while (1) {
        char *line = NULL;
        if (header_count >= 256) {
            while ((line = read_line_conn(conn)) != NULL && line[0] != '\0')
                free(line);
            if (line)
                free(line);
            break;
        }

        line = read_line_conn(conn);
        if (!line || line[0] == '\0') {
            free(line);
            break;
        }

        header_count++;
        if (strncasecmp(line, "location:", 9) == 0 && !redirect_location) {
            const char *loc = line + 9;
            while (*loc == ' ')
                loc++;
            redirect_location = strdup(loc);
            if (!redirect_location) {
                free(line);
                goto fail;
            }
        }

        parse_header_line(line, headers_map);
        free(line);
    }

    if (status_out)
        *status_out = status;
    if (status_text_out)
        *status_text_out = status_text;
    if (headers_map_out)
        *headers_map_out = headers_map;
    if (redirect_location_out)
        *redirect_location_out = redirect_location;
    return 1;

fail:
    free(status_text);
    free(redirect_location);
    if (headers_map && rt_obj_release_check0(headers_map))
        rt_obj_free(headers_map);
    return 0;
}

/// @brief Read response body with Content-Length.
static uint8_t *read_body_content_length_conn(http_conn_t *conn,
                                              size_t content_length,
                                              size_t *out_len) {
    /* Cap to protect against server-controlled malloc(HUGE) (S-09 fix) */
    if (content_length > HTTP_MAX_BODY_SIZE) {
        *out_len = 0;
        return NULL;
    }

    uint8_t *body = (uint8_t *)malloc(content_length > 0 ? content_length : 1);
    if (!body)
        return NULL;

    size_t total_read = 0;
    while (total_read < content_length) {
        size_t remaining = content_length - total_read;
        size_t chunk_size = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

        long len = http_conn_recv(conn, body + total_read, chunk_size);
        if (len <= 0) {
            free(body);
            *out_len = 0;
            return NULL;
        }

        total_read += len;
    }

    *out_len = total_read;
    return body;
}

/// @brief Read chunked transfer encoding body.
static uint8_t *read_body_chunked_conn(http_conn_t *conn, size_t *out_len) {
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1) {
        // Read chunk size line
        char *size_line = read_line_conn(conn);
        if (!size_line) {
            free(body);
            *out_len = 0;
            return NULL;
        }

        // Parse hex chunk size — guard against overflow before each multiply (M-6)
        size_t chunk_size = 0;
        int overflow = 0;
        for (char *p = size_line; *p; p++) {
            char c = *p;
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = (unsigned)(c - 'A' + 10);
            else
                break;
            if (chunk_size > (SIZE_MAX - digit) / 16) {
                overflow = 1;
                break;
            }
            chunk_size = chunk_size * 16 + digit;
        }
        free(size_line);

        if (overflow) {
            free(body);
            *out_len = 0;
            return NULL;
        }

        if (chunk_size == 0) {
            // Last chunk — drain all trailer headers until empty line (RC-13 / RFC 7230 §4.1.2)
            char *trailer;
            while ((trailer = read_line_conn(conn)) != NULL && trailer[0] != '\0')
                free(trailer);
            if (!trailer) {
                free(body);
                *out_len = 0;
                return NULL;
            }
            if (trailer)
                free(trailer);
            break;
        }

        /* Reject chunks that would push the total body past the limit (S-09 fix) */
        if (body_len + chunk_size > HTTP_MAX_BODY_SIZE) {
            free(body);
            *out_len = 0;
            return NULL;
        }

        // Expand body buffer if needed
        while (body_len + chunk_size > body_cap) {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body) {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        // Read chunk data
        size_t bytes_read = 0;
        while (bytes_read < chunk_size) {
            size_t remaining = chunk_size - bytes_read;
            size_t to_read = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

            long len = http_conn_recv(conn, body + body_len, to_read);
            if (len <= 0) {
                free(body);
                *out_len = 0;
                return NULL;
            }

            body_len += len;
            bytes_read += len;
        }

        // Read trailing CRLF after chunk
        char *chunk_end = read_line_conn(conn);
        if (!chunk_end) {
            free(body);
            *out_len = 0;
            return NULL;
        }
        free(chunk_end);
    }

    *out_len = body_len;
    return body;
}

/// @brief Read response body until connection closes.
static uint8_t *read_body_until_close_conn(http_conn_t *conn, size_t *out_len) {
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1) {
        // Expand buffer if needed
        if (body_len + HTTP_BUFFER_SIZE > body_cap) {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body) {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        long len = http_conn_recv(conn, body + body_len, HTTP_BUFFER_SIZE);
        if (len <= 0)
            break;

        body_len += (size_t)len;

        /* Reject bodies that exceed the limit to prevent server-driven OOM (mirrors
           the chunked path guard using HTTP_MAX_BODY_SIZE). */
        if (body_len > HTTP_MAX_BODY_SIZE) {
            free(body);
            *out_len = 0;
            return NULL;
        }
    }

    *out_len = body_len;
    return body;
}

/// @brief Stream a Content-Length body directly into a file.
static int write_body_content_length_conn(http_conn_t *conn,
                                          size_t content_length,
                                          FILE *out,
                                          size_t *out_len) {
    size_t total_read = 0;
    uint8_t buffer[HTTP_BUFFER_SIZE];

    if (content_length > HTTP_MAX_BODY_SIZE) {
        *out_len = 0;
        return 0;
    }

    while (total_read < content_length) {
        size_t remaining = content_length - total_read;
        size_t chunk_size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        long len = http_conn_recv(conn, buffer, chunk_size);
        if (len <= 0) {
            *out_len = 0;
            return 0;
        }
        if (fwrite(buffer, 1, (size_t)len, out) != (size_t)len) {
            *out_len = total_read;
            return 0;
        }
        total_read += (size_t)len;
    }

    *out_len = total_read;
    return 1;
}

/// @brief Stream a chunked body directly into a file.
static int write_body_chunked_conn(http_conn_t *conn, FILE *out, size_t *out_len) {
    size_t total_written = 0;
    uint8_t buffer[HTTP_BUFFER_SIZE];

    while (1) {
        char *size_line = read_line_conn(conn);
        if (!size_line) {
            *out_len = total_written;
            return 0;
        }

        size_t chunk_size = 0;
        int overflow = 0;
        for (char *p = size_line; *p; p++) {
            char c = *p;
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = (unsigned)(c - 'A' + 10);
            else
                break;
            if (chunk_size > (SIZE_MAX - digit) / 16) {
                overflow = 1;
                break;
            }
            chunk_size = chunk_size * 16 + digit;
        }
        free(size_line);

        if (overflow || total_written + chunk_size > HTTP_MAX_BODY_SIZE) {
            *out_len = total_written;
            return 0;
        }

        if (chunk_size == 0) {
            char *trailer = NULL;
            while ((trailer = read_line_conn(conn)) != NULL && trailer[0] != '\0')
                free(trailer);
            if (trailer)
                free(trailer);
            *out_len = total_written;
            return trailer != NULL;
        }

        size_t chunk_read = 0;
        while (chunk_read < chunk_size) {
            size_t remaining = chunk_size - chunk_read;
            size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            long len = http_conn_recv(conn, buffer, to_read);
            if (len <= 0) {
                *out_len = total_written;
                return 0;
            }
            if (fwrite(buffer, 1, (size_t)len, out) != (size_t)len) {
                *out_len = total_written;
                return 0;
            }
            chunk_read += (size_t)len;
            total_written += (size_t)len;
        }

        char *chunk_end = read_line_conn(conn);
        if (!chunk_end) {
            *out_len = total_written;
            return 0;
        }
        free(chunk_end);
    }
}

/// @brief Stream a close-delimited body directly into a file.
static int write_body_until_close_conn(http_conn_t *conn, FILE *out, size_t *out_len) {
    size_t total_written = 0;
    uint8_t buffer[HTTP_BUFFER_SIZE];

    while (1) {
        long len = http_conn_recv(conn, buffer, sizeof(buffer));
        if (len <= 0)
            break;
        if (total_written + (size_t)len > HTTP_MAX_BODY_SIZE) {
            *out_len = total_written;
            return 0;
        }
        if (fwrite(buffer, 1, (size_t)len, out) != (size_t)len) {
            *out_len = total_written;
            return 0;
        }
        total_written += (size_t)len;
    }

    *out_len = total_written;
    return 1;
}

/// @brief Perform HTTP request and return response.
static rt_http_res_t *do_http_request(rt_http_req_t *req, int redirects_remaining) {
    rt_net_init_wsa();

    http_conn_t conn;
    int open_err = Err_NetworkError;
    if (!http_open_connection(req, &conn, &open_err)) {
        if (req->url.use_tls && open_err == Err_TlsError)
            http_trap_tls_error("HTTPS: connection failed", rt_tls_last_error());
        rt_trap_net(req->url.use_tls ? "HTTPS: connection failed" : "HTTP: connection failed",
                    open_err);
        return NULL;
    }

    // Build and send request
    char *request_str = build_request(req);
    if (!request_str) {
        http_conn_pool_release(&conn, 0);
        rt_trap("HTTP: failed to build request");
        return NULL;
    }

    size_t header_len = strlen(request_str);
    size_t req_body_len = req->body ? req->body_len : 0;
    if (req_body_len > SIZE_MAX - header_len) {
        free(request_str);
        http_conn_pool_release(&conn, 0);
        rt_trap_net("HTTP: request too large", Err_ProtocolError);
        return NULL;
    }
    size_t request_len = header_len + req_body_len;
    uint8_t *request_buf = (uint8_t *)malloc(request_len);
    if (!request_buf) {
        free(request_str);
        http_conn_pool_release(&conn, 0);
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }

    memcpy(request_buf, request_str, header_len);
    if (req->body && req->body_len > 0)
        memcpy(request_buf + header_len, req->body, req->body_len);

    free(request_str);

    if (http_conn_send(&conn, request_buf, request_len) < 0) {
        free(request_buf);
        http_conn_pool_release(&conn, 0);
        rt_trap_net("HTTP: send failed", Err_NetworkError);
        return NULL;
    }
    free(request_buf);

    int status = -1;
    int http_minor = 1;
    char *status_text = NULL;
    void *headers_map = NULL;
    char *redirect_location = NULL;
    if (!read_response_head(
            &conn, &status, &http_minor, &status_text, &headers_map, &redirect_location)) {
        http_conn_pool_release(&conn, 0);
        rt_trap_net("HTTP: invalid response", Err_ProtocolError);
        return NULL;
    }

    // Handle redirects (3xx with Location)
    if (req->follow_redirects &&
        (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        redirect_location) {
        if (redirects_remaining <= 0) {
            http_conn_pool_release(&conn, 0);
            free(status_text);
            free(redirect_location);
            if (headers_map && rt_obj_release_check0(headers_map))
                rt_obj_free(headers_map);
            rt_trap_net("HTTP: too many redirects", Err_ProtocolError);
            return NULL;
        }
        http_conn_pool_release(&conn, 0);
        free(status_text);
        if (headers_map && rt_obj_release_check0(headers_map))
            rt_obj_free(headers_map);

        // RFC 7231 / browser de-facto behavior: 303 always switches to GET; 301/302 switch POST to GET.
        if (status == 303 || ((status == 301 || status == 302) && strcmp(req->method, "POST") == 0)) {
            free(req->method);
            req->method = strdup("GET");
            free(req->body);
            req->body = NULL;
            req->body_len = 0;
        }

        if (resolve_redirect_target(&req->url, redirect_location) != 0) {
            free(redirect_location);
            rt_trap_net("HTTP: invalid redirect URL", Err_InvalidUrl);
            return NULL;
        }
        free(redirect_location);

        // Follow redirect
        return do_http_request(req, redirects_remaining - 1);
    }
    free(redirect_location);

    // Determine how to read body
    size_t body_len = 0;
    uint8_t *body = NULL;

    // Check for Content-Length
    rt_string content_length_key = rt_const_cstr("content-length");
    void *content_length_box = rt_map_get(headers_map, content_length_key);
    rt_string content_length_val = NULL;
    if (content_length_box && rt_box_type(content_length_box) == RT_BOX_STR)
        content_length_val = rt_unbox_str(content_length_box);

    // Check for Transfer-Encoding: chunked
    rt_string transfer_encoding_key = rt_const_cstr("transfer-encoding");
    void *transfer_encoding_box = rt_map_get(headers_map, transfer_encoding_key);
    rt_string transfer_encoding_val = NULL;
    if (transfer_encoding_box && rt_box_type(transfer_encoding_box) == RT_BOX_STR)
        transfer_encoding_val = rt_unbox_str(transfer_encoding_box);

    rt_string connection_val = get_header_value(headers_map, "connection");
    int response_closes =
        connection_val && header_value_has_token(rt_string_cstr(connection_val), "close");
    int response_keepalive =
        connection_val && header_value_has_token(rt_string_cstr(connection_val), "keep-alive");
    int has_content_length = content_length_val != NULL;

    bool no_body = response_has_no_body(req, status) != 0;
    bool chunked_transfer =
        transfer_encoding_val && header_value_has_token(rt_string_cstr(transfer_encoding_val), "chunked");

    if (no_body) {
        body = NULL;
        body_len = 0;
    } else if (chunked_transfer) {
        body = read_body_chunked_conn(&conn, &body_len);
    } else if (content_length_val) {
        size_t content_len = 0;
        if (parse_content_length_strict(rt_string_cstr(content_length_val), &content_len) != 0) {
            http_conn_pool_release(&conn, 0);
            if (connection_val)
                rt_string_unref(connection_val);
            if (transfer_encoding_val)
                rt_string_unref(transfer_encoding_val);
            if (content_length_val)
                rt_string_unref(content_length_val);
            if (headers_map && rt_obj_release_check0(headers_map))
                rt_obj_free(headers_map);
            free(status_text);
            rt_trap_net("HTTP: invalid Content-Length", Err_ProtocolError);
            return NULL;
        }
        body = read_body_content_length_conn(&conn, content_len, &body_len);
    } else {
        // Read until connection closes
        body = read_body_until_close_conn(&conn, &body_len);
    }

    if (!no_body && !body) {
        http_conn_pool_release(&conn, 0);
        if (connection_val)
            rt_string_unref(connection_val);
        if (transfer_encoding_val)
            rt_string_unref(transfer_encoding_val);
        if (content_length_val)
            rt_string_unref(content_length_val);
        if (headers_map && rt_obj_release_check0(headers_map))
            rt_obj_free(headers_map);
        free(status_text);
        rt_trap_net("HTTP: incomplete response body", Err_ProtocolError);
        return NULL;
    }

    if (!no_body && body) {
        if (chunked_transfer)
            set_header_value(headers_map, "transfer-encoding", NULL);
        if (!maybe_decode_gzip_body(req, headers_map, &body, &body_len)) {
            http_conn_pool_release(&conn, 0);
            if (connection_val)
                rt_string_unref(connection_val);
            if (transfer_encoding_val)
                rt_string_unref(transfer_encoding_val);
            if (content_length_val)
                rt_string_unref(content_length_val);
            free(body);
            if (headers_map && rt_obj_release_check0(headers_map))
                rt_obj_free(headers_map);
            free(status_text);
            rt_trap_net("HTTP: invalid gzip response body", Err_ProtocolError);
            return NULL;
        }
        set_content_length_header(headers_map, body_len);
    }

    {
        int reusable = http_request_wants_pool(req) &&
                       (no_body || chunked_transfer || has_content_length) &&
                       !response_closes &&
                       (http_minor >= 1 || response_keepalive);
        http_conn_pool_release(&conn, reusable);
    }
    if (connection_val)
        rt_string_unref(connection_val);
    if (transfer_encoding_val)
        rt_string_unref(transfer_encoding_val);
    if (content_length_val)
        rt_string_unref(content_length_val);

    // Create response object (must use rt_obj_new_i64 for GC management)
    rt_http_res_t *res = (rt_http_res_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_res_t));
    if (!res) {
        free(body);
        free(status_text);
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(res, rt_http_res_finalize);

    res->status = status;
    res->status_text = status_text;
    res->headers = headers_map;
    res->body = body;
    res->body_len = body_len;

    return res;
}

/// @brief Execute an HTTP GET/download and stream the response body into an open file.
/// @return 1 on success, 0 on any connection/protocol/write failure.
static int do_http_download_request(rt_http_req_t *req, int redirects_remaining, FILE *out) {
    http_conn_t conn;
    char *request_str = NULL;
    uint8_t *request_buf = NULL;
    int status = -1;
    int http_minor = 1;
    char *status_text = NULL;
    void *headers_map = NULL;
    char *redirect_location = NULL;
    rt_string content_length_val = NULL;
    rt_string transfer_encoding_val = NULL;
    int ok = 0;
    int open_err = Err_NetworkError;

    rt_net_init_wsa();
    memset(&conn, 0, sizeof(conn));
    conn.socket_fd = INVALID_SOCK;

    if (!http_open_connection(req, &conn, &open_err))
        return 0;

    request_str = build_request(req);
    if (!request_str)
        goto cleanup;

    size_t header_len = strlen(request_str);
    if (req->body_len > SIZE_MAX - header_len)
        goto cleanup;

    request_buf = (uint8_t *)malloc(header_len + req->body_len);
    if (!request_buf)
        goto cleanup;

    memcpy(request_buf, request_str, header_len);
    if (req->body && req->body_len > 0)
        memcpy(request_buf + header_len, req->body, req->body_len);

    if (http_conn_send(&conn, request_buf, header_len + req->body_len) < 0)
        goto cleanup;

    if (!read_response_head(
            &conn, &status, &http_minor, &status_text, &headers_map, &redirect_location))
        goto cleanup;
    (void)http_minor;

    if (req->follow_redirects &&
        (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        redirect_location) {
        if (redirects_remaining <= 0)
            goto cleanup;

        http_conn_close(&conn);
        if (status == 303 || ((status == 301 || status == 302) && strcmp(req->method, "POST") == 0)) {
            free(req->method);
            req->method = strdup("GET");
            free(req->body);
            req->body = NULL;
            req->body_len = 0;
            if (!req->method)
                goto cleanup;
        }
        if (resolve_redirect_target(&req->url, redirect_location) != 0)
            goto cleanup;

        if (headers_map && rt_obj_release_check0(headers_map))
            rt_obj_free(headers_map);
        headers_map = NULL;
        free(status_text);
        status_text = NULL;
        free(redirect_location);
        redirect_location = NULL;
        free(request_buf);
        request_buf = NULL;
        free(request_str);
        request_str = NULL;
        return do_http_download_request(req, redirects_remaining - 1, out);
    }

    if (status < 200 || status >= 300)
        goto cleanup;

    {
        rt_string content_length_key = rt_const_cstr("content-length");
        void *content_length_box = rt_map_get(headers_map, content_length_key);
        if (content_length_box && rt_box_type(content_length_box) == RT_BOX_STR)
            content_length_val = rt_unbox_str(content_length_box);
    }

    {
        rt_string transfer_encoding_key = rt_const_cstr("transfer-encoding");
        void *transfer_encoding_box = rt_map_get(headers_map, transfer_encoding_key);
        if (transfer_encoding_box && rt_box_type(transfer_encoding_box) == RT_BOX_STR)
            transfer_encoding_val = rt_unbox_str(transfer_encoding_box);
    }

    if (response_has_no_body(req, status)) {
        ok = 1;
    } else if (transfer_encoding_val &&
               header_value_has_token(rt_string_cstr(transfer_encoding_val), "chunked")) {
        size_t streamed_len = 0;
        ok = write_body_chunked_conn(&conn, out, &streamed_len);
    } else if (content_length_val) {
        size_t content_len = 0;
        size_t streamed_len = 0;
        if (parse_content_length_strict(rt_string_cstr(content_length_val), &content_len) != 0)
            goto cleanup;
        ok = write_body_content_length_conn(&conn, content_len, out, &streamed_len);
    } else {
        size_t streamed_len = 0;
        ok = write_body_until_close_conn(&conn, out, &streamed_len);
    }

cleanup:
    if (transfer_encoding_val)
        rt_string_unref(transfer_encoding_val);
    if (content_length_val)
        rt_string_unref(content_length_val);
    free(redirect_location);
    if (headers_map && rt_obj_release_check0(headers_map))
        rt_obj_free(headers_map);
    free(status_text);
    free(request_buf);
    free(request_str);
    http_conn_close(&conn);
    return ok;
}

//=============================================================================
// Http Static Class Implementation
//=============================================================================
//
// These one-shot helpers (rt_http_get, rt_http_post, …) build an
// `rt_http_req_t` on the stack, run a single transaction with the
// default timeout (`HTTP_DEFAULT_TIMEOUT_MS`) and redirect cap
// (`HTTP_MAX_REDIRECTS`), then return either the body as a string,
// the body as a Bytes object, or in the case of HEAD, just the
// response object. They `rt_trap_net` on malformed URLs and
// transport failures so callers don't need to error-check.
//=============================================================================

/// @brief HTTP GET that returns the body decoded as a UTF-8 string.
/// @throws Err_InvalidUrl on empty / unparsable URL,
///         Err_NetworkError on transport failure.
rt_string rt_http_get(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    // Create request
    rt_http_req_t req = {0};
    req.method = strdup("GET");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    // Execute request
    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP GET that returns the raw response body as a Bytes object.
/// @throws Err_InvalidUrl / Err_NetworkError on failure (see `rt_http_get`).
void *rt_http_get_bytes(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("GET");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP POST with a string body; returns the response body as a string.
///
/// Adds `Content-Type: text/plain; charset=utf-8` automatically
/// when the body is non-empty. The body is copied into a request-
/// owned buffer so the caller's `rt_string` lifetime is independent.
rt_string rt_http_post(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("POST");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    if (body_str) {
        // Copy body so the request owns the data independently of the GC-managed string
        req.body_len = strlen(body_str);
        req.body = (uint8_t *)malloc(req.body_len);
        if (!req.body)
            rt_trap("HTTP: memory allocation failed");
        memcpy(req.body, body_str, req.body_len);
    }

    // Add Content-Type if not empty body
    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP POST with a Bytes body; returns the response body as Bytes.
///
/// Adds `Content-Type: application/octet-stream` automatically when
/// the body is non-empty. The Bytes object is referenced directly
/// (not copied), so it must remain alive for the duration of the call.
void *rt_http_post_bytes(rt_string url, void *body) {
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("POST");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    if (body) {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP GET that streams the response body to a file at `dest_path`.
///
/// Returns 0 (false) on any failure: bad URL, transport error,
/// non-2xx status, file-open failure, short write, or `fclose`
/// error. On a partial write the half-finished file is removed
/// (RC-14). Does not trap, so callers can branch on the boolean
/// instead of installing an exception handler.
/// @return 1 on full successful download, 0 otherwise.
int8_t rt_http_download(rt_string url, rt_string dest_path) {
    const char *url_str = rt_string_cstr(url);
    const char *path_str = rt_string_cstr(dest_path);

    if (!url_str || *url_str == '\0')
        return 0;
    if (!path_str || *path_str == '\0')
        return 0;

    rt_http_req_t req = {0};
    req.method = strdup("GET");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 0;
    req.decode_gzip = 0;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        return 0;
    }

    FILE *f = fopen(path_str, "wb");
    if (!f) {
        free(req.method);
        free_parsed_url(&req.url);
        return 0;
    }

    int ok = do_http_download_request(&req, HTTP_MAX_REDIRECTS, f);
    free(req.method);
    free_parsed_url(&req.url);
    int close_err = fclose(f);

    // RC-14: if fwrite wrote fewer bytes (disk full, etc.) or fclose failed
    // (buffered data flush failure), remove the partial/corrupt file.
    if (!ok || close_err != 0) {
        remove(path_str);
        return 0;
    }
    return 1;
}

/// @brief HTTP HEAD request; returns just the response headers map.
///
/// Useful for size/type probes (Content-Length, Content-Type) and
/// existence checks without paying for the body. Throws on
/// transport failure.
void *rt_http_head(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("HEAD");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    void *headers = rt_http_res_headers(res);
    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return headers;
}

/// @brief HTTP PATCH with a string body; returns the response body as a string.
///
/// Mirrors `rt_http_post` but sends the `PATCH` method
/// (RFC 5789), used to apply partial updates to a resource.
rt_string rt_http_patch(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("PATCH");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    if (body_str) {
        req.body_len = strlen(body_str);
        req.body = (uint8_t *)malloc(req.body_len);
        if (!req.body)
            rt_trap("HTTP: memory allocation failed");
        memcpy(req.body, body_str, req.body_len);
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP OPTIONS request; returns the response body as a string.
///
/// Typically used for CORS preflight discovery — the meaningful
/// data lives in the response headers (`Allow`, `Access-Control-*`).
rt_string rt_http_options(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("OPTIONS");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP PUT with a string body; returns the response body as a string.
///
/// PUT replaces the target resource (RFC 7231 §4.3.4). Body is
/// copied and tagged `Content-Type: text/plain; charset=utf-8`.
rt_string rt_http_put(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("PUT");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    if (body_str) {
        req.body_len = strlen(body_str);
        req.body = (uint8_t *)malloc(req.body_len);
        if (!req.body)
            rt_trap("HTTP: memory allocation failed");
        memcpy(req.body, body_str, req.body_len);
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP PUT with a Bytes body; returns the response body as Bytes.
///
/// As `rt_http_put` but for binary payloads — sets
/// `Content-Type: application/octet-stream` when the body is non-empty.
void *rt_http_put_bytes(rt_string url, void *body) {
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("PUT");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    if (body) {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP DELETE; returns the response body as a string.
rt_string rt_http_delete(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("DELETE");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP DELETE; returns the response body as a Bytes object.
void *rt_http_delete_bytes(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    rt_http_req_t req = {0};
    req.method = strdup("DELETE");
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

//=============================================================================
// HttpReq Instance Class Implementation
//
// The builder-style HttpReq class lets callers configure a request
// (headers, body, timeout, redirect policy) before sending. Setters
// return `obj` so they can be chained. All instances are GC-managed
// and finalized via `rt_http_req_finalize`.
//=============================================================================

/// @brief Construct a new HTTP request with the given method and URL.
///
/// The URL is parsed eagerly so callers see invalid-URL traps at
/// construction time rather than when sending. Defaults: 30s
/// timeout, follow redirects with cap `HTTP_MAX_REDIRECTS`.
/// @throws Err_InvalidUrl on bad URL, generic trap on null method.
void *rt_http_req_new(rt_string method, rt_string url) {
    const char *method_str = rt_string_cstr(method);
    const char *url_str = rt_string_cstr(url);

    if (!method_str || *method_str == '\0')
        rt_trap("HTTP: invalid method");
    if (!url_str || *url_str == '\0')
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);

    // Must use rt_obj_new_i64 for GC management
    rt_http_req_t *req = (rt_http_req_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_req_t));
    if (!req)
        rt_trap("HTTP: memory allocation failed");

    memset(req, 0, sizeof(*req));
    rt_obj_set_finalizer(req, rt_http_req_finalize);
    req->method = strdup(method_str);
    req->timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req->tls_verify = 1;
    req->follow_redirects = 1;
    req->max_redirects = HTTP_MAX_REDIRECTS;
    req->accept_gzip = 1;
    req->decode_gzip = 1;
    req->keep_alive = 0;
    req->connection_pool = NULL;

    if (parse_url(url_str, &req->url) < 0) {
        free(req->method);
        // Note: GC-managed object, so we don't free it directly
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
    }

    return req;
}

/// @brief Append a request header. Silently ignores NULL name/value.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_header(void *obj, rt_string name, rt_string value) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (name_str && value_str)
        add_header(req, name_str, value_str);

    return obj;
}

/// @brief Replace the request body with a Bytes object (copied).
///
/// Frees any previously-set body. The bytes are duplicated into a
/// request-owned buffer so the caller's Bytes lifetime is independent.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_body(void *obj, void *data) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    if (req->body) {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (data) {
        int64_t len = rt_bytes_len(data);
        uint8_t *ptr = bytes_data(data);

        // Make a copy of the body
        req->body = (uint8_t *)malloc(len);
        if (req->body) {
            memcpy(req->body, ptr, len);
            req->body_len = len;
        }
    }

    return obj;
}

/// @brief Replace the request body with a string (copied).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_body_str(void *obj, rt_string text) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *text_str = rt_string_cstr(text);

    if (req->body) {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (text_str) {
        size_t len = strlen(text_str);
        req->body = (uint8_t *)malloc(len);
        if (req->body) {
            memcpy(req->body, text_str, len);
            req->body_len = len;
        }
    }

    return obj;
}

/// @brief Set per-request I/O timeout in milliseconds.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->timeout_ms = (int)timeout_ms;

    return obj;
}

/// @brief Toggle TLS certificate verification for HTTPS requests.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_tls_verify(void *obj, int8_t verify) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->tls_verify = verify ? 1 : 0;
    return obj;
}

/// @brief Toggle automatic redirect following (3xx Location handling).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_follow_redirects(void *obj, int8_t follow) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->follow_redirects = follow ? 1 : 0;
    return obj;
}

/// @brief Override the per-request redirect cap (negative values clamp to 0).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_max_redirects(void *obj, int64_t max_redirects) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    if (max_redirects < 0)
        max_redirects = 0;
    req->max_redirects = (int)max_redirects;
    return obj;
}

/// @brief Toggle keep-alive / pooled transport for this request.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_keep_alive(void *obj, int8_t keep_alive) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->keep_alive = keep_alive ? 1 : 0;
    return obj;
}

/// @brief Attach an internal connection pool to this request.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_connection_pool(void *obj, void *pool) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->connection_pool = pool;
    return obj;
}

/// @brief Execute a configured HttpReq and return the resulting HttpRes.
///
/// Auto-tags a non-empty body with
/// `Content-Type: application/octet-stream` if the caller didn't
/// already set one. Honours the request's redirect cap (which may
/// be 0 to disable following).
/// @return Newly-allocated `rt_http_res_t*` (GC-managed) or NULL on
///         transport failure (`do_http_request` returns NULL).
void *rt_http_req_send(void *obj) {
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    // Add Content-Type for POST with body if not set
    if (req->body && req->body_len > 0 && !has_header(req, "Content-Type")) {
        add_header(req, "Content-Type", "application/octet-stream");
    }

    rt_http_res_t *res = do_http_request(req, req->max_redirects);
    return res;
}

//=============================================================================
// HttpRes Instance Class Implementation
//
// Read-only accessors over the response. Header lookups are
// case-insensitive: the parser stores keys lower-cased so the
// lookup helper just down-cases the request name.
//=============================================================================

/// @brief Status code (e.g. 200, 404). Returns 0 if `obj` is NULL.
int64_t rt_http_res_status(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_res_t *)obj)->status;
}

/// @brief Reason phrase from the status line (e.g. "Not Found"). Empty if unset.
rt_string rt_http_res_status_text(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->status_text)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(res->status_text, strlen(res->status_text));
}

/// @brief Return a *copy* of the response headers as a fresh Map(String→String).
///
/// Copying defends the response's internal map against caller
/// mutation. Header keys are lower-case (already canonicalised by
/// the parser). Empty map is returned if `obj` is NULL or the
/// response has no headers.
void *rt_http_res_headers(void *obj) {
    if (!obj)
        return rt_map_new();

    rt_http_res_t *res = (rt_http_res_t *)obj;
    void *copy = rt_map_new();
    if (!res->headers || !copy)
        return copy ? copy : rt_map_new();

    void *keys = rt_map_keys(res->headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *boxed = rt_map_get(res->headers, key);
        if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
            continue;
        rt_string value = rt_unbox_str(boxed);
        rt_map_set(copy, key, value);
        rt_string_unref(value);
    }
    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
    return copy;
}

/// @brief Return a copy of the raw response body as a Bytes object.
void *rt_http_res_body(void *obj) {
    if (!obj)
        return rt_bytes_new(0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    void *bytes = rt_bytes_new((int64_t)res->body_len);
    if (res->body && res->body_len > 0) {
        uint8_t *ptr = bytes_data(bytes);
        memcpy(ptr, res->body, res->body_len);
    }
    return bytes;
}

/// @brief Return the response body decoded as a UTF-8 string.
///
/// No charset detection — bytes are passed through as if UTF-8.
/// Empty string for null/empty bodies.
rt_string rt_http_res_body_str(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->body || res->body_len == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes((const char *)res->body, res->body_len);
}

/// @brief Look up a single response header by name (case-insensitive).
///
/// Down-cases `name` and probes the parser-canonicalised header
/// map. Returns an empty string on miss or null arguments.
rt_string rt_http_res_header(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;

    // Convert name to lowercase for lookup
    const char *name_str = rt_string_cstr(name);
    if (!name_str)
        return rt_string_from_bytes("", 0);

    size_t len = strlen(name_str);
    char *lower_name = (char *)malloc(len + 1);
    if (!lower_name)
        return rt_string_from_bytes("", 0);

    for (size_t i = 0; i <= len; i++) {
        char c = name_str[i];
        if (c >= 'A' && c <= 'Z')
            lower_name[i] = c + ('a' - 'A');
        else
            lower_name[i] = c;
    }

    rt_string lower_key = rt_string_from_bytes(lower_name, len);
    free(lower_name);

    void *boxed = rt_map_get(res->headers, lower_key);
    rt_string_unref(lower_key);
    if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
        return rt_string_from_bytes("", 0);

    rt_string value = rt_unbox_str(boxed);
    const char *value_cstr = rt_string_cstr(value);
    rt_string copy =
        rt_string_from_bytes(value_cstr ? value_cstr : "", value_cstr ? strlen(value_cstr) : 0);
    rt_string_unref(value);
    return copy;
}

/// @brief Convenience predicate: 2xx success status?
/// @return 1 if `200 <= status < 300`, 0 otherwise (and on NULL).
int8_t rt_http_res_is_ok(void *obj) {
    if (!obj)
        return 0;

    int status = ((rt_http_res_t *)obj)->status;
    return (status >= 200 && status < 300) ? 1 : 0;
}
