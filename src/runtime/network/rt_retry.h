//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_retry.h
// Purpose: Retry policy with configurable backoff strategies.
// Key invariants: Tracks attempt count and computes delays.
//                 Supports fixed, linear, and exponential backoff.
// Ownership/Lifetime: Caller manages policy lifetime.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a retry policy with max retries and base delay.
    /// @param max_retries Maximum number of retries (0 = no retries).
    /// @param base_delay_ms Base delay in milliseconds.
    /// @return New retry policy object.
    void *rt_retry_new(int64_t max_retries, int64_t base_delay_ms);

    /// @brief Create with exponential backoff.
    /// @param max_retries Maximum retries.
    /// @param base_delay_ms Base delay (doubles each attempt).
    /// @param max_delay_ms Maximum delay cap.
    /// @return New retry policy object.
    void *rt_retry_exponential(int64_t max_retries, int64_t base_delay_ms, int64_t max_delay_ms);

    /// @brief Check if another retry is allowed.
    /// @param policy Retry policy object.
    /// @return 1 if can retry, 0 if exhausted.
    int8_t rt_retry_can_retry(void *policy);

    /// @brief Record an attempt and get the delay before next retry.
    /// @param policy Retry policy object.
    /// @return Delay in milliseconds before next retry, or -1 if exhausted.
    int64_t rt_retry_next_delay(void *policy);

    /// @brief Get current attempt number (0-based).
    /// @param policy Retry policy object.
    /// @return Current attempt number.
    int64_t rt_retry_get_attempt(void *policy);

    /// @brief Get maximum retries configured.
    /// @param policy Retry policy object.
    /// @return Maximum retries.
    int64_t rt_retry_get_max_retries(void *policy);

    /// @brief Reset the policy for reuse.
    /// @param policy Retry policy object.
    void rt_retry_reset(void *policy);

    /// @brief Get total number of attempts made.
    /// @param policy Retry policy object.
    /// @return Total attempts.
    int64_t rt_retry_get_total_attempts(void *policy);

    /// @brief Check if the policy is exhausted (all retries used).
    /// @param policy Retry policy object.
    /// @return 1 if exhausted, 0 if retries remain.
    int8_t rt_retry_is_exhausted(void *policy);

#ifdef __cplusplus
}
#endif
