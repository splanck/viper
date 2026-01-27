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
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#elif defined(__viperdos__)
// ViperDOS: Use POSIX-style case-insensitive comparison
// TODO: ViperDOS - strings.h should be available in libc
#include <ctype.h>
static int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
static int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n-- && *s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    if (n == (size_t)-1)
        return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
#else
#include <strings.h>
#include <unistd.h>
#endif

//=============================================================================
// Internal Bytes Access
//=============================================================================

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

// Forward declaration for Windows WSA init
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

/// @brief Parsed URL structure.
typedef struct parsed_url
{
    char *host;  // Allocated hostname
    int port;    // Port number (default 80 for http, 443 for https)
    char *path;  // Path including query string (allocated)
    int use_tls; // 1 for https, 0 for http
} parsed_url_t;

/// @brief HTTP header entry.
typedef struct http_header
{
    char *name;
    char *value;
    struct http_header *next;
} http_header_t;

/// @brief HTTP connection context (TCP or TLS).
typedef struct http_conn
{
    void *tcp;              // TCP connection (for plain HTTP)
    rt_tls_session_t *tls;  // TLS session (for HTTPS)
    int use_tls;            // 1 if using TLS
    uint8_t read_buf[4096]; // Read buffer
    size_t read_buf_len;    // Bytes in buffer
    size_t read_buf_pos;    // Current position in buffer
} http_conn_t;

/// @brief Initialize HTTP connection for TCP.
static void http_conn_init_tcp(http_conn_t *conn, void *tcp)
{
    conn->tcp = tcp;
    conn->tls = NULL;
    conn->use_tls = 0;
    conn->read_buf_len = 0;
    conn->read_buf_pos = 0;
}

/// @brief Initialize HTTP connection for TLS.
static void http_conn_init_tls(http_conn_t *conn, rt_tls_session_t *tls)
{
    conn->tcp = NULL;
    conn->tls = tls;
    conn->use_tls = 1;
    conn->read_buf_len = 0;
    conn->read_buf_pos = 0;
}

/// @brief Send all data over HTTP connection.
static int http_conn_send(http_conn_t *conn, const uint8_t *data, size_t len)
{
    if (conn->use_tls)
    {
        long sent = rt_tls_send(conn->tls, data, len);
        return sent >= 0 ? 0 : -1;
    }
    else
    {
        void *bytes = rt_bytes_new((int64_t)len);
        memcpy(bytes_data(bytes), data, len);
        rt_tcp_send_all(conn->tcp, bytes);
        return 0;
    }
}

/// @brief Receive data from HTTP connection (buffered).
static long http_conn_recv(http_conn_t *conn, uint8_t *buf, size_t len)
{
    size_t total = 0;

    // First, drain any buffered data
    while (total < len && conn->read_buf_pos < conn->read_buf_len)
    {
        buf[total++] = conn->read_buf[conn->read_buf_pos++];
    }

    if (total == len)
        return (long)total;

    // Need more data from network
    if (conn->use_tls)
    {
        long n = rt_tls_recv(conn->tls, buf + total, len - total);
        if (n > 0)
            total += n;
    }
    else
    {
        void *data = rt_tcp_recv(conn->tcp, (int64_t)(len - total));
        int64_t data_len = rt_bytes_len(data);
        if (data_len > 0)
        {
            memcpy(buf + total, bytes_data(data), data_len);
            total += data_len;
        }
    }

    return (long)total;
}

/// @brief Receive exactly one byte from HTTP connection.
static int http_conn_recv_byte(http_conn_t *conn, uint8_t *byte)
{
    // Check buffer first
    if (conn->read_buf_pos < conn->read_buf_len)
    {
        *byte = conn->read_buf[conn->read_buf_pos++];
        return 1;
    }

    // Refill buffer
    if (conn->use_tls)
    {
        long n = rt_tls_recv(conn->tls, conn->read_buf, sizeof(conn->read_buf));
        if (n <= 0)
            return 0;
        conn->read_buf_len = (size_t)n;
        conn->read_buf_pos = 0;
    }
    else
    {
        void *data = rt_tcp_recv(conn->tcp, (int64_t)sizeof(conn->read_buf));
        int64_t data_len = rt_bytes_len(data);
        if (data_len <= 0)
            return 0;
        memcpy(conn->read_buf, bytes_data(data), data_len);
        conn->read_buf_len = (size_t)data_len;
        conn->read_buf_pos = 0;
    }

    *byte = conn->read_buf[conn->read_buf_pos++];
    return 1;
}

/// @brief Close HTTP connection.
static void http_conn_close(http_conn_t *conn)
{
    if (conn->use_tls && conn->tls)
    {
        int sock = rt_tls_get_socket(conn->tls);
        rt_tls_close(conn->tls);
        conn->tls = NULL;
        // Close underlying socket
        if (sock >= 0)
        {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
        }
    }
    else if (conn->tcp)
    {
        rt_tcp_close(conn->tcp);
        conn->tcp = NULL;
    }
}

/// @brief HTTP request structure.
typedef struct rt_http_req
{
    char *method;           // HTTP method
    parsed_url_t url;       // Parsed URL
    http_header_t *headers; // Linked list of headers
    uint8_t *body;          // Request body
    size_t body_len;        // Body length
    int timeout_ms;         // Timeout in milliseconds
} rt_http_req_t;

/// @brief HTTP response structure.
typedef struct rt_http_res
{
    int status;        // HTTP status code
    char *status_text; // Status text (allocated)
    void *headers;     // Map of headers
    uint8_t *body;     // Response body
    size_t body_len;   // Body length
} rt_http_res_t;

/// @brief Free parsed URL.
static void free_parsed_url(parsed_url_t *url)
{
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    url->host = NULL;
    url->path = NULL;
}

