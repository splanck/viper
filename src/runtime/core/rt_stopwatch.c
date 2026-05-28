//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_stopwatch.c
// Purpose: Implements the Stopwatch class for the Viper runtime. Measures
//          elapsed time using a monotonic clock (immune to wall-clock
//          adjustments). Supports Start/Stop/Restart/Reset and elapsed queries
//          in milliseconds, microseconds, and nanoseconds.
//
// Key invariants:
//   - Uses CLOCK_MONOTONIC (POSIX) or QueryPerformanceCounter (Windows) for
//     nanosecond-resolution timing; the clock is not affected by NTP or DST.
//   - Elapsed time accumulates correctly across multiple Start/Stop cycles;
//     total elapsed = accumulated_ns + (current interval if running).
//   - Stopwatch objects are not thread-safe; external synchronization is
//     required for concurrent access to the same instance.
//   - ElapsedMs/ElapsedUs/ElapsedNs queries are valid in both RUNNING and
//     STOPPED states; they snapshot the current time if running.
//
// Ownership/Lifetime:
//   - Stopwatch instances are heap-allocated via rt_obj_new_i64 and managed
//     by the runtime GC; callers do not free them explicitly.
//   - The internal ViperStopwatch struct contains no pointers to external
//     resources; the finalizer is a no-op.
//
// Links: src/runtime/core/rt_stopwatch.h (public API),
//        src/runtime/core/rt_countdown.c (counts down instead of up),
//        src/runtime/core/rt_time.c (platform sleep and tick helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_stopwatch.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_trap.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/// @brief Internal stopwatch structure.
typedef struct {
    int64_t accumulated_ns; ///< Total accumulated nanoseconds from completed intervals.
    int64_t start_time_ns;  ///< Timestamp when current interval started (if running).
    bool running;           ///< True if stopwatch is currently timing.
} ViperStopwatch;

// Overflow-checked signed 64-bit arithmetic for the nanosecond accumulator. Same
// triplet seen in rt_countdown / rt_duration / rt_dateonly — pre-checks operands
// before performing the arithmetic to avoid signed-overflow UB.

/// @brief Overflow-checked signed 64-bit addition. Returns 1 on overflow.
static int stopwatch_checked_add_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return 1;
    *out = a + b;
    return 0;
}

/// @brief Overflow-checked signed 64-bit subtraction. Returns 1 on overflow.
static int stopwatch_checked_sub_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        return 1;
    *out = a - b;
    return 0;
}

/// @brief Overflow-checked signed 64-bit multiplication. Returns 1 on overflow.
static int stopwatch_checked_mul_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b)
                return 1;
        } else if (b < INT64_MIN / a) {
            return 1;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b)
                return 1;
        } else if (a < INT64_MAX / b) {
            return 1;
        }
    }
    *out = a * b;
    return 0;
#endif
}

/// @brief Win32: nanosecond timestamp from `GetTickCount64`, trapping on overflow.
/// @details Win32's millisecond resolution is multiplied by `1_000_000` to land in the
///          nanosecond units the rest of the stopwatch machinery uses; the bound check
///          (`> INT64_MAX / 1_000_000`) catches the moment when the multiply would
///          overflow signed 64-bit on long-uptime systems.
#if defined(_WIN32)
static int64_t stopwatch_tick_count_ns(void) {
    ULONGLONG ticks = GetTickCount64();
    if (ticks > (ULONGLONG)(INT64_MAX / 1000000LL)) {
        rt_trap_ovf();
        return 0;
    }
    return (int64_t)ticks * 1000000LL;
}
#endif

/// @brief POSIX: convert a `struct timespec` into nanoseconds, trapping on overflow.
/// @details Folds `tv_sec * 1_000_000_000 + tv_nsec` through the checked arithmetic
///          helpers so an absurdly large timespec can't silently wrap. Matches the
///          contract of the Win32 `stopwatch_tick_count_ns` helper for the rest of
///          the file.
#if !defined(_WIN32)
static int64_t stopwatch_timespec_to_ns(struct timespec ts) {
    int64_t seconds_ns;
    int64_t result;
    if (stopwatch_checked_mul_i64((int64_t)ts.tv_sec, 1000000000LL, &seconds_ns) ||
        stopwatch_checked_add_i64(seconds_ns, (int64_t)ts.tv_nsec, &result)) {
        rt_trap_ovf();
        return 0;
    }
    return result;
}
#endif

