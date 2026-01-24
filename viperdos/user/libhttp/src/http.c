//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libhttp/src/http.c
// Purpose: HTTP client implementation for ViperDOS.
// Key invariants: Supports HTTP/1.1 and HTTPS via user-space TLS.
// Ownership/Lifetime: Library; stateless functions.
// Links: user/libhttp/include/http.h
//
//===----------------------------------------------------------------------===//

/**
 * @file http.c
 * @brief HTTP client implementation for ViperDOS.
 *
 * Provides HTTP/1.1 and HTTPS client using user-space TLS.
 */

#include "../include/http.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <tls.h>
#include <unistd.h>

/* URL components */
typedef struct {
    char scheme[16];
    char host[256];
    uint16_t port;
    char path[512];
} parsed_url_t;

/* Connection state */
typedef struct {
    int socket_fd;
    tls_session_t *tls;
    int use_tls;
} http_connection_t;

/* Parse URL into components */
static int parse_url(const char *url, parsed_url_t *parsed) {
    memset(parsed, 0, sizeof(*parsed));

    /* Default values */
    strcpy(parsed->scheme, "http");
    parsed->port = 80;
    strcpy(parsed->path, "/");

    /* Parse scheme */
    const char *p = strstr(url, "://");
    if (p) {
        size_t scheme_len = p - url;
        if (scheme_len >= sizeof(parsed->scheme))
            return HTTP_ERROR_PARSE;
        memcpy(parsed->scheme, url, scheme_len);
        parsed->scheme[scheme_len] = '\0';
        url = p + 3;

        if (strcmp(parsed->scheme, "https") == 0)
            parsed->port = 443;
    }

    /* Parse host and port */
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');

    if (colon && (!slash || colon < slash)) {
        /* Has port */
        size_t host_len = colon - url;
        if (host_len >= sizeof(parsed->host))
            return HTTP_ERROR_PARSE;
        memcpy(parsed->host, url, host_len);
        parsed->host[host_len] = '\0';
        parsed->port = (uint16_t)atoi(colon + 1);
        url = slash ? slash : colon + strlen(colon + 1) + 1;
    } else if (slash) {
        size_t host_len = slash - url;
        if (host_len >= sizeof(parsed->host))
            return HTTP_ERROR_PARSE;
        memcpy(parsed->host, url, host_len);
        parsed->host[host_len] = '\0';
        url = slash;
    } else {
        strncpy(parsed->host, url, sizeof(parsed->host) - 1);
        url = "";
    }

    /* Parse path */
    if (*url) {
        strncpy(parsed->path, url, sizeof(parsed->path) - 1);
    }

    return HTTP_OK;
}

/* Connect to server */
static int http_connect(const parsed_url_t *url, http_connection_t *conn, int verify_tls) {
    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = -1;

    /* Resolve hostname */
    struct hostent *he = gethostbyname(url->host);
    if (!he)
        return HTTP_ERROR_CONNECT;

    /* Create socket */
    conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket_fd < 0)
        return HTTP_ERROR_CONNECT;

    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(url->port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], 4);

    if (connect(conn->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        return HTTP_ERROR_CONNECT;
    }

    /* Setup TLS if needed */
    if (strcmp(url->scheme, "https") == 0) {
        conn->use_tls = 1;

        tls_config_t tls_config;
        tls_config_init(&tls_config);
        tls_config.hostname = url->host;
        tls_config.verify_cert = verify_tls;

        conn->tls = tls_new(conn->socket_fd, &tls_config);
        if (!conn->tls) {
            close(conn->socket_fd);
            conn->socket_fd = -1;
            return HTTP_ERROR_TLS;
        }

        int rc = tls_handshake(conn->tls);
        if (rc != TLS_OK) {
            tls_close(conn->tls);
            close(conn->socket_fd);
            conn->socket_fd = -1;
            return HTTP_ERROR_TLS;
        }
    }

    return HTTP_OK;
}

/* Close connection */
static void http_disconnect(http_connection_t *conn) {
    if (conn->tls) {
        tls_close(conn->tls);
        conn->tls = NULL;
    }
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
}

/* Send data */
static long http_send(http_connection_t *conn, const void *data, size_t len) {
    if (conn->use_tls) {
        return tls_send(conn->tls, data, len);
    } else {
        return send(conn->socket_fd, data, len, 0);
    }
}