/// @brief Parse URL into components.
/// @return 0 on success, -1 on error.
static int parse_url(const char *url_str, parsed_url_t *result)
{
    memset(result, 0, sizeof(*result));
    result->port = 80;
    result->use_tls = 0;

    // Check for http:// or https:// prefix
    if (strncmp(url_str, "http://", 7) == 0)
    {
        url_str += 7;
        result->use_tls = 0;
        result->port = 80;
    }
    else if (strncmp(url_str, "https://", 8) == 0)
    {
        url_str += 8;
        result->use_tls = 1;
        result->port = 443;
    }

    // Find end of host (either ':', '/', or end of string)
    const char *host_end = url_str;
    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    size_t host_len = host_end - url_str;
    if (host_len == 0)
        return -1;

    result->host = (char *)malloc(host_len + 1);
    if (!result->host)
        return -1;
    memcpy(result->host, url_str, host_len);
    result->host[host_len] = '\0';

    const char *p = host_end;

    // Parse port if present
    if (*p == ':')
    {
        p++;
        result->port = 0;
        while (*p >= '0' && *p <= '9')
        {
            result->port = result->port * 10 + (*p - '0');
            p++;
        }
        if (result->port <= 0 || result->port > 65535)
        {
            free_parsed_url(result);
            return -1;
        }
    }

    // Parse path (default to "/")
    if (*p == '/')
    {
        size_t path_len = strlen(p);
        result->path = (char *)malloc(path_len + 1);
        if (!result->path)
        {
            free_parsed_url(result);
            return -1;
        }
        memcpy(result->path, p, path_len + 1);
    }
    else
    {
        result->path = (char *)malloc(2);
        if (!result->path)
        {
            free_parsed_url(result);
            return -1;
        }
        result->path[0] = '/';
        result->path[1] = '\0';
    }

    return 0;
}

/// @brief Free header list.
static void free_headers(http_header_t *headers)
{
    while (headers)
    {
        http_header_t *next = headers->next;
        free(headers->name);
        free(headers->value);
        free(headers);
        headers = next;
    }
}

static void rt_http_req_finalize(void *obj)
{
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

static void rt_http_res_finalize(void *obj)
{
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

/// @brief Add header to request.
static void add_header(rt_http_req_t *req, const char *name, const char *value)
{
    http_header_t *h = (http_header_t *)malloc(sizeof(http_header_t));
    if (!h)
        return;
    h->name = strdup(name);
    h->value = strdup(value);
    h->next = req->headers;
    req->headers = h;
}

/// @brief Check if header exists (case-insensitive).
static bool has_header(rt_http_req_t *req, const char *name)
{
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        if (strcasecmp(h->name, name) == 0)
            return true;
    }
    return false;
}

/// @brief Build HTTP request string.
/// @return Allocated string, caller must free.
static char *build_request(rt_http_req_t *req)
{
    // Calculate total size
    size_t size =
        strlen(req->method) + 1 + strlen(req->url.path) + 11; // "METHOD PATH HTTP/1.1\r\n"

    // Host header
    char host_header[300];
    if (req->url.port != 80)
        snprintf(host_header, sizeof(host_header), "Host: %s:%d\r\n", req->url.host, req->url.port);
    else
        snprintf(host_header, sizeof(host_header), "Host: %s\r\n", req->url.host);
    size += strlen(host_header);

    // Content-Length if body
    char content_len_header[64] = "";
    if (req->body && req->body_len > 0)
    {
        snprintf(content_len_header,
                 sizeof(content_len_header),
                 "Content-Length: %zu\r\n",
                 req->body_len);
        size += strlen(content_len_header);
    }

    // Connection header
    size += 19; // "Connection: close\r\n"

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        size += strlen(h->name) + 2 + strlen(h->value) + 2; // "Name: Value\r\n"
    }

    size += 2; // Final CRLF
    size += req->body_len;
    size += 1; // Null terminator

    char *request = (char *)malloc(size);
    if (!request)
        return NULL;

    char *p = request;
    p += sprintf(p, "%s %s HTTP/1.1\r\n", req->method, req->url.path);
    p += sprintf(p, "%s", host_header);

    if (content_len_header[0])
        p += sprintf(p, "%s", content_len_header);

    p += sprintf(p, "Connection: close\r\n");

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        p += sprintf(p, "%s: %s\r\n", h->name, h->value);
    }

    p += sprintf(p, "\r\n");

    // Body
    if (req->body && req->body_len > 0)
    {
        memcpy(p, req->body, req->body_len);
        p += req->body_len;
    }

    *p = '\0';
    return request;
}

