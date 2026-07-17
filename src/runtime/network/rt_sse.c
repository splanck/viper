//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include "rt_network_http_internal.h"
#include "rt_network_internal.h"
#include "rt_time.h"
#include "rt_object.h"
#include "rt_result.h"
#include "rt_string.h"
#include "rt_threads.h"
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

/// @brief Maximum accumulated `data:` payload bytes for a single SSE event.
/// @details The SSE spec permits multiple `data:` lines per event. This cap
///          bounds memory use when a peer sends a never-ending event without a
///          blank-line delimiter.
#define SSE_MAX_EVENT_DATA (4u * 1024u * 1024u)

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    socket_t socket_fd;    // Connected socket for http://, or TLS-owned socket for https://
    rt_tls_session_t *tls; // TLS session for https://
    bool is_open;
    char *url;
    char *last_event_type; // Most recent "event:" field
    char *last_event_id;   // Most recent "id:" field
    int64_t retry_ms;
    uint8_t read_buf[4096];
    size_t read_buf_len;
    size_t read_buf_pos;
    int chunked;
    size_t chunk_remaining;
    // Timed-receive state (VDOC-151): one monotonic deadline is carried
    // through every transport read, and a timeout preserves — rather than
    // corrupts — partial line and partial event state.
    int64_t read_deadline_us; ///< 0 = untimed receive
    int read_timed_out;       ///< last transport failure was a recv timeout
    char *raw_pending;        ///< partial raw line preserved across a timeout
    size_t raw_pending_len;
    char *payload_pending; ///< partial payload line preserved across a timeout
    size_t payload_pending_len;
    char *event_data; ///< partial event accumulation preserved across a timeout
    size_t event_len;
    size_t event_cap;
    int event_saw_data;
    char *pending_event_type; ///< `event:` value for the event being built
    int last_recv_delivered;  ///< 1 when the last receive dispatched an event
} rt_sse_impl;

/// @brief Drop any partial line/event state (stream restart or teardown).
static void sse_reset_partial_state(rt_sse_impl *sse) {
    free(sse->raw_pending);
    sse->raw_pending = NULL;
    sse->raw_pending_len = 0;
    free(sse->payload_pending);
    sse->payload_pending = NULL;
    sse->payload_pending_len = 0;
    sse->event_len = 0;
    sse->event_saw_data = 0;
    free(sse->pending_event_type);
    sse->pending_event_type = NULL;
}

static int sse_host_needs_brackets(const char *host) {
    return host && strchr(host, ':') != NULL && host[0] != '[';
}

static int sse_header_value_is_valid(const char *value) {
    if (!value)
        return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        // Reject the full C0 control range plus DEL so stored IDs can never
        // smuggle control bytes into the reconnect request header.
        if (*p < 0x20u || *p == 0x7Fu)
            return 0;
    }
    return 1;
}

static int sse_host_is_valid(const char *host) {
    if (!host || !*host)
        return 0;
    for (const unsigned char *p = (const unsigned char *)host; *p; ++p) {
        if (*p <= 0x20 || *p == 0x7Fu || *p == '/' || *p == '?' || *p == '#')
            return 0;
    }
    return 1;
}

static int sse_request_target_is_valid(const char *target) {
    if (!target || target[0] != '/')
        return 0;
    for (const unsigned char *p = (const unsigned char *)target; *p; ++p) {
        if (*p <= 0x20 || *p == 0x7Fu || *p == '#')
            return 0;
    }
    return 1;
}

static void sse_set_recv_timeout(rt_sse_impl *sse, int timeout_ms) {
    if (!sse || sse->socket_fd == INVALID_SOCK)
        return;
    set_socket_timeout(sse->socket_fd, timeout_ms, true);
}

static bool sse_set_nonblocking(socket_t sock, bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl(sock, F_SETFL, nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) == 0;
#endif
}

