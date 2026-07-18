//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/network/rt_ratelimit.h
// Purpose: Token bucket rate limiter for network and API operations, continuously refilling tokens
// based on elapsed time up to a maximum capacity.
//
// Key invariants:
//   - Tokens refill continuously using a monotonic clock when the platform provides one.
//   - Available tokens never exceed max capacity.
//   - rt_ratelimit_try_acquire returns immediately; returns 1 if tokens available, 0 otherwise.
//
// Ownership/Lifetime:
//   - Rate limiter objects are runtime-managed heap objects.
//
// Links: src/runtime/network/rt_ratelimit.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Stable managed-object class tag for RateLimiter handles.
/// @details Public entry points validate this class and their minimum private
///          payload size before interpreting an opaque object as bucket state.
#define RT_RATELIMIT_CLASS_ID INT64_C(-0x720204)

/// @brief Create a token bucket rate limiter.
/// @param max_tokens Maximum token capacity.
/// @param refill_per_sec Tokens refilled per second.
/// @return Owned rate limiter object, or NULL after an allocation trap.
void *rt_ratelimit_new(int64_t max_tokens, double refill_per_sec);

/// @brief Try to consume 1 token.
/// @param limiter Rate limiter pointer.
/// @return 1 if token consumed, 0 if no tokens available.
int8_t rt_ratelimit_try_acquire(void *limiter);

/// @brief Try to consume N tokens.
/// @param limiter Rate limiter pointer.
/// @param n Number of tokens to consume.
/// @return 1 if tokens consumed, 0 if insufficient tokens.
int8_t rt_ratelimit_try_acquire_n(void *limiter, int64_t n);

/// @brief Get current available tokens (after refill calculation).
/// @param limiter Rate limiter pointer.
/// @return Number of available tokens (truncated to integer).
int64_t rt_ratelimit_available(void *limiter);

/// @brief Reset the limiter to full capacity.
/// @param limiter Rate limiter pointer.
void rt_ratelimit_reset(void *limiter);

/// @brief Get maximum token capacity.
/// @param limiter Rate limiter pointer.
/// @return Max tokens.
int64_t rt_ratelimit_get_max(void *limiter);

/// @brief Get refill rate in tokens per second.
/// @param limiter Rate limiter pointer.
/// @return Refill rate.
double rt_ratelimit_get_rate(void *limiter);

#ifdef __cplusplus
}
#endif
