//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_smtp.c
// Purpose: Simple SMTP client for sending emails.
// Key invariants:
//   - EHLO → optional STARTTLS → optional AUTH LOGIN → MAIL FROM → RCPT TO → DATA.
//   - Base64 encoding for AUTH LOGIN credentials.
// Ownership/Lifetime:
//   - Client objects are GC-managed.
// Links: rt_smtp.h (API), rt_network.h (TCP)
//
//===----------------------------------------------------------------------===//

// Platform feature macros must appear before ANY includes
#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif

#include "rt_smtp.h"

#include "rt_codec.h"
#include "rt_internal.h"
#include "rt_network_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *tcp;             // Plain TCP connection before STARTTLS / for non-TLS SMTP
    rt_tls_session_t *tls; // TLS session for implicit TLS or STARTTLS
    char *host;
    int port;
    char *username;
    char *password;
    char *last_error;
    int8_t use_tls;
} rt_smtp_impl;

static void rt_smtp_finalize(void *obj) {
    if (!obj)
        return;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;
    if (s->tls) {
        rt_tls_close(s->tls);
        s->tls = NULL;
    }
    if (s->tcp) {
        rt_tcp_close(s->tcp);
        if (rt_obj_release_check0(s->tcp))
            rt_obj_free(s->tcp);
        s->tcp = NULL;
    }
    free(s->host);
    free(s->username);
    free(s->password);
    free(s->last_error);
}

//=============================================================================
// SMTP Helpers
//=============================================================================

static void set_error(rt_smtp_impl *s, const char *msg) {
    free(s->last_error);
    s->last_error = strdup(msg);
}

static void clear_error(rt_smtp_impl *s) {
    if (!s)
        return;
    free(s->last_error);
    s->last_error = NULL;
}

static void smtp_close_transport(rt_smtp_impl *s) {
    if (!s)
        return;
    if (s->tls) {
        rt_tls_close(s->tls);
        s->tls = NULL;
    }
    if (s->tcp) {
        rt_tcp_close(s->tcp);
        if (rt_obj_release_check0(s->tcp))
            rt_obj_free(s->tcp);
        s->tcp = NULL;
    }
}

static void smtp_set_transport_timeouts(rt_smtp_impl *s, int timeout_ms) {
    if (!s)
        return;
    if (s->tls) {
        int fd = rt_tls_get_socket(s->tls);
        if (fd != INVALID_SOCK) {
            set_socket_timeout((socket_t)fd, timeout_ms, true);
            set_socket_timeout((socket_t)fd, timeout_ms, false);
        }
    } else if (s->tcp) {
        rt_tcp_set_recv_timeout(s->tcp, timeout_ms);
        rt_tcp_set_send_timeout(s->tcp, timeout_ms);
    }
}

static int smtp_transport_send_all(rt_smtp_impl *s, const void *data, size_t len) {
    if (s->tls) {
        size_t total = 0;
        while (total < len) {
            long sent = rt_tls_send(s->tls, (const uint8_t *)data + total, len - total);
            if (sent <= 0)
                return 0;
            total += (size_t)sent;
        }
        return 1;
    }

    rt_tcp_send_all_raw(s->tcp, data, (int64_t)len);
    return 1;
}

static long smtp_transport_read(rt_smtp_impl *s, void *buffer, size_t len) {
    if (s->tls)
        return rt_tls_recv(s->tls, buffer, len);

    void *chunk = rt_tcp_recv(s->tcp, (int64_t)len);
    int64_t chunk_len = rt_bytes_len(chunk);
    if (chunk_len > 0)
        memcpy(buffer, bytes_data(chunk), (size_t)chunk_len);
    if (chunk && rt_obj_release_check0(chunk))
        rt_obj_free(chunk);
    return (long)chunk_len;
}

