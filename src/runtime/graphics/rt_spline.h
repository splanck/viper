//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_spline.h
// Purpose: Spline interpolation utilities for Viper.Spline.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a Catmull-Rom spline from a Seq of Vec2 control points.
    /// @param points A Seq of Vec2 objects (at least 2 points required).
    void *rt_spline_catmull_rom(void *points);

    /// @brief Create a cubic Bezier spline from 4 Vec2 control points.
    void *rt_spline_bezier(void *p0, void *p1, void *p2, void *p3);

    /// @brief Create a linear spline (polyline) from a Seq of Vec2 control points.
    void *rt_spline_linear(void *points);

    /// @brief Evaluate the spline at parameter t (0.0 to 1.0).
    /// @return A Vec2 position on the spline.
    void *rt_spline_eval(void *spline, double t);

    /// @brief Evaluate the tangent (derivative) at parameter t.
    /// @return A Vec2 tangent direction (not normalized).
    void *rt_spline_tangent(void *spline, double t);

    /// @brief Get the number of control points in the spline.
    int64_t rt_spline_point_count(void *spline);

    /// @brief Get a control point by index.
    void *rt_spline_point_at(void *spline, int64_t index);

    /// @brief Approximate the arc length of the spline between t0 and t1.
    /// @param steps Number of integration steps (higher = more accurate).
    double rt_spline_arc_length(void *spline, double t0, double t1, int64_t steps);

    /// @brief Sample the spline into a Seq of Vec2 points.
    /// @param count Number of evenly-spaced samples along the curve.
    void *rt_spline_sample(void *spline, int64_t count);

#ifdef __cplusplus
}
#endif