/* Receive data */
static long http_recv(http_connection_t *conn, void *buffer, size_t len) {
    if (conn->use_tls) {
        return tls_recv(conn->tls, buffer, len);
    } else {
        return recv(conn->socket_fd, buffer, len, 0);
    }
}

/* Case-insensitive string compare */
static int strcasecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Parse HTTP response headers */
static int parse_response(const char *data, size_t len, http_response_t *response) {
    const char *p = data;
    const char *end = data + len;

    /* Parse status line: HTTP/1.x STATUS TEXT */
    if (len < 12 || strncmp(p, "HTTP/1.", 7) != 0)
        return HTTP_ERROR_PARSE;

    p += 9; /* Skip "HTTP/1.x " */
    response->status_code = atoi(p);

    /* Find status text */
    while (p < end && *p != ' ')
        p++;
    if (p < end)
        p++;

    const char *status_end = p;
    while (status_end < end && *status_end != '\r' && *status_end != '\n')
        status_end++;

    size_t status_len = status_end - p;
    if (status_len >= sizeof(response->status_text))
        status_len = sizeof(response->status_text) - 1;
    memcpy(response->status_text, p, status_len);
    response->status_text[status_len] = '\0';

    /* Skip to end of line */
    p = status_end;
    while (p < end && (*p == '\r' || *p == '\n'))
        p++;

    /* Parse headers */
    response->header_count = 0;
    while (p < end && *p != '\r' && *p != '\n') {
        const char *name_start = p;
        while (p < end && *p != ':')
            p++;
        if (p >= end)
            break;

        size_t name_len = p - name_start;
        p++; /* Skip colon */
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        const char *value_start = p;
        while (p < end && *p != '\r' && *p != '\n')
            p++;
        size_t value_len = p - value_start;

        if (response->header_count < HTTP_MAX_HEADERS) {
            http_header_t *hdr = &response->headers[response->header_count];
            if (name_len >= sizeof(hdr->name))
                name_len = sizeof(hdr->name) - 1;
            memcpy(hdr->name, name_start, name_len);
            hdr->name[name_len] = '\0';

            if (value_len >= sizeof(hdr->value))
                value_len = sizeof(hdr->value) - 1;
            memcpy(hdr->value, value_start, value_len);
            hdr->value[value_len] = '\0';

            /* Parse important headers */
            if (strcasecmp_local(hdr->name, "Content-Length") == 0) {
                response->content_length = (size_t)atol(hdr->value);
            } else if (strcasecmp_local(hdr->name, "Content-Type") == 0) {
                strncpy(response->content_type, hdr->value, sizeof(response->content_type) - 1);
            } else if (strcasecmp_local(hdr->name, "Transfer-Encoding") == 0) {
                if (strstr(hdr->value, "chunked"))
                    response->chunked = 1;
            }

            response->header_count++;
        }

        /* Skip CRLF */
        while (p < end && (*p == '\r' || *p == '\n'))
            p++;
    }

    return HTTP_OK;
}

void http_request_init(http_request_t *request) {
    memset(request, 0, sizeof(*request));
    request->method = HTTP_GET;
    request->timeout_ms = 10000;
    request->follow_redirects = 1;
    request->max_redirects = 5;
    request->verify_tls = 0; /* Disabled by default for bring-up */
}

int http_request_add_header(http_request_t *request, const char *name, const char *value) {
    if (request->header_count >= HTTP_MAX_HEADERS)
        return HTTP_ERROR;

    http_header_t *hdr = &request->headers[request->header_count];
    strncpy(hdr->name, name, sizeof(hdr->name) - 1);
    strncpy(hdr->value, value, sizeof(hdr->value) - 1);
    request->header_count++;

    return HTTP_OK;
}

int http_get(const char *url, http_response_t *response) {
    http_request_t request;
    http_request_init(&request);
    request.url = url;
    return http_request(&request, response);
}

