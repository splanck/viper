//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_tween.c
// Purpose: Frame-counted value interpolation ("tweening") for Viper games and
//   UIs. A Tween smoothly animates a scalar value from a start to an end over
//   a specified number of frames, optionally applying one of 19 easing curves
//   (linear, quad, cubic, sine, exponential, back-overshoot, and bounced
//   variants). Typical uses: moving UI panels, fading colors, scaling entities,
//   and any animation that must complete in a predictable number of frames.
//
// Key invariants:
//   - Time is measured in integer frames (not wall-clock milliseconds).
//     Duration must be >= 1; zero or negative durations are clamped to 1.
//   - The tween progresses by calling rt_tween_update() once per frame.
//     Update returns 1 on the frame the tween completes, 0 otherwise.
//     After completion, is_complete() returns 1 and the value is pinned to `to`.
//   - Easing functions operate on a normalized progress t ∈ [0.0, 1.0]:
//       Linear:  f(t) = t
//       In-Quad: f(t) = t²     (starts slow, ends fast)
//       Back:    overshoots the target slightly before settling (c1 = 1.70158)
//       Bounce:  simulates a physical bounce using piecewise polynomials
//     The `ease_type` parameter is one of the RT_EASE_* constants defined in
//     rt_tween.h. Unknown types fall back to linear.
//   - Pause/Resume halt and resume update progression without resetting elapsed.
//   - rt_tween_reset() rewinds to frame 0 and resumes from the original `from`.
//   - rt_tween_value_i64() rounds halves away from zero and saturates at the
//     int64 limits — suitable for pixel coordinates within double precision.
//
// Ownership/Lifetime:
//   - Tween objects are GC-managed (rt_obj_new_i64). rt_tween_destroy() calls
//     rt_obj_free() for callers that manage lifetimes explicitly; the GC also
//     collects them automatically.
//
// Links: src/runtime/game/rt_tween.h (public API, easing constants),
//        docs/viperlib/game.md (Tween and TweenChain sections)
//
//===----------------------------------------------------------------------===//

