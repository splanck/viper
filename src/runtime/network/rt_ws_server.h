//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ws_server.h
// Purpose: WebSocket server that accepts upgrade requests on a TCP port.
// Key invariants:
//   - Performs HTTP upgrade handshake per RFC 6455.
//   - Each client handled in a dedicated thread.
//   - Broadcast sends to all connected clients.
// Ownership/Lifetime:
//   - Server and client objects are GC-managed.
// Links: rt_websocket.h (client-side framing), rt_network.h (TCP)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_ws_server_new(int64_t port);
void rt_ws_server_start(void *server);
void rt_ws_server_stop(void *server);
void rt_ws_server_broadcast(void *server, rt_string message);
void rt_ws_server_broadcast_bytes(void *server, void *data);
int64_t rt_ws_server_client_count(void *server);
int64_t rt_ws_server_port(void *server);
int8_t rt_ws_server_is_running(void *server);
void *rt_ws_server_accept(void *server);
rt_string rt_ws_server_client_recv(void *client);
void rt_ws_server_client_send(void *client, rt_string message);
void rt_ws_server_client_close(void *client);

#ifdef __cplusplus
}
#endif
