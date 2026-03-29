//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_easing.c
// Purpose: Implements the standard easing function library for animation and
//          motion interpolation. Covers linear, polynomial (quad/cubic/quart/
//          quint), sinusoidal, exponential, circular, elastic, back, and bounce
//          families, each in ease-in, ease-out, and ease-in-out variants.
//
// Key invariants:
//   - All functions accept t in [0.0, 1.0] and return values in the same
//     domain for most easing types; elastic and back functions may return
//     values slightly outside [0, 1] by design.
//   - Functions are pure: no side effects, no global state, safe to call
//     concurrently from multiple threads.
//   - Input values outside [0, 1] produce extrapolated results; callers are
//     responsible for clamping if strict domain adherence is required.
//   - M_PI is defined locally if the math header does not provide it.
//
// Ownership/Lifetime:
//   - All functions operate on scalar double values; no allocation is performed.
//   - No state is retained between calls.
//
// Links: src/runtime/core/rt_easing.h (public API),
//        src/runtime/core/rt_perlin.c (complementary procedural utilities)
//
//===----------------------------------------------------------------------===//

#include "rt_easing.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// @brief Linear interpolation (no easing).
/// @details Constant velocity from start to end — t is returned unchanged.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_linear(double t) {
    return t;
}

/// @brief Quadratic ease-in (accelerating from zero velocity).
/// @details Starts slow, accelerates: t^2.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_quad(double t) {
    return t * t;
}

/// @brief Quadratic ease-out (decelerating to zero velocity).
/// @details Starts fast, decelerates: 1-(1-t)^2.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_quad(double t) {
    return t * (2.0 - t);
}

/// @brief Quadratic ease-in-out (accelerate then decelerate).
/// @details Smooth S-curve using quadratic segments.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_quad(double t) {
    return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
}

/// @brief Cubic ease-in (accelerating from zero velocity).
/// @details Starts slow, accelerates: t^3.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_cubic(double t) {
    return t * t * t;
}

/// @brief Cubic ease-out (decelerating to zero velocity).
/// @details Starts fast, decelerates: 1-(1-t)^3.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_cubic(double t) {
    double u = t - 1.0;
    return u * u * u + 1.0;
}

/// @brief Cubic ease-in-out (accelerate then decelerate).
/// @details Smooth S-curve using cubic segments.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_cubic(double t) {
    return t < 0.5 ? 4.0 * t * t * t : (t - 1.0) * (2.0 * t - 2.0) * (2.0 * t - 2.0) + 1.0;
}

/// @brief Quartic ease-in (very slow start).
/// @details Starts very slow, accelerates: t^4.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_quart(double t) {
    return t * t * t * t;
}

/// @brief Quartic ease-out (very slow end).
/// @details Starts fast, decelerates: 1-(1-t)^4.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_quart(double t) {
    double u = t - 1.0;
    return 1.0 - u * u * u * u;
}

/// @brief Quartic ease-in-out.
/// @details S-curve with quartic segments.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_quart(double t) {
    double u = t - 1.0;
    return t < 0.5 ? 8.0 * t * t * t * t : 1.0 - 8.0 * u * u * u * u;
}

/// @brief Sinusoidal ease-in.
/// @details Uses sin() for a natural-feeling acceleration.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_sine(double t) {
    return 1.0 - cos(t * M_PI / 2.0);
}

/// @brief Sinusoidal ease-out.
/// @details Uses sin() for a natural-feeling deceleration.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_sine(double t) {
    return sin(t * M_PI / 2.0);
}

/// @brief Sinusoidal ease-in-out.
/// @details Uses cos() for a natural S-curve.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_sine(double t) {
    return 0.5 * (1.0 - cos(M_PI * t));
}

/// @brief Exponential ease-in.
/// @details Starts nearly invisible then explodes: 2^(10*(t-1)).
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_expo(double t) {
    return t <= 0.0 ? 0.0 : pow(2.0, 10.0 * (t - 1.0));
}

/// @brief Exponential ease-out.
/// @details Fast start that asymptotically approaches 1.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_expo(double t) {
    return t >= 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}

