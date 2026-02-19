//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_websocket.h
// Purpose: WebSocket client for real-time bidirectional communication.
// Key invariants: Implements RFC 6455 WebSocket protocol.
// Ownership/Lifetime: Connection objects are managed by GC.
// Links: docs/viperlib/network.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // WebSocket - Creation
    //=========================================================================

    /// @brief Connect to a WebSocket server.
    /// @param url WebSocket URL (ws:// or wss://).
    /// @return WebSocket connection object.
    /// @note Traps on connection error, invalid URL, or handshake failure.
    void *rt_ws_connect(rt_string url);

    /// @brief Connect to a WebSocket server with timeout.
    /// @param url WebSocket URL (ws:// or wss://).
    /// @param timeout_ms Connection timeout in milliseconds.
    /// @return WebSocket connection object.
    /// @note Traps on connection error, timeout, or handshake failure.
    void *rt_ws_connect_for(rt_string url, int64_t timeout_ms);

    //=========================================================================
    // WebSocket - Properties
    //=========================================================================

    /// @brief Get the connected URL.
    /// @param obj WebSocket connection object.
    /// @return URL string.
    rt_string rt_ws_url(void *obj);

    /// @brief Check if connection is open.
    /// @param obj WebSocket connection object.
    /// @return 1 if open, 0 if closed.
    int8_t rt_ws_is_open(void *obj);

    /// @brief Get the close code if connection was closed.
    /// @param obj WebSocket connection object.
    /// @return Close code (1000 = normal, 0 if still open).
    int64_t rt_ws_close_code(void *obj);

    /// @brief Get the close reason if connection was closed.
    /// @param obj WebSocket connection object.
    /// @return Close reason string (empty if no reason provided).
    rt_string rt_ws_close_reason(void *obj);

    //=========================================================================
    // WebSocket - Send Methods
    //=========================================================================

    /// @brief Send a text message.
    /// @param obj WebSocket connection object.
    /// @param text Text message to send.
    /// @note Traps if connection is closed.
    void rt_ws_send(void *obj, rt_string text);

    /// @brief Send binary data.
    /// @param obj WebSocket connection object.
    /// @param data Bytes object to send.
    /// @note Traps if connection is closed.
    void rt_ws_send_bytes(void *obj, void *data);

    /// @brief Send a ping frame.
    /// @param obj WebSocket connection object.
    void rt_ws_ping(void *obj);

    //=========================================================================
    // WebSocket - Receive Methods
    //=========================================================================

    /// @brief Receive a message (blocks until message arrives).
    /// @param obj WebSocket connection object.
    /// @return Text message string, or empty string on close.
    /// @note Control frames (ping/pong/close) are handled automatically.
    rt_string rt_ws_recv(void *obj);

    /// @brief Receive a message with timeout.
    /// @param obj WebSocket connection object.
    /// @param timeout_ms Timeout in milliseconds.
    /// @return Text message string, or NULL on timeout.
    /// @note Returns empty string on close.
    rt_string rt_ws_recv_for(void *obj, int64_t timeout_ms);

    /// @brief Receive binary data (blocks until message arrives).
    /// @param obj WebSocket connection object.
    /// @return Bytes object, or empty bytes on close.
    void *rt_ws_recv_bytes(void *obj);

    /// @brief Receive binary data with timeout.
    /// @param obj WebSocket connection object.
    /// @param timeout_ms Timeout in milliseconds.
    /// @return Bytes object, or NULL on timeout.
    void *rt_ws_recv_bytes_for(void *obj, int64_t timeout_ms);

    //=========================================================================
    // WebSocket - Close
    //=========================================================================

    /// @brief Close the connection gracefully.
    /// @param obj WebSocket connection object.
    void rt_ws_close(void *obj);

    /// @brief Close the connection with a code and reason.
    /// @param obj WebSocket connection object.
    /// @param code Close status code (1000 = normal).
    /// @param reason Close reason string.
    void rt_ws_close_with(void *obj, int64_t code, rt_string reason);

#ifdef __cplusplus
}
#endif
