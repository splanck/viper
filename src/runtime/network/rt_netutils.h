//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_netutils.h
// Purpose: Static network utility functions — port checking, CIDR matching,
//          interface enumeration, and IP classification.
// Key invariants:
//   - All functions are stateless and thread-safe.
//   - Port checks use a brief connect+close cycle with timeout.
// Ownership/Lifetime:
//   - Pure functions; no state, no heap allocation beyond return values.
// Links: rt_network.h (socket primitives)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Check if a remote port is open (accepts connections).
int8_t rt_netutils_is_port_open(rt_string host, int64_t port, int64_t timeout_ms);

/// @brief Get a free (available) port on the local machine.
int64_t rt_netutils_get_free_port(void);

/// @brief Check if an IP address matches a CIDR range (e.g., "10.0.0.0/8").
int8_t rt_netutils_match_cidr(rt_string ip, rt_string cidr);

/// @brief Check if an IP address is in a private range (RFC 1918).
int8_t rt_netutils_is_private_ip(rt_string ip);

/// @brief Get the primary local IPv4 address (non-loopback).
rt_string rt_netutils_local_ipv4(void);

#ifdef __cplusplus
}
#endif
