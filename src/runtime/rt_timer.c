//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_timer.c
/// @brief Implementation of frame-based timer.
///
//===----------------------------------------------------------------------===//

#include "rt_timer.h"
#include <stdlib.h>

/// Internal structure for Timer.
struct rt_timer_impl
{
    int64_t duration; // Total frames for the timer
    int64_t elapsed;  // Frames elapsed since start
    int8_t running;   // 1 if timer is running
    int8_t repeating; // 1 if timer auto-restarts
};

rt_timer rt_timer_new(void)
{
    struct rt_timer_impl *timer = malloc(sizeof(struct rt_timer_impl));
    if (!timer)
    {
        return NULL;
    }

    timer->duration = 0;
    timer->elapsed = 0;
    timer->running = 0;
    timer->repeating = 0;

    return timer;
}

void rt_timer_destroy(rt_timer timer)
{
    if (timer)
    {
        free(timer);
    }
}

void rt_timer_start(rt_timer timer, int64_t frames)
{
    if (!timer || frames <= 0)
        return;

    timer->duration = frames;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 0;
}

void rt_timer_start_repeating(rt_timer timer, int64_t frames)
{
    if (!timer || frames <= 0)
        return;

    timer->duration = frames;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 1;
}

void rt_timer_stop(rt_timer timer)
{
    if (!timer)
        return;
    timer->running = 0;
}

void rt_timer_reset(rt_timer timer)
{
    if (!timer)
        return;
    timer->elapsed = 0;
}

int8_t rt_timer_update(rt_timer timer)
{
    if (!timer || !timer->running)
    {
        return 0;
    }

    timer->elapsed++;

    if (timer->elapsed >= timer->duration)
    {
        if (timer->repeating)
        {
            // Wrap around for repeating timers
            timer->elapsed = 0;
        }
        else
        {
            timer->running = 0;
        }
        return 1; // Timer expired this frame
    }

    return 0;
}

int8_t rt_timer_is_running(rt_timer timer)
{
    return timer ? timer->running : 0;
}

int8_t rt_timer_is_expired(rt_timer timer)
{
    if (!timer)
        return 0;
    return (!timer->running && timer->elapsed >= timer->duration) ? 1 : 0;
}

int64_t rt_timer_elapsed(rt_timer timer)
{
    return timer ? timer->elapsed : 0;
}

int64_t rt_timer_remaining(rt_timer timer)
{
    if (!timer || timer->duration == 0)
        return 0;

    int64_t remaining = timer->duration - timer->elapsed;
    return (remaining > 0) ? remaining : 0;
}

int64_t rt_timer_progress(rt_timer timer)
{
    if (!timer || timer->duration == 0)
        return 0;

    int64_t progress = (timer->elapsed * 100) / timer->duration;
    return (progress > 100) ? 100 : progress;
}

int64_t rt_timer_duration(rt_timer timer)
{
    return timer ? timer->duration : 0;
}

int8_t rt_timer_is_repeating(rt_timer timer)
{
    return timer ? timer->repeating : 0;
}

void rt_timer_set_duration(rt_timer timer, int64_t frames)
{
    if (!timer || frames <= 0)
        return;
    timer->duration = frames;
}