#include "rt_tween.h"
#include "rt_object.h"
#include "rt_trap.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Internal structure for Tween.
struct rt_tween_impl {
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

/// @brief Safe-cast a handle to the Tween impl, trapping @p api on a class-id
///        mismatch. @return The tween, or NULL if @p tween is NULL.
static rt_tween checked_tween(rt_tween tween, const char *api) {
    if (!tween)
        return NULL;
    if (rt_obj_class_id(tween) != RT_TWEEN_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return tween;
}

/// @brief Return @p value if finite, else @p fallback (NaN/Inf sanitizer).
static double tween_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Linear interpolation that avoids overflowing the endpoint delta for
///        large opposite-signed values.
static double tween_lerp_double(double from, double to, double t) {
    if (!isfinite(t))
        t = 0.0;
    if (t == 0.0)
        return from;
    if (t == 1.0)
        return to;

    double result = from * (1.0 - t) + to * t;
    if (isfinite(result))
        return result;

    result = from + (to - from) * t;
    if (isfinite(result))
        return result;

    return t < 0.5 ? from : to;
}

/// @brief Round-half-away-from-zero to int64, saturating; 0 for non-finite.
static int64_t tween_round_to_i64(double value) {
    if (!isfinite(value))
        return 0;
    if (value >= (double)INT64_MAX)
        return INT64_MAX;
    if (value <= (double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value + (value >= 0 ? 0.5 : -0.5));
}

/// @brief Integer percentage value*100/total, clamped to [0, 100]; 0 for
///        non-positive inputs.
static int64_t tween_percent_i64(int64_t value, int64_t total) {
    if (value <= 0 || total <= 0)
        return 0;
    long double scaled = ((long double)value * 100.0L) / (long double)total;
    int64_t pct = scaled >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scaled;
    return pct > 100 ? 100 : pct;
}

// Forward declaration of public easing function
/// @brief Apply an easing curve to a linear progress value t in [0,1].
double rt_tween_ease(double t, int64_t ease_type);

// Forward declaration of internal easing functions
// NOTE: These duplicate rt_easing.c implementations. A future refactor could
// have rt_tween call the public rt_ease_* API instead.
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

/// @brief Create a new tween interpolator (starts inactive until start() is called).
rt_tween rt_tween_new(void) {
    struct rt_tween_impl *tween = (struct rt_tween_impl *)rt_obj_new_i64(
        RT_TWEEN_CLASS_ID, (int64_t)sizeof(struct rt_tween_impl));
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

/// @brief Destroy a tween and release its GC allocation.
void rt_tween_destroy(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Destroy: expected Viper.Game.Tween");
    if (tween && rt_obj_release_check0(tween))
        rt_obj_free(tween);
}

/// @brief Start interpolating from one value to another over the given duration with easing.
void rt_tween_start(rt_tween tween, double from, double to, int64_t duration, int64_t ease_type) {
    tween = checked_tween(tween, "Tween.Start: expected Viper.Game.Tween");
    if (!tween)
        return;
    from = tween_finite_or(from, 0.0);
    to = tween_finite_or(to, from);
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

/// @brief Start an integer-valued tween (convenience wrapper, converts to double internally).
void rt_tween_start_i64(
    rt_tween tween, int64_t from, int64_t to, int64_t duration, int64_t ease_type) {
    rt_tween_start(tween, (double)from, (double)to, duration, ease_type);
}

/// @brief Advance the tween by one tick. Returns 1 if the tween just completed.
int8_t rt_tween_update(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Update: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    if (!tween->running || tween->paused)
        return 0;

    if (tween->elapsed < INT64_MAX)
        tween->elapsed++;

    // Calculate progress (0.0 to 1.0)
    double t = (double)tween->elapsed / (double)tween->duration;
    if (t > 1.0)
        t = 1.0;

    // Apply easing
    double eased_t = rt_tween_ease(t, tween->ease_type);

    // Interpolate
    tween->current = tween_lerp_double(tween->from, tween->to, eased_t);

    // Check for completion
    if (tween->elapsed >= tween->duration) {
        tween->running = 0;
        tween->complete = 1;
        tween->current = tween->to; // Ensure exact end value
        return 1;                   // Just completed
    }

    return 0;
}

/// @brief Get the current interpolated value as a double.
double rt_tween_value(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Value: expected Viper.Game.Tween");
    if (!tween)
        return 0.0;
    return tween->current;
}

/// @brief Get the current interpolated value rounded to the nearest integer.
int64_t rt_tween_value_i64(rt_tween tween) {
    tween = checked_tween(tween, "Tween.ValueI64: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    // Round to nearest integer
    return tween_round_to_i64(tween->current);
}

/// @brief Check whether the tween is actively interpolating (running and not paused).
int8_t rt_tween_is_running(rt_tween tween) {
    tween = checked_tween(tween, "Tween.IsRunning: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    return tween->running && !tween->paused;
}

/// @brief Check whether the tween has reached its end value.
int8_t rt_tween_is_complete(rt_tween tween) {
    tween = checked_tween(tween, "Tween.IsComplete: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    return tween->complete;
}

/// @brief Get the tween progress as a percentage (0–100).
int64_t rt_tween_progress(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Progress: expected Viper.Game.Tween");
    if (!tween || tween->duration == 0)
        return 0;
    return tween_percent_i64(tween->elapsed, tween->duration);
}

/// @brief Get the number of ticks elapsed since the tween was started.
int64_t rt_tween_elapsed(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Elapsed: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    return tween->elapsed;
}

/// @brief Get the total duration of the tween in ticks.
int64_t rt_tween_duration(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Duration: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    return tween->duration;
}

/// @brief Stop the tween and clear the paused state.
void rt_tween_stop(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Stop: expected Viper.Game.Tween");
    if (!tween)
        return;
    tween->running = 0;
    tween->paused = 0;
}

/// @brief Reset the tween to its start value and restart playback.
void rt_tween_reset(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Reset: expected Viper.Game.Tween");
    if (!tween)
        return;
    tween->elapsed = 0;
    tween->current = tween->from;
    tween->complete = 0;
    if (tween->duration > 0)
        tween->running = 1;
    tween->paused = 0;
}

/// @brief Pause the tween at the current position (can be resumed).
void rt_tween_pause(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Pause: expected Viper.Game.Tween");
    if (!tween)
        return;
    if (tween->running && !tween->complete)
        tween->paused = 1;
}

/// @brief Resume a paused tween from where it left off.
void rt_tween_resume(rt_tween tween) {
    tween = checked_tween(tween, "Tween.Resume: expected Viper.Game.Tween");
    if (!tween)
        return;
    tween->paused = 0;
}

/// @brief Check whether the tween is currently paused.
int8_t rt_tween_is_paused(rt_tween tween) {
    tween = checked_tween(tween, "Tween.IsPaused: expected Viper.Game.Tween");
    if (!tween)
        return 0;
    return tween->paused;
}

//=============================================================================
// Static interpolation functions
//=============================================================================

// Note: rt_lerp is provided by rt_math.c

/// @brief Linearly interpolate between two integers at parameter t, rounded to nearest.
int64_t rt_tween_lerp_i64(int64_t from, int64_t to, double t) {
    if (!isfinite(t))
        t = 0.0;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    double result = tween_lerp_double((double)from, (double)to, t);
    return tween_round_to_i64(result);
}

/// @brief Apply an easing curve to a linear progress value t in [0,1].
/// @details Dispatches to one of 19 easing functions (linear, quad, cubic, sine,
///          expo, back, bounce — each in in/out/in-out variants). Returns 0 for
///          t<=0 and 1 for t>=1 to guarantee exact endpoints.
double rt_tween_ease(double t, int64_t ease_type) {
    if (!isfinite(t))
        return 0.0;
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    switch (ease_type) {
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
//
// Each takes a normalized progress t in [0, 1] and returns the eased value
// (also ~[0, 1], though "back"/"bounce" curves overshoot). Naming follows the
// Penner convention: "in" accelerates from rest, "out" decelerates to rest,
// "in_out" does both around the midpoint.
//=============================================================================

/// @brief Linear (identity) easing: returns @p t unchanged.
static double ease_linear(double t) {
    return t;
}

/// @brief Quadratic ease-in (t^2).
static double ease_in_quad(double t) {
    return t * t;
}

/// @brief Quadratic ease-out.
static double ease_out_quad(double t) {
    return t * (2.0 - t);
}

/// @brief Quadratic ease-in-out.
static double ease_in_out_quad(double t) {
    if (t < 0.5)
        return 2.0 * t * t;
    return -1.0 + (4.0 - 2.0 * t) * t;
}

/// @brief Cubic ease-in (t^3).
static double ease_in_cubic(double t) {
    return t * t * t;
}

/// @brief Cubic ease-out.
static double ease_out_cubic(double t) {
    double t1 = t - 1.0;
    return t1 * t1 * t1 + 1.0;
}

/// @brief Cubic ease-in-out.
static double ease_in_out_cubic(double t) {
    if (t < 0.5)
        return 4.0 * t * t * t;
    double t1 = 2.0 * t - 2.0;
    return 0.5 * t1 * t1 * t1 + 1.0;
}

/// @brief Sinusoidal ease-in (1 - cos).
static double ease_in_sine(double t) {
    return 1.0 - cos(t * M_PI / 2.0);
}

/// @brief Sinusoidal ease-out (sin).
static double ease_out_sine(double t) {
    return sin(t * M_PI / 2.0);
}

/// @brief Sinusoidal ease-in-out.
static double ease_in_out_sine(double t) {
    return 0.5 * (1.0 - cos(M_PI * t));
}

/// @brief Exponential ease-in (2^(10(t-1))).
static double ease_in_expo(double t) {
    if (t == 0.0)
        return 0.0;
    return pow(2.0, 10.0 * (t - 1.0));
}

/// @brief Exponential ease-out.
static double ease_out_expo(double t) {
    if (t == 1.0)
        return 1.0;
    return 1.0 - pow(2.0, -10.0 * t);
}

/// @brief Exponential ease-in-out.
static double ease_in_out_expo(double t) {
    if (t == 0.0)
        return 0.0;
    if (t == 1.0)
        return 1.0;
    if (t < 0.5)
        return 0.5 * pow(2.0, 20.0 * t - 10.0);
    return 1.0 - 0.5 * pow(2.0, -20.0 * t + 10.0);
}

/// @brief "Back" ease-in: slight anticipation (undershoot) before advancing.
static double ease_in_back(double t) {
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    return c3 * t * t * t - c1 * t * t;
}

/// @brief "Back" ease-out: overshoots the target then settles back.
static double ease_out_back(double t) {
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    double t1 = t - 1.0;
    return 1.0 + c3 * t1 * t1 * t1 + c1 * t1 * t1;
}

/// @brief "Back" ease-in-out: anticipation at the start, overshoot at the end.
static double ease_in_out_back(double t) {
    const double c1 = 1.70158;
    const double c2 = c1 * 1.525;
    if (t < 0.5) {
        double t2 = 2.0 * t;
        return 0.5 * t2 * t2 * ((c2 + 1.0) * t2 - c2);
    }
    double t2 = 2.0 * t - 2.0;
    return 0.5 * (t2 * t2 * ((c2 + 1.0) * t2 + c2) + 2.0);
}

/// @brief "Bounce" ease-out: decaying piecewise-parabolic bounces to the end.
static double ease_out_bounce(double t) {
    const double n1 = 7.5625;
    const double d1 = 2.75;

    if (t < 1.0 / d1) {
        return n1 * t * t;
    } else if (t < 2.0 / d1) {
        double t1 = t - 1.5 / d1;
        return n1 * t1 * t1 + 0.75;
    } else if (t < 2.5 / d1) {
        double t1 = t - 2.25 / d1;
        return n1 * t1 * t1 + 0.9375;
    } else {
        double t1 = t - 2.625 / d1;
        return n1 * t1 * t1 + 0.984375;
    }
}

/// @brief "Bounce" ease-in: time-reversed ease_out_bounce.
static double ease_in_bounce(double t) {
    return 1.0 - ease_out_bounce(1.0 - t);
}

/// @brief "Bounce" ease-in-out: bounce-in for the first half, bounce-out the second.
static double ease_in_out_bounce(double t) {
    if (t < 0.5)
        return 0.5 * (1.0 - ease_out_bounce(1.0 - 2.0 * t));
    return 0.5 * (1.0 + ease_out_bounce(2.0 * t - 1.0));
}