static bool sse_connect_socket_with_timeout(
    socket_t sock, const struct sockaddr *addr, socklen_t addrlen, int timeout_ms, int *err_out) {
    if (err_out)
        *err_out = 0;

    if (timeout_ms > 0) {
        if (!sse_set_nonblocking(sock, true)) {
            if (err_out)
                *err_out = GET_LAST_ERROR();
            return false;
        }

        if (connect(sock, addr, addrlen) == SOCK_ERROR) {
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
                    sse_set_nonblocking(sock, false);
                    return false;
                }

                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                if (so_error != 0) {
                    if (err_out)
                        *err_out = so_error;
                    sse_set_nonblocking(sock, false);
                    return false;
                }
            } else {
                if (err_out)
                    *err_out = err;
                sse_set_nonblocking(sock, false);
                return false;
            }
        }

        if (!sse_set_nonblocking(sock, false)) {
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

static socket_t sse_create_tcp_socket(const char *host, int port, int timeout_ms, int *err_code) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    socket_t sock = INVALID_SOCK;
    int last_err = 0;
    char port_str[16];

    if (err_code)
        *err_code = Err_NetworkError;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        if (err_code)
            *err_code = Err_HostNotFound;
        return INVALID_SOCK;
    }

    for (rp = res; rp; rp = rp->ai_next) {
        socket_t candidate = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (candidate == INVALID_SOCK)
            continue;
        suppress_sigpipe(candidate);
        if (sse_connect_socket_with_timeout(
                candidate, rp->ai_addr, (socklen_t)rp->ai_addrlen, timeout_ms, &last_err)) {
            sock = candidate;
            break;
        }
        CLOSE_SOCKET(candidate);
    }
    freeaddrinfo(res);

    if (sock == INVALID_SOCK && err_code) {
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

    return sock;
}

static void sse_format_connect_error(char *msg, size_t msg_cap, const char *prefix, int err_code) {
    const char *detail = "connection failed";
    if (err_code == Err_HostNotFound)
        detail = "host not found";
    else if (err_code == Err_ConnectionRefused)
        detail = "connection refused";
    else if (err_code == Err_Timeout)
        detail = "connection timeout";
    snprintf(msg, msg_cap, "%s: %s", prefix, detail);
}

static void sse_close_transport(rt_sse_impl *sse) {
    if (!sse)
        return;
    if (sse->tls) {
        rt_tls_close(sse->tls);
        sse->tls = NULL;
        sse->socket_fd = INVALID_SOCK;
    }
    if (sse->socket_fd != INVALID_SOCK) {
        CLOSE_SOCKET(sse->socket_fd);
        sse->socket_fd = INVALID_SOCK;
    }
    sse->read_buf_len = 0;
    sse->read_buf_pos = 0;
    sse->chunk_remaining = 0;
    sse->chunked = 0;
}

/// @brief GC finalizer: tear down the active transport (TLS/TCP) and free the cached last-event
/// metadata strings.
static void rt_sse_finalize(void *obj) {
    if (!obj)
        return;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    sse_close_transport(sse);
    sse_reset_partial_state(sse);
    free(sse->event_data);
    sse->event_data = NULL;
    sse->event_cap = 0;
    free(sse->url);
    free(sse->last_event_type);
    free(sse->last_event_id);
}

//=============================================================================
// Transport Helpers
//=============================================================================

/// @brief Loop-write `len` bytes through whichever transport is active. Same dual-path pattern
/// as rt_smtp.c — TLS retries until drained; plain TCP delegates to the rt_tcp send-all helper.
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
    size_t total = 0;
    while (total < len) {
        int sent = send(sse->socket_fd,
                        (const char *)data + total,
                        (int)((len - total) > INT_MAX ? INT_MAX : (len - total)),
                        SEND_FLAGS);
        if (sent <= 0)
            return 0;
        total += (size_t)sent;
    }
    return 1;
}

/// @brief Read up to `len` bytes from the active transport. TCP path allocates a temporary Bytes
/// chunk and copies into the caller's buffer; TLS path reads directly. Same shape as rt_smtp's
/// version — slight extra copy for TCP in exchange for transport-uniform line-reading code above.
static long sse_transport_read(rt_sse_impl *sse, void *buffer, size_t len) {
    if (sse->tls)
        return rt_tls_recv(sse->tls, buffer, len);
    return recv(sse->socket_fd, (char *)buffer, (int)(len > INT_MAX ? INT_MAX : len), 0);
}

static int sse_raw_recv_byte(rt_sse_impl *sse, uint8_t *byte) {
    if (sse->read_buf_pos < sse->read_buf_len) {
        *byte = sse->read_buf[sse->read_buf_pos++];
        return 1;
    }

    // One monotonic deadline across every read of a timed receive: a peer
    // trickling one byte per interval cannot extend the call past its budget.
    if (sse->read_deadline_us > 0) {
        int64_t remaining_ms = (sse->read_deadline_us - rt_clock_ticks_us()) / 1000;
        if (remaining_ms <= 0) {
            sse->read_timed_out = 1;
            return 0;
        }
        if (remaining_ms > INT_MAX)
            remaining_ms = INT_MAX;
        sse_set_recv_timeout(sse, (int)remaining_ms);
    }

    long n = sse_transport_read(sse, sse->read_buf, sizeof(sse->read_buf));
    if (n <= 0) {
        sse->read_timed_out = (n < 0 && rt_socket_recv_timed_out()) ? 1 : 0;
        return 0;
    }

    sse->read_buf_len = (size_t)n;
    sse->read_buf_pos = 0;
    *byte = sse->read_buf[sse->read_buf_pos++];
    return 1;
}

