//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_time.h
// Purpose: Public timing primitives for sleep and monotonic clock access used
// by the Zanna runtime and the Zanna.Time.Clock surface.
//
// Key invariants:
//   - Clock values prefer a monotonic source; POSIX uses realtime as a failure
//     fallback, ratcheted through a process-local floor so it stays
//     non-decreasing, and all-source failure returns 0 (VDOC-223).
//   - Negative sleep durations are clamped to 0.
//   - All functions are thread-safe and have no shared mutable state.
//
// Ownership/Lifetime:
//   - All APIs operate on scalar values only and allocate no heap state.
//
// Links: src/runtime/core/rt_time.c (implementation),
//        include/zanna/runtime/rt.h (umbrella public header)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Sleep for approximately @p ms milliseconds.
/// @param ms Milliseconds to sleep; negative values are treated as 0.
void rt_sleep_ms(int32_t ms);

/// @brief Return milliseconds from the best available elapsed-time clock.
/// @return Tick count, or 0 if all clock queries fail. POSIX's realtime fallback
/// is adjustable. Traps on signed 64-bit overflow.
int64_t rt_timer_ms(void);

/// @brief Zanna.Time.Clock.Sleep entry point.
/// @param ms Milliseconds to sleep; negative values are treated as 0.
void rt_clock_sleep(int64_t ms);

/// @brief Zanna.Time.Clock.Ticks entry point.
/// @return Tick count, with the fallback behavior of @ref rt_timer_ms. Traps on
/// signed 64-bit overflow.
int64_t rt_clock_ticks(void);

/// @brief Zanna.Time.Clock.TicksUs entry point.
/// @return Microsecond tick count, or 0 if all clock queries fail. POSIX's
/// realtime fallback is adjustable. Traps on signed 64-bit overflow.
int64_t rt_clock_ticks_us(void);

/// @brief Clamp a fallback clock reading up to a process-local monotonic floor.
/// @details Internal helper shared by the Clock, Stopwatch, and Countdown POSIX
/// CLOCK_REALTIME fallbacks. A lock-free CAS-max keeps the returned sequence
/// non-decreasing even if wall-clock time is adjusted backward (VDOC-223). Not a
/// registered runtime surface symbol.
/// @param floor Caller-owned process-local floor for one scale/call site.
/// @param candidate Freshly sampled fallback value.
/// @return The greater of @p candidate and the retained floor.
int64_t rt_time_monotonic_ratchet(int64_t *floor, int64_t candidate);

#ifdef __cplusplus
}
#endif
