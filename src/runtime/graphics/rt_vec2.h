//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_vec2.h
// Purpose: 2D vector math utilities for Viper.Vec2 with immutable value semantics, providing
// arithmetic, dot product, magnitude, normalization, rotation, and lerp.
//
// Key invariants:
//   - Vec2 objects are immutable; all operations return new Vec2 objects.
//   - All operations are done in double-precision floating point.
//   - Normalize traps on zero-length vectors.
//   - Angle is measured in radians.
//
// Ownership/Lifetime:
//   - Vec2 objects are runtime-managed (heap-allocated, GC'd via thread-local pool).
//   - The thread-local pool (P2-3.6) resurrects Vec2 objects on finalization for reuse.
//
// Links: src/runtime/graphics/rt_vec2.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Vec2 with given x and y components.
    /// @param x The x (horizontal) component.
    /// @param y The y (vertical) component.
    /// @return A new Vec2 object with the specified components.
    void *rt_vec2_new(double x, double y);

    /// @brief Create a new Vec2 at origin (0, 0).
    /// @return A new Vec2 object with both components set to zero.
    void *rt_vec2_zero(void);

    /// @brief Create a new Vec2 (1, 1).
    /// @return A new Vec2 object with both components set to one.
    void *rt_vec2_one(void);

    /// @brief Get the X component.
    /// @param v The Vec2 object.
    /// @return The x (horizontal) component of the vector.
    double rt_vec2_x(void *v);

    /// @brief Get the Y component.
    /// @param v The Vec2 object.
    /// @return The y (vertical) component of the vector.
    double rt_vec2_y(void *v);

    /// @brief Add two vectors: a + b.
    /// @param a The first Vec2 operand.
    /// @param b The second Vec2 operand.
    /// @return A new Vec2 representing the component-wise sum (a.x+b.x, a.y+b.y).
    void *rt_vec2_add(void *a, void *b);

    /// @brief Subtract two vectors: a - b.
    /// @param a The Vec2 minuend.
    /// @param b The Vec2 subtrahend.
    /// @return A new Vec2 representing the component-wise difference
    ///         (a.x-b.x, a.y-b.y).
    void *rt_vec2_sub(void *a, void *b);

    /// @brief Multiply vector by scalar: v * s.
    /// @param v The Vec2 operand.
    /// @param s The scalar multiplier.
    /// @return A new Vec2 with each component multiplied by @p s.
    void *rt_vec2_mul(void *v, double s);

    /// @brief Divide vector by scalar: v / s.
    /// @param v The Vec2 operand.
    /// @param s The scalar divisor (must not be zero).
    /// @return A new Vec2 with each component divided by @p s.
    void *rt_vec2_div(void *v, double s);

    /// @brief Dot product of two vectors.
    /// @param a The first Vec2 operand.
    /// @param b The second Vec2 operand.
    /// @return The scalar dot product (a.x*b.x + a.y*b.y).
    double rt_vec2_dot(void *a, void *b);

    /// @brief 2D cross product (returns scalar z-component).
    /// @param a The first Vec2 operand.
    /// @param b The second Vec2 operand.
    /// @return The scalar z-component of the cross product
    ///         (a.x*b.y - a.y*b.x). Positive if b is counter-clockwise
    ///         from a.
    double rt_vec2_cross(void *a, void *b);

    /// @brief Length (magnitude) of vector.
    /// @param v The Vec2 object.
    /// @return The Euclidean length sqrt(x^2 + y^2).
    double rt_vec2_len(void *v);

    /// @brief Squared length of vector (avoids sqrt).
    /// @param v The Vec2 object.
    /// @return The squared Euclidean length (x^2 + y^2). Useful for
    ///         distance comparisons without the cost of a square root.
    double rt_vec2_len_sq(void *v);

    /// @brief Normalize vector to unit length (returns zero vector if length is zero).
    /// @param v The Vec2 object.
    /// @return A new unit-length Vec2 pointing in the same direction as @p v,
    ///         or the zero vector if the input length is zero.
    void *rt_vec2_norm(void *v);

    /// @brief Distance between two points (vectors).
    /// @param a The first point as a Vec2.
    /// @param b The second point as a Vec2.
    /// @return The Euclidean distance between @p a and @p b.
    double rt_vec2_dist(void *a, void *b);

    /// @brief Linear interpolation between two vectors: a + (b - a) * t.
    /// @param a The start Vec2 (returned when t = 0).
    /// @param b The end Vec2 (returned when t = 1).
    /// @param t The interpolation parameter, typically in [0, 1].
    /// @return A new Vec2 representing the linearly interpolated position.
    void *rt_vec2_lerp(void *a, void *b, double t);

    /// @brief Angle of vector in radians (atan2(y, x)).
    /// @param v The Vec2 object.
    /// @return The angle in radians from the positive x-axis to the vector,
    ///         in the range [-pi, pi].
    double rt_vec2_angle(void *v);

    /// @brief Rotate vector by angle in radians.
    /// @param v The Vec2 to rotate.
    /// @param angle The rotation angle in radians (counter-clockwise positive).
    /// @return A new Vec2 rotated by @p angle radians around the origin.
    void *rt_vec2_rotate(void *v, double angle);

    /// @brief Negate vector: -v.
    /// @param v The Vec2 object.
    /// @return A new Vec2 with both components negated (-x, -y).
    void *rt_vec2_neg(void *v);

#ifdef __cplusplus
}
#endif
