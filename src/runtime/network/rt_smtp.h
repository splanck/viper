//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_smtp.h
// Purpose: Simple SMTP client for sending emails with optional STARTTLS.
// Key invariants:
//   - Implements SMTP EHLO, AUTH LOGIN, MAIL FROM/RCPT TO, DATA.
//   - STARTTLS upgrades the connection to TLS before AUTH.
//   - MIME messages built internally for plain text and HTML.
// Ownership/Lifetime:
//   - Client objects are GC-managed.
// Links: rt_network.h (TCP), rt_tls.h (STARTTLS)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_smtp_new(rt_string host, int64_t port);
    void rt_smtp_set_auth(void *client, rt_string username, rt_string password);
    void rt_smtp_set_tls(void *client, int8_t enable);
    int8_t rt_smtp_send(
        void *client, rt_string from, rt_string to, rt_string subject, rt_string body);
    int8_t rt_smtp_send_html(
        void *client, rt_string from, rt_string to, rt_string subject, rt_string html_body);
    rt_string rt_smtp_last_error(void *client);
    void rt_smtp_close(void *client);

#ifdef __cplusplus
}
#endif
