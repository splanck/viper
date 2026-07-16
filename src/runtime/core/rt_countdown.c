//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_countdown.c
// Purpose: Implements the Countdown timer class for the Viper runtime.
//          A countdown tracks elapsed time against a fixed target interval and
//          exposes HasExpired/Remaining/Reset semantics for timeouts, cooldowns,
//          rate limiters, and game turn timers.
//
// Key invariants:
//   - Time is measured in milliseconds using the platform monotonic clock.
//     When the monotonic clock is unavailable and POSIX falls back to
//     CLOCK_REALTIME, the reading is clamped to a process-local atomic floor
//     (CAS-max) so the deadline cannot recede on a backward clock adjustment
//     (VDOC-223).
//   - A countdown in the STOPPED state accumulates no elapsed time.
//   - Elapsed time is the sum of accumulated milliseconds from completed
//     intervals plus the current running interval (if any).
//   - HasExpired returns true when elapsed >= interval_ms. A never-started
//     positive countdown is not expired, while a zero interval is immediately expired.
//   - Countdown objects are not thread-safe; callers must synchronize externally.
//
// Ownership/Lifetime:
//   - Countdown instances are heap-allocated via rt_obj_new_i64 and managed
//     by the runtime GC; no manual free is required by callers.
//   - The internal ViperCountdown struct contains no pointers to external
//     resources; finalizer is a no-op.
//
// Links: src/runtime/core/rt_countdown.h (public API),
//        src/runtime/core/rt_stopwatch.c (counts up instead of down),
//        src/runtime/core/rt_time.c (platform clock helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_countdown.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_time.h"
#include "rt_trap.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "rt_atomic_compat.h"

/// @brief Return the QPC frequency, caching it through an atomic slot.
/// @details `QueryPerformanceFrequency` is fixed for the life of the system, so
///          the value can be cached process-wide. Reading and publishing the
///          cache slot with acquire/release atomics makes concurrent first use
///          well-defined instead of a C data race (VDOC-224); duplicate stores
///          write the identical constant, so no once primitive is needed.
/// @return QPC ticks-per-second, or 0 when the counter is unavailable.
static int64_t countdown_qpc_freq(void) {
    static int64_t cached = 0; // 0 = not yet resolved
    int64_t freq = __atomic_load_n(&cached, __ATOMIC_ACQUIRE);
    if (freq != 0)
        return freq;
    LARGE_INTEGER f;
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0)
        return 0;
    __atomic_store_n(&cached, (int64_t)f.QuadPart, __ATOMIC_RELEASE);
    return (int64_t)f.QuadPart;
}
#else
#include <time.h>
#endif

/// @brief Internal countdown structure.
typedef struct {
    int64_t interval_ms;    ///< Target interval duration in milliseconds.
    int64_t accumulated_ms; ///< Total accumulated ms from completed intervals.
    int64_t start_time_ms;  ///< Timestamp when current interval started (if running).
    bool running;           ///< True if countdown is currently timing.
} ViperCountdown;

/// @brief Overflow-checked signed 64-bit addition. Returns 1 on overflow, 0 on success.
/// @details Used by the countdown deadline math so a malformed timer setup can be reported
///          via `rt_trap_ovf()` instead of silently wrapping. The check is performed
///          *before* the add to avoid signed-overflow UB.
static int countdown_checked_add_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return 1;
    *out = a + b;
    return 0;
}

/// @brief Overflow-checked signed 64-bit subtraction. Returns 1 on overflow, 0 on success.
static int countdown_checked_sub_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        return 1;
    *out = a - b;
    return 0;
}

