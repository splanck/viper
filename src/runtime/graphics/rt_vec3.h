//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_vec3.h
// Purpose: 3D vector math utilities for Viper.Vec3 with immutable value semantics, providing
// arithmetic, dot product, cross product, magnitude, normalization, and lerp.
//
// Key invariants:
//   - Vec3 objects are immutable; all operations return new Vec3 objects.
//   - All operations are done in double-precision floating point.
//   - Normalize traps on zero-length vectors.
//   - Cross product follows the right-hand rule.
//
// Ownership/Lifetime:
//   - Vec3 objects are runtime-managed (heap-allocated, GC'd via thread-local pool).
//   - The thread-local pool (P2-3.6) resurrects Vec3 objects on finalization for reuse.
//
// Links: src/runtime/graphics/rt_vec3.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Vec3 with given x, y, and z components.
    /// @param x The x component.
    /// @param y The y component.
    /// @param z The z component.
    /// @return A new Vec3 object with the specified components.
    void *rt_vec3_new(double x, double y, double z);

    /// @brief Create a new Vec3 at origin (0, 0, 0).
    /// @return A new Vec3 object with all components set to zero.
    void *rt_vec3_zero(void);

    /// @brief Create a new Vec3 (1, 1, 1).
    /// @return A new Vec3 object with all components set to one.
    void *rt_vec3_one(void);

    /// @brief Get the X component.
    /// @param v The Vec3 object.
    /// @return The x component of the vector.
    double rt_vec3_x(void *v);

    /// @brief Get the Y component.
    /// @param v The Vec3 object.
    /// @return The y component of the vector.
    double rt_vec3_y(void *v);

    /// @brief Get the Z component.
    /// @param v The Vec3 object.
    /// @return The z component of the vector.
    double rt_vec3_z(void *v);

    /// @brief Add two vectors: a + b.
    /// @param a The first Vec3 operand.
    /// @param b The second Vec3 operand.
    /// @return A new Vec3 representing the component-wise sum
    ///         (a.x+b.x, a.y+b.y, a.z+b.z).
    void *rt_vec3_add(void *a, void *b);

    /// @brief Subtract two vectors: a - b.
    /// @param a The Vec3 minuend.
    /// @param b The Vec3 subtrahend.
    /// @return A new Vec3 representing the component-wise difference
    ///         (a.x-b.x, a.y-b.y, a.z-b.z).
    void *rt_vec3_sub(void *a, void *b);

    /// @brief Multiply vector by scalar: v * s.
    /// @param v The Vec3 operand.
    /// @param s The scalar multiplier.
    /// @return A new Vec3 with each component multiplied by @p s.
    void *rt_vec3_mul(void *v, double s);

    /// @brief Divide vector by scalar: v / s.
    /// @param v The Vec3 operand.
    /// @param s The scalar divisor (must not be zero).
    /// @return A new Vec3 with each component divided by @p s.
    void *rt_vec3_div(void *v, double s);

    /// @brief Dot product of two vectors.
    /// @param a The first Vec3 operand.
    /// @param b The second Vec3 operand.
    /// @return The scalar dot product (a.x*b.x + a.y*b.y + a.z*b.z).
    double rt_vec3_dot(void *a, void *b);

    /// @brief Cross product of two vectors (returns Vec3).
    /// @param a The first Vec3 operand (left-hand side).
    /// @param b The second Vec3 operand (right-hand side).
    /// @return A new Vec3 perpendicular to both @p a and @p b, following
    ///         the right-hand rule. The magnitude equals |a|*|b|*sin(theta).
    void *rt_vec3_cross(void *a, void *b);

    /// @brief Length (magnitude) of vector.
    /// @param v The Vec3 object.
    /// @return The Euclidean length sqrt(x^2 + y^2 + z^2).
    double rt_vec3_len(void *v);

    /// @brief Squared length of vector (avoids sqrt).
    /// @param v The Vec3 object.
    /// @return The squared Euclidean length (x^2 + y^2 + z^2). Useful for
    ///         distance comparisons without the cost of a square root.
    double rt_vec3_len_sq(void *v);

    /// @brief Normalize vector to unit length (returns zero vector if length is zero).
    /// @param v The Vec3 object.
    /// @return A new unit-length Vec3 pointing in the same direction as @p v,
    ///         or the zero vector if the input length is zero.
    void *rt_vec3_norm(void *v);

    /// @brief Distance between two points (vectors).
    /// @param a The first point as a Vec3.
    /// @param b The second point as a Vec3.
    /// @return The Euclidean distance between @p a and @p b.
    double rt_vec3_dist(void *a, void *b);

    /// @brief Linear interpolation between two vectors: a + (b - a) * t.
    /// @param a The start Vec3 (returned when t = 0).
    /// @param b The end Vec3 (returned when t = 1).
    /// @param t The interpolation parameter, typically in [0, 1].
    /// @return A new Vec3 representing the linearly interpolated position.
    void *rt_vec3_lerp(void *a, void *b, double t);

    /// @brief Negate vector: -v.
    /// @param v The Vec3 object.
    /// @return A new Vec3 with all components negated (-x, -y, -z).
    void *rt_vec3_neg(void *v);

#ifdef __cplusplus
}
#endif