static rt_string smtp_recv_line(rt_smtp_impl *s) {
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line) {
        set_error(s, "SMTP: OOM");
        return NULL;
    }

    while (1) {
        uint8_t c = 0;
        long received = smtp_transport_read(s, &c, 1);
        if (received <= 0) {
            free(line);
            set_error(s, "SMTP: no response");
            return NULL;
        }

        if (c == '\n') {
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char *grown = (char *)realloc(line, new_cap);
            if (!grown) {
                free(line);
                set_error(s, "SMTP: OOM");
                return NULL;
            }
            line = grown;
            cap = new_cap;
        }
        line[len++] = (char)c;
    }

    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

static char *smtp_sanitize_header_value(const char *value) {
    if (!value)
        return strdup("");

    size_t len = strlen(value);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;

    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        copy[i] = (c == '\r' || c == '\n') ? ' ' : c;
    }
    copy[len] = '\0';
    return copy;
}

static char *smtp_dot_stuff_body(const char *body) {
    const char *src = body ? body : "";
    size_t src_len = strlen(src);
    size_t cap = src_len * 2 + 8;
    char *out = (char *)malloc(cap);
    if (!out)
        return NULL;

    size_t pos = 0;
    int at_line_start = 1;
    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];

        if (at_line_start && c == '.') {
            if (pos + 1 >= cap) {
                cap *= 2;
                char *grown = (char *)realloc(out, cap);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
            }
            out[pos++] = '.';
        }

        if (c == '\r') {
            if (i + 1 < src_len && src[i + 1] == '\n')
                i++;
            if (pos + 2 >= cap) {
                cap *= 2;
                char *grown = (char *)realloc(out, cap);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
            }
            out[pos++] = '\r';
            out[pos++] = '\n';
            at_line_start = 1;
            continue;
        }

        if (c == '\n') {
            if (pos + 2 >= cap) {
                cap *= 2;
                char *grown = (char *)realloc(out, cap);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
            }
            out[pos++] = '\r';
            out[pos++] = '\n';
            at_line_start = 1;
            continue;
        }

        if (pos + 1 >= cap) {
            cap *= 2;
            char *grown = (char *)realloc(out, cap);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
        }
        out[pos++] = c;
        at_line_start = 0;
    }

    if (pos < 2 || out[pos - 2] != '\r' || out[pos - 1] != '\n') {
        if (pos + 2 >= cap) {
            cap += 4;
            char *grown = (char *)realloc(out, cap);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
        }
        out[pos++] = '\r';
        out[pos++] = '\n';
    }

    out[pos] = '\0';
    return out;
}

/// @brief Send a command and read the response code.
static int smtp_command(rt_smtp_impl *s, const char *cmd, int expected_code) {
    if (cmd) {
        if (!smtp_transport_send_all(s, cmd, strlen(cmd))) {
            set_error(s, "SMTP: send failed");
            return -1;
        }
    }

    // Read response line(s)
    rt_string line = smtp_recv_line(s);
    if (!line) {
        return -1;
    }
    const char *l = rt_string_cstr(line);
    int code = l ? atoi(l) : -1;

    // Drain multi-line responses (line[3] == '-')
    while (l && strlen(l) > 3 && l[3] == '-') {
        rt_string_unref(line);
        line = smtp_recv_line(s);
        if (!line) {
            set_error(s, "SMTP: incomplete multi-line response");
            break;
        }
        l = rt_string_cstr(line);
        if (l)
            code = atoi(l);
    }
    if (line)
        rt_string_unref(line);

    if (expected_code > 0 && code != expected_code) {
        char err[128];
        snprintf(err, sizeof(err), "SMTP: expected %d, got %d", expected_code, code);
        set_error(s, err);
        return -1;
    }

    return code;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_smtp_new(rt_string host, int64_t port) {
    const char *h = rt_string_cstr(host);
    if (!h || port < 1 || port > 65535)
        rt_trap("SmtpClient: invalid host or port");

    rt_smtp_impl *s = (rt_smtp_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_smtp_impl));
    if (!s)
        rt_trap("SmtpClient: OOM");
    memset(s, 0, sizeof(*s));
    s->host = strdup(h);
    s->port = (int)port;
    s->use_tls = (port == 465) ? 1 : 0; // Port 465 = implicit TLS
    rt_obj_set_finalizer(s, rt_smtp_finalize);
    return s;
}

