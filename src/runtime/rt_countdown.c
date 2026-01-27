//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_countdown.c
/// @brief Countdown timer for interval timing with expiration detection.
///
/// This file implements a countdown timer that tracks elapsed time against a
/// target interval. The Countdown class is useful for implementing timeouts,
/// cooldowns, delays, and rate limiting in applications and games.
///
/// **Countdown vs Stopwatch:**
/// - Stopwatch: Measures elapsed time (counts up from 0)
/// - Countdown: Tracks time until expiration (counts down to 0)
///
/// **Timer States:**
/// ```
/// ┌─────────────────────────────────────────────────────────────┐
/// │                                                             │
/// │  STOPPED ─────► RUNNING ─────► EXPIRED                      │
/// │     │              │              │                         │
/// │     │              │              │                         │
/// │     ◄──────────────┴──────────────┘                         │
/// │           Reset()                                           │
/// └─────────────────────────────────────────────────────────────┘
/// ```
///
/// **Time Tracking:**
/// ```
/// Start      Now                    Interval End
///   │         │                          │
///   ▼         ▼                          ▼
///   ├─────────┼──────────────────────────┤
///   │ Elapsed │       Remaining          │
///   └─────────┴──────────────────────────┘
///            ◄──────────────────────────►
///                     Interval
/// ```
///
/// **Use Cases:**
/// - Game cooldowns (ability cooldowns, spawn timers)
/// - Timeouts (network requests, user input)
/// - Rate limiting (API calls, actions per minute)
/// - Delays (screen transitions, animations)
/// - Turn timers (chess clocks, quiz timers)
///
/// **Thread Safety:** Countdown objects are not thread-safe. External
/// synchronization is required for multi-threaded access.
///
/// @see rt_stopwatch.c For measuring elapsed time (counting up)
///
//===----------------------------------------------------------------------===//

#include "rt_countdown.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

/// @brief Internal countdown structure.
typedef struct
{
    int64_t interval_ms;    ///< Target interval duration in milliseconds.
    int64_t accumulated_ms; ///< Total accumulated ms from completed intervals.
    int64_t start_time_ms;  ///< Timestamp when current interval started (if running).
    bool running;           ///< True if countdown is currently timing.
} ViperCountdown;

/// @brief Get current timestamp in milliseconds from monotonic clock.
/// @return Milliseconds since unspecified epoch.
static int64_t get_timestamp_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000LL) / freq.QuadPart);
#elif defined(__viperdos__)
    // ViperDOS: Use rt_timer_ms from rt_time.c
    extern int64_t rt_timer_ms(void);
    return rt_timer_ms();
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    }
#endif
    // Fallback to CLOCK_REALTIME
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    }
    return 0;
#endif
}

/// @brief Internal helper to get total elapsed milliseconds.
/// @param cd Countdown pointer.
/// @return Total elapsed milliseconds including current interval if running.
static int64_t countdown_get_elapsed_ms(ViperCountdown *cd)
{
    int64_t total = cd->accumulated_ms;

    if (cd->running)
    {
        total += get_timestamp_ms() - cd->start_time_ms;
    }

    return total;
}

/// @brief Sleep for the specified number of milliseconds.
/// @param ms Duration to sleep.
static void sleep_ms(int64_t ms)
{
    if (ms <= 0)
        return;

#if defined(_WIN32)
    Sleep((DWORD)ms);
#elif defined(__viperdos__)
    // ViperDOS: Use rt_sleep_ms from rt_time.c
    extern void rt_sleep_ms(int32_t ms);
    rt_sleep_ms((int32_t)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
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
void *rt_countdown_new(int64_t interval_ms)
{
    ViperCountdown *cd = (ViperCountdown *)rt_obj_new_i64(0, (int64_t)sizeof(ViperCountdown));
    if (!cd)
    {
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
void rt_countdown_start(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

    if (!cd->running)
    {
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
void rt_countdown_stop(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

    if (cd->running)
    {
        int64_t now = get_timestamp_ms();
        cd->accumulated_ms += now - cd->start_time_ms;
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
void rt_countdown_reset(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

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
int64_t rt_countdown_elapsed(void *obj)
{
    return countdown_get_elapsed_ms((ViperCountdown *)obj);
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
int64_t rt_countdown_remaining(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;
    int64_t elapsed = countdown_get_elapsed_ms(cd);
    int64_t remaining = cd->interval_ms - elapsed;
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
int8_t rt_countdown_expired(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;
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
int64_t rt_countdown_interval(void *obj)
{
    return ((ViperCountdown *)obj)->interval_ms;
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
void rt_countdown_set_interval(void *obj, int64_t interval_ms)
{
    ((ViperCountdown *)obj)->interval_ms = interval_ms > 0 ? interval_ms : 0;
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
int8_t rt_countdown_is_running(void *obj)
{
    return ((ViperCountdown *)obj)->running ? 1 : 0;
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
void rt_countdown_wait(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

    // Start if not running
    if (!cd->running)
    {
        rt_countdown_start(obj);
    }

    // Get remaining time and sleep
    int64_t remaining = rt_countdown_remaining(obj);
    if (remaining > 0)
    {
        sleep_ms(remaining);
    }
}
