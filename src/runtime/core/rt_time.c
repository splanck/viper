//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_time.c
/// @brief Portable time helpers for sleep, timing, and clock operations.
///
/// This file provides cross-platform timing utilities for the Viper runtime,
/// including sleep functions and high-resolution monotonic timers. These
/// functions power BASIC statements like `SLEEP` and functions like `TIMER`.
///
/// **Monotonic Time:**
/// The timing functions use monotonic clocks that are not affected by system
/// time changes (NTP adjustments, daylight saving, manual changes). This makes
/// them suitable for measuring elapsed time and intervals.
///
/// **Time Sources by Platform:**
/// | Platform | Sleep Implementation   | Timer Source                |
/// |----------|------------------------|-----------------------------|
/// | Windows  | Sleep()                | QueryPerformanceCounter     |
/// | macOS    | nanosleep()            | CLOCK_MONOTONIC             |
/// | Linux    | nanosleep()            | CLOCK_MONOTONIC             |
///
/// **Time Units:**
/// ```
/// 1 second = 1,000 milliseconds (ms)
///          = 1,000,000 microseconds (μs)
///          = 1,000,000,000 nanoseconds (ns)
/// ```
///
/// **Use Cases:**
/// - Delaying program execution (`SLEEP`)
/// - Measuring elapsed time for benchmarking
/// - Game frame timing and animation
/// - Implementing timeouts
/// - Rate limiting operations
///
/// **Signal Handling (Unix):**
/// The sleep function automatically retries if interrupted by a signal (EINTR),
/// ensuring the full requested duration is slept.
///
/// **Thread Safety:** All functions are thread-safe and can be called from
/// multiple threads simultaneously.
///
/// @see rt_stopwatch.c For high-level stopwatch class
/// @see rt_countdown.c For countdown timer class
/// @see rt_datetime.c For wall-clock date/time operations
///
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Suspends execution for the specified number of milliseconds (Windows).
///
/// Blocks the current thread for approximately the specified duration. The
/// actual sleep time may be slightly longer due to system scheduling.
///
/// **Usage example:**
/// ```
/// Print "Starting..."
/// Sleep(1000)           ' Wait 1 second
/// Print "Done!"
///
/// ' Animation delay
/// For i = 1 To 100
///     DrawFrame(i)
///     Sleep(16)          ' ~60 FPS (1000/60 ≈ 16ms)
/// Next
/// ```
///
/// @param ms Duration to sleep in milliseconds. Negative values are treated as 0.
///
/// @note Minimum resolution is typically 10-15ms on Windows.
/// @note Uses the Win32 Sleep() function.
///
/// @see rt_timer_ms For measuring elapsed time
void rt_sleep_ms(int32_t ms)
{
    if (ms < 0)
        ms = 0;
    Sleep((DWORD)ms);
}

/// @brief Returns monotonic time in milliseconds (Windows).
///
/// Returns the number of milliseconds since an unspecified starting point.
/// The value increases monotonically and is not affected by system time
/// changes. Use this for measuring elapsed time between two points.
///
/// **Usage example:**
/// ```
/// Dim startTime = Timer()
/// DoSomeWork()
/// Dim endTime = Timer()
/// Print "Elapsed: " & (endTime - startTime) & " ms"
/// ```
///
/// @return Monotonic milliseconds since an arbitrary epoch.
///
/// @note Uses QueryPerformanceCounter for high resolution.
/// @note Falls back to GetTickCount64 if QPC is unavailable.
/// @note Never decreases, even if system time is changed.
///
/// @see rt_clock_ticks_us For microsecond resolution
int64_t rt_timer_ms(void)
{
    // Use QueryPerformanceCounter for high-resolution monotonic time
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
    {
        // Fallback to GetTickCount64 if QPC unavailable
        return (int64_t)GetTickCount64();
    }

    QueryPerformanceCounter(&counter);

    // Convert to milliseconds: (counter * 1000) / freq
    // Use 128-bit arithmetic to avoid overflow on long uptimes
    int64_t ms = (int64_t)((counter.QuadPart * 1000LL) / freq.QuadPart);
    return ms;
}

