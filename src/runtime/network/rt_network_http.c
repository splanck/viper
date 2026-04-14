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

#include "rt_network.h"
#include "rt_tls.h"

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
// Unix and ViperDOS: strings.h provides strcasecmp/strncasecmp.
#include <strings.h>
#include <unistd.h>
#endif

// Forward declarations (defined in rt_io.c).
#include "rt_trap.h"
extern void rt_trap_net(const char *msg, int err_code);

//=============================================================================
// Internal Bytes Access
//=============================================================================

typedef struct {
    int64_t len;
    uint8_t *data;
} bytes_impl;

/// @brief Reach into a Bytes object's internal storage. Returns NULL for NULL `obj`.
static inline uint8_t *bytes_data(void *obj) {
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

/// @brief True if `host` is an IPv6 literal that needs `[…]` wrapping in a URL/Host header.
///
/// Detects bare IPv6 addresses (containing ':' but no leading '[')
/// per RFC 3986 §3.2.2 — these must be bracketed when embedded in a
/// URL or `Host:` header.
static bool host_needs_brackets(const char *host) {
    return host && strchr(host, ':') != NULL && host[0] != '[';
}

/// @brief Lazy WinSock initialiser (no-op on POSIX). Defined in rt_network.c.
void rt_net_init_wsa(void);

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
    void *tcp;              // TCP connection (for plain HTTP)
    rt_tls_session_t *tls;  // TLS session (for HTTPS)
    int use_tls;            // 1 if using TLS
    uint8_t read_buf[4096]; // Read buffer
    size_t read_buf_len;    // Bytes in buffer
    size_t read_buf_pos;    // Current position in buffer
} http_conn_t;

/// @brief Initialize HTTP connection for TCP.
static void http_conn_init_tcp(http_conn_t *conn, void *tcp) {
    conn->tcp = tcp;
    conn->tls = NULL;
    conn->use_tls = 0;
    conn->read_buf_len = 0;
    conn->read_buf_pos = 0;
}

/// @brief Initialize HTTP connection for TLS.
static void http_conn_init_tls(http_conn_t *conn, rt_tls_session_t *tls) {
    conn->tcp = NULL;
    conn->tls = tls;
    conn->use_tls = 1;
    conn->read_buf_len = 0;
    conn->read_buf_pos = 0;
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
        rt_tcp_send_all_raw(conn->tcp, data, (int64_t)len);
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
        void *data = rt_tcp_recv(conn->tcp, (int64_t)(len - total));
        int64_t data_len = rt_bytes_len(data);
        if (data_len > 0) {
            memcpy(buf + total, bytes_data(data), data_len);
            total += data_len;
        }
        if (data && rt_obj_release_check0(data))
            rt_obj_free(data);
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
        void *data = rt_tcp_recv(conn->tcp, (int64_t)sizeof(conn->read_buf));
        int64_t data_len = rt_bytes_len(data);
        if (data_len <= 0) {
            if (data && rt_obj_release_check0(data))
                rt_obj_free(data);
            return 0;
        }
        memcpy(conn->read_buf, bytes_data(data), data_len);
        if (rt_obj_release_check0(data))
            rt_obj_free(data);
        conn->read_buf_len = (size_t)data_len;
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
    } else if (conn->tcp) {
        rt_tcp_close(conn->tcp);
        conn->tcp = NULL;
    }
}

/// @brief HTTP request structure.
typedef struct rt_http_req {
    char *method;           // HTTP method
    parsed_url_t url;       // Parsed URL
    http_header_t *headers; // Linked list of headers
    uint8_t *body;          // Request body
    size_t body_len;        // Body length
    int timeout_ms;         // Timeout in milliseconds
    int follow_redirects;   // Automatically follow redirect responses
    int max_redirects;      // Redirect limit for this request
} rt_http_req_t;

/// @brief HTTP response structure.
typedef struct rt_http_res {
    int status;        // HTTP status code
    char *status_text; // Status text (allocated)
    void *headers;     // Map of headers
    uint8_t *body;     // Response body
    size_t body_len;   // Body length
} rt_http_res_t;

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

/// @brief Build HTTP request string.
/// @return Allocated string, caller must free.
static char *build_request(rt_http_req_t *req) {
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

    // Connection header
    size += 19; // "Connection: close\r\n"

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next) {
        size += strlen(h->name) + 2 + strlen(h->value) + 2; // "Name: Value\r\n"
    }

    size += 2; // Final CRLF
    size += req->body_len;
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

    SNPRINTF_OR_FAIL("%s", "Connection: close\r\n");

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next) {
        SNPRINTF_OR_FAIL("%s: %s\r\n", h->name, h->value);
    }

    SNPRINTF_OR_FAIL("%s", "\r\n");