static rt_string sse_raw_recv_line(rt_sse_impl *sse) {
    size_t cap = 256;
    size_t len = 0;
    char *line = NULL;

    // Resume a partial line preserved across a retryable timeout so timeout
    // recovery never turns a fragment into a complete logical line.
    if (sse->raw_pending) {
        line = sse->raw_pending;
        len = sse->raw_pending_len;
        cap = len + 256;
        char *grown = (char *)realloc(line, cap);
        if (!grown) {
            free(line);
            sse->raw_pending = NULL;
            sse->raw_pending_len = 0;
            return NULL;
        }
        line = grown;
        sse->raw_pending = NULL;
        sse->raw_pending_len = 0;
    } else {
        line = (char *)malloc(cap);
        if (!line)
            return NULL;
    }

    while (1) {
        uint8_t byte = 0;
        if (!sse_raw_recv_byte(sse, &byte)) {
            if (sse->read_timed_out && len > 0) {
                // Retryable timeout mid-line: stash the fragment for resume.
                sse->raw_pending = line;
                sse->raw_pending_len = len;
                return NULL;
            }
            // EOF / hard failure: an unterminated final fragment is discarded
            // (per the EventSource incomplete-line rule) instead of being
            // delivered as a complete line.
            free(line);
            return NULL;
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
    int saw_digit = 0;
    if (!p || !*p)
        return 0;

    while (*p) {
        char c = *p;
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            digit = 10 + (c - 'A');
        else
            break;

        saw_digit = 1;
        if (size > (SIZE_MAX >> 4))
            return 0;
        size = (size << 4) | (size_t)digit;
        p++;
    }
    if (!saw_digit)
        return 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '\0' && *p != ';')
        return 0;

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
    char *line = NULL;

    // Resume a partial line preserved across a retryable timeout so timeout
    // recovery never turns a fragment into a complete logical line.
    if (sse->payload_pending) {
        line = sse->payload_pending;
        len = sse->payload_pending_len;
        cap = len + 256;
        char *grown = (char *)realloc(line, cap);
        if (!grown) {
            free(line);
            sse->payload_pending = NULL;
            sse->payload_pending_len = 0;
            return NULL;
        }
        line = grown;
        sse->payload_pending = NULL;
        sse->payload_pending_len = 0;
    } else {
        line = (char *)malloc(cap);
        if (!line)
            return NULL;
    }

    while (1) {
        uint8_t byte = 0;
        if (!sse_payload_recv_byte(sse, &byte)) {
            if (sse->read_timed_out && len > 0) {
                // Retryable timeout mid-line: stash the fragment for resume.
                sse->payload_pending = line;
                sse->payload_pending_len = len;
                return NULL;
            }
            // EOF / hard failure: an unterminated final fragment is discarded
            // (per the EventSource incomplete-line rule) instead of being
            // delivered as a complete line.
            free(line);
            return NULL;
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
    int timeout_int = 0;
    if (timeout_ms <= 0)
        return 1;
    if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int))
        return 0;
    if (sse->read_buf_pos < sse->read_buf_len)
        return 1;
    if (sse->tls) {
        if (rt_tls_has_buffered_data(sse->tls))
            return 1;
        return wait_socket(rt_tls_get_socket(sse->tls), timeout_int, false) > 0;
    }
    return sse->socket_fd != INVALID_SOCK && wait_socket(sse->socket_fd, timeout_int, false) > 0;
}

static char *sse_strdup_trimmed(const char *text) {
    const char *start = text ? text : "";
    size_t len = strlen(start);
    while (len > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        len--;
    }
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'))
        len--;
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static int sse_parse_http_status_line(const char *status_line) {
    if (!status_line)
        return -1;
    if (strncmp(status_line, "HTTP/1.1 ", 9) != 0 && strncmp(status_line, "HTTP/1.0 ", 9) != 0)
        return -1;
    if (status_line[9] < '0' || status_line[9] > '9' || status_line[10] < '0' ||
        status_line[10] > '9' || status_line[11] < '0' || status_line[11] > '9') {
        return -1;
    }
    return (status_line[9] - '0') * 100 + (status_line[10] - '0') * 10 + (status_line[11] - '0');
}

static int sse_content_encoding_supported(const char *value) {
    const char *p = value;
    int saw_token = 0;
    if (!value)
        return 1;
    while (*p) {
        const char *start;
        const char *end;
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (*p == '\0')
            break;
        start = p;
        while (*p && *p != ',' && *p != ';')
            p++;
        end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if (end > start) {
            saw_token = 1;
            if ((size_t)(end - start) != strlen("identity") ||
                strncasecmp(start, "identity", strlen("identity")) != 0) {
                return 0;
            }
        }
        while (*p && *p != ',')
            p++;
    }
    return saw_token ? 1 : 0;
}

//=============================================================================
// Public API
//=============================================================================

static int sse_open_url(
    rt_sse_impl *sse, const char *url_str, int allow_resume, char *err_msg, size_t err_msg_cap) {
    rt_string url = NULL;
    void *url_obj = NULL;
    rt_string scheme = NULL;
    rt_string host = NULL;
    rt_string path = NULL;
    rt_string query = NULL;
    const char *scheme_cstr;
    const char *host_cstr;
    const char *path_cstr;
    const char *query_cstr;
    char *current_url = NULL;
    char *target = NULL;
    char *request = NULL;
    char *redirect_location = NULL;
    rt_string status_line = NULL;
    const char *last_event_id =
        (allow_resume && sse->last_event_id && *sse->last_event_id) ? sse->last_event_id : NULL;
    int redirects_left = 5;
    int status = -1;
    int is_secure;
    int saw_stream_type = 0;
    int unsupported_content_encoding = 0;
    int unsupported_transfer_encoding = 0;
    int rlen;
    int64_t port;
    size_t path_len;
    size_t query_len;
    size_t target_len;
    size_t request_cap;
    char host_header[512];
    int host_header_len = 0;

    if (err_msg && err_msg_cap > 0)
        err_msg[0] = '\0';

    current_url = url_str ? strdup(url_str) : NULL;
    if (!current_url) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: OOM");
        return 0;
    }

retry:
    sse_close_transport(sse);
    sse->chunked = 0;
    sse->chunk_remaining = 0;
    sse->read_buf_len = 0;
    sse->read_buf_pos = 0;
    saw_stream_type = 0;
    unsupported_content_encoding = 0;
    unsupported_transfer_encoding = 0;
    status = -1;

    url = rt_string_from_bytes(current_url, strlen(current_url));
    if (!url) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: OOM");
        goto fail;
    }
    url_obj = rt_url_parse(url);
    if (!url_obj) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: invalid URL");
        goto fail;
    }
    scheme = rt_url_scheme(url_obj);
    host = rt_url_host(url_obj);
    path = rt_url_path(url_obj);
    query = rt_url_query(url_obj);
    scheme_cstr = rt_string_cstr(scheme);
    host_cstr = rt_string_cstr(host);
    path_cstr = rt_string_cstr(path);
    query_cstr = rt_string_cstr(query);
    is_secure = scheme_cstr && strcmp(scheme_cstr, "https") == 0;

    if (!scheme_cstr || !sse_host_is_valid(host_cstr) ||
        (strcmp(scheme_cstr, "http") != 0 && strcmp(scheme_cstr, "https") != 0)) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: invalid URL");
        goto fail;
    }

    port = rt_url_port(url_obj);
    if (port == 0)
        port = is_secure ? 443 : 80;
    if (port < 1 || port > 65535) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: invalid URL port");
        goto fail;
    }
    if (is_secure) {
        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = host_cstr;
        cfg.alpn_protocol = "http/1.1";
        cfg.timeout_ms = 30000;
        sse->tls = rt_tls_connect(host_cstr, (uint16_t)port, &cfg);
        if (!sse->tls) {
            const char *detail = rt_tls_last_error();
            if (err_msg && err_msg_cap > 0) {
                if (detail && *detail)
                    snprintf(err_msg, err_msg_cap, "SSE: TLS connection failed: %s", detail);
                else
                    snprintf(err_msg, err_msg_cap, "SSE: TLS connection failed");
            }
            goto fail;
        }
        sse->socket_fd = (socket_t)rt_tls_get_socket(sse->tls);
    } else {
        int err_code = Err_NetworkError;
        sse->socket_fd = sse_create_tcp_socket(host_cstr, (int)port, 30000, &err_code);
        if (sse->socket_fd == INVALID_SOCK) {
            if (err_msg && err_msg_cap > 0)
                sse_format_connect_error(err_msg, err_msg_cap, "SSE", err_code);
            goto fail;
        }
        set_socket_timeout(sse->socket_fd, 30000, true);
        set_socket_timeout(sse->socket_fd, 30000, false);
    }

    path_len = path_cstr && *path_cstr ? strlen(path_cstr) : 1;
    query_len = query_cstr && *query_cstr ? strlen(query_cstr) : 0;
    target_len = path_len + (query_len ? query_len + 1 : 0);
    target = (char *)malloc(target_len + 1);
    if (!target) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: OOM");
        goto fail;
    }
    snprintf(target,
             target_len + 1,
             query_len ? "%s?%s" : "%s",
             path_cstr && *path_cstr ? path_cstr : "/",
             query_cstr ? query_cstr : "");
    if (!sse_request_target_is_valid(target)) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: invalid URL target");
        goto fail;
    }

    if (port != (is_secure ? 443 : 80)) {
        host_header_len = snprintf(host_header,
                                   sizeof(host_header),
                                   sse_host_needs_brackets(host_cstr) ? "[%s]:%lld" : "%s:%lld",
                                   host_cstr,
                                   (long long)port);
    } else {
        host_header_len = snprintf(host_header,
                                   sizeof(host_header),
                                   sse_host_needs_brackets(host_cstr) ? "[%s]" : "%s",
                                   host_cstr);
    }
    if (host_header_len <= 0 || (size_t)host_header_len >= sizeof(host_header)) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: Host header too large");
        goto fail;
    }
    if (last_event_id && !sse_header_value_is_valid(last_event_id))
        last_event_id = NULL;

    {
        size_t event_id_len = last_event_id ? strlen(last_event_id) : 0;
        size_t target_size = strlen(target);
        size_t host_size = strlen(host_header);
        if (target_size > SIZE_MAX - host_size || target_size + host_size > SIZE_MAX - 256 ||
            (last_event_id && event_id_len > SIZE_MAX - 32) ||
            (last_event_id && target_size + host_size + 256 > SIZE_MAX - event_id_len - 32)) {
            if (err_msg && err_msg_cap > 0)
                snprintf(err_msg, err_msg_cap, "SSE: request too large");
            goto fail;
        }
        request_cap = target_size + host_size + 256 + (last_event_id ? event_id_len + 32 : 0);
    }
    request = (char *)malloc(request_cap);
    if (!request) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: OOM");
        goto fail;
    }
    rlen = snprintf(request,
                    request_cap,
                    "GET %s HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Accept: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Connection: keep-alive\r\n"
                    "%s%s%s"
                    "\r\n",
                    target,
                    host_header,
                    last_event_id ? "Last-Event-ID: " : "",
                    last_event_id ? last_event_id : "",
                    last_event_id ? "\r\n" : "");
    if (rlen <= 0 || (size_t)rlen >= request_cap) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: request too large");
        goto fail;
    }

    if (!sse_transport_send_all(sse, request, (size_t)rlen)) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: request send failed");
        goto fail;
    }

    status_line = sse_raw_recv_line(sse);
    if (!status_line) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: no HTTP response");
        goto fail;
    }
    status = sse_parse_http_status_line(rt_string_cstr(status_line));
    rt_string_unref(status_line);
    status_line = NULL;
    if (status < 0) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: invalid HTTP response");
        goto fail;
    }

    while (1) {
        rt_string line = sse_raw_recv_line(sse);
        if (!line) {
            if (err_msg && err_msg_cap > 0)
                snprintf(err_msg, err_msg_cap, "SSE: incomplete HTTP headers");
            goto fail;
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
            // Exact media type: parameters may follow, but longer type names
            // such as text/event-streaming are rejected.
            saw_stream_type = strncasecmp(value, "text/event-stream", 17) == 0 &&
                              (value[17] == '\0' || value[17] == ';' || value[17] == ' ' ||
                               value[17] == '\t');
        } else if (strncasecmp(line_cstr, "Transfer-Encoding:", 18) == 0) {
            const char *value = line_cstr + 18;
            while (*value == ' ' || *value == '\t')
                value++;
            // Only the exact `chunked` coding is implemented; any other (or
            // additional) coding would be misparsed, so reject the stream.
            size_t vlen = strlen(value);
            while (vlen > 0 && (value[vlen - 1] == ' ' || value[vlen - 1] == '\t'))
                vlen--;
            if (vlen == 7 && strncasecmp(value, "chunked", 7) == 0) {
                sse->chunked = 1;
            } else {
                unsupported_transfer_encoding = 1;
            }
        } else if (strncasecmp(line_cstr, "Location:", 9) == 0) {
            free(redirect_location);
            redirect_location = sse_strdup_trimmed(line_cstr + 9);
        } else if (strncasecmp(line_cstr, "Content-Encoding:", 17) == 0) {
            const char *value = line_cstr + 17;
            while (*value == ' ' || *value == '\t')
                value++;
            if (!sse_content_encoding_supported(value))
                unsupported_content_encoding = 1;
        }

        rt_string_unref(line);
    }

    if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        redirect_location && *redirect_location) {
        rt_string current_rt = NULL;
        rt_string location_rt = NULL;
        rt_string next_rt = NULL;
        char *next_url = NULL;

        if (redirects_left-- <= 0) {
            if (err_msg && err_msg_cap > 0)
                snprintf(err_msg, err_msg_cap, "SSE: too many redirects");
            goto fail;
        }

        current_rt = rt_string_from_bytes(current_url, strlen(current_url));
        location_rt = rt_string_from_bytes(redirect_location, strlen(redirect_location));
        next_rt = rt_http_resolve_redirect_url(current_rt, location_rt);
        next_url = strdup(rt_string_cstr(next_rt) ? rt_string_cstr(next_rt) : "");
        rt_string_unref(next_rt);
        rt_string_unref(location_rt);
        rt_string_unref(current_rt);
        if (!next_url || !*next_url) {
            free(next_url);
            if (err_msg && err_msg_cap > 0)
                snprintf(err_msg, err_msg_cap, "SSE: invalid redirect URL");
            goto fail;
        }

        free(request);
        request = NULL;
        free(target);
        target = NULL;
        free(redirect_location);
        redirect_location = NULL;
        rt_string_unref(query);
        query = NULL;
        rt_string_unref(path);
        path = NULL;
        rt_string_unref(host);
        host = NULL;
        rt_string_unref(scheme);
        scheme = NULL;
        if (url_obj && rt_obj_release_check0(url_obj))
            rt_obj_free(url_obj);
        url_obj = NULL;
        rt_string_unref(url);
        url = NULL;
        free(current_url);
        current_url = next_url;
        goto retry;
    }

    if (status != 200) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: endpoint did not return HTTP 200");
        goto fail;
    }

    if (unsupported_content_encoding) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: unsupported Content-Encoding");
        goto fail;
    }

    if (unsupported_transfer_encoding) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: unsupported Transfer-Encoding");
        goto fail;
    }

    if (!saw_stream_type) {
        if (err_msg && err_msg_cap > 0)
            snprintf(err_msg, err_msg_cap, "SSE: response is not text/event-stream");
        goto fail;
    }

    free(request);
    free(target);
    free(redirect_location);
    rt_string_unref(query);
    rt_string_unref(path);
    rt_string_unref(host);
    rt_string_unref(scheme);
    if (url_obj && rt_obj_release_check0(url_obj))
        rt_obj_free(url_obj);
    rt_string_unref(url);
    free(current_url);
    sse->is_open = true;
    return 1;