/// @brief Validate that @p obj is a non-NULL Stopwatch receiver, trapping otherwise.
/// @details Centralises the null-receiver guard so every public method reads
///          `ViperStopwatch *sw = require_stopwatch(obj); if (!sw) return ...;`.
static ViperStopwatch *require_stopwatch(void *obj) {
    if (!obj) {
        rt_trap("Stopwatch: null receiver");
        return NULL;
    }
    return (ViperStopwatch *)obj;
}

/// @brief Get current timestamp in nanoseconds from monotonic clock.
/// @return Nanoseconds since unspecified epoch.
static int64_t get_timestamp_ns(void) {
#if defined(_WIN32)
    // Benign race: concurrent first calls may both query frequency, but QPC
    // frequency is constant so duplicate init produces identical results.
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0 && (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)) {
        return stopwatch_tick_count_ns();
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
        return stopwatch_tick_count_ns();

    int64_t whole = counter.QuadPart / freq.QuadPart;
    int64_t rem = counter.QuadPart % freq.QuadPart;
    int64_t whole_ns;
    int64_t result;
    if (whole > INT64_MAX / 1000000000LL) {
        rt_trap_ovf();
        return 0;
    }
    whole_ns = whole * 1000000000LL;
    int64_t rem_ns = (int64_t)(((long double)rem * 1000000000.0L) / (long double)freq.QuadPart);
    if (stopwatch_checked_add_i64(whole_ns, rem_ns, &result)) {
        rt_trap_ovf();
        return 0;
    }
    return result;
#else
    // Unix and ViperDOS: use clock_gettime.
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return stopwatch_timespec_to_ns(ts);
    }
#endif
    // Fallback to CLOCK_REALTIME
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return stopwatch_timespec_to_ns(ts);
    }
    return 0;
#endif
}

