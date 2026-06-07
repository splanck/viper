//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_websocket_internal.h
// Purpose: WebSocket crypto/key helpers (SHA-1 accept key, nonce) shared
//   between rt_ws_crypto.c and the client logic in rt_websocket.c.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

rt_string generate_ws_key(void);
int ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
int ws_is_valid_utf8(const uint8_t *data, size_t len);