int http_request(const http_request_t *request, http_response_t *response) {
    memset(response, 0, sizeof(*response));

    if (!request || !request->url)
        return HTTP_ERROR;

    /* Parse URL */
    parsed_url_t url;
    int rc = parse_url(request->url, &url);
    if (rc != HTTP_OK)
        return rc;

    /* Connect */
    http_connection_t conn;
    rc = http_connect(&url, &conn, request->verify_tls);
    if (rc != HTTP_OK)
        return rc;

    /* Build request */
    char req_buf[4096];
    const char *method_str = "GET";
    switch (request->method) {
        case HTTP_GET:
            method_str = "GET";
            break;
        case HTTP_POST:
            method_str = "POST";
            break;
        case HTTP_PUT:
            method_str = "PUT";
            break;
        case HTTP_DELETE:
            method_str = "DELETE";
            break;
        case HTTP_HEAD:
            method_str = "HEAD";
            break;
    }

    int len = snprintf(req_buf,
                       sizeof(req_buf),
                       "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
                       method_str,
                       url.path,
                       url.host);

    /* Add custom headers */
    for (int i = 0; i < request->header_count; i++) {
        len += snprintf(req_buf + len,
                        sizeof(req_buf) - len,
                        "%s: %s\r\n",
                        request->headers[i].name,
                        request->headers[i].value);
    }

    /* Add content length for POST/PUT */
    if (request->body && request->body_len > 0) {
        len += snprintf(
            req_buf + len, sizeof(req_buf) - len, "Content-Length: %zu\r\n", request->body_len);
    }

    /* End headers */
    len += snprintf(req_buf + len, sizeof(req_buf) - len, "\r\n");

    /* Send request */
    if (http_send(&conn, req_buf, len) < 0) {
        http_disconnect(&conn);
        return HTTP_ERROR_CONNECT;
    }

    /* Send body if present */
    if (request->body && request->body_len > 0) {
        if (http_send(&conn, request->body, request->body_len) < 0) {
            http_disconnect(&conn);
            return HTTP_ERROR_CONNECT;
        }
    }

    /* Receive response */
    char *recv_buf = malloc(HTTP_MAX_BODY);
    if (!recv_buf) {
        http_disconnect(&conn);
        return HTTP_ERROR_MEMORY;
    }

    size_t recv_len = 0;
    size_t header_end = 0;

    /* Read until we have headers */
    while (recv_len < HTTP_MAX_BODY - 1) {
        long n = http_recv(&conn, recv_buf + recv_len, HTTP_MAX_BODY - 1 - recv_len);
        if (n <= 0)
            break;
        recv_len += n;
        recv_buf[recv_len] = '\0';

        /* Look for end of headers */
        char *hdr_end = strstr(recv_buf, "\r\n\r\n");
        if (hdr_end) {
            header_end = (hdr_end - recv_buf) + 4;
            break;
        }
    }

    if (header_end == 0) {
        free(recv_buf);
        http_disconnect(&conn);
        return HTTP_ERROR_PARSE;
    }

    /* Parse headers */
    rc = parse_response(recv_buf, header_end, response);
    if (rc != HTTP_OK) {
        free(recv_buf);
        http_disconnect(&conn);
        return rc;
    }

    /* Read body */
    size_t body_start = header_end;
    size_t body_len = recv_len - body_start;

    if (response->content_length > 0) {
        /* Known content length */
        while (body_len < response->content_length && recv_len < HTTP_MAX_BODY - 1) {
            long n = http_recv(&conn, recv_buf + recv_len, HTTP_MAX_BODY - 1 - recv_len);
            if (n <= 0)
                break;
            recv_len += n;
            body_len = recv_len - body_start;
        }
    } else if (!response->chunked) {
        /* Read until connection close */
        while (recv_len < HTTP_MAX_BODY - 1) {
            long n = http_recv(&conn, recv_buf + recv_len, HTTP_MAX_BODY - 1 - recv_len);
            if (n <= 0)
                break;
            recv_len += n;
        }
        body_len = recv_len - body_start;
    }

    /* Copy body */
    if (body_len > 0) {
        response->body = malloc(body_len + 1);
        if (response->body) {
            memcpy(response->body, recv_buf + body_start, body_len);
            response->body[body_len] = '\0';
            response->body_len = body_len;
        }
    }

    free(recv_buf);
    http_disconnect(&conn);

    return HTTP_OK;
}

void http_response_free(http_response_t *response) {
    if (response && response->body) {
        free(response->body);
        response->body = NULL;
        response->body_len = 0;
    }
}

const char *http_response_get_header(const http_response_t *response, const char *name) {
    if (!response || !name)
        return NULL;

    for (int i = 0; i < response->header_count; i++) {
        if (strcasecmp_local(response->headers[i].name, name) == 0) {
            return response->headers[i].value;
        }
    }

    return NULL;
}
