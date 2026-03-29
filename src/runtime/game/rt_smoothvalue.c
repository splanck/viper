//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_smoothvalue.c
// Purpose: Exponential-smoothing scalar for Viper games. A SmoothValue glides
//   its current value toward a target each frame using the classic
//   "exponential moving average" formula:
//       current = current × smoothing + target × (1 - smoothing)
//   This produces a natural deceleration curve — the closer the value is to
//   the target, the slower it moves. Typical uses: smooth camera follow,
//   health bar animation, velocity damping, and UI slide-in panels.
//
// Key invariants:
//   - Smoothing factor ∈ [0.0, 0.999]. At 0.0 the value snaps to the target
//     instantly each frame. At 0.999 it barely moves per frame. Values >= 1.0
//     are clamped to 0.999 to prevent the value from stalling permanently.
//   - SMOOTH_EPSILON (0.001) is the convergence threshold: once |current -
//     target| < epsilon, current is snapped to target and velocity is zeroed.
//     This prevents infinite asymptotic drift at low smoothing values.
//   - velocity is the per-frame delta (current_frame - prev_frame). It is
//     zeroed on snap. Useful for secondary motion effects (motion blur, trails).
//   - rt_smoothvalue_set_immediate() sets both current and target to a value
//     and zeros velocity — equivalent to constructing a new SmoothValue at that
//     position. Use this to teleport without a visible interpolation glitch.
//   - rt_smoothvalue_impulse() directly offsets current without touching target.
//     On the next update() the value will smoothly return toward the target.
//
// Ownership/Lifetime:
//   - SmoothValue objects are GC-managed (rt_obj_new_i64). rt_smoothvalue_destroy()
//     calls rt_obj_free() explicitly; the GC also collects them automatically.
//
// Links: src/runtime/game/rt_smoothvalue.h (public API),
//        docs/viperlib/game.md (SmoothValue section)
//
//===----------------------------------------------------------------------===//

#include "rt_smoothvalue.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>

/// Default epsilon for "at target" detection.
#define SMOOTH_EPSILON 0.001

/// Internal structure for SmoothValue.
struct rt_smoothvalue_impl {
    double current;   ///< Current interpolated value.
    double target;    ///< Target value to approach.
    double smoothing; ///< Smoothing factor (0.0-1.0).
    double velocity;  ///< Current rate of change.
};

/// @brief Create a new smoothvalue object.
rt_smoothvalue rt_smoothvalue_new(double initial, double smoothing) {
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

/// @brief Release resources and destroy the smoothvalue.
void rt_smoothvalue_destroy(rt_smoothvalue sv) {
    if (sv && rt_obj_release_check0(sv))
        rt_obj_free(sv);
}

/// @brief Get a value from the smoothvalue.
double rt_smoothvalue_get(rt_smoothvalue sv) {
    if (!sv)
        return 0.0;
    return sv->current;
}

/// @brief Return the current smoothed value rounded to the nearest integer.
int64_t rt_smoothvalue_get_i64(rt_smoothvalue sv) {
    if (!sv)
        return 0;
    return (int64_t)(sv->current + (sv->current >= 0 ? 0.5 : -0.5));
}

/// @brief Get a value from the smoothvalue.
double rt_smoothvalue_target(rt_smoothvalue sv) {
    if (!sv)
        return 0.0;
    return sv->target;
}

/// @brief Get a value from the smoothvalue.
void rt_smoothvalue_set_target(rt_smoothvalue sv, double target) {
    if (!sv)
        return;
    sv->target = target;
}

/// @brief Snap both current and target to a value instantly (no interpolation).
void rt_smoothvalue_set_immediate(rt_smoothvalue sv, double value) {
    if (!sv)
        return;
    sv->current = value;
    sv->target = value;
    sv->velocity = 0.0;
}

/// @brief Return the current smoothing factor.
double rt_smoothvalue_smoothing(rt_smoothvalue sv) {
    if (!sv)
        return 0.0;
    return sv->smoothing;
}

/// @brief Set the smoothing factor [0.0, 0.999]; higher = slower interpolation.
void rt_smoothvalue_set_smoothing(rt_smoothvalue sv, double smoothing) {
    if (!sv)
        return;
    if (smoothing < 0.0)
        smoothing = 0.0;
    if (smoothing > 0.999)
        smoothing = 0.999;
    sv->smoothing = smoothing;
}

/// @brief Update the smoothvalue state (called per frame/tick).
void rt_smoothvalue_update(rt_smoothvalue sv) {
    if (!sv)
        return;

    // Exponential smoothing: current = current * smoothing + target * (1 - smoothing)
    double prev = sv->current;
    double factor = 1.0 - sv->smoothing;
    sv->current = sv->current * sv->smoothing + sv->target * factor;

    // Calculate velocity
    sv->velocity = sv->current - prev;

    // Snap to target if very close (prevent floating point drift)
    if (fabs(sv->current - sv->target) < SMOOTH_EPSILON) {
        sv->current = sv->target;
        sv->velocity = 0.0;
    }
}

/// @brief Get a value from the smoothvalue.
int8_t rt_smoothvalue_at_target(rt_smoothvalue sv) {
    if (!sv)
        return 1;
    return fabs(sv->current - sv->target) < SMOOTH_EPSILON ? 1 : 0;
}

/// @brief Velocity the smoothvalue.
double rt_smoothvalue_velocity(rt_smoothvalue sv) {
    if (!sv)
        return 0.0;
    return sv->velocity;
}

/// @brief Impulse the smoothvalue.
void rt_smoothvalue_impulse(rt_smoothvalue sv, double impulse) {
    if (!sv)
        return;
    sv->current += impulse;
}
