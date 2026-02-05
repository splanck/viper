//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_ratelimit.h
// Purpose: Token bucket rate limiter for network/API operations.
// Key invariants: Tokens refill continuously based on elapsed time.
//                 Available tokens never exceed max capacity.
// Ownership/Lifetime: Rate limiter objects are heap-allocated; caller
//                     responsible for lifetime management.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a token bucket rate limiter.
    /// @param max_tokens Maximum token capacity.
    /// @param refill_per_sec Tokens refilled per second.
    /// @return New rate limiter object.
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