/// @brief Read a line from connection (up to CRLF).
/// @return Allocated line without CRLF, or NULL on error.
static char *read_line_conn(http_conn_t *conn)
{
    char *line = NULL;
    size_t len = 0;
    size_t cap = 256;
    line = (char *)malloc(cap);
    if (!line)
        return NULL;

    while (1)
    {
        uint8_t byte;
        if (http_conn_recv_byte(conn, &byte) == 0)
        {
            // Connection closed
            if (len == 0)
            {
                free(line);
                return NULL;
            }
            break;
        }

        char c = (char)byte;

        if (c == '\n')
        {
            // Remove trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len + 1 >= cap)
        {
            cap *= 2;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line)
            {
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
static int parse_status_line(const char *line, char **status_text_out)
{
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
    while (*p >= '0' && *p <= '9')
    {
        status = status * 10 + (*p - '0');
        p++;
    }

    if (status < 100 || status > 599)
        return -1;

    // Skip space and get status text
    if (*p == ' ')
        p++;

    if (status_text_out)
        *status_text_out = strdup(p);

    return status;
}

/// @brief Parse header line into name and value.
static void parse_header_line(const char *line, void *headers_map)
{
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
    for (char *p = name; *p; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = *p + ('a' - 'A');
    }

    rt_string name_str = rt_string_from_bytes(name, strlen(name));
    rt_string value_str = rt_string_from_bytes(value, strlen(value));
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
                                              size_t *out_len)
{
    uint8_t *body = (uint8_t *)malloc(content_length);
    if (!body)
        return NULL;

    size_t total_read = 0;
    while (total_read < content_length)
    {
        size_t remaining = content_length - total_read;
        size_t chunk_size = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

        long len = http_conn_recv(conn, body + total_read, chunk_size);
        if (len <= 0)
            break;

        total_read += len;
    }

    *out_len = total_read;
    return body;
}

/// @brief Read chunked transfer encoding body.
static uint8_t *read_body_chunked_conn(http_conn_t *conn, size_t *out_len)
{
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1)
    {
        // Read chunk size line
        char *size_line = read_line_conn(conn);
        if (!size_line)
            break;

        // Parse hex chunk size
        size_t chunk_size = 0;
        for (char *p = size_line; *p; p++)
        {
            char c = *p;
            if (c >= '0' && c <= '9')
                chunk_size = chunk_size * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f')
                chunk_size = chunk_size * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                chunk_size = chunk_size * 16 + (c - 'A' + 10);
            else
                break;
        }
        free(size_line);

        if (chunk_size == 0)
        {
            // Last chunk - read trailing CRLF
            char *trailer = read_line_conn(conn);
            free(trailer);
            break;
        }

        // Expand body buffer if needed
        while (body_len + chunk_size > body_cap)
        {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body)
            {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        // Read chunk data
        size_t bytes_read = 0;
        while (bytes_read < chunk_size)
        {
            size_t remaining = chunk_size - bytes_read;
            size_t to_read = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

            long len = http_conn_recv(conn, body + body_len, to_read);
            if (len <= 0)
            {
                *out_len = body_len;
                return body;
            }

            body_len += len;
            bytes_read += len;
        }

        // Read trailing CRLF after chunk
        char *chunk_end = read_line_conn(conn);
        free(chunk_end);
    }

    *out_len = body_len;
    return body;
}

/// @brief Read response body until connection closes.
static uint8_t *read_body_until_close_conn(http_conn_t *conn, size_t *out_len)
{
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1)
    {
        // Expand buffer if needed
        if (body_len + HTTP_BUFFER_SIZE > body_cap)
        {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body)
            {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        long len = http_conn_recv(conn, body + body_len, HTTP_BUFFER_SIZE);
        if (len <= 0)
            break;

        body_len += len;
    }

    *out_len = body_len;
    return body;
}

/// @brief Perform HTTP request and return response.
static rt_http_res_t *do_http_request(rt_http_req_t *req, int redirects_remaining)
{
    rt_net_init_wsa();

    if (redirects_remaining <= 0)
    {
        rt_trap("HTTP: too many redirects");
        return NULL;
    }

    // Create connection (TLS or plain TCP)
    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));

    if (req->url.use_tls)
    {
        // HTTPS - use TLS
        rt_tls_config_t tls_config;
        rt_tls_config_init(&tls_config);
        tls_config.hostname = req->url.host;
        tls_config.verify_cert = 0; // Skip cert verification for now
        if (req->timeout_ms > 0)
            tls_config.timeout_ms = req->timeout_ms;

        rt_tls_session_t *tls = rt_tls_connect(req->url.host, (uint16_t)req->url.port, &tls_config);
        if (!tls)
        {
            rt_trap("HTTPS: connection failed");
            return NULL;
        }
        http_conn_init_tls(&conn, tls);
    }
    else
    {
        // HTTP - use plain TCP
        rt_string host = rt_string_from_bytes(req->url.host, strlen(req->url.host));
        void *tcp = req->timeout_ms > 0 ? rt_tcp_connect_for(host, req->url.port, req->timeout_ms)
                                        : rt_tcp_connect(host, req->url.port);
        rt_string_unref(host);

        if (!tcp || !rt_tcp_is_open(tcp))
        {
            rt_trap("HTTP: connection failed");
            return NULL;
        }

        // Set socket timeout
        if (req->timeout_ms > 0)
        {
            rt_tcp_set_recv_timeout(tcp, req->timeout_ms);
            rt_tcp_set_send_timeout(tcp, req->timeout_ms);
        }
        http_conn_init_tcp(&conn, tcp);
    }

    // Build and send request
    char *request_str = build_request(req);
    if (!request_str)
    {
        http_conn_close(&conn);
        rt_trap("HTTP: failed to build request");
        return NULL;
    }

    size_t header_len = strlen(request_str);
    size_t request_len = header_len + (req->body ? req->body_len : 0);
    uint8_t *request_buf = (uint8_t *)malloc(request_len);
    if (!request_buf)
    {
        free(request_str);
        http_conn_close(&conn);
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }

    memcpy(request_buf, request_str, header_len);
    if (req->body && req->body_len > 0)
        memcpy(request_buf + header_len, req->body, req->body_len);

    free(request_str);

    if (http_conn_send(&conn, request_buf, request_len) < 0)
    {
        free(request_buf);
        http_conn_close(&conn);
        rt_trap("HTTP: send failed");
        return NULL;
    }
    free(request_buf);

    // Read status line
    char *status_line = read_line_conn(&conn);
    if (!status_line)
    {
        http_conn_close(&conn);
        rt_trap("HTTP: invalid response");
        return NULL;
    }

    char *status_text = NULL;
    int status = parse_status_line(status_line, &status_text);
    free(status_line);

    if (status < 0)
    {
        http_conn_close(&conn);
        rt_trap("HTTP: invalid status line");
        return NULL;
    }

    // Read headers
    void *headers_map = rt_map_new();
    char *redirect_location = NULL;

    while (1)
    {
        char *line = read_line_conn(&conn);
        if (!line || line[0] == '\0')
        {
            free(line);
            break;
        }

        // Check for Location header (for redirects)
        if (strncasecmp(line, "location:", 9) == 0)
        {
            const char *loc = line + 9;
            while (*loc == ' ')
                loc++;
            redirect_location = strdup(loc);
        }

        parse_header_line(line, headers_map);
        free(line);
    }

    // Handle redirects (3xx with Location)
    if ((status == 301 || status == 302 || status == 307 || status == 308) && redirect_location)
    {
        http_conn_close(&conn);
        free(status_text);

        // Parse new URL
        parsed_url_t new_url;
        if (parse_url(redirect_location, &new_url) < 0)
        {
            // Relative URL - use same host and scheme
            if (redirect_location[0] == '/')
            {
                free(req->url.path);
                req->url.path = redirect_location;
            }
            else
            {
                free(redirect_location);
                rt_trap("HTTP: invalid redirect URL");
                return NULL;
            }
        }
        else
        {
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

    if (is_head)
    {
        // HEAD requests have no body
        body = NULL;
        body_len = 0;
    }
    else if (transfer_encoding_val && strstr(rt_string_cstr(transfer_encoding_val), "chunked"))
    {
        body = read_body_chunked_conn(&conn, &body_len);
    }
    else if (content_length_val)
    {
        size_t content_len = (size_t)atoll(rt_string_cstr(content_length_val));
        body = read_body_content_length_conn(&conn, content_len, &body_len);
    }
    else
    {
        // Read until connection closes
        body = read_body_until_close_conn(&conn, &body_len);
    }

    http_conn_close(&conn);
    if (transfer_encoding_val)
        rt_string_unref(transfer_encoding_val);
    if (content_length_val)
        rt_string_unref(content_length_val);

    // Create response object (must use rt_obj_new_i64 for GC management)
    rt_http_res_t *res = (rt_http_res_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_res_t));
    if (!res)
    {
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

rt_string rt_http_get(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    // Create request
    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    // Execute request
    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

void *rt_http_get_bytes(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

rt_string rt_http_post(rt_string url, rt_string body)
{
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "POST";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    if (body_str)
    {
        req.body = (uint8_t *)body_str;
        req.body_len = strlen(body_str);
    }

    // Add Content-Type if not empty body
    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap("HTTP: request failed");

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

void *rt_http_post_bytes(rt_string url, void *body)
{
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "POST";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    if (body)
    {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap("HTTP: request failed");

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

int8_t rt_http_download(rt_string url, rt_string dest_path)
{
    const char *url_str = rt_string_cstr(url);
    const char *path_str = rt_string_cstr(dest_path);

    if (!url_str || *url_str == '\0')
        return 0;
    if (!path_str || *path_str == '\0')
        return 0;

    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        return 0;

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        return 0;

    if (res->status < 200 || res->status >= 300)
    {
        if (rt_obj_release_check0(res))
            rt_obj_free(res);
        return 0;
    }

    // Write to file
    FILE *f = fopen(path_str, "wb");
    if (!f)
    {
        if (rt_obj_release_check0(res))
            rt_obj_free(res);
        return 0;
    }

    size_t written = fwrite(res->body, 1, res->body_len, f);
    fclose(f);

    size_t expected = res->body_len;
    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return written == expected ? 1 : 0;
}

void *rt_http_head(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "HEAD";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    return res;
}

//=============================================================================
// HttpReq Instance Class Implementation
//=============================================================================

void *rt_http_req_new(rt_string method, rt_string url)
{
    const char *method_str = rt_string_cstr(method);
    const char *url_str = rt_string_cstr(url);

    if (!method_str || *method_str == '\0')
        rt_trap("HTTP: invalid method");
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    // Must use rt_obj_new_i64 for GC management
    rt_http_req_t *req = (rt_http_req_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_req_t));
    if (!req)
        rt_trap("HTTP: memory allocation failed");

    memset(req, 0, sizeof(*req));
    rt_obj_set_finalizer(req, rt_http_req_finalize);
    req->method = strdup(method_str);
    req->timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req->url) < 0)
    {
        free(req->method);
        // Note: GC-managed object, so we don't free it directly
        rt_trap("HTTP: invalid URL format");
    }

    return req;
}

void *rt_http_req_set_header(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (name_str && value_str)
        add_header(req, name_str, value_str);

    return obj;
}

void *rt_http_req_set_body(void *obj, void *data)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    if (req->body)
    {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (data)
    {
        int64_t len = rt_bytes_len(data);
        uint8_t *ptr = bytes_data(data);

        // Make a copy of the body
        req->body = (uint8_t *)malloc(len);
        if (req->body)
        {
            memcpy(req->body, ptr, len);
            req->body_len = len;
        }
    }

    return obj;
}

void *rt_http_req_set_body_str(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *text_str = rt_string_cstr(text);

    if (req->body)
    {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (text_str)
    {
        size_t len = strlen(text_str);
        req->body = (uint8_t *)malloc(len);
        if (req->body)
        {
            memcpy(req->body, text_str, len);
            req->body_len = len;
        }
    }

    return obj;
}

void *rt_http_req_set_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->timeout_ms = (int)timeout_ms;

    return obj;
}

void *rt_http_req_send(void *obj)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    // Add Content-Type for POST with body if not set
    if (req->body && req->body_len > 0 && !has_header(req, "Content-Type"))
    {
        add_header(req, "Content-Type", "application/octet-stream");
    }

    rt_http_res_t *res = do_http_request(req, HTTP_MAX_REDIRECTS);
    return res;
}

//=============================================================================
// HttpRes Instance Class Implementation
//=============================================================================

int64_t rt_http_res_status(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_http_res_t *)obj)->status;
}

rt_string rt_http_res_status_text(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->status_text)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(res->status_text, strlen(res->status_text));
}

void *rt_http_res_headers(void *obj)
{
    if (!obj)
        return rt_map_new();
    return ((rt_http_res_t *)obj)->headers;
}

void *rt_http_res_body(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    void *bytes = rt_bytes_new((int64_t)res->body_len);
    if (res->body && res->body_len > 0)
    {
        uint8_t *ptr = bytes_data(bytes);
        memcpy(ptr, res->body, res->body_len);
    }
    return bytes;
}

rt_string rt_http_res_body_str(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->body || res->body_len == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes((const char *)res->body, res->body_len);
}

rt_string rt_http_res_header(void *obj, rt_string name)
{
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

    for (size_t i = 0; i <= len; i++)
    {
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

    return rt_unbox_str(boxed);
}

int8_t rt_http_res_is_ok(void *obj)
{
    if (!obj)
        return 0;

    int status = ((rt_http_res_t *)obj)->status;
    return (status >= 200 && status < 300) ? 1 : 0;
}

//=============================================================================
// URL Parsing and Construction Implementation
//=============================================================================

/// @brief URL structure.
typedef struct rt_url
{
    char *scheme;   // URL scheme (e.g., "http", "https")
    char *user;     // Username (optional)
    char *pass;     // Password (optional)
    char *host;     // Hostname
    int64_t port;   // Port number (0 = not specified)
    char *path;     // Path component
    char *query;    // Query string (without leading ?)
    char *fragment; // Fragment (without leading #)
} rt_url_t;

/// @brief Get default port for a scheme.
/// @return Default port or 0 if unknown.
static int64_t default_port_for_scheme(const char *scheme)
{
    if (!scheme)
        return 0;
    if (strcmp(scheme, "http") == 0)
        return 80;
    if (strcmp(scheme, "https") == 0)
        return 443;
    if (strcmp(scheme, "ftp") == 0)
        return 21;
    if (strcmp(scheme, "ssh") == 0)
        return 22;
    if (strcmp(scheme, "telnet") == 0)
        return 23;
    if (strcmp(scheme, "smtp") == 0)
        return 25;
    if (strcmp(scheme, "dns") == 0)
        return 53;
    if (strcmp(scheme, "pop3") == 0)
        return 110;
    if (strcmp(scheme, "imap") == 0)
        return 143;
    if (strcmp(scheme, "ldap") == 0)
        return 389;
    if (strcmp(scheme, "ws") == 0)
        return 80;
    if (strcmp(scheme, "wss") == 0)
        return 443;
    return 0;
}

/// @brief Check if character is unreserved (RFC 3986).
static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

// Note: hex_char_to_int functionality provided by rt_hex_digit_value() in rt_internal.h

/// @brief Percent-encode a string.
/// @return Allocated string, caller must free.
static char *percent_encode(const char *str, bool encode_slash)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    // Worst case: every char becomes %XX
    char *result = (char *)malloc(len * 3 + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (is_unreserved(c) || (!encode_slash && c == '/'))
        {
            *p++ = c;
        }
        else
        {
            *p++ = '%';
            *p++ = rt_hex_chars_upper[(unsigned char)c >> 4];
            *p++ = rt_hex_chars_upper[(unsigned char)c & 0x0F];
        }
    }
    *p = '\0';
    return result;
}

/// @brief Percent-decode a string.
/// @return Allocated string, caller must free.
static char *percent_decode(const char *str)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] == '%' && i + 2 < len)
        {
            int high = rt_hex_digit_value(str[i + 1]);
            int low = rt_hex_digit_value(str[i + 2]);
            if (high >= 0 && low >= 0)
            {
                *p++ = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        else if (str[i] == '+')
        {
            // Plus is space in query strings
            *p++ = ' ';
            continue;
        }
        *p++ = str[i];
    }
    *p = '\0';
    return result;
}

/// @brief Duplicate a string safely (handles NULL).
static char *safe_strdup(const char *str)
{
    return str ? strdup(str) : NULL;
}

static void free_url(rt_url_t *url);

/// @brief Internal URL parsing.
/// @return 0 on success, -1 on error.
static int parse_url_full(const char *url_str, rt_url_t *result)
{
    memset(result, 0, sizeof(*result));

    if (!url_str || *url_str == '\0')
        return -1;

    const char *p = url_str;

    // Parse scheme (if present)
    const char *scheme_end = strstr(p, "://");
    bool has_authority = false;
    if (scheme_end)
    {
        size_t scheme_len = scheme_end - p;
        result->scheme = (char *)malloc(scheme_len + 1);
        if (!result->scheme)
            return -1;
        memcpy(result->scheme, p, scheme_len);
        result->scheme[scheme_len] = '\0';

        // Convert scheme to lowercase
        for (char *s = result->scheme; *s; s++)
        {
            if (*s >= 'A' && *s <= 'Z')
                *s = *s + ('a' - 'A');
        }

        p = scheme_end + 3; // Skip "://"
        has_authority = true;
    }
    else if (p[0] == '/' && p[1] == '/')
    {
        // Network-path reference (starts with //)
        p += 2;
        has_authority = true;
    }

    // Parse authority (userinfo@host:port) - only if we have a scheme or //
    if (has_authority && *p && *p != '/' && *p != '?' && *p != '#')
    {
        // Find end of authority
        const char *auth_end = p;
        while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
            auth_end++;

        // Check for userinfo (@)
        const char *at_sign = NULL;
        for (const char *s = p; s < auth_end; s++)
        {
            if (*s == '@')
            {
                at_sign = s;
                break;
            }
        }

        const char *host_start = p;
        if (at_sign)
        {
            // Parse userinfo
            const char *colon = NULL;
            for (const char *s = p; s < at_sign; s++)
            {
                if (*s == ':')
                {
                    colon = s;
                    break;
                }
            }

            if (colon)
            {
                // user:pass
                size_t user_len = colon - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }

                size_t pass_len = at_sign - colon - 1;
                result->pass = (char *)malloc(pass_len + 1);
                if (result->pass)
                {
                    memcpy(result->pass, colon + 1, pass_len);
                    result->pass[pass_len] = '\0';
                }
            }
            else
            {
                // Just user
                size_t user_len = at_sign - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }
            }
            host_start = at_sign + 1;
        }

        // Parse host:port
        // Check for IPv6 literal [...]
        const char *port_colon = NULL;
        if (*host_start == '[')
        {
            // IPv6 literal
            const char *bracket_end = strchr(host_start, ']');
            if (bracket_end && bracket_end < auth_end)
            {
                size_t host_len = bracket_end - host_start + 1;
                result->host = (char *)malloc(host_len + 1);
                if (result->host)
                {
                    memcpy(result->host, host_start, host_len);
                    result->host[host_len] = '\0';
                }
                if (bracket_end + 1 < auth_end && *(bracket_end + 1) == ':')
                    port_colon = bracket_end + 1;
            }
        }
        else
        {
            // Regular host
            for (const char *s = host_start; s < auth_end; s++)
            {
                if (*s == ':')
                {
                    port_colon = s;
                    break;
                }
            }

            const char *host_end = port_colon ? port_colon : auth_end;
            size_t host_len = host_end - host_start;
            result->host = (char *)malloc(host_len + 1);
            if (result->host)
            {
                memcpy(result->host, host_start, host_len);
                result->host[host_len] = '\0';
            }
        }

        // Parse port
        if (port_colon && port_colon + 1 < auth_end)
        {
            result->port = 0;
            for (const char *s = port_colon + 1; s < auth_end && *s >= '0' && *s <= '9'; s++)
            {
                result->port = result->port * 10 + (*s - '0');
            }
        }

        p = auth_end;
    }
    else if (has_authority)
    {
        free_url(result);
        return -1;
    }

    if (has_authority && (!result->host || result->host[0] == '\0'))
    {
        free_url(result);
        return -1;
    }

    // Parse path
    const char *path_start = p;
    const char *path_end = p;
    while (*path_end && *path_end != '?' && *path_end != '#')
        path_end++;

    if (path_end > path_start)
    {
        size_t path_len = path_end - path_start;
        result->path = (char *)malloc(path_len + 1);
        if (result->path)
        {
            memcpy(result->path, path_start, path_len);
            result->path[path_len] = '\0';
        }
    }

    p = path_end;

    // Parse query
    if (*p == '?')
    {
        p++;
        const char *query_end = p;
        while (*query_end && *query_end != '#')
            query_end++;

        size_t query_len = query_end - p;
        result->query = (char *)malloc(query_len + 1);
        if (result->query)
        {
            memcpy(result->query, p, query_len);
            result->query[query_len] = '\0';
        }

        p = query_end;
    }

    // Parse fragment
    if (*p == '#')
    {
        p++;
        size_t frag_len = strlen(p);
        result->fragment = (char *)malloc(frag_len + 1);
        if (result->fragment)
        {
            memcpy(result->fragment, p, frag_len);
            result->fragment[frag_len] = '\0';
        }
    }

    return 0;
}

