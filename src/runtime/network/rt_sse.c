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

// Platform feature macros must appear before ANY includes
#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif

#include "rt_sse.h"

#include "rt_internal.h"
#include "rt_network_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_tls.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include "rt_trap.h"

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *tcp;             // TCP connection for http://
    rt_tls_session_t *tls; // TLS session for https://
    bool is_open;
    char *last_event_type; // Most recent "event:" field
    char *last_event_id;   // Most recent "id:" field
    uint8_t read_buf[4096];
    size_t read_buf_len;
    size_t read_buf_pos;
    int chunked;
    size_t chunk_remaining;
} rt_sse_impl;

static int sse_host_needs_brackets(const char *host) {
    return host && strchr(host, ':') != NULL && host[0] != '[';
}

static void sse_close_transport(rt_sse_impl *sse) {
    if (!sse)
        return;
    if (sse->tls) {
        rt_tls_close(sse->tls);
        sse->tls = NULL;
    }
    if (sse->tcp) {
        rt_tcp_close(sse->tcp);
        if (rt_obj_release_check0(sse->tcp))
            rt_obj_free(sse->tcp);
        sse->tcp = NULL;
    }
    sse->read_buf_len = 0;
    sse->read_buf_pos = 0;
    sse->chunk_remaining = 0;
}

static void rt_sse_finalize(void *obj) {
    if (!obj)
        return;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    sse_close_transport(sse);
    free(sse->last_event_type);
    free(sse->last_event_id);
}

//=============================================================================
// Transport Helpers
//=============================================================================

static int sse_transport_send_all(rt_sse_impl *sse, const void *data, size_t len) {
    if (sse->tls) {
        size_t total = 0;
        while (total < len) {
            long sent = rt_tls_send(sse->tls, (const uint8_t *)data + total, len - total);
            if (sent <= 0)
                return 0;
            total += (size_t)sent;
        }
        return 1;
    }
    rt_tcp_send_all_raw(sse->tcp, data, (int64_t)len);
    return 1;
}

static long sse_transport_read(rt_sse_impl *sse, void *buffer, size_t len) {
    if (sse->tls)
        return rt_tls_recv(sse->tls, buffer, len);

    void *chunk = rt_tcp_recv(sse->tcp, (int64_t)len);
    int64_t chunk_len = rt_bytes_len(chunk);
    if (chunk_len > 0)
        memcpy(buffer, bytes_data(chunk), (size_t)chunk_len);
    if (chunk && rt_obj_release_check0(chunk))
        rt_obj_free(chunk);
    return (long)chunk_len;
}

static int sse_raw_recv_byte(rt_sse_impl *sse, uint8_t *byte) {
    if (sse->read_buf_pos < sse->read_buf_len) {
        *byte = sse->read_buf[sse->read_buf_pos++];
        return 1;
    }

    long n = sse_transport_read(sse, sse->read_buf, sizeof(sse->read_buf));
    if (n <= 0)
        return 0;

    sse->read_buf_len = (size_t)n;
    sse->read_buf_pos = 0;
    *byte = sse->read_buf[sse->read_buf_pos++];
    return 1;
}

static rt_string sse_raw_recv_line(rt_sse_impl *sse) {
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        return NULL;

    while (1) {
        uint8_t byte = 0;
        if (!sse_raw_recv_byte(sse, &byte)) {
            if (len == 0) {
                free(line);
                return NULL;
            }
            break;
        }

        if (byte == '\n') {
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len >= 65536) {
            free(line);
            return NULL;
        }

        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            if (new_cap > 65536)
                new_cap = 65536;
            char *grown = (char *)realloc(line, new_cap);
            if (!grown) {
                free(line);
                return NULL;
            }
            line = grown;
            cap = new_cap;
        }

        line[len++] = (char)byte;
    }

    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

static int sse_parse_chunk_size(const char *line, size_t *size_out) {
    size_t size = 0;
    const char *p = line;
    if (!p || !*p)
        return 0;

    while (*p && *p != ';') {
        char c = *p++;
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            digit = 10 + (c - 'A');
        else
            return 0;

        if (size > (SIZE_MAX >> 4))
            return 0;
        size = (size << 4) | (size_t)digit;
    }

    *size_out = size;
    return 1;
}