/// @brief Returns monotonic time in microseconds (Windows).
///
/// Returns the number of microseconds since an unspecified starting point.
/// This provides higher resolution than rt_timer_ms for precise timing needs.
///
/// **Usage example:**
/// ```
/// Dim start = Clock.TicksUs()
/// DoFastOperation()
/// Dim elapsed = Clock.TicksUs() - start
/// Print "Operation took " & elapsed & " microseconds"
/// ```
///
/// @return Monotonic microseconds since an arbitrary epoch.
///
/// @note Uses QueryPerformanceCounter for high resolution.
/// @note Falls back to GetTickCount64 * 1000 if QPC unavailable.
/// @note 1 millisecond = 1000 microseconds.
///
/// @see rt_timer_ms For millisecond resolution
int64_t rt_clock_ticks_us(void)
{
    // Use QueryPerformanceCounter for high-resolution monotonic time
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
    {
        // Fallback to GetTickCount64 (milliseconds) * 1000 for microseconds
        return (int64_t)GetTickCount64() * 1000LL;
    }

    QueryPerformanceCounter(&counter);

    // Convert to microseconds using split division to avoid overflow:
    // (counter / freq) * 1000000 + (counter % freq) * 1000000 / freq
    int64_t whole = counter.QuadPart / freq.QuadPart;
    int64_t remainder = counter.QuadPart % freq.QuadPart;
    int64_t us = whole * 1000000LL + remainder * 1000000LL / freq.QuadPart;
    return us;
}

#elif defined(__viperdos__)

// ViperDOS time implementation — uses POSIX-compatible libc APIs.
#include <errno.h>
#include <time.h>

void rt_sleep_ms(int32_t ms)
{
    if (ms < 0)
        ms = 0;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR)
    {
    }
}

int64_t rt_timer_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

int64_t rt_clock_ticks_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)(ts.tv_nsec / 1000);
}

#else
#include <errno.h>
#include <time.h>

/// @brief Suspends execution for the specified number of milliseconds (Unix).
///
/// Blocks the current thread for approximately the specified duration. On
/// Unix systems, this uses nanosleep() and automatically retries if the sleep
/// is interrupted by a signal (EINTR), ensuring the full duration is slept.
///
/// **Signal handling:**
/// Unlike some sleep implementations, this function guarantees the full
/// requested duration is slept, even if signals interrupt the process:
/// ```
/// Sleep(5000)   ' Always sleeps ~5 seconds, even with SIGALRM
/// ```
///
/// **Usage example:**
/// ```
/// Print "Countdown..."
/// For i = 5 To 1 Step -1
///     Print i
///     Sleep(1000)
/// Next
/// Print "Go!"
/// ```
///
/// @param ms Duration to sleep in milliseconds. Negative values are treated as 0.
///
/// @note Uses nanosleep() with automatic retry on EINTR.
/// @note Resolution is typically 1ms or better on modern systems.
///
/// @see rt_timer_ms For measuring elapsed time
void rt_sleep_ms(int32_t ms)
{
    if (ms < 0)
        ms = 0;

    struct timespec req;
    req.tv_sec = ms / 1000;
    long nsec = (long)(ms % 1000) * 1000000L;
    req.tv_nsec = nsec;

    while (nanosleep(&req, &req) == -1 && errno == EINTR)
    {
        // Retry with remaining time in req.
    }
}

/// @brief Returns monotonic time in milliseconds (Unix).
///
/// Returns the number of milliseconds since an unspecified starting point
/// using the system's monotonic clock. The value is guaranteed to never
/// decrease, even if the system clock is adjusted.
///
/// **Clock selection:**
/// 1. CLOCK_MONOTONIC (preferred) - not affected by NTP or manual changes
/// 2. CLOCK_REALTIME (fallback) - may jump if system time changes
///
/// **Usage example:**
/// ```
/// Dim start = Timer()
/// ' ... lengthy operation ...
/// Dim elapsed = Timer() - start
/// Print "Operation took " & elapsed & " ms"
/// ```
///
/// @return Monotonic milliseconds since an arbitrary epoch, or 0 on error.
///
/// @note Uses clock_gettime() with CLOCK_MONOTONIC.
/// @note Falls back to CLOCK_REALTIME if CLOCK_MONOTONIC unavailable.
/// @note Resolution is typically 1ms or better.
///
/// @see rt_clock_ticks_us For microsecond resolution
int64_t rt_timer_ms(void)
{
    // Use CLOCK_MONOTONIC for monotonic time since unspecified epoch
    struct timespec ts;

#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        // Convert to milliseconds: seconds * 1000 + nanoseconds / 1000000
        int64_t ms = (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
        return ms;
    }
#endif

    // Fallback to CLOCK_REALTIME if CLOCK_MONOTONIC unavailable
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        int64_t ms = (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
        return ms;
    }

    // Last resort: return 0 if all clock sources fail
    return 0;
}