/// @brief Free URL structure contents.
static void free_url(rt_url_t *url)
{
    if (url->scheme)
        free(url->scheme);
    if (url->user)
        free(url->user);
    if (url->pass)
        free(url->pass);
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    if (url->query)
        free(url->query);
    if (url->fragment)
        free(url->fragment);
    memset(url, 0, sizeof(*url));
}

static void rt_url_finalize(void *obj)
{
    if (!obj)
        return;
    rt_url_t *url = (rt_url_t *)obj;
    free_url(url);
}

void *rt_url_parse(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str)
        rt_trap("URL: Invalid URL string");

    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);

    if (parse_url_full(str, url) != 0)
    {
        rt_trap("URL: Failed to parse URL");
    }

    return url;
}

void *rt_url_new(void)
{
    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);
    return url;
}

rt_string rt_url_scheme(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->scheme)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->scheme, strlen(url->scheme));
}

void rt_url_set_scheme(void *obj, rt_string scheme)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->scheme)
        free(url->scheme);

    const char *str = rt_string_cstr(scheme);
    url->scheme = str ? strdup(str) : NULL;

    // Convert to lowercase
    if (url->scheme)
    {
        for (char *p = url->scheme; *p; p++)
        {
            if (*p >= 'A' && *p <= 'Z')
                *p = *p + ('a' - 'A');
        }
    }
}

