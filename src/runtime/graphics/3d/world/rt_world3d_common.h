//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static inline double rt_world3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

static inline double rt_world3d_clamp_abs_or(double value, double fallback, double max_abs) {
    if (!isfinite(value))
        return fallback;
    if (value > max_abs)
        return max_abs;
    if (value < -max_abs)
        return -max_abs;
    return value;
}

static inline double rt_world3d_clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

static inline double rt_world3d_clamp_positive_or(double value,
                                                  double fallback,
                                                  double max_value) {
    if (!isfinite(value) || value <= 0.0)
        return fallback;
    return value > max_value ? max_value : value;
}

static inline double rt_world3d_clamp_nonnegative(double value, double max_value) {
    if (!isfinite(value) || value < 0.0)
        return 0.0;
    return value > max_value ? max_value : value;
}

static inline int rt_world3d_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}