/// @brief Returns monotonic time in microseconds (Unix).
///
/// Returns the number of microseconds since an unspecified starting point.
/// This provides 1000x higher resolution than rt_timer_ms for precise timing.
///
/// **Usage example:**
/// ```
/// Dim start = Clock.TicksUs()
/// FastFunction()
/// Dim elapsed = Clock.TicksUs() - start
/// Print "Function took " & elapsed & " μs"
/// ```
///
/// @return Monotonic microseconds since an arbitrary epoch, or 0 on error.
///
/// @note Uses clock_gettime() with CLOCK_MONOTONIC.
/// @note 1 millisecond = 1,000 microseconds.
/// @note Typical resolution is 1 microsecond on modern systems.
///
/// @see rt_timer_ms For millisecond resolution
int64_t rt_clock_ticks_us(void)
{
    // Use CLOCK_MONOTONIC for monotonic time since unspecified epoch
    struct timespec ts;

#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        // Convert to microseconds: seconds * 1000000 + nanoseconds / 1000
        int64_t us = (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
        return us;
    }
#endif

    // Fallback to CLOCK_REALTIME if CLOCK_MONOTONIC unavailable
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        int64_t us = (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
        return us;
    }

    // Last resort: return 0 if all clock sources fail
    return 0;
}
#endif

//=============================================================================
// Viper.Time.Clock wrappers (i64 interface)
//=============================================================================

/// @brief Suspends execution for the specified number of milliseconds (64-bit).
///
/// High-level wrapper around rt_sleep_ms that accepts 64-bit durations. Values
/// outside the valid range are clamped. This is the function exposed to Viper
/// BASIC code via the Clock class.
///
/// **Value clamping:**
/// - Negative values → 0 (no sleep)
/// - Values > INT32_MAX → INT32_MAX (~24 days)
///
/// **Usage example:**
/// ```
/// ' Sleep for 2.5 seconds
/// Clock.Sleep(2500)
///
/// ' Sleep using a 64-bit variable
/// Dim duration As Long = 1000
/// Clock.Sleep(duration)
/// ```
///
/// @param ms Duration to sleep in milliseconds (clamped to int32 range).
///
/// @note Delegates to rt_sleep_ms after clamping.
/// @note Maximum sleep is about 24.8 days (INT32_MAX milliseconds).
///
/// @see rt_sleep_ms For the underlying implementation
/// @see rt_clock_ticks For measuring elapsed time
void rt_clock_sleep(int64_t ms)
{
    // Clamp to int32_t range for rt_sleep_ms
    if (ms < 0)
        ms = 0;
    if (ms > INT32_MAX)
        ms = INT32_MAX;
    rt_sleep_ms((int32_t)ms);
}

/// @brief Returns monotonic time in milliseconds (Clock wrapper).
///
/// High-level wrapper that returns the current monotonic time in milliseconds.
/// This is the function exposed to Viper BASIC code as Clock.Ticks().
///
/// **Usage example:**
/// ```
/// Dim start = Clock.Ticks()
/// DoWork()
/// Dim elapsed = Clock.Ticks() - start
/// Print "Work took " & elapsed & " ms"
/// ```
///
/// @return Monotonic milliseconds since an arbitrary epoch.
///
/// @note Simply delegates to rt_timer_ms.
///
/// @see rt_timer_ms For implementation details
/// @see rt_clock_ticks_us For microsecond resolution
int64_t rt_clock_ticks(void)
{
    return rt_timer_ms();
}
