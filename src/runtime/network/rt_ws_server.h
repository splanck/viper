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

/// @brief Create a WebSocket server bound to the given local port (not yet listening).
void *rt_ws_server_new(int64_t port);
/// @brief Begin listening for connections (spawns the accept thread).
void rt_ws_server_start(void *server);
/// @brief Stop accepting and close all client connections.
void rt_ws_server_stop(void *server);
/// @brief Send a text message to every connected client.
void rt_ws_server_broadcast(void *server, rt_string message);
/// @brief Send a binary frame (Bytes) to every connected client.
void rt_ws_server_broadcast_bytes(void *server, void *data);
/// @brief Count of currently-connected clients.
int64_t rt_ws_server_client_count(void *server);
/// @brief Get the server's listen port.
int64_t rt_ws_server_port(void *server);
/// @brief True if the server is currently accepting connections.
int8_t rt_ws_server_is_running(void *server);
/// @brief Block until a client connects; returns a client handle (NULL on shutdown).
void *rt_ws_server_accept(void *server);
/// @brief Receive the next text message from @p client (empty string on close).
rt_string rt_ws_server_client_recv(void *client);
/// @brief Send a text message to @p client.
void rt_ws_server_client_send(void *client, rt_string message);
/// @brief Close the @p client connection (sends WebSocket close frame).
void rt_ws_server_client_close(void *client);

#ifdef __cplusplus
}
#endif