rt_string rt_url_host(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->host, strlen(url->host));
}

void rt_url_set_host(void *obj, rt_string host)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->host)
        free(url->host);

    const char *str = rt_string_cstr(host);
    url->host = str ? strdup(str) : NULL;
}

int64_t rt_url_port(void *obj)
{
    if (!obj)
        return 0;

    return ((rt_url_t *)obj)->port;
}

void rt_url_set_port(void *obj, int64_t port)
{
    if (!obj)
        return;

    ((rt_url_t *)obj)->port = port;
}

rt_string rt_url_path(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->path)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->path, strlen(url->path));
}

void rt_url_set_path(void *obj, rt_string path)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->path)
        free(url->path);

    const char *str = rt_string_cstr(path);
    url->path = str ? strdup(str) : NULL;
}

rt_string rt_url_query(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->query, strlen(url->query));
}

void rt_url_set_query(void *obj, rt_string query)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->query)
        free(url->query);

    const char *str = rt_string_cstr(query);
    url->query = str ? strdup(str) : NULL;
}

rt_string rt_url_fragment(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->fragment)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->fragment, strlen(url->fragment));
}

void rt_url_set_fragment(void *obj, rt_string fragment)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->fragment)
        free(url->fragment);

    const char *str = rt_string_cstr(fragment);
    url->fragment = str ? strdup(str) : NULL;
}

