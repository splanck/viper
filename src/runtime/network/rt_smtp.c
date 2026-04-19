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

typedef struct {
    int8_t supports_starttls;
    int8_t supports_auth_login;
} smtp_caps_t;

/// @brief GC finalizer: tear down TLS session (if any), close + release the TCP socket, and free
/// all heap-owned strings (host, credentials, last error).
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

/// @brief Replace the cached last-error message (frees prior copy, strdups the new one).
static void set_error(rt_smtp_impl *s, const char *msg) {
    free(s->last_error);
    s->last_error = strdup(msg);
}

/// @brief Drop the cached last-error so a successful call doesn't leak a stale prior failure.
static void clear_error(rt_smtp_impl *s) {
    if (!s)
        return;
    free(s->last_error);
    s->last_error = NULL;
}

/// @brief Tear down whichever transport is active (TLS or plain TCP). Idempotent and ordering-safe:
/// closes TLS first because a STARTTLS upgrade may have already detached the underlying socket.
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

/// @brief Apply the same send + recv timeout to whichever transport is active. For TLS, reach
/// through to the underlying socket fd; for plain TCP, use the rt_tcp_set_*_timeout helpers.
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

/// @brief Loop-write `len` bytes through whichever transport is active. For TLS, retries until
/// the byte count is fully drained; for plain TCP, delegates to the rt_tcp send-all helper.
/// Returns 0 on partial write/error, 1 on success.
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

/// @brief Read up to `len` bytes from the active transport. For plain TCP this allocates a
/// `Bytes` chunk via rt_tcp_recv, copies into the caller's buffer, then releases the chunk —
/// a small extra copy in exchange for letting `smtp_recv_line` work uniformly across transports.
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

/// @brief Read a single CRLF-terminated SMTP response line into a freshly-allocated rt_string.
/// Performs byte-at-a-time `transport_read` (chatty but simpler than buffering across calls),
/// strips the trailing CR, and grows the line buffer geometrically (×2) if it overflows 256 chars.
/// Returns NULL on transport error or OOM (with `last_error` populated).
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

/// @brief **Header-injection guard:** replace any CR/LF in a header value with a space, so a
/// hostile `Subject:` cannot smuggle additional headers (e.g. `Bcc:`, content-type override) into
/// the outgoing message. Caller must `free()` the returned copy. NULL input → "" (still strdup'd).
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

/// @brief **RFC 5321 §4.5.2 dot-stuffing:** double any leading `.` on a line so the SMTP DATA
/// terminator (a lone `.` line) cannot be triggered by user content. Also normalizes line
/// endings to CRLF and ensures the message ends with CRLF (required before the `.` terminator).
/// Returned buffer is heap-owned; caller must `free()`.
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

/// @brief Send an SMTP command (or just read, if `cmd == NULL`) and return the response code.
/// Handles **multi-line responses** (lines beginning `XXX-` are intermediate; the final line
/// uses `XXX `, no dash). When `expected_code > 0`, mismatching codes are turned into a
/// formatted error in `last_error` and the call returns -1.
typedef void (*smtp_line_callback_t)(const char *line, void *ctx);

