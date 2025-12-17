//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_vec3.h
// Purpose: 3D vector math utilities for Viper.Vec3.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Vec3 with given x, y, and z components.
    void *rt_vec3_new(double x, double y, double z);

    /// @brief Create a new Vec3 at origin (0, 0, 0).
    void *rt_vec3_zero(void);

    /// @brief Create a new Vec3 (1, 1, 1).
    void *rt_vec3_one(void);

    /// @brief Get the X component.
    double rt_vec3_x(void *v);

    /// @brief Get the Y component.
    double rt_vec3_y(void *v);

    /// @brief Get the Z component.
    double rt_vec3_z(void *v);

    /// @brief Add two vectors: a + b.
    void *rt_vec3_add(void *a, void *b);

    /// @brief Subtract two vectors: a - b.
    void *rt_vec3_sub(void *a, void *b);

    /// @brief Multiply vector by scalar: v * s.
    void *rt_vec3_mul(void *v, double s);

    /// @brief Divide vector by scalar: v / s.
    void *rt_vec3_div(void *v, double s);

    /// @brief Dot product of two vectors.
    double rt_vec3_dot(void *a, void *b);

    /// @brief Cross product of two vectors (returns Vec3).
    void *rt_vec3_cross(void *a, void *b);

    /// @brief Length (magnitude) of vector.
    double rt_vec3_len(void *v);

    /// @brief Squared length of vector (avoids sqrt).
    double rt_vec3_len_sq(void *v);

    /// @brief Normalize vector to unit length (returns zero vector if length is zero).
    void *rt_vec3_norm(void *v);

    /// @brief Distance between two points (vectors).
    double rt_vec3_dist(void *a, void *b);

    /// @brief Linear interpolation between two vectors: a + (b - a) * t.
    void *rt_vec3_lerp(void *a, void *b, double t);

    /// @brief Negate vector: -v.
    void *rt_vec3_neg(void *v);

#ifdef __cplusplus
}
#endif