/// @brief Exponential ease-in-out.
/// @details S-curve with exponential segments.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_expo(double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    return t < 0.5 ? 0.5 * pow(2.0, 20.0 * t - 10.0) : 1.0 - 0.5 * pow(2.0, -20.0 * t + 10.0);
}

/// @brief Circular ease-in.
/// @details Quarter-circle acceleration curve.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_circ(double t) {
    return 1.0 - sqrt(1.0 - t * t);
}

/// @brief Circular ease-out.
/// @details Quarter-circle deceleration curve.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_circ(double t) {
    double u = t - 1.0;
    return sqrt(1.0 - u * u);
}

/// @brief Circular ease-in-out.
/// @details Semicircular S-curve.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_circ(double t) {
    if (t < 0.5)
        return 0.5 * (1.0 - sqrt(1.0 - 4.0 * t * t));
    double u = 2.0 * t - 2.0;
    return 0.5 * (sqrt(1.0 - u * u) + 1.0);
}

#define BACK_C1 1.70158
#define BACK_C2 (BACK_C1 * 1.525)
#define BACK_C3 (BACK_C1 + 1.0)

/// @brief Ease-in with overshoot (pulls back first).
/// @details Moves slightly backward before accelerating forward.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_back(double t) {
    return BACK_C3 * t * t * t - BACK_C1 * t * t;
}

/// @brief Ease-out with overshoot (overshoots then settles).
/// @details Overshoots the target then returns.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_back(double t) {
    double u = t - 1.0;
    return 1.0 + BACK_C3 * u * u * u + BACK_C1 * u * u;
}

/// @brief Ease-in-out with overshoot.
/// @details Pulls back, overshoots, then settles.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_back(double t) {
    if (t < 0.5) {
        double s = 2.0 * t;
        return 0.5 * (s * s * ((BACK_C2 + 1.0) * s - BACK_C2));
    }
    double s = 2.0 * t - 2.0;
    return 0.5 * (s * s * ((BACK_C2 + 1.0) * s + BACK_C2) + 2.0);
}

#define ELASTIC_C4 (2.0 * M_PI / 3.0)
#define ELASTIC_C5 (2.0 * M_PI / 4.5)

/// @brief Elastic ease-in (rubber-band effect at start).
/// @details Oscillating approach from the start side.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_elastic(double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    return -pow(2.0, 10.0 * t - 10.0) * sin((10.0 * t - 10.75) * ELASTIC_C4);
}

/// @brief Elastic ease-out (rubber-band effect at end).
/// @details Oscillating settlement at the end.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_elastic(double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    return pow(2.0, -10.0 * t) * sin((10.0 * t - 0.75) * ELASTIC_C4) + 1.0;
}

/// @brief Elastic ease-in-out.
/// @details Oscillating S-curve with rubber-band feel.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_elastic(double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    if (t < 0.5)
        return -0.5 * pow(2.0, 20.0 * t - 10.0) * sin((20.0 * t - 11.125) * ELASTIC_C5);
    return 0.5 * pow(2.0, -20.0 * t + 10.0) * sin((20.0 * t - 11.125) * ELASTIC_C5) + 1.0;
}

/// @brief Bounce ease-out (bounces at the end).
/// @details Simulates a ball dropping and bouncing to rest.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_out_bounce(double t) {
    const double n1 = 7.5625;
    const double d1 = 2.75;
    if (t < 1.0 / d1)
        return n1 * t * t;
    if (t < 2.0 / d1) {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1) {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}

/// @brief Bounce ease-in (bounces at the start).
/// @details Inverted bounce: simulates bouncing away from origin.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_bounce(double t) {
    return 1.0 - rt_ease_out_bounce(1.0 - t);
}

/// @brief Bounce ease-in-out.
/// @details Bouncing from both sides.
/// @param t Normalized time in [0, 1] (0=start, 1=end).
/// @return Eased value in [0, 1] (may exceed range for overshoot/elastic).
double rt_ease_in_out_bounce(double t) {
    return t < 0.5 ? 0.5 * (1.0 - rt_ease_out_bounce(1.0 - 2.0 * t))
                   : 0.5 * (1.0 + rt_ease_out_bounce(2.0 * t - 1.0));
}
