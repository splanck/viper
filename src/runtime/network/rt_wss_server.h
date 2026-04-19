//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_wss_server.h
// Purpose: TLS-backed WebSocket server using the in-tree TLS 1.3 runtime.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_wss_server_new(int64_t port, rt_string cert_file, rt_string key_file);
void rt_wss_server_start(void *server);
void rt_wss_server_stop(void *server);
void rt_wss_server_broadcast(void *server, rt_string message);
void rt_wss_server_broadcast_bytes(void *server, void *data);
int64_t rt_wss_server_client_count(void *server);
int64_t rt_wss_server_port(void *server);
int8_t rt_wss_server_is_running(void *server);

#ifdef __cplusplus
}
#endif
