//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_time.h
// Purpose: Public timing primitives for sleep and monotonic clock access used
// by the Viper runtime and the Viper.Time.Clock surface.
//
// Key invariants:
//   - Clock values prefer a monotonic source; POSIX uses adjustable realtime as
//     a failure fallback, and all-source failure returns 0 (VDOC-223).
//   - Negative sleep durations are clamped to 0.
//   - All functions are thread-safe and have no shared mutable state.
//
// Ownership/Lifetime:
//   - All APIs operate on scalar values only and allocate no heap state.
//
// Links: src/runtime/core/rt_time.c (implementation),
//        include/viper/runtime/rt.h (umbrella public header)
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

/// @brief Viper.Time.Clock.Sleep entry point.
/// @param ms Milliseconds to sleep; negative values are treated as 0.
void rt_clock_sleep(int64_t ms);

/// @brief Viper.Time.Clock.Ticks entry point.
/// @return Tick count, with the fallback behavior of @ref rt_timer_ms. Traps on
/// signed 64-bit overflow.
int64_t rt_clock_ticks(void);

/// @brief Viper.Time.Clock.TicksUs entry point.
/// @return Microsecond tick count, or 0 if all clock queries fail. POSIX's
/// realtime fallback is adjustable. Traps on signed 64-bit overflow.
int64_t rt_clock_ticks_us(void);

#ifdef __cplusplus
}
#endif
