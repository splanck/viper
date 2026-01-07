//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_vec2.h
// Purpose: 2D vector math utilities for Viper.Vec2.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Vec2 with given x and y components.
    void *rt_vec2_new(double x, double y);

    /// @brief Create a new Vec2 at origin (0, 0).
    void *rt_vec2_zero(void);

    /// @brief Create a new Vec2 (1, 1).
    void *rt_vec2_one(void);

    /// @brief Get the X component.
    double rt_vec2_x(void *v);

    /// @brief Get the Y component.
    double rt_vec2_y(void *v);

    /// @brief Add two vectors: a + b.
    void *rt_vec2_add(void *a, void *b);

    /// @brief Subtract two vectors: a - b.
    void *rt_vec2_sub(void *a, void *b);

    /// @brief Multiply vector by scalar: v * s.
    void *rt_vec2_mul(void *v, double s);

    /// @brief Divide vector by scalar: v / s.
    void *rt_vec2_div(void *v, double s);

    /// @brief Dot product of two vectors.
    double rt_vec2_dot(void *a, void *b);

    /// @brief 2D cross product (returns scalar z-component).
    double rt_vec2_cross(void *a, void *b);

    /// @brief Length (magnitude) of vector.
    double rt_vec2_len(void *v);

    /// @brief Squared length of vector (avoids sqrt).
    double rt_vec2_len_sq(void *v);

    /// @brief Normalize vector to unit length (returns zero vector if length is zero).
    void *rt_vec2_norm(void *v);

    /// @brief Distance between two points (vectors).
    double rt_vec2_dist(void *a, void *b);

    /// @brief Linear interpolation between two vectors: a + (b - a) * t.
    void *rt_vec2_lerp(void *a, void *b, double t);

    /// @brief Angle of vector in radians (atan2(y, x)).
    double rt_vec2_angle(void *v);

    /// @brief Rotate vector by angle in radians.
    void *rt_vec2_rotate(void *v, double angle);

    /// @brief Negate vector: -v.
    void *rt_vec2_neg(void *v);

#ifdef __cplusplus
}
#endif
