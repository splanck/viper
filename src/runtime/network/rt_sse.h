//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_sse.h
// Purpose: Server-Sent Events (SSE) client for receiving event streams.
// Key invariants:
//   - Connects via HTTP and reads text/event-stream data.
//   - Events are parsed per the SSE spec (event, data, id fields).
// Ownership/Lifetime:
//   - SSE client objects are GC-managed.
// Links: rt_network_http.c (HTTP connection)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Open an SSE connection to @p url (HTTP GET with `Accept: text/event-stream`).
void *rt_sse_connect(rt_string url);
/// @brief Block until the next event arrives; returns the event's `data:` payload (empty on close).
rt_string rt_sse_recv(void *client);
/// @brief Like `_recv` but returns empty string after @p timeout_ms milliseconds with no event.
rt_string rt_sse_recv_for(void *client, int64_t timeout_ms);
/// @brief True if the SSE stream is still open.
int8_t rt_sse_is_open(void *client);
/// @brief Close the SSE connection.
void rt_sse_close(void *client);
/// @brief Get the `event:` field of the most recent event (empty for default `message` events).
rt_string rt_sse_last_event_type(void *client);
/// @brief Get the `id:` field of the most recent event (used for resuming after disconnect).
rt_string rt_sse_last_event_id(void *client);

#ifdef __cplusplus
}
#endif
