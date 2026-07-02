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
/// @brief Send a plain-text email and return a Result instead of using LastError.
/// @details This performs the same SMTP session as rt_smtp_send(). Success is
///          returned as OkI64(1); SMTP, network, and validation failures are
///          returned as ErrStr(message). Use this in new code instead of calling
///          rt_smtp_send() and then rt_smtp_last_error().
/// @param client Opaque Viper.Network.SmtpClient object.
/// @param from Sender mailbox path.
/// @param to Recipient mailbox path.
/// @param subject Email subject; CR/LF are sanitized before DATA.
/// @param body Plain-text message body.
/// @return Opaque Viper.Result object containing OkI64(1) or ErrStr(message).
void *rt_smtp_send_result(
    void *client, rt_string from, rt_string to, rt_string subject, rt_string body);
/// @brief Send an HTML-format email (Content-Type: text/html). Returns 1 on success.
int8_t rt_smtp_send_html(
    void *client, rt_string from, rt_string to, rt_string subject, rt_string html_body);
/// @brief Send an HTML email and return a Result instead of using LastError.
/// @details Mirrors rt_smtp_send_html() with a side-channel-free result shape.
///          Success is OkI64(1); failures are ErrStr(message).
/// @param client Opaque Viper.Network.SmtpClient object.
/// @param from Sender mailbox path.
/// @param to Recipient mailbox path.
/// @param subject Email subject; CR/LF are sanitized before DATA.
/// @param html_body HTML message body; callers remain responsible for escaping.
/// @return Opaque Viper.Result object containing OkI64(1) or ErrStr(message).
void *rt_smtp_send_html_result(
    void *client, rt_string from, rt_string to, rt_string subject, rt_string html_body);
/// @brief Get the last SMTP error message (response code + text), empty if no error.
rt_string rt_smtp_last_error(void *client);
/// @brief Send QUIT and close the connection.
void rt_smtp_close(void *client);

#ifdef __cplusplus
}
#endif