rt_string rt_url_user(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->user)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->user, strlen(url->user));
}

void rt_url_set_user(void *obj, rt_string user)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->user)
        free(url->user);

    const char *str = rt_string_cstr(user);
    url->user = str ? strdup(str) : NULL;
}

rt_string rt_url_pass(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->pass)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->pass, strlen(url->pass));
}

void rt_url_set_pass(void *obj, rt_string pass)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->pass)
        free(url->pass);

    const char *str = rt_string_cstr(pass);
    url->pass = str ? strdup(str) : NULL;
}

rt_string rt_url_authority(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate size: user:pass@host:port
    size_t size = 0;
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass); // :pass
        size += 1;                         // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 8; // :65535

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    if (url->user)
    {
        p += sprintf(p, "%s", url->user);
        if (url->pass)
            p += sprintf(p, ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += sprintf(p, "%s", url->host);
    if (url->port > 0)
        p += sprintf(p, ":%lld", (long long)url->port);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

rt_string rt_url_host_port(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    // Check if port is default for scheme
    int64_t default_port = default_port_for_scheme(url->scheme);
    bool show_port = url->port > 0 && url->port != default_port;

    size_t size = strlen(url->host) + (show_port ? 8 : 0);
    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    if (show_port)
        sprintf(result, "%s:%lld", url->host, (long long)url->port);
    else
        strcpy(result, url->host);

    rt_string str = rt_string_from_bytes(result, strlen(result));
    free(result);
    return str;
}

rt_string rt_url_full(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate total size
    size_t size = 0;
    if (url->scheme)
        size += strlen(url->scheme) + 3; // scheme://
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass);
        size += 1; // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 8; // :65535
    if (url->path)
        size += strlen(url->path);
    if (url->query)
        size += 1 + strlen(url->query); // ?query
    if (url->fragment)
        size += 1 + strlen(url->fragment); // #fragment

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    if (url->scheme)
        p += sprintf(p, "%s://", url->scheme);
    if (url->user)
    {
        p += sprintf(p, "%s", url->user);
        if (url->pass)
            p += sprintf(p, ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += sprintf(p, "%s", url->host);
    if (url->port > 0)
    {
        int64_t default_port = default_port_for_scheme(url->scheme);
        if (url->port != default_port)
            p += sprintf(p, ":%lld", (long long)url->port);
    }
    if (url->path)
        p += sprintf(p, "%s", url->path);
    if (url->query && url->query[0])
        p += sprintf(p, "?%s", url->query);
    if (url->fragment && url->fragment[0])
        p += sprintf(p, "#%s", url->fragment);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

void *rt_url_set_query_param(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (!name_str)
        return obj;

    // Encode name and value
    char *enc_name = percent_encode(name_str, true);
    char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

    if (!enc_name || !enc_value)
    {
        free(enc_name);
        free(enc_value);
        return obj;
    }

    // Parse existing query into map
    void *map = rt_url_decode_query(
        rt_string_from_bytes(url->query ? url->query : "", url->query ? strlen(url->query) : 0));

    // Set the new param
    void *boxed = rt_box_str(value);
    rt_map_set(map, name, boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Rebuild query string
    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);
    url->query = strdup(rt_string_cstr(new_query));

    free(enc_name);
    free(enc_value);
    return obj;
}

rt_string rt_url_get_query_param(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    void *boxed = rt_map_get(map, name);

    if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
        return rt_string_from_bytes("", 0);

    return rt_unbox_str(boxed);
}

int8_t rt_url_has_query_param(void *obj, rt_string name)
{
    if (!obj)
        return 0;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return 0;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    return rt_map_has(map, name);
}

void *rt_url_del_query_param(void *obj, rt_string name)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return obj;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    rt_map_remove(map, name);

    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);

    const char *query_str = rt_string_cstr(new_query);
    url->query = query_str && *query_str ? strdup(query_str) : NULL;

    return obj;
}

void *rt_url_query_map(void *obj)
{
    if (!obj)
        return rt_map_new();

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_map_new();

    return rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
}

void *rt_url_resolve(void *obj, rt_string relative)
{
    if (!obj)
        rt_trap("URL: NULL base URL");

    rt_url_t *base = (rt_url_t *)obj;
    const char *rel_str = rt_string_cstr(relative);

    if (!rel_str || *rel_str == '\0')
        return rt_url_clone(obj);

    // Parse relative URL
    rt_url_t rel;
    memset(&rel, 0, sizeof(rel));
    parse_url_full(rel_str, &rel);

    // Create new URL
    rt_url_t *result = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!result)
        rt_trap("URL: Memory allocation failed");
    memset(result, 0, sizeof(*result));
    rt_obj_set_finalizer(result, rt_url_finalize);

    // RFC 3986 resolution algorithm
    if (rel.scheme)
    {
        // Relative has scheme - use as-is
        result->scheme = safe_strdup(rel.scheme);
        result->user = safe_strdup(rel.user);
        result->pass = safe_strdup(rel.pass);
        result->host = safe_strdup(rel.host);
        result->port = rel.port;
        result->path = safe_strdup(rel.path);
        result->query = safe_strdup(rel.query);
    }
    else
    {
        if (rel.host)
        {
            // Relative has authority
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(rel.user);
            result->pass = safe_strdup(rel.pass);
            result->host = safe_strdup(rel.host);
            result->port = rel.port;
            result->path = safe_strdup(rel.path);
            result->query = safe_strdup(rel.query);
        }
        else
        {
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(base->user);
            result->pass = safe_strdup(base->pass);
            result->host = safe_strdup(base->host);
            result->port = base->port;

            if (!rel.path || *rel.path == '\0')
            {
                result->path = safe_strdup(base->path);
                if (rel.query)
                    result->query = safe_strdup(rel.query);
                else
                    result->query = safe_strdup(base->query);
            }
            else
            {
                if (rel.path[0] == '/')
                {
                    result->path = safe_strdup(rel.path);
                }
                else
                {
                    // Merge paths
                    if (!base->host || !base->path || *base->path == '\0')
                    {
                        // No base authority or empty base path
                        size_t len = strlen(rel.path) + 2;
                        result->path = (char *)malloc(len);
                        if (result->path)
                            sprintf(result->path, "/%s", rel.path);
                    }
                    else
                    {
                        // Remove last segment of base path
                        const char *last_slash = strrchr(base->path, '/');
                        if (last_slash)
                        {
                            size_t base_len = last_slash - base->path + 1;
                            size_t len = base_len + strlen(rel.path) + 1;
                            result->path = (char *)malloc(len);
                            if (result->path)
                            {
                                memcpy(result->path, base->path, base_len);
                                strcpy(result->path + base_len, rel.path);
                            }
                        }
                        else
                        {
                            result->path = safe_strdup(rel.path);
                        }
                    }
                }
                result->query = safe_strdup(rel.query);
            }
        }
    }

    result->fragment = safe_strdup(rel.fragment);

    // Clean up relative URL
    free_url(&rel);

    return result;
}

void *rt_url_clone(void *obj)
{
    if (!obj)
        return rt_url_new();

    rt_url_t *url = (rt_url_t *)obj;
    rt_url_t *clone = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!clone)
        rt_trap("URL: Memory allocation failed");
    memset(clone, 0, sizeof(*clone));
    rt_obj_set_finalizer(clone, rt_url_finalize);

    clone->scheme = safe_strdup(url->scheme);
    clone->user = safe_strdup(url->user);
    clone->pass = safe_strdup(url->pass);
    clone->host = safe_strdup(url->host);
    clone->port = url->port;
    clone->path = safe_strdup(url->path);
    clone->query = safe_strdup(url->query);
    clone->fragment = safe_strdup(url->fragment);

    return clone;
}

