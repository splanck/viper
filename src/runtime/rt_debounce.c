//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_debounce.h"

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

// --- Helper: current time in milliseconds ---

static int64_t current_time_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000LL) / freq.QuadPart);
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
    return 0;
#endif
}

// --- Debouncer ---

typedef struct
{
    int64_t delay_ms;
    int64_t last_signal_time;
    int64_t signal_count;
} rt_debounce_data;

static void debounce_finalizer(void *obj)
{
    (void)obj;
}

void *rt_debounce_new(int64_t delay_ms)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_debounce_data));
    rt_debounce_data *data = (rt_debounce_data *)obj;
    data->delay_ms = delay_ms > 0 ? delay_ms : 0;
    data->last_signal_time = 0;
    data->signal_count = 0;
    rt_obj_set_finalizer(obj, debounce_finalizer);
    return obj;
}

void rt_debounce_signal(void *debouncer)
{
    if (!debouncer)
        return;
    rt_debounce_data *data = (rt_debounce_data *)debouncer;
    data->last_signal_time = current_time_ms();
    data->signal_count++;
}

int8_t rt_debounce_is_ready(void *debouncer)
{
    if (!debouncer)
        return 0;
    rt_debounce_data *data = (rt_debounce_data *)debouncer;
    if (data->last_signal_time == 0)
        return 0; // Never signaled
    int64_t elapsed = current_time_ms() - data->last_signal_time;
    return elapsed >= data->delay_ms ? 1 : 0;
}

void rt_debounce_reset(void *debouncer)
{
    if (!debouncer)
        return;
    rt_debounce_data *data = (rt_debounce_data *)debouncer;
    data->last_signal_time = 0;
    data->signal_count = 0;
}

int64_t rt_debounce_get_delay(void *debouncer)
{
    if (!debouncer)
        return 0;
    return ((rt_debounce_data *)debouncer)->delay_ms;
}

int64_t rt_debounce_get_signal_count(void *debouncer)
{
    if (!debouncer)
        return 0;
    return ((rt_debounce_data *)debouncer)->signal_count;
}

// --- Throttler ---

typedef struct
{
    int64_t interval_ms;
    int64_t last_allowed_time;
    int64_t count;
} rt_throttle_data;

static void throttle_finalizer(void *obj)
{
    (void)obj;
}

void *rt_throttle_new(int64_t interval_ms)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_throttle_data));
    rt_throttle_data *data = (rt_throttle_data *)obj;
    data->interval_ms = interval_ms > 0 ? interval_ms : 0;
    data->last_allowed_time = 0;
    data->count = 0;
    rt_obj_set_finalizer(obj, throttle_finalizer);
    return obj;
}

int8_t rt_throttle_try(void *throttler)
{
    if (!throttler)
        return 0;
    rt_throttle_data *data = (rt_throttle_data *)throttler;
    int64_t now = current_time_ms();
    int64_t elapsed = now - data->last_allowed_time;
    if (data->last_allowed_time == 0 || elapsed >= data->interval_ms)
    {
        data->last_allowed_time = now;
        data->count++;
        return 1;
    }
    return 0;
}

int8_t rt_throttle_can_proceed(void *throttler)
{
    if (!throttler)
        return 0;
    rt_throttle_data *data = (rt_throttle_data *)throttler;
    if (data->last_allowed_time == 0)
        return 1;
    int64_t elapsed = current_time_ms() - data->last_allowed_time;
    return elapsed >= data->interval_ms ? 1 : 0;
}

void rt_throttle_reset(void *throttler)
{
    if (!throttler)
        return;
    rt_throttle_data *data = (rt_throttle_data *)throttler;
    data->last_allowed_time = 0;
    data->count = 0;
}

int64_t rt_throttle_get_interval(void *throttler)
{
    if (!throttler)
        return 0;
    return ((rt_throttle_data *)throttler)->interval_ms;
}

int64_t rt_throttle_get_count(void *throttler)
{
    if (!throttler)
        return 0;
    return ((rt_throttle_data *)throttler)->count;
}

int64_t rt_throttle_remaining_ms(void *throttler)
{
    if (!throttler)
        return 0;
    rt_throttle_data *data = (rt_throttle_data *)throttler;
    if (data->last_allowed_time == 0)
        return 0;
    int64_t elapsed = current_time_ms() - data->last_allowed_time;
    int64_t remaining = data->interval_ms - elapsed;
    return remaining > 0 ? remaining : 0;
}
