//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_spline.h
// Purpose: Spline interpolation for smooth curved paths, providing Catmull-Rom splines from Vec2
// control points with uniform and non-uniform parametrization.
//
// Key invariants:
//   - At least 2 control points are required for a valid spline.
//   - Catmull-Rom splines pass through all control points.
//   - The parameter t is in [0, 1] over the full spline length.
//   - Point evaluation is O(n) in the number of control points.
//
// Ownership/Lifetime:
//   - Spline objects are heap-allocated opaque pointers.
//   - Control point Seq is consumed at creation; the spline copies the data.
//
// Links: src/runtime/graphics/rt_spline.c (implementation), src/runtime/graphics/rt_vec2.h
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
