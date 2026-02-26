//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_debounce.h
// Purpose: Debouncer and Throttler utilities for rate-limiting operations: Debouncer delays
// execution until a quiet period elapses; Throttler limits to at most once per interval.
//
// Key invariants:
//   - Debouncer resets its timer on each call; fires only after quiet_ms have passed.
//   - Throttler fires at most once per interval_ms regardless of call frequency.
//   - Both utilities are time-based using the monotonic clock.
//   - Callback functions are invoked synchronously from the polling thread.
//
// Ownership/Lifetime:
//   - Debouncer and Throttler objects are heap-allocated; caller manages lifetime.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/threads/rt_debounce.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // --- Debouncer ---

    /// @brief Create a new debouncer with the given delay in milliseconds.
    /// @param delay_ms Debounce delay in milliseconds.
    /// @return New debouncer object.
    void *rt_debounce_new(int64_t delay_ms);

    /// @brief Signal the debouncer (resets the timer).
    /// @param debouncer Debouncer object.
    void rt_debounce_signal(void *debouncer);

    /// @brief Check if the debouncer has settled (delay elapsed since last signal).
    /// @param debouncer Debouncer object.
    /// @return 1 if settled, 0 if still waiting.
    int8_t rt_debounce_is_ready(void *debouncer);

    /// @brief Reset the debouncer to initial state.
    /// @param debouncer Debouncer object.
    void rt_debounce_reset(void *debouncer);

    /// @brief Get the configured delay in milliseconds.
    /// @param debouncer Debouncer object.
    /// @return Delay in milliseconds.
    int64_t rt_debounce_get_delay(void *debouncer);

    /// @brief Get number of signals received since last ready state.
    /// @param debouncer Debouncer object.
    /// @return Signal count.
    int64_t rt_debounce_get_signal_count(void *debouncer);

    // --- Throttler ---

    /// @brief Create a new throttler with the given interval in milliseconds.
    /// @param interval_ms Minimum interval between allowed operations.
    /// @return New throttler object.
    void *rt_throttle_new(int64_t interval_ms);

    /// @brief Check if an operation is allowed (and mark as executed if so).
    /// @param throttler Throttler object.
    /// @return 1 if allowed, 0 if throttled.
    int8_t rt_throttle_try(void *throttler);

    /// @brief Check if an operation would be allowed (without marking).
    /// @param throttler Throttler object.
    /// @return 1 if would be allowed, 0 if would be throttled.
    int8_t rt_throttle_can_proceed(void *throttler);

    /// @brief Reset the throttler to allow immediate operation.
    /// @param throttler Throttler object.
    void rt_throttle_reset(void *throttler);

    /// @brief Get the configured interval in milliseconds.
    /// @param throttler Throttler object.
    /// @return Interval in milliseconds.
    int64_t rt_throttle_get_interval(void *throttler);

    /// @brief Get the number of operations allowed so far.
    /// @param throttler Throttler object.
    /// @return Count of allowed operations.
    int64_t rt_throttle_get_count(void *throttler);

    /// @brief Get time remaining until next operation is allowed, in ms.
    /// @param throttler Throttler object.
    /// @return Milliseconds remaining (0 if ready now).
    int64_t rt_throttle_remaining_ms(void *throttler);

#ifdef __cplusplus
}
#endif