/// @brief Overflow-checked signed 64-bit multiplication.
/// @details Uses `__builtin_mul_overflow` on GCC/Clang and a manual divide-bound check on
///          MSVC. Returns 1 on overflow without writing @p out, 0 on success after writing
///          the product to @p out.
static int countdown_checked_mul_i64(int64_t a, int64_t b, int64_t *out) {
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

/// @brief Win32: read the monotonic millisecond tick count, trapping on >INT64_MAX.
/// @details `GetTickCount64` returns a `ULONGLONG`; clamp at `INT64_MAX` since the rest
///          of the countdown machinery uses signed 64-bit math and would interpret
///          high values as negative.
#if defined(_WIN32)
static int64_t countdown_tick_count_ms(void) {
    ULONGLONG ticks = GetTickCount64();
    if (ticks > (ULONGLONG)INT64_MAX) {
        rt_trap_ovf();
        return 0;
    }
    return (int64_t)ticks;
}
#endif

/// @brief Validate that @p obj is a live Countdown receiver, trapping otherwise.
/// @details Centralises the receiver guard so every public Countdown method reads
///          like `ViperCountdown *cd = require_countdown(obj); if (!cd) return ...;`.
///          Verifies the heap kind, class ID, and payload size via
///          rt_obj_is_instance so a null receiver *or* an unrelated object (e.g. a
///          Seq passed to the static compatibility form) traps instead of being
///          reinterpreted as a Countdown payload (VDOC-229).
static ViperCountdown *require_countdown(void *obj) {
    if (!rt_obj_is_instance(obj, RT_COUNTDOWN_CLASS_ID, sizeof(ViperCountdown))) {
        rt_trap("Countdown: invalid receiver");
        return NULL;
    }
    return (ViperCountdown *)obj;
}

/// @brief POSIX: convert a `struct timespec` into a millisecond `int64_t`, trapping on overflow.
/// @details Multiplies `tv_sec * 1000` through `countdown_checked_mul_i64` and adds
///          `tv_nsec / 1000000` through `countdown_checked_add_i64` so an absurdly large
///          timespec can't silently wrap. Excludes ViperDOS where `clock_gettime` is
///          unavailable.
#if !defined(_WIN32) && !defined(__viperdos__)
static int64_t countdown_timespec_to_ms(struct timespec ts) {
    int64_t seconds_ms;
    int64_t result;
    if (countdown_checked_mul_i64((int64_t)ts.tv_sec, 1000LL, &seconds_ms) ||
        countdown_checked_add_i64(seconds_ms, (int64_t)ts.tv_nsec / 1000000LL, &result)) {
        rt_trap_ovf();
        return 0;
    }
    return result;
}
#endif

/// @brief Get current timestamp in milliseconds from monotonic clock.
/// @return Milliseconds since unspecified epoch.
static int64_t get_timestamp_ms(void) {
#if defined(_WIN32)
    // QPC frequency is constant; cache it through an atomic slot so concurrent
    // first use is defined rather than a data race (VDOC-224).
    int64_t freq = countdown_qpc_freq();
    if (freq == 0)
        return countdown_tick_count_ms();

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
        return countdown_tick_count_ms();

    int64_t whole = counter.QuadPart / freq;
    int64_t rem = counter.QuadPart % freq;
    if (whole > INT64_MAX / 1000LL) {
        rt_trap_ovf();
        return 0;
    }
    int64_t whole_ms = whole * 1000LL;
    int64_t rem_ms = (int64_t)(((long double)rem * 1000.0L) / (long double)freq);
    int64_t result;
    if (countdown_checked_add_i64(whole_ms, rem_ms, &result)) {
        rt_trap_ovf();
        return 0;
    }
    return result;
#elif defined(__viperdos__)
    return rt_timer_ms();
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return countdown_timespec_to_ms(ts);
    }
#endif
    // Fallback to CLOCK_REALTIME. Ratchet the wall-clock reading through a
    // process-local atomic floor so a backward system-clock adjustment on this
    // failure path cannot make the countdown deadline recede (VDOC-223).
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        static int64_t floor_ms = 0;
        return rt_time_monotonic_ratchet(&floor_ms, countdown_timespec_to_ms(ts));
    }
    rt_trap("Countdown: clock unavailable");
    return 0;
#endif
}

