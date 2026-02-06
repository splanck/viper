//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_tween.c
/// @brief Implementation of tweening and interpolation utilities.
///
//===----------------------------------------------------------------------===//

#include "rt_tween.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Internal structure for Tween.
struct rt_tween_impl
{
    double from;       ///< Starting value.
    double to;         ///< Ending value.
    double current;    ///< Current interpolated value.
    int64_t duration;  ///< Total duration in frames.
    int64_t elapsed;   ///< Elapsed frames.
    int64_t ease_type; ///< Easing function type.
    int8_t running;    ///< 1 if tween is running.
    int8_t complete;   ///< 1 if tween has completed.
    int8_t paused;     ///< 1 if tween is paused.
};

// Forward declaration of public easing function
double rt_tween_ease(double t, int64_t ease_type);

// Forward declaration of internal easing functions
static double ease_linear(double t);
static double ease_in_quad(double t);
static double ease_out_quad(double t);
static double ease_in_out_quad(double t);
static double ease_in_cubic(double t);
static double ease_out_cubic(double t);
static double ease_in_out_cubic(double t);
static double ease_in_sine(double t);
static double ease_out_sine(double t);
static double ease_in_out_sine(double t);
static double ease_in_expo(double t);
static double ease_out_expo(double t);
static double ease_in_out_expo(double t);
static double ease_in_back(double t);
static double ease_out_back(double t);
static double ease_in_out_back(double t);
static double ease_in_bounce(double t);
static double ease_out_bounce(double t);
static double ease_in_out_bounce(double t);

rt_tween rt_tween_new(void)
{
    struct rt_tween_impl *tween = malloc(sizeof(struct rt_tween_impl));
    if (!tween)
        return NULL;

    tween->from = 0.0;
    tween->to = 0.0;
    tween->current = 0.0;
    tween->duration = 0;
    tween->elapsed = 0;
    tween->ease_type = RT_EASE_LINEAR;
    tween->running = 0;
    tween->complete = 0;
    tween->paused = 0;

    return tween;
}

void rt_tween_destroy(rt_tween tween)
{
    if (tween)
        free(tween);
}

void rt_tween_start(rt_tween tween, double from, double to, int64_t duration, int64_t ease_type)
{
    if (!tween)
        return;
    if (duration < 1)
        duration = 1;
    if (ease_type < 0 || ease_type >= RT_EASE_COUNT)
        ease_type = RT_EASE_LINEAR;

    tween->from = from;
    tween->to = to;
    tween->current = from;
    tween->duration = duration;
    tween->elapsed = 0;
    tween->ease_type = ease_type;
    tween->running = 1;
    tween->complete = 0;
    tween->paused = 0;
}

void rt_tween_start_i64(
    rt_tween tween, int64_t from, int64_t to, int64_t duration, int64_t ease_type)
{
    rt_tween_start(tween, (double)from, (double)to, duration, ease_type);
}

int8_t rt_tween_update(rt_tween tween)
{
    if (!tween)
        return 0;
    if (!tween->running || tween->paused)
        return 0;

    tween->elapsed++;

    // Calculate progress (0.0 to 1.0)
    double t = (double)tween->elapsed / (double)tween->duration;
    if (t > 1.0)
        t = 1.0;

    // Apply easing
    double eased_t = rt_tween_ease(t, tween->ease_type);

    // Interpolate
    tween->current = tween->from + (tween->to - tween->from) * eased_t;

    // Check for completion
    if (tween->elapsed >= tween->duration)
    {
        tween->running = 0;
        tween->complete = 1;
        tween->current = tween->to; // Ensure exact end value
        return 1;                   // Just completed
    }

    return 0;
}

double rt_tween_value(rt_tween tween)
{
    if (!tween)
        return 0.0;
    return tween->current;
}

int64_t rt_tween_value_i64(rt_tween tween)
{
    if (!tween)
        return 0;
    // Round to nearest integer
    return (int64_t)(tween->current + (tween->current >= 0 ? 0.5 : -0.5));
}

int8_t rt_tween_is_running(rt_tween tween)
{
    if (!tween)
        return 0;
    return tween->running && !tween->paused;
}

int8_t rt_tween_is_complete(rt_tween tween)
{
    if (!tween)
        return 0;
    return tween->complete;
}

int64_t rt_tween_progress(rt_tween tween)
{
    if (!tween || tween->duration == 0)
        return 0;
    int64_t progress = (tween->elapsed * 100) / tween->duration;
    if (progress > 100)
        progress = 100;
    return progress;
}

int64_t rt_tween_elapsed(rt_tween tween)
{
    if (!tween)
        return 0;
    return tween->elapsed;
}

int64_t rt_tween_duration(rt_tween tween)
{
    if (!tween)
        return 0;
    return tween->duration;
}

void rt_tween_stop(rt_tween tween)
{
    if (!tween)
        return;
    tween->running = 0;
    tween->paused = 0;
}

void rt_tween_reset(rt_tween tween)
{
    if (!tween)
        return;
    tween->elapsed = 0;
    tween->current = tween->from;
    tween->complete = 0;
    if (tween->duration > 0)
        tween->running = 1;
    tween->paused = 0;
}

void rt_tween_pause(rt_tween tween)
{
    if (!tween)
        return;
    tween->paused = 1;
}

void rt_tween_resume(rt_tween tween)
{
    if (!tween)
        return;
    tween->paused = 0;
}

int8_t rt_tween_is_paused(rt_tween tween)
{
    if (!tween)
        return 0;
    return tween->paused;
}

//=============================================================================
// Static interpolation functions
//=============================================================================