fail:
    if (status_line)
        rt_string_unref(status_line);
    free(redirect_location);
    free(request);
    free(target);
    sse_close_transport(sse);
    if (query)
        rt_string_unref(query);
    if (path)
        rt_string_unref(path);
    if (host)
        rt_string_unref(host);
    if (scheme)
        rt_string_unref(scheme);
    if (url_obj && rt_obj_release_check0(url_obj))
        rt_obj_free(url_obj);
    if (url)
        rt_string_unref(url);
    free(current_url);
    return 0;
}

static int sse_try_reconnect(rt_sse_impl *sse) {
    char err_msg[512];
    if (!sse || !sse->url || !sse->is_open)
        return 0;
    if (sse->retry_ms > 0)
        rt_thread_sleep(sse->retry_ms);
    return sse_open_url(sse, sse->url, 1, err_msg, sizeof(err_msg));
}

void *rt_sse_connect(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str) {
        rt_trap("SSE: NULL URL");
        return NULL;
    }

    rt_sse_impl *sse = (rt_sse_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sse_impl));
    if (!sse) {
        rt_trap("SSE: OOM");
        return NULL;
    }
    memset(sse, 0, sizeof(*sse));
    rt_obj_set_finalizer(sse, rt_sse_finalize);
    sse->socket_fd = INVALID_SOCK;
    sse->retry_ms = 3000;
    sse->url = strdup(url_str);
    if (!sse->url) {
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        rt_trap("SSE: OOM");
        return NULL;
    }

    char err_msg[512];
    if (!sse_open_url(sse, url_str, 0, err_msg, sizeof(err_msg))) {
        if (rt_obj_release_check0(sse))
            rt_obj_free(sse);
        // Exactly one categorized trap, then stop: a returning trap hook must
        // not see a second generic trap or receive the freed pointer.
        if (strstr(err_msg, "TLS") != NULL) {
            rt_trap_net(err_msg[0] ? err_msg : "SSE: TLS connection failed", Err_TlsError);
            return NULL;
        }
        rt_trap(err_msg[0] ? err_msg : "SSE: connection failed");
        return NULL;
    }
    return sse;
}

