//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_countdown.h
// Purpose: Countdown timer for interval timing with expiration detection.
// Key invariants: Tracks elapsed time against interval; Remaining = max(0, interval - elapsed);
//                 Expired = elapsed >= interval. All times in milliseconds.
// Ownership/Lifetime: Countdown objects are heap-allocated; caller responsible
//                     for lifetime management.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new countdown timer with specified interval.
    /// @param interval_ms Interval duration in milliseconds.
    /// @return Pointer to new countdown object (stopped, elapsed = 0).
    void *rt_countdown_new(int64_t interval_ms);

    /// @brief Start or resume the countdown timer.
    /// @param obj Countdown pointer.
    /// @details Has no effect if already running.
    void rt_countdown_start(void *obj);

    /// @brief Stop/pause the countdown timer.
    /// @param obj Countdown pointer.
    /// @details Preserves elapsed time. Has no effect if already stopped.
    void rt_countdown_stop(void *obj);

    /// @brief Reset the countdown timer to zero elapsed time.
    /// @param obj Countdown pointer.
    /// @details Stops the timer and resets elapsed to 0.
    void rt_countdown_reset(void *obj);

    /// @brief Get elapsed time in milliseconds.
    /// @param obj Countdown pointer.
    /// @return Total elapsed milliseconds since start/last reset.
    int64_t rt_countdown_elapsed(void *obj);

    /// @brief Get remaining time in milliseconds.
    /// @param obj Countdown pointer.
    /// @return max(0, interval - elapsed) in milliseconds.
    int64_t rt_countdown_remaining(void *obj);

    /// @brief Check if the countdown has expired.
    /// @param obj Countdown pointer.
    /// @return True if elapsed >= interval.
    int8_t rt_countdown_expired(void *obj);

    /// @brief Get the interval duration in milliseconds.
    /// @param obj Countdown pointer.
    /// @return Interval duration in milliseconds.
    int64_t rt_countdown_interval(void *obj);

    /// @brief Set a new interval duration.
    /// @param obj Countdown pointer.
    /// @param interval_ms New interval duration in milliseconds.
    /// @details Does not reset elapsed time.
    void rt_countdown_set_interval(void *obj, int64_t interval_ms);

    /// @brief Check if the countdown timer is currently running.
    /// @param obj Countdown pointer.
    /// @return Non-zero if running, zero if stopped.
    int8_t rt_countdown_is_running(void *obj);

    /// @brief Wait (block) until the countdown expires.
    /// @param obj Countdown pointer.
    /// @details If already expired, returns immediately. Starts timer if not running.
    void rt_countdown_wait(void *obj);

#ifdef __cplusplus
}
#endif