/// @brief Internal helper to get total elapsed nanoseconds.
/// @param sw Stopwatch pointer.
/// @return Total elapsed nanoseconds including current interval if running.
static int64_t stopwatch_get_elapsed_ns(ViperStopwatch *sw) {
    int64_t total = sw->accumulated_ns;

    if (sw->running) {
        int64_t interval;
        if (stopwatch_checked_sub_i64(get_timestamp_ns(), sw->start_time_ns, &interval) ||
            stopwatch_checked_add_i64(total, interval, &total)) {
            rt_trap_ovf();
            return 0;
        }
    }

    return total;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Creates a new stopwatch in stopped state.
///
/// Allocates and initializes a Stopwatch object with zero elapsed time.
/// The stopwatch starts in a stopped state. Call Start() to begin timing.
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.New()    ' Creates stopped stopwatch
/// ' ... prepare for measurement ...
/// sw.Start()                  ' Begin timing
/// ' ... code to measure ...
/// sw.Stop()
/// Print sw.ElapsedMs & " ms"
/// ```
///
/// @return A new Stopwatch object in stopped state. Traps on allocation failure.
///
/// @note O(1) time complexity.
/// @note The stopwatch is managed by Viper's garbage collector.
///
/// @see rt_stopwatch_start_new For creating and immediately starting
/// @see rt_stopwatch_start For starting the stopwatch
void *rt_stopwatch_new(void) {
    ViperStopwatch *sw = (ViperStopwatch *)rt_obj_new_i64(0, (int64_t)sizeof(ViperStopwatch));
    if (!sw) {
        rt_trap("Stopwatch: memory allocation failed");
        return NULL; // Unreachable after trap
    }

    sw->accumulated_ns = 0;
    sw->start_time_ns = 0;
    sw->running = false;

    return sw;
}

/// @brief Creates a new stopwatch and immediately starts it.
///
/// Convenience function that creates a new Stopwatch object and starts it
/// in a single call. This is the most common way to create a stopwatch when
/// you want to begin timing immediately.
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.StartNew()
/// ' ... code to measure ...
/// sw.Stop()
/// Print "Elapsed: " & sw.ElapsedMs & " ms"
/// ```
///
/// **Equivalent to:**
/// ```
/// Dim sw = Stopwatch.New()
/// sw.Start()
/// ```
///
/// @return A new Stopwatch object that is already running.
///
/// @note O(1) time complexity.
/// @note This is the preferred way to create a stopwatch for benchmarking.
///
/// @see rt_stopwatch_new For creating without starting
void *rt_stopwatch_start_new(void) {
    ViperStopwatch *sw = (ViperStopwatch *)rt_stopwatch_new();
    rt_stopwatch_start(sw);
    return sw;
}

/// @brief Starts or resumes the stopwatch.
///
/// Begins or resumes tracking elapsed time. If the stopwatch is already
/// running, this function has no effect. When started after being stopped,
/// new time is added to the previously accumulated time.
///
/// **State transitions:**
/// - Stopped → Running (begins timing)
/// - Running → Running (no change)
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.New()
/// sw.Start()               ' Begin timing
/// Sleep(100)
/// sw.Stop()                ' Pause timing (100ms elapsed)
/// Sleep(100)               ' This time is not counted
/// sw.Start()               ' Resume timing
/// Sleep(100)
/// sw.Stop()
/// Print sw.ElapsedMs       ' ~200ms (not 300ms)
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @note O(1) time complexity.
/// @note Has no effect if already running.
/// @note Elapsed time accumulates across multiple start/stop cycles.
///
/// @see rt_stopwatch_stop For pausing the stopwatch
/// @see rt_stopwatch_restart For resetting and starting
void rt_stopwatch_start(void *obj) {
    ViperStopwatch *sw = require_stopwatch(obj);

    if (!sw->running) {
        sw->start_time_ns = get_timestamp_ns();
        sw->running = true;
    }
}

/// @brief Stops (pauses) the stopwatch.
///
/// Pauses time tracking while preserving the accumulated elapsed time. The
/// stopwatch can be resumed later with Start(). If already stopped, has no
/// effect.
///
/// **State transitions:**
/// - Running → Stopped (preserves time)
/// - Stopped → Stopped (no change)
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.StartNew()
/// ' ... measured code ...
/// sw.Stop()
///
/// ' Read elapsed time multiple times (it won't change while stopped)
/// Print "First read: " & sw.ElapsedMs
/// Print "Second read: " & sw.ElapsedMs  ' Same value
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @note O(1) time complexity.
/// @note Preserves accumulated elapsed time.
///
/// @see rt_stopwatch_start For resuming the stopwatch
/// @see rt_stopwatch_reset For clearing elapsed time
void rt_stopwatch_stop(void *obj) {
    ViperStopwatch *sw = require_stopwatch(obj);

    if (sw->running) {
        int64_t now = get_timestamp_ns();
        int64_t interval;
        if (stopwatch_checked_sub_i64(now, sw->start_time_ns, &interval) ||
            stopwatch_checked_add_i64(sw->accumulated_ns, interval, &sw->accumulated_ns)) {
            rt_trap_ovf();
            return;
        }
        sw->running = false;
    }
}

/// @brief Resets the stopwatch to zero and stops it.
///
/// Clears all accumulated elapsed time and stops the stopwatch. After reset,
/// the stopwatch is as if it were newly created.
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.StartNew()
/// Sleep(100)
/// Print sw.ElapsedMs         ' ~100ms
///
/// sw.Reset()
/// Print sw.ElapsedMs         ' 0
/// Print sw.IsRunning         ' False
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @note O(1) time complexity.
/// @note After reset: Elapsed = 0, IsRunning = false
///
/// @see rt_stopwatch_restart For resetting and immediately starting
/// @see rt_stopwatch_start For starting after reset
void rt_stopwatch_reset(void *obj) {
    ViperStopwatch *sw = require_stopwatch(obj);

    sw->accumulated_ns = 0;
    sw->start_time_ns = 0;
    sw->running = false;
}

/// @brief Resets the stopwatch to zero and immediately starts it.
///
/// Atomically clears all accumulated time and starts timing from zero. This
/// is equivalent to Reset() followed by Start(), but done in a single call
/// for convenience and to avoid race conditions.
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.StartNew()
///
/// For i = 1 To 5
///     sw.Restart()           ' Reset and start for each iteration
///     DoSomeWork()
///     Print "Iteration " & i & ": " & sw.ElapsedMs & " ms"
/// Next
/// ```
///
/// **Equivalent to:**
/// ```
/// sw.Reset()
/// sw.Start()
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @note O(1) time complexity.
/// @note After restart: Elapsed = 0, IsRunning = true
/// @note Useful for repeated measurements in a loop.
///
/// @see rt_stopwatch_reset For resetting without starting
/// @see rt_stopwatch_start For starting without resetting
void rt_stopwatch_restart(void *obj) {
    ViperStopwatch *sw = require_stopwatch(obj);

    sw->accumulated_ns = 0;
    sw->start_time_ns = get_timestamp_ns();
    sw->running = true;
}

/// @brief Gets the total elapsed time in nanoseconds.
///
/// Returns the total accumulated elapsed time with nanosecond precision.
/// If the stopwatch is running, includes the time since it was started.
///
/// **Time conversion reference:**
/// ```
/// nanoseconds / 1,000         = microseconds
/// nanoseconds / 1,000,000     = milliseconds
/// nanoseconds / 1,000,000,000 = seconds
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @return Total elapsed time in nanoseconds.
///
/// @note O(1) time complexity.
/// @note Can be called while running (returns current elapsed time).
/// @note Maximum measurable time: ~292 years at nanosecond precision.
///
/// @see rt_stopwatch_elapsed_us For microseconds
/// @see rt_stopwatch_elapsed_ms For milliseconds
int64_t rt_stopwatch_elapsed_ns(void *obj) {
    return stopwatch_get_elapsed_ns(require_stopwatch(obj));
}

/// @brief Gets the total elapsed time in microseconds.
///
/// Returns the total accumulated elapsed time in microseconds (μs).
/// One microsecond is one millionth of a second.
///
/// **Useful for:**
/// - Measuring fast operations (< 1ms)
/// - Higher precision than milliseconds
/// - Database query timing
/// - API call latency
///
/// @param obj Pointer to a Stopwatch object.
///
/// @return Total elapsed time in microseconds.
///
/// @note O(1) time complexity.
/// @note Truncates (does not round) nanoseconds.
///
/// @see rt_stopwatch_elapsed_ns For nanoseconds (highest precision)
/// @see rt_stopwatch_elapsed_ms For milliseconds
int64_t rt_stopwatch_elapsed_us(void *obj) {
    return stopwatch_get_elapsed_ns(require_stopwatch(obj)) / 1000;
}

/// @brief Gets the total elapsed time in milliseconds.
///
/// Returns the total accumulated elapsed time in milliseconds (ms).
/// This is the most commonly used time unit for performance measurement.
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.StartNew()
/// LoadDataFromDatabase()
/// sw.Stop()
///
/// If sw.ElapsedMs > 1000 Then
///     Print "Warning: Query took " & sw.ElapsedMs & " ms"
/// End If
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @return Total elapsed time in milliseconds.
///
/// @note O(1) time complexity.
/// @note Truncates (does not round) nanoseconds.
///
/// @see rt_stopwatch_elapsed_ns For nanoseconds (highest precision)
/// @see rt_stopwatch_elapsed_us For microseconds
int64_t rt_stopwatch_elapsed_ms(void *obj) {
    return stopwatch_get_elapsed_ns(require_stopwatch(obj)) / 1000000;
}

/// @brief Checks if the stopwatch is currently running.
///
/// Returns true if the stopwatch is actively tracking time (Start() was
/// called and Stop() has not been called since).
///
/// **Usage example:**
/// ```
/// Dim sw = Stopwatch.New()
/// Print sw.IsRunning          ' False
///
/// sw.Start()
/// Print sw.IsRunning          ' True
///
/// sw.Stop()
/// Print sw.IsRunning          ' False
/// ```
///
/// @param obj Pointer to a Stopwatch object.
///
/// @return 1 (true) if running, 0 (false) if stopped.
///
/// @note O(1) time complexity.
///
/// @see rt_stopwatch_start For starting the stopwatch
/// @see rt_stopwatch_stop For stopping the stopwatch
int8_t rt_stopwatch_is_running(void *obj) {
    return require_stopwatch(obj)->running ? 1 : 0;
}
