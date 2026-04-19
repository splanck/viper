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

typedef struct rt_tls_server_ctx rt_tls_server_ctx_t;

typedef struct rt_tls_server_config {
    const char *cert_file;
    const char *key_file;
    const char *alpn_protocol;
    int timeout_ms;
} rt_tls_server_config_t;

void rt_tls_server_config_init(rt_tls_server_config_t *config);
rt_tls_server_ctx_t *rt_tls_server_ctx_new(const rt_tls_server_config_t *config);
void rt_tls_server_ctx_free(rt_tls_server_ctx_t *ctx);
rt_tls_session_t *rt_tls_server_accept_socket(int socket_fd, const rt_tls_server_ctx_t *ctx);
const char *rt_tls_server_last_error(void);

#ifdef __cplusplus
}
#endif
