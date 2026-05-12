//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls_server_internal.h
// Purpose: Internal TLS 1.3 server context + accept helpers used by
//          HttpsServer and WssServer.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque server-side TLS context (cert, key, ALPN policy).
typedef struct rt_tls_server_ctx rt_tls_server_ctx_t;

/// @brief Server-side TLS configuration consumed by @ref rt_tls_server_ctx_new.
/// @details Paths are read once at context creation; the parsed cert chain
///          and private key are held inside the context for every
///          subsequent @ref rt_tls_server_accept_socket call.
typedef struct rt_tls_server_config {
    const char *cert_file;      ///< PEM-encoded server cert chain (leaf first).
    const char *key_file;       ///< PEM-encoded private key matching the leaf.
    const char *alpn_protocol;  ///< Optional comma-separated ALPN advertise list.
    int timeout_ms;             ///< Per-handshake timeout; 0 = default 30 s.
} rt_tls_server_config_t;

/// @brief Initialise @p config with default field values.
void rt_tls_server_config_init(rt_tls_server_config_t *config);

/// @brief Parse the cert + key files and build a shared server context.
/// @return New context, or NULL on parse/IO failure (cause via @ref rt_tls_server_last_error).
rt_tls_server_ctx_t *rt_tls_server_ctx_new(const rt_tls_server_config_t *config);

/// @brief Release @p ctx and zero its private-key buffers.
void rt_tls_server_ctx_free(rt_tls_server_ctx_t *ctx);

/// @brief Accept a client TLS handshake on an already-connected @p socket_fd.
/// @details Allocates a fresh @ref rt_tls_session_t, drives the server
///          handshake to completion using the cert/key + ALPN policy
///          in @p ctx, and returns the live session on success. Returns
///          NULL on any handshake failure with the cause captured via
///          @ref rt_tls_server_last_error.
rt_tls_session_t *rt_tls_server_accept_socket(int socket_fd, const rt_tls_server_ctx_t *ctx);

/// @brief Return the most recent server-side TLS error string for diagnostics.
const char *rt_tls_server_last_error(void);

#ifdef __cplusplus
}
#endif
