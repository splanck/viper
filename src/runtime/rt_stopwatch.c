//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_stopwatch.c
// Purpose: High-precision stopwatch for benchmarking and performance measurement.
// Key invariants: Accumulated time is monotonic; stopwatch state is consistent
//                 across start/stop cycles; nanosecond resolution where available.
// Ownership/Lifetime: Stopwatch objects are heap-allocated; caller responsible
//                     for lifetime management.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#include "rt_stopwatch.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/// @brief Internal stopwatch structure.
typedef struct
{
    int64_t accumulated_ns; ///< Total accumulated nanoseconds from completed intervals.
    int64_t start_time_ns;  ///< Timestamp when current interval started (if running).
    bool running;           ///< True if stopwatch is currently timing.
} ViperStopwatch;

/// @brief Get current timestamp in nanoseconds from monotonic clock.
/// @return Nanoseconds since unspecified epoch.
static int64_t get_timestamp_ns(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
#endif
    // Fallback to CLOCK_REALTIME
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
    return 0;
#endif
}

/// @brief Internal helper to get total elapsed nanoseconds.
/// @param sw Stopwatch pointer.
/// @return Total elapsed nanoseconds including current interval if running.
static int64_t stopwatch_get_elapsed_ns(ViperStopwatch *sw)
{
    int64_t total = sw->accumulated_ns;

    if (sw->running)
    {
        total += get_timestamp_ns() - sw->start_time_ns;
    }

    return total;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Create a new stopped stopwatch.
/// @return Pointer to new stopwatch object.
void *rt_stopwatch_new(void)
{
    ViperStopwatch *sw = (ViperStopwatch *)rt_obj_new_i64(0, (int64_t)sizeof(ViperStopwatch));
    if (!sw)
    {
        rt_trap("Stopwatch: memory allocation failed");
        return NULL; // Unreachable after trap
    }

    sw->accumulated_ns = 0;
    sw->start_time_ns = 0;
    sw->running = false;

    return sw;
}

/// @brief Create and immediately start a new stopwatch.
/// @return Pointer to new running stopwatch object.
void *rt_stopwatch_start_new(void)
{
    ViperStopwatch *sw = (ViperStopwatch *)rt_stopwatch_new();
    rt_stopwatch_start(sw);
    return sw;
}

/// @brief Start or resume the stopwatch.
/// @param obj Stopwatch pointer.
/// @details Has no effect if already running.
void rt_stopwatch_start(void *obj)
{
    ViperStopwatch *sw = (ViperStopwatch *)obj;

    if (!sw->running)
    {
        sw->start_time_ns = get_timestamp_ns();
        sw->running = true;
    }
}

/// @brief Stop/pause the stopwatch.
/// @param obj Stopwatch pointer.
/// @details Preserves accumulated time. Has no effect if already stopped.
void rt_stopwatch_stop(void *obj)
{
    ViperStopwatch *sw = (ViperStopwatch *)obj;

    if (sw->running)
    {
        int64_t now = get_timestamp_ns();
        sw->accumulated_ns += now - sw->start_time_ns;
        sw->running = false;
    }
}

/// @brief Reset the stopwatch to zero and stop it.
/// @param obj Stopwatch pointer.
void rt_stopwatch_reset(void *obj)
{
    ViperStopwatch *sw = (ViperStopwatch *)obj;

    sw->accumulated_ns = 0;
    sw->start_time_ns = 0;
    sw->running = false;
}

/// @brief Reset and immediately start the stopwatch.
/// @param obj Stopwatch pointer.
/// @details Equivalent to Reset() followed by Start() but atomic.
void rt_stopwatch_restart(void *obj)
{
    ViperStopwatch *sw = (ViperStopwatch *)obj;

    sw->accumulated_ns = 0;
    sw->start_time_ns = get_timestamp_ns();
    sw->running = true;
}

/// @brief Get elapsed time in nanoseconds.
/// @param obj Stopwatch pointer.
/// @return Total elapsed nanoseconds.
int64_t rt_stopwatch_elapsed_ns(void *obj)
{
    return stopwatch_get_elapsed_ns((ViperStopwatch *)obj);
}

/// @brief Get elapsed time in microseconds.
/// @param obj Stopwatch pointer.
/// @return Total elapsed microseconds.
int64_t rt_stopwatch_elapsed_us(void *obj)
{
    return stopwatch_get_elapsed_ns((ViperStopwatch *)obj) / 1000;
}

/// @brief Get elapsed time in milliseconds.
/// @param obj Stopwatch pointer.
/// @return Total elapsed milliseconds.
int64_t rt_stopwatch_elapsed_ms(void *obj)
{
    return stopwatch_get_elapsed_ns((ViperStopwatch *)obj) / 1000000;
}

/// @brief Check if the stopwatch is currently running.
/// @param obj Stopwatch pointer.
/// @return Non-zero if running, zero if stopped.
int8_t rt_stopwatch_is_running(void *obj)
{
    return ((ViperStopwatch *)obj)->running ? 1 : 0;
}