/// @brief Internal helper to get total elapsed milliseconds.
/// @param cd Countdown pointer.
/// @return Total elapsed milliseconds including current interval if running.
static int64_t countdown_get_elapsed_ms(ViperCountdown *cd) {
    int64_t total = cd->accumulated_ms;

    if (cd->running) {
        int64_t interval;
        if (countdown_checked_sub_i64(get_timestamp_ms(), cd->start_time_ms, &interval) ||
            countdown_checked_add_i64(total, interval, &total)) {
            rt_trap_ovf();
            return 0;
        }
    }

    return total;
}

/// @brief Sleep for the specified number of milliseconds.
/// @param ms Duration to sleep.
static void sleep_ms(int64_t ms) {
    if (ms <= 0)
        return;

    rt_clock_sleep(ms);
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Creates a new countdown timer with the specified interval.
///
/// Allocates and initializes a Countdown object that will expire after the
/// given number of milliseconds of running time.
///
/// The countdown starts in a stopped state. Call Start() to begin timing.
///
/// **Usage example:**
/// ```
/// ' Create a 5-second countdown
/// Dim timer = Countdown.New(5000)
/// timer.Start()
///
/// ' Game loop
/// Do While Not timer.Expired
///     Print "Time remaining: " & timer.Remaining & "ms"
///     ' ... game logic ...
/// Loop
/// Print "Time's up!"
/// ```
///
/// @param interval_ms The countdown interval in milliseconds. If <= 0, the
///                    countdown will be expired immediately upon starting.
///
/// @return A new Countdown object in stopped state. Traps on allocation failure.
///
/// @note O(1) time complexity.
/// @note The returned countdown is reference-counted and garbage collected.
///
/// @see rt_countdown_start For starting the countdown
/// @see rt_countdown_remaining For checking time left
/// @see rt_countdown_expired For checking if expired
void *rt_countdown_new(int64_t interval_ms) {
    ViperCountdown *cd =
        (ViperCountdown *)rt_obj_new_i64(RT_COUNTDOWN_CLASS_ID, (int64_t)sizeof(ViperCountdown));
    if (!cd) {
        rt_trap("Countdown: memory allocation failed");
        return NULL; // Unreachable after trap
    }

    cd->interval_ms = interval_ms > 0 ? interval_ms : 0;
    cd->accumulated_ms = 0;
    cd->start_time_ms = 0;
    cd->running = false;

    return cd;
}

/// @brief Starts or resumes the countdown timer.
///
/// Begins tracking elapsed time toward the interval. If the countdown is
/// already running, this function has no effect.
///
/// **State transitions:**
/// - Stopped → Running
/// - Running → Running (no change)
///
/// **Usage example:**
/// ```
/// Dim cooldown = Countdown.New(3000)  ' 3 second cooldown
/// cooldown.Start()
///
/// ' ... later, after ability is used ...
/// cooldown.Reset()
/// cooldown.Start()  ' Restart the cooldown
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @note O(1) time complexity.
/// @note Has no effect if already running.
///
/// @see rt_countdown_stop For pausing the countdown
/// @see rt_countdown_reset For resetting to initial state
void rt_countdown_start(void *obj) {
    ViperCountdown *cd = require_countdown(obj);

    if (!cd->running) {
        cd->start_time_ms = get_timestamp_ms();
        cd->running = true;
    }
}

/// @brief Stops (pauses) the countdown timer.
///
/// Pauses time tracking while preserving the elapsed time. The countdown
/// can be resumed later with Start(). If already stopped, has no effect.
///
/// **State transitions:**
/// - Running → Stopped
/// - Stopped → Stopped (no change)
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(60000)  ' 1 minute timer
/// timer.Start()
///
/// ' Game paused
/// timer.Stop()
/// ' ... pause menu ...
///
/// ' Game resumed
/// timer.Start()  ' Continues from where it left off
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @note O(1) time complexity.
/// @note Preserves accumulated elapsed time.
///
/// @see rt_countdown_start For resuming the countdown
/// @see rt_countdown_reset For clearing elapsed time
void rt_countdown_stop(void *obj) {
    ViperCountdown *cd = require_countdown(obj);

    if (cd->running) {
        int64_t now = get_timestamp_ms();
        int64_t interval;
        if (countdown_checked_sub_i64(now, cd->start_time_ms, &interval) ||
            countdown_checked_add_i64(cd->accumulated_ms, interval, &cd->accumulated_ms)) {
            rt_trap_ovf();
            return;
        }
        cd->running = false;
    }
}

/// @brief Resets the countdown to its initial state.
///
/// Clears all accumulated elapsed time and stops the countdown. After reset,
/// the countdown is as if it were newly created (but with the same interval).
///
/// **Usage example:**
/// ```
/// Dim respawnTimer = Countdown.New(5000)
/// respawnTimer.Start()
///
/// ' Player respawns
/// respawnTimer.Reset()   ' Ready for next death
///
/// ' Later, player dies again
/// respawnTimer.Start()   ' Full 5 seconds again
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @note O(1) time complexity.
/// @note After reset: Elapsed = 0, Remaining = Interval, Expired = false
///
/// @see rt_countdown_start For starting after reset
/// @see rt_countdown_set_interval For changing the interval
void rt_countdown_reset(void *obj) {
    ViperCountdown *cd = require_countdown(obj);

    cd->accumulated_ms = 0;
    cd->start_time_ms = 0;
    cd->running = false;
}

/// @brief Gets the total elapsed time in milliseconds.
///
/// Returns how much time has passed since the countdown started (including
/// time from previous start/stop cycles if the countdown was paused).
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(10000)  ' 10 seconds
/// timer.Start()
///
/// ' ... 3 seconds later ...
/// Print timer.Elapsed    ' ~3000
/// Print timer.Remaining  ' ~7000
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @return Total elapsed milliseconds since start (accumulates across pauses).
///
/// @note O(1) time complexity.
/// @note Continues to increase even after expiration.
///
/// @see rt_countdown_remaining For time left until expiration
/// @see rt_countdown_expired For checking if time has run out
int64_t rt_countdown_elapsed(void *obj) {
    return countdown_get_elapsed_ms(require_countdown(obj));
}

/// @brief Gets the remaining time until expiration in milliseconds.
///
/// Returns how much time is left before the countdown expires. Returns 0
/// if the countdown has already expired (does not return negative values).
///
/// **Formula:** Remaining = max(0, Interval - Elapsed)
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(5000)  ' 5 seconds
/// timer.Start()
///
/// Do While timer.Remaining > 0
///     Print "Time left: " & (timer.Remaining / 1000) & " seconds"
///     Sleep(1000)
/// Loop
/// Print "Expired!"
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @return Remaining milliseconds (>= 0). Returns 0 if expired.
///
/// @note O(1) time complexity.
/// @note Never returns negative values.
///
/// @see rt_countdown_elapsed For total time passed
/// @see rt_countdown_expired For boolean expiration check
int64_t rt_countdown_remaining(void *obj) {
    ViperCountdown *cd = require_countdown(obj);
    int64_t elapsed = countdown_get_elapsed_ms(cd);
    int64_t remaining;
    if (countdown_checked_sub_i64(cd->interval_ms, elapsed, &remaining)) {
        rt_trap_ovf();
        return 0;
    }
    return remaining > 0 ? remaining : 0;
}

/// @brief Checks if the countdown has expired.
///
/// Returns true if the elapsed time has reached or exceeded the interval.
/// The countdown can continue running after expiration (Elapsed will keep
/// increasing).
///
/// **Usage example:**
/// ```
/// Dim ability = Countdown.New(3000)  ' 3 second cooldown
/// ability.Start()
///
/// Sub UseAbility()
///     If ability.Expired Then
///         ' Use the ability
///         ability.Reset()
///         ability.Start()
///     Else
///         Print "Cooldown: " & ability.Remaining & "ms"
///     End If
/// End Sub
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @return 1 (true) if Elapsed >= Interval, 0 (false) otherwise.
///
/// @note O(1) time complexity.
/// @note A stopped countdown can still be expired if it ran long enough.
///
/// @see rt_countdown_remaining For checking exact time left
/// @see rt_countdown_reset For restarting after expiration
int8_t rt_countdown_expired(void *obj) {
    ViperCountdown *cd = require_countdown(obj);
    int64_t elapsed = countdown_get_elapsed_ms(cd);
    return elapsed >= cd->interval_ms ? 1 : 0;
}

/// @brief Gets the countdown interval in milliseconds.
///
/// Returns the target duration that was set when the countdown was created
/// or last modified with SetInterval().
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(5000)
/// Print timer.Interval   ' 5000
///
/// timer.SetInterval(10000)
/// Print timer.Interval   ' 10000
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @return The interval in milliseconds.
///
/// @note O(1) time complexity.
///
/// @see rt_countdown_set_interval For changing the interval
/// @see rt_countdown_remaining For time left
int64_t rt_countdown_interval(void *obj) {
    return require_countdown(obj)->interval_ms;
}

/// @brief Sets a new countdown interval.
///
/// Changes the target duration of the countdown. This takes effect
/// immediately for remaining time calculations.
///
/// **Note:** This does not reset the elapsed time. To start a fresh
/// countdown with the new interval, call Reset() then Start().
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(5000)
/// timer.Start()
///
/// ' ... game difficulty increases ...
/// timer.SetInterval(3000)   ' Less time now!
///
/// ' Or start fresh with new interval:
/// timer.SetInterval(3000)
/// timer.Reset()
/// timer.Start()
/// ```
///
/// @param obj Pointer to a Countdown object.
/// @param interval_ms New interval in milliseconds. If <= 0, sets to 0.
///
/// @note O(1) time complexity.
/// @note Does not reset elapsed time.
///
/// @see rt_countdown_interval For getting the current interval
/// @see rt_countdown_reset For resetting elapsed time
void rt_countdown_set_interval(void *obj, int64_t interval_ms) {
    require_countdown(obj)->interval_ms = interval_ms > 0 ? interval_ms : 0;
}

/// @brief Checks if the countdown is currently running.
///
/// Returns true if the countdown is actively tracking time (Start() was
/// called and Stop() has not been called since).
///
/// **Usage example:**
/// ```
/// Dim timer = Countdown.New(5000)
/// Print timer.IsRunning  ' False
///
/// timer.Start()
/// Print timer.IsRunning  ' True
///
/// timer.Stop()
/// Print timer.IsRunning  ' False
/// ```
///
/// @param obj Pointer to a Countdown object.
///
/// @return 1 (true) if running, 0 (false) if stopped.
///
/// @note O(1) time complexity.
///
/// @see rt_countdown_start For starting the timer
/// @see rt_countdown_stop For stopping the timer
int8_t rt_countdown_is_running(void *obj) {
    return require_countdown(obj)->running ? 1 : 0;
}

/// @brief Blocks execution until the countdown expires.
///
/// If the countdown is not running, starts it first. Then sleeps until
/// the remaining time reaches zero. After Wait() returns, the countdown
/// will be expired.
///
/// **Usage example:**
/// ```
/// ' Simple delay
/// Dim delay = Countdown.New(2000)
/// delay.Wait()   ' Blocks for 2 seconds
/// Print "Delay complete!"
///
/// ' Rate limiting
/// Dim limiter = Countdown.New(100)  ' 100ms between calls
/// Do While True
///     limiter.Reset()
///     ProcessRequest()
///     limiter.Wait()   ' Ensures at least 100ms between requests
/// Loop
/// ```
///
/// **Caution:** This blocks the current thread. For non-blocking behavior,
/// check Expired() in a loop instead.
///
/// @param obj Pointer to a Countdown object.
///
/// @note Blocks the calling thread.
/// @note Automatically starts the countdown if not running.
/// @note Returns immediately if already expired.
///
/// @see rt_countdown_remaining For non-blocking time checks
/// @see rt_countdown_expired For non-blocking expiration checks
void rt_countdown_wait(void *obj) {
    ViperCountdown *cd = require_countdown(obj);

    // Start if not running
    if (!cd->running) {
        rt_countdown_start(obj);
    }

    int64_t remaining;
    while ((remaining = rt_countdown_remaining(obj)) > 0) {
        sleep_ms(remaining);
    }
}