/// @brief Ensure the SSE event payload buffer can hold a pending append.
/// @details The receive path starts with a caller-owned stack buffer for the
///          common small-event case. When an event grows beyond that buffer,
///          this helper allocates a heap buffer, copies the already-accumulated
///          payload, and then uses checked geometric `realloc` growth for later
///          appends. The requested size is the payload length excluding the
///          final NUL terminator; this helper reserves one extra byte for
///          callers that want to materialize the buffer as a C string. Requests
///          beyond @c SSE_MAX_EVENT_DATA trap and fail closed.
/// @param buf In/out event buffer pointer; initially points at @p stack_buf.
/// @param cap In/out capacity of `*buf` in bytes.
/// @param stack_buf Original caller-owned stack buffer.
/// @param stack_cap Capacity of @p stack_buf in bytes.
/// @param used_len Bytes already written into `*buf`.
/// @param needed Non-NUL payload bytes required after the append.
/// @return `true` when `*buf` can hold `needed + 1` bytes; `false` after trap on overflow/OOM.
/// @brief Grow the connection's event-accumulation buffer to hold @p needed
///        bytes (+NUL). Impl-owned so partial events survive a timed receive.
static bool sse_reserve_event_data(rt_sse_impl *sse, size_t needed) {
    if (!sse || needed == SIZE_MAX || sse->event_len > needed) {
        rt_trap("SSE.Recv: event data length overflow");
        return false;
    }
    if (needed > SSE_MAX_EVENT_DATA) {
        rt_trap("SSE.Recv: event data exceeds maximum size");
        return false;
    }
    size_t required = needed + 1u;
    while (required > sse->event_cap) {
        size_t next_cap = sse->event_cap ? (sse->event_cap * 2u) : 4096u;
        if (next_cap <= sse->event_cap || next_cap < required)
            next_cap = required;
        char *grown = (char *)realloc(sse->event_data, next_cap);
        if (!grown) {
            rt_trap("SSE.Recv: memory allocation failed");
            return false;
        }
        sse->event_data = grown;
        sse->event_cap = next_cap;
    }
    return true;
}