static int smtp_command_ex(rt_smtp_impl *s,
                           const char *cmd,
                           int expected_code,
                           smtp_line_callback_t line_cb,
                           void *line_ctx) {
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
    if (l && line_cb)
        line_cb(l, line_ctx);

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
        if (l && line_cb)
            line_cb(l, line_ctx);
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

static int smtp_command(rt_smtp_impl *s, const char *cmd, int expected_code) {
    return smtp_command_ex(s, cmd, expected_code, NULL, NULL);
}

static void smtp_parse_ehlo_caps_line(const char *line, void *ctx) {
    smtp_caps_t *caps = (smtp_caps_t *)ctx;
    const char *value = line;
    if (!caps || !line)
        return;
    if (strlen(line) < 4)
        return;
    value = line + 3;
    if (*value == '-' || *value == ' ')
        value++;
    while (*value == ' ' || *value == '\t')
        value++;

    if (strncasecmp(value, "STARTTLS", 8) == 0 && (value[8] == '\0' || value[8] == ' ' ||
                                                   value[8] == '\t')) {
        caps->supports_starttls = 1;
        return;
    }

    if (strncasecmp(value, "AUTH", 4) == 0 && (value[4] == '\0' || value[4] == ' ' || value[4] == '\t')) {
        const char *token = value + 4;
        while (*token) {
            const char *start;
            const char *end;
            while (*token == ' ' || *token == '\t')
                token++;
            if (*token == '\0')
                break;
            start = token;
            while (*token && *token != ' ' && *token != '\t')
                token++;
            end = token;
            if ((size_t)(end - start) == strlen("LOGIN") &&
                strncasecmp(start, "LOGIN", strlen("LOGIN")) == 0) {
                caps->supports_auth_login = 1;
                break;
            }
        }
    }
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Construct an SMTP client targeting `(host, port)`. Port 465 implicitly enables TLS;
/// other ports default to plain TCP (call `rt_smtp_set_tls(true)` to upgrade via STARTTLS).
/// Validates host and port (1–65535) up front and traps via `rt_trap` on bad inputs / OOM.
/// Returns a GC-managed handle wired to `rt_smtp_finalize`.
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

/// @brief Cache username + password for AUTH LOGIN. Strings are duplicated so the caller can
/// release the originals immediately. Setting either to NULL disables authentication.
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

/// @brief Toggle TLS opportunistically. With `enable=1` and a non-465 port, the next send will
/// issue STARTTLS after EHLO. With `enable=0`, the connection stays plain (insecure for auth!).
void rt_smtp_set_tls(void *obj, int8_t enable) {
    if (!obj)
        return;
    ((rt_smtp_impl *)obj)->use_tls = enable;
}

/// @brief Open the transport and walk the SMTP greeting + (optional) STARTTLS + (optional)
/// AUTH LOGIN sequence. Sequence:
///   1. Connect: implicit TLS via `rt_tls_connect` (port 465) or plain `rt_tcp_connect_for`.
///   2. Read 220 greeting; send `EHLO localhost`, expect 250.
///   3. If `use_tls && port != 465`: send STARTTLS (220), wrap the existing socket in a TLS
///      session, detach the underlying TCP handle so it isn't double-closed, and re-issue EHLO.
///   4. If credentials are set: send AUTH LOGIN (334) → base64(username) (334) → base64(password)
///      (235). Each base64 step uses `rt_codec_base64_enc`, then `rt_string_unref`s the encoder
///      output to keep peak memory low.
/// All steps populate `last_error` on failure and return -1; success returns 0.
static int smtp_connect_and_handshake(rt_smtp_impl *s) {
    smtp_caps_t caps = {0, 0};
    smtp_close_transport(s);
    clear_error(s);

    if (s->use_tls && s->port == 465) {
        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = s->host;
        cfg.timeout_ms = 30000;
        s->tls = rt_tls_connect(s->host, (uint16_t)s->port, &cfg);
        if (!s->tls) {
            const char *detail = rt_tls_last_error();
            char msg[512];
            if (detail && *detail) {
                snprintf(msg, sizeof(msg), "SMTP: TLS connection failed: %s", detail);
                set_error(s, msg);
            } else {
                set_error(s, "SMTP: TLS connection failed");
            }
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
    if (smtp_command_ex(s, ehlo, 250, smtp_parse_ehlo_caps_line, &caps) < 0)
        return -1;

    if (s->use_tls && s->port != 465) {
        if (!caps.supports_starttls) {
            set_error(s, "SMTP: server does not advertise STARTTLS");
            return -1;
        }
        if (smtp_command(s, "STARTTLS\r\n", 220) < 0)
            return -1;

        rt_tls_config_t cfg;
        rt_tls_config_init(&cfg);
        cfg.hostname = s->host;
        cfg.timeout_ms = 30000;
        s->tls = rt_tls_new((int)rt_tcp_socket_fd(s->tcp), &cfg);
        if (!s->tls) {
            const char *detail = rt_tls_last_error();
            char msg[512];
            if (detail && *detail) {
                snprintf(msg, sizeof(msg), "SMTP: TLS setup failed: %s", detail);
                set_error(s, msg);
            } else {
                set_error(s, "SMTP: TLS setup failed");
            }
            return -1;
        }
        if (rt_tls_handshake(s->tls) != RT_TLS_OK) {
            const char *detail = rt_tls_get_error(s->tls);
            char msg[512];
            if (detail && *detail) {
                snprintf(msg, sizeof(msg), "SMTP: TLS handshake failed: %s", detail);
                set_error(s, msg);
            } else {
                set_error(s, "SMTP: TLS handshake failed");
            }
            rt_tls_close(s->tls);
            s->tls = NULL;
            return -1;
        }

        rt_tcp_detach_socket(s->tcp);
        if (rt_obj_release_check0(s->tcp))
            rt_obj_free(s->tcp);
        s->tcp = NULL;
        smtp_set_transport_timeouts(s, 30000);

        caps.supports_starttls = 0;
        caps.supports_auth_login = 0;
        if (smtp_command_ex(s, ehlo, 250, smtp_parse_ehlo_caps_line, &caps) < 0)
            return -1;
    }

    // AUTH LOGIN if credentials provided
    if (s->username && s->password) {
        if (!s->tls && s->port != 465) {
            set_error(s, "SMTP: refusing AUTH LOGIN over an unencrypted connection");
            return -1;
        }
        if (!caps.supports_auth_login) {
            set_error(s, "SMTP: server does not advertise AUTH LOGIN");
            return -1;
        }
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

/// @brief Walk the MAIL FROM (250) → RCPT TO (250) → DATA (354) → message body (250) sequence.
/// Header values are run through `smtp_sanitize_header_value` (CR/LF stripping) before being
/// formatted into the MIME wrapper; the body is dot-stuffed and a trailing `.\r\n` terminator is
/// appended in the same `snprintf`. Single-recipient only — multi-recipient support would
/// require iterating RCPT TO. Returns 0 on success, -1 on any failure (last_error populated).
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
    {
        int rcpt_code = smtp_command(s, cmd, 0);
        if (!(rcpt_code == 250 || rcpt_code == 251 || rcpt_code == 252)) {
            char err[128];
            snprintf(err, sizeof(err), "SMTP: recipient rejected (%d)", rcpt_code);
            set_error(s, err);
            return -1;
        }
    }

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

/// @brief One-shot send of a `text/plain` UTF-8 email. Connects, handshakes, sends, QUITs (221),
/// and closes the transport — every call is a fresh SMTP session so there's no state to manage
/// between sends. Returns 1 on success, 0 on failure (call `rt_smtp_last_error` for details).
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

/// @brief Identical to `rt_smtp_send` but the body is sent with `Content-Type: text/html`. Caller
/// is responsible for any HTML escaping; the runtime only sanitizes header values, not body content.
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

/// @brief Return the last failure message (cleared on the next successful send). Empty rt_string
/// when no error has been recorded.
rt_string rt_smtp_last_error(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_smtp_impl *s = (rt_smtp_impl *)obj;
    const char *e = s->last_error ? s->last_error : "";
    return rt_string_from_bytes(e, strlen(e));
}

/// @brief Force-close the active transport (TLS + TCP) without sending QUIT. Useful for cancelling
/// a stuck send. Subsequent `rt_smtp_send` calls will reconnect transparently.
void rt_smtp_close(void *obj) {
    if (!obj)
        return;
    smtp_close_transport((rt_smtp_impl *)obj);
}