rt_string rt_url_encode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *encoded = percent_encode(str, true);
    if (!encoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(encoded, strlen(encoded));
    free(encoded);
    return result;
}

rt_string rt_url_decode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *decoded = percent_decode(str);
    if (!decoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(decoded, strlen(decoded));
    free(decoded);
    return result;
}

rt_string rt_url_encode_query(void *map)
{
    if (!map)
        return rt_string_from_bytes("", 0);

    void *keys = rt_map_keys(map);
    int64_t len = rt_seq_len(keys);

    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Build query string
    size_t cap = 256;
    char *result = (char *)malloc(cap);
    if (!result)
        return rt_string_from_bytes("", 0);

    size_t pos = 0;
    for (int64_t i = 0; i < len; i++)
    {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(map, key);

        const char *key_str = rt_string_cstr(key);
        rt_string value_str_handle = NULL;
        if (value && rt_box_type(value) == RT_BOX_STR)
        {
            value_str_handle = rt_unbox_str(value);
        }
        else
        {
            value_str_handle = (rt_string)value;
            if (value_str_handle)
                rt_string_ref(value_str_handle);
        }
        const char *value_str = value_str_handle ? rt_string_cstr(value_str_handle) : "";

        char *enc_key = percent_encode(key_str, true);
        char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

        if (!enc_key || !enc_value)
        {
            free(enc_key);
            free(enc_value);
            continue;
        }

        size_t needed = strlen(enc_key) + 1 + strlen(enc_value) + 2; // key=value&
        if (pos + needed >= cap)
        {
            cap = (pos + needed) * 2;
            char *new_result = (char *)realloc(result, cap);
            if (!new_result)
            {
                free(enc_key);
                free(enc_value);
                break;
            }
            result = new_result;
        }

        if (i > 0)
            result[pos++] = '&';
        pos += sprintf(result + pos, "%s=%s", enc_key, enc_value);

        free(enc_key);
        free(enc_value);
        if (value_str_handle)
            rt_string_unref(value_str_handle);
    }

    result[pos] = '\0';
    rt_string str = rt_string_from_bytes(result, pos);
    free(result);
    return str;
}