void rt_smtp_set_auth(void *obj, rt_string username, rt_string password) {
    if (!obj)
        return;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;
    const char *u = rt_string_cstr(username);
    const char *p = rt_string_cstr(password);
    free(s->username);
    free(s->password);
    s->username = u ? strdup(u) : NULL;
    s->password = p ? strdup(p) : NULL;
}

void rt_smtp_set_tls(void *obj, int8_t enable) {
    if (!obj)
        return;
    ((rt_smtp_impl *)obj)->use_tls = enable;
}

static int smtp_connect_and_handshake(rt_smtp_impl *s) {
    smtp_close_transport(s);
    clear_error(s);

    if (s->use_tls && s->port == 465) {
        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = s->host;
        cfg.timeout_ms = 30000;
        s->tls = rt_tls_connect(s->host, (uint16_t)s->port, &cfg);
        if (!s->tls) {
            set_error(s, "SMTP: TLS connection failed");
            return -1;
        }
        smtp_set_transport_timeouts(s, 30000);
    } else {
        rt_string host_str = rt_string_from_bytes(s->host, strlen(s->host));
        s->tcp = rt_tcp_connect_for(host_str, s->port, 30000);
        rt_string_unref(host_str);

        if (!s->tcp || !rt_tcp_is_open(s->tcp)) {
            set_error(s, "SMTP: connection failed");
            return -1;
        }
        smtp_set_transport_timeouts(s, 30000);
    }

    if (smtp_command(s, NULL, 220) < 0)
        return -1;

    char ehlo[300];
    snprintf(ehlo, sizeof(ehlo), "EHLO localhost\r\n");
    if (smtp_command(s, ehlo, 250) < 0)
        return -1;

    if (s->use_tls && s->port != 465) {
        if (smtp_command(s, "STARTTLS\r\n", 220) < 0)
            return -1;

        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = s->host;
        cfg.timeout_ms = 30000;
        s->tls = rt_tls_new((int)rt_tcp_socket_fd(s->tcp), &cfg);
        if (!s->tls) {
            set_error(s, "SMTP: TLS setup failed");
            return -1;
        }
        if (rt_tls_handshake(s->tls) != RT_TLS_OK) {
            set_error(s, "SMTP: TLS handshake failed");
            rt_tls_close(s->tls);
            s->tls = NULL;
            return -1;
        }

        rt_tcp_detach_socket(s->tcp);
        if (rt_obj_release_check0(s->tcp))
            rt_obj_free(s->tcp);
        s->tcp = NULL;
        smtp_set_transport_timeouts(s, 30000);

        if (smtp_command(s, ehlo, 250) < 0)
            return -1;
    }

    // AUTH LOGIN if credentials provided
    if (s->username && s->password) {
        if (smtp_command(s, "AUTH LOGIN\r\n", 334) < 0)
            return -1;

        // Send base64-encoded username
        rt_string user_str = rt_string_from_bytes(s->username, strlen(s->username));
        rt_string user_b64 = rt_codec_base64_enc(user_str);
        rt_string_unref(user_str);

        const char *ub = rt_string_cstr(user_b64);
        char user_cmd[512];
        snprintf(user_cmd, sizeof(user_cmd), "%s\r\n", ub);
        rt_string_unref(user_b64);
        if (smtp_command(s, user_cmd, 334) < 0)
            return -1;

        // Send base64-encoded password
        rt_string pass_str = rt_string_from_bytes(s->password, strlen(s->password));
        rt_string pass_b64 = rt_codec_base64_enc(pass_str);
        rt_string_unref(pass_str);

        const char *pb = rt_string_cstr(pass_b64);
        char pass_cmd[512];
        snprintf(pass_cmd, sizeof(pass_cmd), "%s\r\n", pb);
        rt_string_unref(pass_b64);
        if (smtp_command(s, pass_cmd, 235) < 0)
            return -1;
    }

    return 0;
}