/// @brief Receive the next SSE event's accumulated `data:` payload.
/// @details Reads event-stream lines until a blank-line delimiter. Consecutive
///          `data:` fields are joined with a single `\n`, matching the SSE
///          specification. Returns the empty string on timeout, transport
///          failure, reconnect failure, or allocation failure.
rt_string rt_sse_recv(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open || (sse->socket_fd == INVALID_SOCK && !sse->tls))
        return rt_str_empty();

    sse->last_recv_delivered = 0;

    // Accumulate "data:" lines until a blank line (event boundary). The
    // accumulation lives on the connection so a timed receive that expires
    // mid-event resumes exactly where it stopped instead of losing data.
    while (sse->is_open) {
        rt_string line = sse_payload_recv_line(sse);
        if (!line) {
            if (sse->read_timed_out) {
                // Clean timeout: preserve partial line/event state for the
                // next receive and report "no event yet".
                sse->read_timed_out = 0;
                return rt_str_empty();
            }
            // Transport ended: drop partial fragments across reconnects.
            sse_reset_partial_state(sse);
            if (sse_try_reconnect(sse))
                continue;
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
            if (sse->event_saw_data) {
                // Dispatch: the event's type is what THIS event declared.
                free(sse->last_event_type);
                sse->last_event_type = sse->pending_event_type;
                sse->pending_event_type = NULL;
                break;
            }
            continue;
        }

        if (strncmp(l, "data:", 5) == 0) {
            const char *val = l + 5;
            if (*val == ' ')
                val++;
            size_t vlen = strlen(val);
            size_t separator = sse->event_len > 0 ? 1u : 0u;
            if (vlen > SIZE_MAX - sse->event_len - separator ||
                !sse_reserve_event_data(sse, sse->event_len + separator + vlen)) {
                rt_string_unref(line);
                sse->event_len = 0;
                sse->event_saw_data = 0;
                break;
            }
            if (sse->event_len > 0)
                sse->event_data[sse->event_len++] = '\n'; // Multi-line data separated by \n
            memcpy(sse->event_data + sse->event_len, val, vlen);
            sse->event_len += vlen;
            sse->event_saw_data = 1;
        } else if (strncmp(l, "event:", 6) == 0) {
            const char *val = l + 6;
            if (*val == ' ')
                val++;
            // Buffered until dispatch: an event without its own `event:`
            // field reports the default (empty) type instead of leaking the
            // previous event's type.
            free(sse->pending_event_type);
            sse->pending_event_type = strdup(val);
        } else if (strncmp(l, "id:", 3) == 0) {
            const char *val = l + 3;
            if (*val == ' ')
                val++;
            if (sse_header_value_is_valid(val)) {
                free(sse->last_event_id);
                sse->last_event_id = strdup(val);
            }
        } else if (strncmp(l, "retry:", 6) == 0) {
            const char *val = l + 6;
            int64_t retry_ms = 0;
            int saw_digit = 0;
            int valid_retry = 1;
            if (*val == ' ')
                val++;
            while (*val >= '0' && *val <= '9') {
                int digit = *val - '0';
                if (retry_ms > (INT64_MAX - digit) / 10) {
                    valid_retry = 0;
                    break;
                }
                retry_ms = retry_ms * 10 + digit;
                saw_digit = 1;
                val++;
            }
            while (*val >= '0' && *val <= '9')
                val++;
            while (*val == ' ' || *val == '\t')
                val++;
            if (valid_retry && saw_digit && *val == '\0')
                sse->retry_ms = retry_ms;
        }
        // Ignore "retry:" and comment lines (starting with ':')

        rt_string_unref(line);
    }

    sse->last_recv_delivered = sse->event_saw_data ? 1 : 0;
    rt_string result =
        rt_string_from_bytes(sse->event_data ? sse->event_data : "", sse->event_len);
    sse->event_len = 0;
    sse->event_saw_data = 0;
    return result;
}

