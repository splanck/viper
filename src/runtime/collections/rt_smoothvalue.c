//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_smoothvalue.c
/// @brief Implementation of smooth value interpolation.
///
//===----------------------------------------------------------------------===//

#include "rt_smoothvalue.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>

/// Default epsilon for "at target" detection.
#define SMOOTH_EPSILON 0.001

/// Internal structure for SmoothValue.
struct rt_smoothvalue_impl
{
    double current;   ///< Current interpolated value.
    double target;    ///< Target value to approach.
    double smoothing; ///< Smoothing factor (0.0-1.0).
    double velocity;  ///< Current rate of change.
};

rt_smoothvalue rt_smoothvalue_new(double initial, double smoothing)
{
    struct rt_smoothvalue_impl *sv = (struct rt_smoothvalue_impl *)rt_obj_new_i64(
        0, (int64_t)sizeof(struct rt_smoothvalue_impl));
    if (!sv)
        return NULL;

    sv->current = initial;
    sv->target = initial;
    sv->velocity = 0.0;

    // Clamp smoothing to valid range
    if (smoothing < 0.0)
        smoothing = 0.0;
    if (smoothing > 0.999)
        smoothing = 0.999; // Prevent complete stall
    sv->smoothing = smoothing;

    return sv;
}

void rt_smoothvalue_destroy(rt_smoothvalue sv)
{
    if (sv)
        rt_obj_free(sv);
}

double rt_smoothvalue_get(rt_smoothvalue sv)
{
    if (!sv)
        return 0.0;
    return sv->current;
}

int64_t rt_smoothvalue_get_i64(rt_smoothvalue sv)
{
    if (!sv)
        return 0;
    return (int64_t)(sv->current + (sv->current >= 0 ? 0.5 : -0.5));
}

double rt_smoothvalue_target(rt_smoothvalue sv)
{
    if (!sv)
        return 0.0;
    return sv->target;
}

void rt_smoothvalue_set_target(rt_smoothvalue sv, double target)
{
    if (!sv)
        return;
    sv->target = target;
}

void rt_smoothvalue_set_immediate(rt_smoothvalue sv, double value)
{
    if (!sv)
        return;
    sv->current = value;
    sv->target = value;
    sv->velocity = 0.0;
}

double rt_smoothvalue_smoothing(rt_smoothvalue sv)
{
    if (!sv)
        return 0.0;
    return sv->smoothing;
}

void rt_smoothvalue_set_smoothing(rt_smoothvalue sv, double smoothing)
{
    if (!sv)
        return;
    if (smoothing < 0.0)
        smoothing = 0.0;
    if (smoothing > 0.999)
        smoothing = 0.999;
    sv->smoothing = smoothing;
}

void rt_smoothvalue_update(rt_smoothvalue sv)
{
    if (!sv)
        return;

    // Exponential smoothing: current = current * smoothing + target * (1 - smoothing)
    double prev = sv->current;
    double factor = 1.0 - sv->smoothing;
    sv->current = sv->current * sv->smoothing + sv->target * factor;

    // Calculate velocity
    sv->velocity = sv->current - prev;

    // Snap to target if very close (prevent floating point drift)
    if (fabs(sv->current - sv->target) < SMOOTH_EPSILON)
    {
        sv->current = sv->target;
        sv->velocity = 0.0;
    }
}

int8_t rt_smoothvalue_at_target(rt_smoothvalue sv)
{
    if (!sv)
        return 1;
    return fabs(sv->current - sv->target) < SMOOTH_EPSILON ? 1 : 0;
}

double rt_smoothvalue_velocity(rt_smoothvalue sv)
{
    if (!sv)
        return 0.0;
    return sv->velocity;
}

void rt_smoothvalue_impulse(rt_smoothvalue sv, double impulse)
{
    if (!sv)
        return;
    sv->current += impulse;
}