static int sse_payload_recv_byte(rt_sse_impl *sse, uint8_t *byte) {
    if (!sse->chunked)
        return sse_raw_recv_byte(sse, byte);

    while (sse->chunk_remaining == 0) {
        rt_string size_line = sse_raw_recv_line(sse);
        if (!size_line)
            return 0;

        const char *line_cstr = rt_string_cstr(size_line);
        size_t next_chunk = 0;
        int ok = sse_parse_chunk_size(line_cstr, &next_chunk);
        rt_string_unref(size_line);
        if (!ok)
            return 0;

        if (next_chunk == 0) {
            while (1) {
                rt_string trailer = sse_raw_recv_line(sse);
                if (!trailer)
                    return 0;
                const char *trailer_cstr = rt_string_cstr(trailer);
                int done = !trailer_cstr || *trailer_cstr == '\0';
                rt_string_unref(trailer);
                if (done)
                    return 0;
            }
        }

        sse->chunk_remaining = next_chunk;
    }

    if (!sse_raw_recv_byte(sse, byte))
        return 0;

    sse->chunk_remaining--;
    if (sse->chunk_remaining == 0) {
        uint8_t crlf[2];
        if (!sse_raw_recv_byte(sse, &crlf[0]) || !sse_raw_recv_byte(sse, &crlf[1]))
            return 0;
        if (crlf[0] != '\r' || crlf[1] != '\n')
            return 0;
    }

    return 1;
}

static rt_string sse_payload_recv_line(rt_sse_impl *sse) {
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        return NULL;

    while (1) {
        uint8_t byte = 0;
        if (!sse_payload_recv_byte(sse, &byte)) {
            if (len == 0) {
                free(line);
                return NULL;
            }
            break;
        }

        if (byte == '\n') {
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len >= 65536) {
            free(line);
            return NULL;
        }

        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            if (new_cap > 65536)
                new_cap = 65536;
            char *grown = (char *)realloc(line, new_cap);
            if (!grown) {
                free(line);
                return NULL;
            }
            line = grown;
            cap = new_cap;
        }

        line[len++] = (char)byte;
    }

    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