/// @brief Receive the next SSE event with a timeout, returning empty on timeout or error.
/// @details The timeout is one monotonic EVENT deadline carried through every
///          transport read (a peer trickling bytes cannot extend the call), and
///          a timeout is lossless: partially received lines and partially
///          accumulated events are preserved on the connection and resumed by
///          the next receive. `timeout_ms == 0` waits indefinitely (like
///          `Recv`).
rt_string rt_sse_recv_for(void *obj, int64_t timeout_ms) {
    if (!obj)
        return rt_str_empty();
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open)
        return rt_str_empty();
    if (timeout_ms < 0 || timeout_ms > INT_MAX) {
        rt_trap("SSE: invalid timeout");
        return rt_str_empty();
    }
    sse->last_recv_delivered = 0;
    if (timeout_ms > 0) {
        if (!sse_wait_readable(sse, timeout_ms))
            return rt_str_empty();
        sse->read_deadline_us = rt_clock_ticks_us() + timeout_ms * 1000;
    }
    sse->read_timed_out = 0;
    rt_string result = rt_sse_recv(obj);
    sse->read_deadline_us = 0;
    sse_set_recv_timeout(sse, 30000);
    return result;
}

/// @brief Result-shaped timed receive (VDOC-152): distinguishes a delivered
///        event (including one with empty data) from timeout and stream end.
/// @return `Result.Ok(data)` when an event was dispatched, `Result.ErrStr`
///         with "SSE: timeout" or "SSE: stream closed" otherwise.
void *rt_sse_recv_for_result(void *obj, int64_t timeout_ms) {
    if (!obj) {
        rt_trap("SSE: NULL client");
        return NULL;
    }
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open)
        return rt_result_err_str(rt_const_cstr("SSE: stream closed"));

    rt_string data = rt_sse_recv_for(obj, timeout_ms);
    if (sse->last_recv_delivered)
        return rt_result_ok_str(data);
    rt_string_unref(data);
    if (!sse->is_open)
        return rt_result_err_str(rt_const_cstr("SSE: stream closed"));
    return rt_result_err_str(rt_const_cstr("SSE: timeout"));
}

/// @brief Return whether the local SSE state still owns an open-looking transport.
/// @details This does not probe remote liveness. It becomes 0 after a detected transport failure
///          or `_close`; an unobserved peer close can still report 1.
int8_t rt_sse_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    if (!sse->is_open)
        return 0;
    if (sse->tls)
        return rt_tls_get_socket(sse->tls) != INVALID_SOCK ? 1 : 0;
    return sse->socket_fd != INVALID_SOCK ? 1 : 0;
}

/// @brief Close the sse.
/// @brief Force-close the active SSE connection (TLS + TCP). Idempotent; subsequent `_recv`
/// returns empty string and `_is_open` returns 0.
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
        return rt_str_empty();
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    const char *t = sse->last_event_type ? sse->last_event_type : "";
    return rt_string_from_bytes(t, strlen(t));
}

/// @brief Last the event id of the sse.
rt_string rt_sse_last_event_id(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_sse_impl *sse = (rt_sse_impl *)obj;
    const char *id = sse->last_event_id ? sse->last_event_id : "";
    return rt_string_from_bytes(id, strlen(id));
}
