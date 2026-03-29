//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_sse.c
// Purpose: Server-Sent Events (SSE) client — reads text/event-stream over HTTP.
// Key invariants:
//   - Connects via plain TCP (or TLS for https) and sends a GET with Accept: text/event-stream.
//   - Parses event lines: "data:", "event:", "id:", separated by blank lines.
// Ownership/Lifetime:
//   - Client objects are GC-managed.
// Links: rt_sse.h (API), rt_network.h (TCP)
//
//===----------------------------------------------------------------------===//

#include "rt_sse.h"

#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *tcp; // TCP connection
    bool is_open;
    char *last_event_type; // Most recent "event:" field
    char *last_event_id;   // Most recent "id:" field
} rt_sse_impl;

static void rt_sse_finalize(void *obj) {
    if (!obj)
        return;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (sse->tcp)
        rt_tcp_close(sse->tcp);
    free(sse->last_event_type);
    free(sse->last_event_id);
}

//=============================================================================
// URL Parsing Helper (simplified for SSE)
//=============================================================================

static int parse_sse_url(const char *url, char **host, int *port, char **path) {
    *host = NULL;
    *port = 80;
    *path = NULL;
    if (strncmp(url, "http://", 7) == 0)
        url += 7;
    else if (strncmp(url, "https://", 8) == 0) {
        url += 8;
        *port = 443;
    } else
        return 0;

    const char *end = url;
    while (*end && *end != ':' && *end != '/')
        end++;
    size_t hlen = (size_t)(end - url);
    *host = (char *)malloc(hlen + 1);
    if (!*host)
        return 0;
    memcpy(*host, url, hlen);
    (*host)[hlen] = '\0';

    if (*end == ':') {
        *port = atoi(end + 1);
        while (*end && *end != '/')
            end++;
    }
    *path = strdup(*end == '/' ? end : "/");
    return 1;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_sse_connect(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str)
        rt_trap("SSE: NULL URL");

    char *host = NULL, *path = NULL;
    int port = 80;
    if (!parse_sse_url(url_str, &host, &port, &path))
        rt_trap("SSE: invalid URL");

    // Connect TCP
    rt_string host_str = rt_string_from_bytes(host, strlen(host));
    void *tcp = rt_tcp_connect_for(host_str, port, 30000);
    rt_string_unref(host_str);

    if (!tcp || !rt_tcp_is_open(tcp)) {
        free(host);
        free(path);
        rt_trap("SSE: connection failed");
        return NULL;
    }

    // Send HTTP GET with Accept: text/event-stream
    char request[2048];
    int rlen = snprintf(request,
                        sizeof(request),
                        "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Accept: text/event-stream\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path,
                        host);
    free(host);
    free(path);

    rt_string req_str = rt_string_from_bytes(request, (size_t)rlen);
    rt_tcp_send_str(tcp, req_str);
    rt_string_unref(req_str);

    // Read and skip the HTTP response headers
    while (1) {
        rt_string line = rt_tcp_recv_line(tcp);
        if (!line)
            break;
        const char *l = rt_string_cstr(line);
        if (!l || *l == '\0') {
            rt_string_unref(line);
            break;
        }
        rt_string_unref(line);
    }

    rt_sse_impl *sse = (rt_sse_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sse_impl));
    if (!sse) {
        rt_tcp_close(tcp);
        rt_trap("SSE: OOM");
        return NULL;
    }
    memset(sse, 0, sizeof(*sse));
    sse->tcp = tcp;
    sse->is_open = true;
    rt_obj_set_finalizer(sse, rt_sse_finalize);
    return sse;
}

/// @brief Recv the sse.
rt_string rt_sse_recv(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open || !sse->tcp)
        return rt_string_from_bytes("", 0);

    // Accumulate "data:" lines until a blank line (event boundary)
    size_t cap = 4096, len = 0;
    char *data_buf = (char *)malloc(cap);
    if (!data_buf)
        return rt_string_from_bytes("", 0);

    while (rt_tcp_is_open(sse->tcp)) {
        rt_string line = rt_tcp_recv_line(sse->tcp);
        if (!line) {
            sse->is_open = false;
            break;
        }
        const char *l = rt_string_cstr(line);
        if (!l) {
            rt_string_unref(line);
            break;
        }

        if (*l == '\0') {
            // Blank line = event boundary; deliver accumulated data
            rt_string_unref(line);
            if (len > 0)
                break;
            continue;
        }

        if (strncmp(l, "data:", 5) == 0) {
            const char *val = l + 5;
            if (*val == ' ')
                val++;
            size_t vlen = strlen(val);
            if (len + vlen + 2 > cap) {
                cap = (len + vlen + 2) * 2;
                char *nb = (char *)realloc(data_buf, cap);
                if (!nb) {
                    rt_string_unref(line);
                    break;
                }
                data_buf = nb;
            }
            if (len > 0)
                data_buf[len++] = '\n'; // Multi-line data separated by \n
            memcpy(data_buf + len, val, vlen);
            len += vlen;
        } else if (strncmp(l, "event:", 6) == 0) {
            const char *val = l + 6;
            if (*val == ' ')
                val++;
            free(sse->last_event_type);
            sse->last_event_type = strdup(val);
        } else if (strncmp(l, "id:", 3) == 0) {
            const char *val = l + 3;
            if (*val == ' ')
                val++;
            free(sse->last_event_id);
            sse->last_event_id = strdup(val);
        }
        // Ignore "retry:" and comment lines (starting with ':')

        rt_string_unref(line);
    }

    rt_string result = rt_string_from_bytes(data_buf, len);
    free(data_buf);
    return result;
}

/// @brief Receive the next SSE event with a timeout, returning empty on timeout or error.
/// @details Temporarily sets the socket recv timeout, reads one event, then clears the timeout.
rt_string rt_sse_recv_for(void *obj, int64_t timeout_ms) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open)
        return rt_string_from_bytes("", 0);

    // Set recv timeout on the socket
    rt_tcp_set_recv_timeout(sse->tcp, timeout_ms);
    rt_string result = rt_sse_recv(obj);
    rt_tcp_set_recv_timeout(sse->tcp, 0); // Clear timeout
    return result;
}

/// @brief Check whether the SSE connection is still open and the underlying TCP socket is alive.
int8_t rt_sse_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    return sse->is_open && sse->tcp && rt_tcp_is_open(sse->tcp) ? 1 : 0;
}

/// @brief Close the sse.
void rt_sse_close(void *obj) {
    if (!obj)
        return;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    sse->is_open = false;
    if (sse->tcp) {
        rt_tcp_close(sse->tcp);
        sse->tcp = NULL;
    }
}

/// @brief Last the event type of the sse.
rt_string rt_sse_last_event_type(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    const char *t = sse->last_event_type ? sse->last_event_type : "";
    return rt_string_from_bytes(t, strlen(t));
}

/// @brief Last the event id of the sse.
rt_string rt_sse_last_event_id(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    const char *id = sse->last_event_id ? sse->last_event_id : "";
    return rt_string_from_bytes(id, strlen(id));
}