static int smtp_send_message(rt_smtp_impl *s,
                             const char *from,
                             const char *to,
                             const char *subject,
                             const char *body,
                             const char *content_type) {
    char cmd[1024];

    // MAIL FROM
    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>\r\n", from);
    if (smtp_command(s, cmd, 250) < 0)
        return -1;

    // RCPT TO
    snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>\r\n", to);
    if (smtp_command(s, cmd, 250) < 0)
        return -1;

    // DATA
    if (smtp_command(s, "DATA\r\n", 354) < 0)
        return -1;

    // Build MIME message
    char *safe_from = smtp_sanitize_header_value(from);
    char *safe_to = smtp_sanitize_header_value(to);
    char *safe_subject = smtp_sanitize_header_value(subject);
    char *stuffed_body = smtp_dot_stuff_body(body);
    if (!safe_from || !safe_to || !safe_subject || !stuffed_body) {
        free(safe_from);
        free(safe_to);
        free(safe_subject);
        free(stuffed_body);
        set_error(s, "SMTP: OOM");
        return -1;
    }

    size_t msg_cap = 1024 + strlen(stuffed_body);
    char *msg = (char *)malloc(msg_cap);
    if (!msg) {
        free(safe_from);
        free(safe_to);
        free(safe_subject);
        free(stuffed_body);
        set_error(s, "SMTP: OOM");
        return -1;
    }

    int mlen = snprintf(msg,
                        msg_cap,
                        "From: %s\r\n"
                        "To: %s\r\n"
                        "Subject: %s\r\n"
                        "MIME-Version: 1.0\r\n"
                        "Content-Type: %s; charset=utf-8\r\n"
                        "\r\n"
                        "%s"
                        ".\r\n",
                        safe_from,
                        safe_to,
                        safe_subject,
                        content_type,
                        stuffed_body);
    free(safe_from);
    free(safe_to);
    free(safe_subject);
    free(stuffed_body);

    if (mlen < 0 || (size_t)mlen >= msg_cap) {
        free(msg);
        set_error(s, "SMTP: message too large");
        return -1;
    }

    if (!smtp_transport_send_all(s, msg, (size_t)mlen)) {
        free(msg);
        set_error(s, "SMTP: send failed");
        return -1;
    }
    free(msg);

    if (smtp_command(s, NULL, 250) < 0)
        return -1;
    return 0;
}

int8_t rt_smtp_send(void *obj, rt_string from, rt_string to, rt_string subject, rt_string body) {
    if (!obj)
        return 0;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;

    if (smtp_connect_and_handshake(s) < 0) {
        smtp_close_transport(s);
        return 0;
    }

    const char *f = rt_string_cstr(from);
    const char *t = rt_string_cstr(to);
    const char *sub = rt_string_cstr(subject);
    const char *b = rt_string_cstr(body);

    int result =
        smtp_send_message(s, f ? f : "", t ? t : "", sub ? sub : "", b ? b : "", "text/plain");

    // QUIT
    smtp_command(s, "QUIT\r\n", 221);
    smtp_close_transport(s);

    if (result == 0)
        clear_error(s);
    return result == 0 ? 1 : 0;
}

int8_t rt_smtp_send_html(
    void *obj, rt_string from, rt_string to, rt_string subject, rt_string html_body) {
    if (!obj)
        return 0;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;

    if (smtp_connect_and_handshake(s) < 0) {
        smtp_close_transport(s);
        return 0;
    }

    const char *f = rt_string_cstr(from);
    const char *t = rt_string_cstr(to);
    const char *sub = rt_string_cstr(subject);
    const char *b = rt_string_cstr(html_body);

    int result =
        smtp_send_message(s, f ? f : "", t ? t : "", sub ? sub : "", b ? b : "", "text/html");

    smtp_command(s, "QUIT\r\n", 221);
    smtp_close_transport(s);

    if (result == 0)
        clear_error(s);
    return result == 0 ? 1 : 0;
}

rt_string rt_smtp_last_error(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_smtp_impl *s = (rt_smtp_impl *)obj;
    const char *e = s->last_error ? s->last_error : "";
    return rt_string_from_bytes(e, strlen(e));
}

void rt_smtp_close(void *obj) {
    if (!obj)
        return;
    smtp_close_transport((rt_smtp_impl *)obj);
}
