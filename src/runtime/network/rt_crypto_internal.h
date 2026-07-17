//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_crypto_internal.h
// Purpose: Shared internal crypto helpers used across the crypto translation
//   units (rt_crypto.c primitives + rt_x25519.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>

/// @brief Best-effort constant-time memory wipe (defined in rt_crypto.c).
void rt_secure_zero(void *ptr, size_t len);
