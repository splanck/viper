//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_stopwatch.h
// Purpose: High-precision stopwatch for benchmarking and performance measurement, supporting
// start/stop/reset operations and elapsed-time queries in nanoseconds, microseconds, and
// milliseconds.
//
// Key invariants:
//   - Accumulated time is monotonic; it never decreases across start/stop cycles.
//   - Nanosecond resolution is used where the platform clock permits.
//   - Elapsed queries while running include time since the last start call.
//   - rt_stopwatch_restart resets elapsed to zero and immediately starts timing.
//
// Ownership/Lifetime:
//   - Stopwatch objects are heap-allocated; caller is responsible for lifetime management.
//   - No reference counting; caller must explicitly free the object when done.
//
// Links: src/runtime/core/rt_stopwatch.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new stopped stopwatch.
    /// @return Pointer to new stopwatch object.
    void *rt_stopwatch_new(void);

    /// @brief Create and immediately start a new stopwatch.
    /// @return Pointer to new running stopwatch object.
    void *rt_stopwatch_start_new(void);

    /// @brief Start or resume the stopwatch.
    /// @param obj Stopwatch pointer.
    /// @details Has no effect if already running.
    void rt_stopwatch_start(void *obj);

    /// @brief Stop/pause the stopwatch.
    /// @param obj Stopwatch pointer.
    /// @details Preserves accumulated time. Has no effect if already stopped.
    void rt_stopwatch_stop(void *obj);

    /// @brief Reset the stopwatch to zero and stop it.
    /// @param obj Stopwatch pointer.
    void rt_stopwatch_reset(void *obj);

    /// @brief Reset and immediately start the stopwatch.
    /// @param obj Stopwatch pointer.
    /// @details Equivalent to Reset() followed by Start() but atomic.
    void rt_stopwatch_restart(void *obj);

    /// @brief Get elapsed time in nanoseconds.
    /// @param obj Stopwatch pointer.
    /// @return Total elapsed nanoseconds.
    int64_t rt_stopwatch_elapsed_ns(void *obj);

    /// @brief Get elapsed time in microseconds.
    /// @param obj Stopwatch pointer.
    /// @return Total elapsed microseconds.
    int64_t rt_stopwatch_elapsed_us(void *obj);

    /// @brief Get elapsed time in milliseconds.
    /// @param obj Stopwatch pointer.
    /// @return Total elapsed milliseconds.
    int64_t rt_stopwatch_elapsed_ms(void *obj);

    /// @brief Check if the stopwatch is currently running.
    /// @param obj Stopwatch pointer.
    /// @return Non-zero if running, zero if stopped.
    int8_t rt_stopwatch_is_running(void *obj);

#ifdef __cplusplus
}
#endif