#undef SNPRINTF_OR_FAIL

    // Body
    if (req->body && req->body_len > 0) {
        if (req->body_len >= remaining) {
            free(request);
            return NULL;
        }
        memcpy(p, req->body, req->body_len);
        p += req->body_len;
        remaining -= req->body_len;
    }

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
static int parse_status_line(const char *line, char **status_text_out) {
    // Format: HTTP/1.x STATUS_CODE STATUS_TEXT
    if (strncmp(line, "HTTP/1.", 7) != 0)
        return -1;

    const char *p = line + 7;
    // Skip version digit
    if (*p != '0' && *p != '1')
        return -1;
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

/// @brief Perform HTTP request and return response.
static rt_http_res_t *do_http_request(rt_http_req_t *req, int redirects_remaining) {
    rt_net_init_wsa();

    // Create connection (TLS or plain TCP)
    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));

    if (req->url.use_tls) {
        // HTTPS - use TLS
        rt_tls_config_t tls_config;
        rt_tls_config_init(&tls_config);
        tls_config.hostname = req->url.host;
        /* Do NOT override verify_cert — let the config default take effect (S-07 fix) */
        if (req->timeout_ms > 0)
            tls_config.timeout_ms = req->timeout_ms;

        rt_tls_session_t *tls = rt_tls_connect(req->url.host, (uint16_t)req->url.port, &tls_config);
        if (!tls) {
            rt_trap_net("HTTPS: connection failed", Err_NetworkError);
            return NULL;
        }
        http_conn_init_tls(&conn, tls);
    } else {
        // HTTP - use plain TCP
        rt_string host = rt_string_from_bytes(req->url.host, strlen(req->url.host));
        void *tcp = req->timeout_ms > 0 ? rt_tcp_connect_for(host, req->url.port, req->timeout_ms)
                                        : rt_tcp_connect(host, req->url.port);
        rt_string_unref(host);

        if (!tcp || !rt_tcp_is_open(tcp)) {
            rt_trap_net("HTTP: connection failed", Err_NetworkError);
            return NULL;
        }

        // Set socket timeout
        if (req->timeout_ms > 0) {
            rt_tcp_set_recv_timeout(tcp, req->timeout_ms);
            rt_tcp_set_send_timeout(tcp, req->timeout_ms);
        }
        http_conn_init_tcp(&conn, tcp);
    }

    // Build and send request
    char *request_str = build_request(req);
    if (!request_str) {
        http_conn_close(&conn);
        rt_trap("HTTP: failed to build request");
        return NULL;
    }

    size_t header_len = strlen(request_str);
    size_t req_body_len = req->body ? req->body_len : 0;
    if (req_body_len > SIZE_MAX - header_len) {
        free(request_str);
        http_conn_close(&conn);
        rt_trap_net("HTTP: request too large", Err_ProtocolError);
        return NULL;
    }
    size_t request_len = header_len + req_body_len;
    uint8_t *request_buf = (uint8_t *)malloc(request_len);
    if (!request_buf) {
        free(request_str);
        http_conn_close(&conn);
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }

    memcpy(request_buf, request_str, header_len);
    if (req->body && req->body_len > 0)
        memcpy(request_buf + header_len, req->body, req->body_len);

    free(request_str);

    if (http_conn_send(&conn, request_buf, request_len) < 0) {
        free(request_buf);
        http_conn_close(&conn);
        rt_trap_net("HTTP: send failed", Err_NetworkError);
        return NULL;
    }
    free(request_buf);

    // Read status line
    char *status_line = read_line_conn(&conn);
    if (!status_line) {
        http_conn_close(&conn);
        rt_trap_net("HTTP: invalid response", Err_ProtocolError);
        return NULL;
    }

    char *status_text = NULL;
    int status = parse_status_line(status_line, &status_text);
    free(status_line);

    if (status < 0) {
        http_conn_close(&conn);
        rt_trap_net("HTTP: invalid status line", Err_ProtocolError);
        return NULL;
    }

    // Read headers (cap at 256 to prevent server-driven allocation loops)
    void *headers_map = rt_map_new();
    char *redirect_location = NULL;
    int header_count = 0;

    while (1) {
        if (header_count >= 256) {
            // Drain remaining headers without storing them
            char *line;
            while ((line = read_line_conn(&conn)) != NULL && line[0] != '\0')
                free(line);
            free(line);
            break;
        }

        char *line = read_line_conn(&conn);
        if (!line || line[0] == '\0') {
            free(line);
            break;
        }

        header_count++;

        // Check for Location header (for redirects)
        if (strncasecmp(line, "location:", 9) == 0) {
            const char *loc = line + 9;
            while (*loc == ' ')
                loc++;
            redirect_location = strdup(loc);
            if (!redirect_location) {
                free(line);
                http_conn_close(&conn);
                rt_trap("HTTP: redirect location allocation failed");
                return NULL;
            }
        }

        parse_header_line(line, headers_map);
        free(line);
    }

    // Handle redirects (3xx with Location)
    if (req->follow_redirects &&
        (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        redirect_location) {
        if (redirects_remaining <= 0) {
            http_conn_close(&conn);
            free(status_text);
            free(redirect_location);
            if (headers_map && rt_obj_release_check0(headers_map))
                rt_obj_free(headers_map);
            rt_trap_net("HTTP: too many redirects", Err_ProtocolError);
            return NULL;
        }
        http_conn_close(&conn);
        free(status_text);
        if (headers_map && rt_obj_release_check0(headers_map))
            rt_obj_free(headers_map);

        // RFC 7231 §6.4.4: 303 See Other must change method to GET and remove body
        if (status == 303) {
            free(req->method);
            req->method = strdup("GET");
            free(req->body);
            req->body = NULL;
            req->body_len = 0;
        }

        // Parse new URL
        parsed_url_t new_url;
        if (parse_url(redirect_location, &new_url) < 0) {
            // Relative URL - use same host and scheme
            if (redirect_location[0] == '/') {
                free(req->url.path);
                req->url.path = redirect_location;
            } else {
                free(redirect_location);
                rt_trap_net("HTTP: invalid redirect URL", Err_InvalidUrl);
                return NULL;
            }
        } else {
            free_parsed_url(&req->url);
            req->url = new_url;
            free(redirect_location);
        }

        // Follow redirect
        return do_http_request(req, redirects_remaining - 1);
    }
    free(redirect_location);

    // Determine how to read body
    size_t body_len = 0;
    uint8_t *body = NULL;

    // Check for Content-Length
    rt_string content_length_key = rt_string_from_bytes("content-length", 14);
    void *content_length_box = rt_map_get(headers_map, content_length_key);
    rt_string content_length_val = NULL;
    if (content_length_box && rt_box_type(content_length_box) == RT_BOX_STR)
        content_length_val = rt_unbox_str(content_length_box);
    rt_string_unref(content_length_key);

    // Check for Transfer-Encoding: chunked
    rt_string transfer_encoding_key = rt_string_from_bytes("transfer-encoding", 17);
    void *transfer_encoding_box = rt_map_get(headers_map, transfer_encoding_key);
    rt_string transfer_encoding_val = NULL;
    if (transfer_encoding_box && rt_box_type(transfer_encoding_box) == RT_BOX_STR)
        transfer_encoding_val = rt_unbox_str(transfer_encoding_box);
    rt_string_unref(transfer_encoding_key);

    bool is_head = strcmp(req->method, "HEAD") == 0;

    if (is_head) {
        // HEAD requests have no body
        body = NULL;
        body_len = 0;
    } else if (transfer_encoding_val && strstr(rt_string_cstr(transfer_encoding_val), "chunked")) {
        body = read_body_chunked_conn(&conn, &body_len);
    } else if (content_length_val) {
        // M-5: atoll returns -1 on parse error; guard against negative cast to SIZE_MAX
        long long parsed_len = atoll(rt_string_cstr(content_length_val));
        size_t content_len = parsed_len > 0 ? (size_t)parsed_len : 0;
        body = read_body_content_length_conn(&conn, content_len, &body_len);
    } else {
        // Read until connection closes
        body = read_body_until_close_conn(&conn, &body_len);
    }

    http_conn_close(&conn);
    if (transfer_encoding_val)
        rt_string_unref(transfer_encoding_val);
    if (content_length_val)
        rt_string_unref(content_length_val);

    if (!is_head && !body) {
        if (headers_map && rt_obj_release_check0(headers_map))
            rt_obj_free(headers_map);
        free(status_text);
        rt_trap_net("HTTP: incomplete response body", Err_ProtocolError);
        return NULL;
    }

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

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        return 0;
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res)
        return 0;

    if (res->status < 200 || res->status >= 300) {
        if (rt_obj_release_check0(res))
            rt_obj_free(res);
        return 0;
    }

    // Write to file
    FILE *f = fopen(path_str, "wb");
    if (!f) {
        if (rt_obj_release_check0(res))
            rt_obj_free(res);
        return 0;
    }

    size_t expected = res->body_len;
    size_t written = fwrite(res->body, 1, expected, f);
    int close_err = fclose(f);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    // RC-14: if fwrite wrote fewer bytes (disk full, etc.) or fclose failed
    // (buffered data flush failure), remove the partial/corrupt file.
    if (written != expected || close_err != 0) {
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
    req->follow_redirects = 1;
    req->max_redirects = HTTP_MAX_REDIRECTS;

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
