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

void *rt_sse_connect(rt_string url);
rt_string rt_sse_recv(void *client);
rt_string rt_sse_recv_for(void *client, int64_t timeout_ms);
int8_t rt_sse_is_open(void *client);
void rt_sse_close(void *client);
rt_string rt_sse_last_event_type(void *client);
rt_string rt_sse_last_event_id(void *client);

#ifdef __cplusplus
}
#endif