// Note: rt_lerp is provided by rt_math.c

int64_t rt_tween_lerp_i64(int64_t from, int64_t to, double t)
{
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    double result = (double)from + ((double)to - (double)from) * t;
    return (int64_t)(result + (result >= 0 ? 0.5 : -0.5));
}

double rt_tween_ease(double t, int64_t ease_type)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    switch (ease_type)
    {
        case RT_EASE_LINEAR:
            return ease_linear(t);
        case RT_EASE_IN_QUAD:
            return ease_in_quad(t);
        case RT_EASE_OUT_QUAD:
            return ease_out_quad(t);
        case RT_EASE_IN_OUT_QUAD:
            return ease_in_out_quad(t);
        case RT_EASE_IN_CUBIC:
            return ease_in_cubic(t);
        case RT_EASE_OUT_CUBIC:
            return ease_out_cubic(t);
        case RT_EASE_IN_OUT_CUBIC:
            return ease_in_out_cubic(t);
        case RT_EASE_IN_SINE:
            return ease_in_sine(t);
        case RT_EASE_OUT_SINE:
            return ease_out_sine(t);
        case RT_EASE_IN_OUT_SINE:
            return ease_in_out_sine(t);
        case RT_EASE_IN_EXPO:
            return ease_in_expo(t);
        case RT_EASE_OUT_EXPO:
            return ease_out_expo(t);
        case RT_EASE_IN_OUT_EXPO:
            return ease_in_out_expo(t);
        case RT_EASE_IN_BACK:
            return ease_in_back(t);
        case RT_EASE_OUT_BACK:
            return ease_out_back(t);
        case RT_EASE_IN_OUT_BACK:
            return ease_in_out_back(t);
        case RT_EASE_IN_BOUNCE:
            return ease_in_bounce(t);
        case RT_EASE_OUT_BOUNCE:
            return ease_out_bounce(t);
        case RT_EASE_IN_OUT_BOUNCE:
            return ease_in_out_bounce(t);
        default:
            return t;
    }
}

//=============================================================================
// Internal easing function implementations
//=============================================================================

static double ease_linear(double t)
{
    return t;
}

static double ease_in_quad(double t)
{
    return t * t;
}

static double ease_out_quad(double t)
{
    return t * (2.0 - t);
}

static double ease_in_out_quad(double t)
{
    if (t < 0.5)
        return 2.0 * t * t;
    return -1.0 + (4.0 - 2.0 * t) * t;
}

static double ease_in_cubic(double t)
{
    return t * t * t;
}

static double ease_out_cubic(double t)
{
    double t1 = t - 1.0;
    return t1 * t1 * t1 + 1.0;
}

static double ease_in_out_cubic(double t)
{
    if (t < 0.5)
        return 4.0 * t * t * t;
    double t1 = 2.0 * t - 2.0;
    return 0.5 * t1 * t1 * t1 + 1.0;
}

static double ease_in_sine(double t)
{
    return 1.0 - cos(t * M_PI / 2.0);
}

static double ease_out_sine(double t)
{
    return sin(t * M_PI / 2.0);
}

static double ease_in_out_sine(double t)
{
    return 0.5 * (1.0 - cos(M_PI * t));
}

static double ease_in_expo(double t)
{
    if (t == 0.0)
        return 0.0;
    return pow(2.0, 10.0 * (t - 1.0));
}

static double ease_out_expo(double t)
{
    if (t == 1.0)
        return 1.0;
    return 1.0 - pow(2.0, -10.0 * t);
}

static double ease_in_out_expo(double t)
{
    if (t == 0.0)
        return 0.0;
    if (t == 1.0)
        return 1.0;
    if (t < 0.5)
        return 0.5 * pow(2.0, 20.0 * t - 10.0);
    return 1.0 - 0.5 * pow(2.0, -20.0 * t + 10.0);
}

static double ease_in_back(double t)
{
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    return c3 * t * t * t - c1 * t * t;
}

static double ease_out_back(double t)
{
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    double t1 = t - 1.0;
    return 1.0 + c3 * t1 * t1 * t1 + c1 * t1 * t1;
}

static double ease_in_out_back(double t)
{
    const double c1 = 1.70158;
    const double c2 = c1 * 1.525;
    if (t < 0.5)
    {
        double t2 = 2.0 * t;
        return 0.5 * t2 * t2 * ((c2 + 1.0) * t2 - c2);
    }
    double t2 = 2.0 * t - 2.0;
    return 0.5 * (t2 * t2 * ((c2 + 1.0) * t2 + c2) + 2.0);
}

static double ease_out_bounce(double t)
{
    const double n1 = 7.5625;
    const double d1 = 2.75;

    if (t < 1.0 / d1)
    {
        return n1 * t * t;
    }
    else if (t < 2.0 / d1)
    {
        double t1 = t - 1.5 / d1;
        return n1 * t1 * t1 + 0.75;
    }
    else if (t < 2.5 / d1)
    {
        double t1 = t - 2.25 / d1;
        return n1 * t1 * t1 + 0.9375;
    }
    else
    {
        double t1 = t - 2.625 / d1;
        return n1 * t1 * t1 + 0.984375;
    }
}

static double ease_in_bounce(double t)
{
    return 1.0 - ease_out_bounce(1.0 - t);
}

static double ease_in_out_bounce(double t)
{
    if (t < 0.5)
        return 0.5 * (1.0 - ease_out_bounce(1.0 - 2.0 * t));
    return 0.5 * (1.0 + ease_out_bounce(2.0 * t - 1.0));
}
