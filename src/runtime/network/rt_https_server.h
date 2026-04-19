//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_https_server.h
// Purpose: TLS-backed HTTP/1.1 server using the in-tree TLS 1.3 runtime.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_http_server.h"
#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_https_server_new(int64_t port, rt_string cert_file, rt_string key_file);
void rt_https_server_get(void *server, rt_string pattern, rt_string handler_tag);
void rt_https_server_post(void *server, rt_string pattern, rt_string handler_tag);
void rt_https_server_put(void *server, rt_string pattern, rt_string handler_tag);
void rt_https_server_del(void *server, rt_string pattern, rt_string handler_tag);
void rt_https_server_bind_handler(void *server, rt_string handler_tag, void *entry);
void rt_https_server_bind_handler_dispatch(
    void *server, rt_string handler_tag, void *dispatch, void *ctx, void *cleanup);
void rt_https_server_start(void *server);
void rt_https_server_stop(void *server);
int64_t rt_https_server_port(void *server);
int8_t rt_https_server_is_running(void *server);

#ifdef __cplusplus
}
#endif
