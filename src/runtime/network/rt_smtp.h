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
extern "C" {
#endif

/// @brief Create an SMTP client targeting @p host on the given port (e.g., 25, 465, 587).
void *rt_smtp_new(rt_string host, int64_t port);
/// @brief Set credentials for AUTH LOGIN (sent only after STARTTLS if TLS is enabled).
void rt_smtp_set_auth(void *client, rt_string username, rt_string password);
/// @brief Toggle STARTTLS upgrade after EHLO (required for most providers).
void rt_smtp_set_tls(void *client, int8_t enable);
/// @brief Send a plain-text email. Returns 1 on success, 0 on any SMTP/network error.
int8_t rt_smtp_send(void *client, rt_string from, rt_string to, rt_string subject, rt_string body);
/// @brief Send an HTML-format email (Content-Type: text/html). Returns 1 on success.
int8_t rt_smtp_send_html(
    void *client, rt_string from, rt_string to, rt_string subject, rt_string html_body);
/// @brief Get the last SMTP error message (response code + text), empty if no error.
rt_string rt_smtp_last_error(void *client);
/// @brief Send QUIT and close the connection.
void rt_smtp_close(void *client);

#ifdef __cplusplus
}
#endif