void *rt_url_decode_query(rt_string query)
{
    void *map = rt_map_new();
    const char *str = rt_string_cstr(query);

    if (!str || *str == '\0')
        return map;

    const char *p = str;
    while (*p)
    {
        // Find end of key
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');

        if (!eq || (amp && amp < eq))
        {
            // Key without value
            const char *end = amp ? amp : p + strlen(p);
            if (end > p)
            {
                char *key = (char *)malloc(end - p + 1);
                if (key)
                {
                    memcpy(key, p, end - p);
                    key[end - p] = '\0';
                    char *dec_key = percent_decode(key);
                    if (dec_key)
                    {
                        rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                        rt_string empty = rt_string_from_bytes("", 0);
                        void *boxed = rt_box_str(empty);
                        rt_map_set(map, key_str, boxed);
                        if (boxed && rt_obj_release_check0(boxed))
                            rt_obj_free(boxed);
                        rt_string_unref(empty);
                        free(dec_key);
                    }
                    free(key);
                }
            }
            p = amp ? amp + 1 : p + strlen(p);
        }
        else
        {
            // Key=Value
            size_t key_len = eq - p;
            const char *val_start = eq + 1;
            const char *val_end = amp ? amp : val_start + strlen(val_start);

            char *key = (char *)malloc(key_len + 1);
            char *val = (char *)malloc(val_end - val_start + 1);

            if (key && val)
            {
                memcpy(key, p, key_len);
                key[key_len] = '\0';
                memcpy(val, val_start, val_end - val_start);
                val[val_end - val_start] = '\0';

                char *dec_key = percent_decode(key);
                char *dec_val = percent_decode(val);

                if (dec_key && dec_val)
                {
                    rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                    rt_string val_str = rt_string_from_bytes(dec_val, strlen(dec_val));
                    void *boxed = rt_box_str(val_str);
                    rt_map_set(map, key_str, boxed);
                    if (boxed && rt_obj_release_check0(boxed))
                        rt_obj_free(boxed);
                    rt_string_unref(val_str);
                }

                free(dec_key);
                free(dec_val);
            }

            free(key);
            free(val);
            p = amp ? amp + 1 : val_end;
        }
    }

    return map;
}

int8_t rt_url_is_valid(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str || *str == '\0')
        return 0;

    // Reject strings with unencoded spaces (common non-URL indicator)
    for (const char *p = str; *p; p++)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            return 0;
    }

    // Reject URLs starting with :// (missing scheme)
    if (str[0] == ':' && str[1] == '/' && str[2] == '/')
        return 0;

    // Check for scheme - must have letters before ://
    const char *scheme_sep = strstr(str, "://");
    if (scheme_sep)
    {
        // Scheme must be at least 1 character and only contain [a-zA-Z0-9+.-]
        if (scheme_sep == str)
            return 0; // Empty scheme
        for (const char *p = str; p < scheme_sep; p++)
        {
            char c = *p;
            int valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
            if (!valid)
                return 0;
        }
        // First character of scheme must be a letter
        if (!((str[0] >= 'a' && str[0] <= 'z') || (str[0] >= 'A' && str[0] <= 'Z')))
            return 0;
    }

    rt_url_t url;
    memset(&url, 0, sizeof(url));

    int result = parse_url_full(str, &url);
    free_url(&url);

    return result == 0 ? 1 : 0;
}
