//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_countdown.c
// Purpose: Countdown timer for interval timing with expiration detection.
// Key invariants: Tracks elapsed time against interval; Remaining = max(0, interval - elapsed);
//                 Expired = elapsed >= interval. All times in milliseconds.
// Ownership/Lifetime: Countdown objects are heap-allocated; caller responsible
//                     for lifetime management.
// Links: docs/viperlib.md
//
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

void rt_countdown_start(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

    if (!cd->running)
    {
        cd->start_time_ms = get_timestamp_ms();
        cd->running = true;
    }
}

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

void rt_countdown_reset(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;

    cd->accumulated_ms = 0;
    cd->start_time_ms = 0;
    cd->running = false;
}

int64_t rt_countdown_elapsed(void *obj)
{
    return countdown_get_elapsed_ms((ViperCountdown *)obj);
}

int64_t rt_countdown_remaining(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;
    int64_t elapsed = countdown_get_elapsed_ms(cd);
    int64_t remaining = cd->interval_ms - elapsed;
    return remaining > 0 ? remaining : 0;
}

int8_t rt_countdown_expired(void *obj)
{
    ViperCountdown *cd = (ViperCountdown *)obj;
    int64_t elapsed = countdown_get_elapsed_ms(cd);
    return elapsed >= cd->interval_ms ? 1 : 0;
}

int64_t rt_countdown_interval(void *obj)
{
    return ((ViperCountdown *)obj)->interval_ms;
}

void rt_countdown_set_interval(void *obj, int64_t interval_ms)
{
    ((ViperCountdown *)obj)->interval_ms = interval_ms > 0 ? interval_ms : 0;
}

int8_t rt_countdown_is_running(void *obj)
{
    return ((ViperCountdown *)obj)->running ? 1 : 0;
}

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
