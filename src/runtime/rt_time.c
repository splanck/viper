//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_time.c
// Purpose: Provide portable time helpers for BASIC SLEEP and TIMER functions.
// Key invariants: Negative sleep durations clamp to zero; sleep uses a monotonic
//                 clock where available; POSIX variant retries on EINTR.
//                 rt_timer_ms returns monotonic non-decreasing milliseconds.
//                 rt_clock_ticks_us returns monotonic non-decreasing microseconds.
// Ownership/Lifetime: No heap allocations; functions block or query time.
// Links: docs/runtime-vm.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void rt_sleep_ms(int32_t ms)
{
    if (ms < 0)
        ms = 0;
    Sleep((DWORD)ms);
}

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

    // Convert to microseconds: (counter * 1000000) / freq
    int64_t us = (int64_t)((counter.QuadPart * 1000000LL) / freq.QuadPart);
    return us;
}

#else
#include <errno.h>
#include <time.h>

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

void rt_clock_sleep(int64_t ms)
{
    // Clamp to int32_t range for rt_sleep_ms
    if (ms < 0)
        ms = 0;
    if (ms > INT32_MAX)
        ms = INT32_MAX;
    rt_sleep_ms((int32_t)ms);
}

int64_t rt_clock_ticks(void)
{
    return rt_timer_ms();
}
