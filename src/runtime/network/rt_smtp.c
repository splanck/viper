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

#include "rt_smtp.h"

#include "rt_codec.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *tcp; // TCP connection
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
    if (s->tcp)
        rt_tcp_close(s->tcp);
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

/// @brief Send a command and read the response code.
static int smtp_command(rt_smtp_impl *s, const char *cmd, int expected_code) {
    if (cmd) {
        rt_string cmd_str = rt_string_from_bytes(cmd, strlen(cmd));
        rt_tcp_send_str(s->tcp, cmd_str);
        rt_string_unref(cmd_str);
    }

    // Read response line(s)
    rt_string line = rt_tcp_recv_line(s->tcp);
    if (!line) {
        set_error(s, "SMTP: no response");
        return -1;
    }
    const char *l = rt_string_cstr(line);
    int code = l ? atoi(l) : -1;
    rt_string_unref(line);

    // Drain multi-line responses (line[3] == '-')
    while (l && strlen(l) > 3 && l[3] == '-') {
        line = rt_tcp_recv_line(s->tcp);
        if (!line)
            break;
        l = rt_string_cstr(line);
        rt_string_unref(line);
    }

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
    // Connect
    rt_string host_str = rt_string_from_bytes(s->host, strlen(s->host));
    s->tcp = rt_tcp_connect_for(host_str, s->port, 30000);
    rt_string_unref(host_str);

    if (!s->tcp || !rt_tcp_is_open(s->tcp)) {
        set_error(s, "SMTP: connection failed");
        return -1;
    }

    rt_tcp_set_recv_timeout(s->tcp, 30000);

    // Read greeting (220)
    if (smtp_command(s, NULL, 220) < 0)
        return -1;

    // EHLO
    char ehlo[300];
    snprintf(ehlo, sizeof(ehlo), "EHLO localhost\r\n");
    if (smtp_command(s, ehlo, 250) < 0)
        return -1;

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
    size_t msg_cap = 1024 + strlen(body);
    char *msg = (char *)malloc(msg_cap);
    if (!msg) {
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
                        "%s\r\n"
                        ".\r\n",
                        from,
                        to,
                        subject,
                        content_type,
                        body);

    rt_string msg_str = rt_string_from_bytes(msg, (size_t)mlen);
    free(msg);
    rt_tcp_send_str(s->tcp, msg_str);
    rt_string_unref(msg_str);

    if (smtp_command(s, NULL, 250) < 0)
        return -1;
    return 0;
}

int8_t rt_smtp_send(void *obj, rt_string from, rt_string to, rt_string subject, rt_string body) {
    if (!obj)
        return 0;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;

    if (smtp_connect_and_handshake(s) < 0)
        return 0;

    const char *f = rt_string_cstr(from);
    const char *t = rt_string_cstr(to);
    const char *sub = rt_string_cstr(subject);
    const char *b = rt_string_cstr(body);

    int result =
        smtp_send_message(s, f ? f : "", t ? t : "", sub ? sub : "", b ? b : "", "text/plain");

    // QUIT
    smtp_command(s, "QUIT\r\n", 221);
    rt_tcp_close(s->tcp);
    s->tcp = NULL;

    return result == 0 ? 1 : 0;
}

int8_t rt_smtp_send_html(
    void *obj, rt_string from, rt_string to, rt_string subject, rt_string html_body) {
    if (!obj)
        return 0;
    rt_smtp_impl *s = (rt_smtp_impl *)obj;

    if (smtp_connect_and_handshake(s) < 0)
        return 0;

    const char *f = rt_string_cstr(from);
    const char *t = rt_string_cstr(to);
    const char *sub = rt_string_cstr(subject);
    const char *b = rt_string_cstr(html_body);

    int result =
        smtp_send_message(s, f ? f : "", t ? t : "", sub ? sub : "", b ? b : "", "text/html");

    smtp_command(s, "QUIT\r\n", 221);
    rt_tcp_close(s->tcp);
    s->tcp = NULL;

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
    rt_smtp_impl *s = (rt_smtp_impl *)obj;
    if (s->tcp) {
        rt_tcp_close(s->tcp);
        s->tcp = NULL;
    }
}