static int sse_wait_readable(rt_sse_impl *sse, int64_t timeout_ms) {
    if (timeout_ms <= 0)
        return 1;
    if (sse->read_buf_pos < sse->read_buf_len)
        return 1;
    if (sse->tls) {
        if (rt_tls_has_buffered_data(sse->tls))
            return 1;
        return wait_socket(rt_tls_get_socket(sse->tls), (int)timeout_ms, false) > 0;
    }
    return wait_socket(rt_tcp_socket_fd(sse->tcp), (int)timeout_ms, false) > 0;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_sse_connect(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str)
        rt_trap("SSE: NULL URL");

    rt_sse_impl *sse = (rt_sse_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sse_impl));
    if (!sse) {
        rt_trap("SSE: OOM");
        return NULL;
    }
    memset(sse, 0, sizeof(*sse));
    rt_obj_set_finalizer(sse, rt_sse_finalize);

    void *url_obj = rt_url_parse(url);
    rt_string scheme = rt_url_scheme(url_obj);
    rt_string host = rt_url_host(url_obj);
    rt_string path = rt_url_path(url_obj);
    rt_string query = rt_url_query(url_obj);
    const char *scheme_cstr = rt_string_cstr(scheme);
    const char *host_cstr = rt_string_cstr(host);
    const char *path_cstr = rt_string_cstr(path);
    const char *query_cstr = rt_string_cstr(query);
    int is_secure = scheme_cstr && strcmp(scheme_cstr, "https") == 0;

    if (!scheme_cstr || !host_cstr || !*host_cstr ||
        (strcmp(scheme_cstr, "http") != 0 && strcmp(scheme_cstr, "https") != 0)) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: invalid URL");
    }

    int64_t port = rt_url_port(url_obj);
    if (is_secure) {
        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = host_cstr;
        cfg.timeout_ms = 30000;
        sse->tls = rt_tls_connect(host_cstr, (uint16_t)port, &cfg);
        if (!sse->tls) {
            rt_string_unref(query);
            rt_string_unref(path);
            rt_string_unref(host);
            rt_string_unref(scheme);
            if (url_obj && rt_obj_release_check0(url_obj))
                rt_obj_free(url_obj);
            if (rt_obj_release_check0(sse))
                rt_obj_free(sse);
            rt_trap("SSE: TLS connection failed");
        }
    } else {
        sse->tcp = rt_tcp_connect_for(host, port, 30000);
        if (!sse->tcp || !rt_tcp_is_open(sse->tcp)) {
            rt_string_unref(query);
            rt_string_unref(path);
            rt_string_unref(host);
            rt_string_unref(scheme);
            if (url_obj && rt_obj_release_check0(url_obj))
                rt_obj_free(url_obj);
            if (rt_obj_release_check0(sse))
                rt_obj_free(sse);
            rt_trap("SSE: connection failed");
        }
    }

    size_t path_len = path_cstr && *path_cstr ? strlen(path_cstr) : 1;
    size_t query_len = query_cstr && *query_cstr ? strlen(query_cstr) : 0;
    size_t target_len = path_len + (query_len ? query_len + 1 : 0);
    char *target = (char *)malloc(target_len + 1);
    if (!target) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: OOM");
    }
    snprintf(target,
             target_len + 1,
             query_len ? "%s?%s" : "%s",
             path_cstr && *path_cstr ? path_cstr : "/",
             query_cstr ? query_cstr : "");

    char host_header[512];
    int default_port = is_secure ? 443 : 80;
    if (port != default_port) {
        snprintf(host_header,
                 sizeof(host_header),
                 sse_host_needs_brackets(host_cstr) ? "[%s]:%lld" : "%s:%lld",
                 host_cstr,
                 (long long)port);
    } else {
        snprintf(host_header,
                 sizeof(host_header),
                 sse_host_needs_brackets(host_cstr) ? "[%s]" : "%s",
                 host_cstr);
    }

    char request[2048];
    int rlen = snprintf(request,
                        sizeof(request),
                        "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Accept: text/event-stream\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        target,
                        host_header);
    free(target);
    if (rlen <= 0 || (size_t)rlen >= sizeof(request)) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: request too large");
    }

    if (!sse_transport_send_all(sse, request, (size_t)rlen)) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: request send failed");
    }

    rt_string status_line = sse_raw_recv_line(sse);
    if (!status_line) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: no HTTP response");
    }
    const char *status_cstr = rt_string_cstr(status_line);
    int ok_status = status_cstr && (strncmp(status_cstr, "HTTP/1.1 200", 12) == 0 ||
                                    strncmp(status_cstr, "HTTP/1.0 200", 12) == 0);
    rt_string_unref(status_line);
    if (!ok_status) {
        rt_string_unref(query);
        rt_string_unref(path);
        rt_string_unref(host);
        rt_string_unref(scheme);
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: endpoint did not return HTTP 200");
    }

    int saw_stream_type = 0;
    while (1) {
        rt_string line = sse_raw_recv_line(sse);
        if (!line) {
            rt_string_unref(query);
            rt_string_unref(path);
            rt_string_unref(host);
            rt_string_unref(scheme);
            if (url_obj && rt_obj_release_check0(url_obj))
                rt_obj_free(url_obj);
            if (rt_obj_release_check0(sse))
                rt_obj_free(sse);
            rt_trap("SSE: incomplete HTTP headers");
        }

        const char *line_cstr = rt_string_cstr(line);
        if (!line_cstr || *line_cstr == '\0') {
            rt_string_unref(line);
            break;
        }

        if (strncasecmp(line_cstr, "Content-Type:", 13) == 0) {
            const char *value = line_cstr + 13;
            while (*value == ' ' || *value == '\t')
                value++;
            saw_stream_type = strncasecmp(value, "text/event-stream", 17) == 0;
        } else if (strncasecmp(line_cstr, "Transfer-Encoding:", 18) == 0) {
            const char *value = line_cstr + 18;
            while (*value == ' ' || *value == '\t')
                value++;
            sse->chunked = strncasecmp(value, "chunked", 7) == 0;
        }

        rt_string_unref(line);
    }

    rt_string_unref(query);
    rt_string_unref(path);
    rt_string_unref(host);
    rt_string_unref(scheme);
    if (url_obj && rt_obj_release_check0(url_obj))
        rt_obj_free(url_obj);

    if (!saw_stream_type) {
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: response is not text/event-stream");
    }

    sse->is_open = true;
    return sse;
}

/// @brief Recv the sse.
rt_string rt_sse_recv(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open || (!sse->tcp && !sse->tls))
        return rt_string_from_bytes("", 0);

    // Accumulate "data:" lines until a blank line (event boundary)
    size_t cap = 4096, len = 0;
    char *data_buf = (char *)malloc(cap);
    if (!data_buf)
        return rt_string_from_bytes("", 0);

    while (sse->is_open) {
        rt_string line = sse_payload_recv_line(sse);
        if (!line) {
            sse->is_open = false;
            sse_close_transport(sse);
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
    if (!sse_wait_readable(sse, timeout_ms))
        return rt_string_from_bytes("", 0);
    return rt_sse_recv(obj);
}

/// @brief Check whether the SSE connection is still open and the underlying TCP socket is alive.
int8_t rt_sse_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open)
        return 0;
    if (sse->tls)
        return rt_tls_get_socket(sse->tls) != INVALID_SOCK ? 1 : 0;
    return sse->tcp && rt_tcp_is_open(sse->tcp) ? 1 : 0;
}

/// @brief Close the sse.
void rt_sse_close(void *obj) {
    if (!obj)
        return;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    sse->is_open = false;
    sse_close_transport(sse);
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
