//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_world_projection.c
// Purpose: Stateless 2D world-to-screen projection helpers for game code.
//
//===----------------------------------------------------------------------===//

#include "rt_world_projection.h"

#include <math.h>

/// @brief Clamp @p value to [0.0, 1.0]; non-finite input yields 0.0.
static double projection_clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Clamp a projection depth away from zero (to ±1e-6, preserving sign)
///        so perspective division never divides by ~0; non-finite -> 1.0.
static double projection_safe_depth(double depth) {
    if (!isfinite(depth))
        return 1.0;
    if (depth >= 0.0 && depth < 0.000001)
        return 0.000001;
    if (depth < 0.0 && depth > -0.000001)
        return -0.000001;
    return depth;
}

double rt_world_projection_linear_x(double world_x,
                                    double camera_x,
                                    double origin_x,
                                    double pixels_per_unit) {
    return origin_x + (world_x - camera_x) * pixels_per_unit;
}

double rt_world_projection_linear_y(
    double world_y, double camera_y, double origin_y, double pixels_per_unit, int8_t flip_y) {
    double offset = (world_y - camera_y) * pixels_per_unit;
    return flip_y ? origin_y - offset : origin_y + offset;
}

double rt_world_projection_isometric_x(double world_x,
                                       double world_y,
                                       double origin_x,
                                       double tile_width) {
    return origin_x + (world_x - world_y) * (tile_width * 0.5);
}

double rt_world_projection_isometric_y(double world_x,
                                       double world_y,
                                       double origin_y,
                                       double tile_height) {
    return origin_y + (world_x + world_y) * (tile_height * 0.5);
}

double rt_world_projection_perspective_scale(
    double depth, double near_depth, double far_depth, double near_scale, double far_scale) {
    if (!isfinite(depth) || !isfinite(near_depth) || !isfinite(far_depth) ||
        !isfinite(near_scale) || !isfinite(far_scale)) {
        return near_scale;
    }

    double span = far_depth - near_depth;
    if (fabs(span) < 0.000001)
        return near_scale;

    double t = projection_clamp01((depth - near_depth) / span);
    return near_scale + (far_scale - near_scale) * t;
}

double rt_world_projection_perspective_x(double world_x,
                                         double center_x,
                                         double depth,
                                         double focal_length) {
    return center_x + world_x * focal_length / projection_safe_depth(depth);
}

double rt_world_projection_perspective_y(
    double world_y, double center_y, double depth, double focal_length, int8_t flip_y) {
    double offset = world_y * focal_length / projection_safe_depth(depth);
    return flip_y ? center_y - offset : center_y + offset;
}
