//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_quat.h
// Purpose: Quaternion math for 3D rotation (Viper.Quat), providing construction from axis/angle/Euler, conjugate, product, SLERP, and conversion to/from rotation matrices.
//
// Key invariants:
//   - Quaternions are stored as (x, y, z, w) where w is the scalar part.
//   - Unit quaternions represent 3D rotations; non-unit quaternions produce undefined rotation results.
//   - All operations return new Quat objects; inputs are not modified.
//   - SLERP correctly handles antipodal quaternions by negating one operand.
//
// Ownership/Lifetime:
//   - Quat objects are runtime-managed (heap-allocated).
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_quat.c (implementation), src/runtime/graphics/rt_mat4.h, src/runtime/graphics/rt_vec3.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a quaternion from components (x, y, z, w) where w is scalar.
    /// @param x The first imaginary component (i).
    /// @param y The second imaginary component (j).
    /// @param z The third imaginary component (k).
    /// @param w The scalar (real) component.
    /// @return A new quaternion object with the specified components.
    void *rt_quat_new(double x, double y, double z, double w);

    /// @brief Create the identity quaternion (0, 0, 0, 1).
    /// @return A new quaternion representing no rotation.
    void *rt_quat_identity(void);

    /// @brief Create a quaternion from axis-angle representation.
    /// @param axis A Vec3 representing the rotation axis (will be normalized).
    /// @param angle Rotation angle in radians.
    /// @return A new unit quaternion encoding a rotation of @p angle radians
    ///         about @p axis.
    void *rt_quat_from_axis_angle(void *axis, double angle);

    /// @brief Create a quaternion from Euler angles (pitch, yaw, roll) in radians.
    /// @param pitch Rotation about the X axis in radians.
    /// @param yaw Rotation about the Y axis in radians.
    /// @param roll Rotation about the Z axis in radians.
    /// @return A new unit quaternion representing the combined rotation,
    ///         applied in yaw-pitch-roll (YXZ) intrinsic order.
    void *rt_quat_from_euler(double pitch, double yaw, double roll);

    /// @brief Get the X component (first imaginary).
    /// @param q The quaternion object.
    /// @return The x (i) component of the quaternion.
    double rt_quat_x(void *q);

    /// @brief Get the Y component (second imaginary).
    /// @param q The quaternion object.
    /// @return The y (j) component of the quaternion.
    double rt_quat_y(void *q);

    /// @brief Get the Z component (third imaginary).
    /// @param q The quaternion object.
    /// @return The z (k) component of the quaternion.
    double rt_quat_z(void *q);

    /// @brief Get the W component (scalar/real part).
    /// @param q The quaternion object.
    /// @return The w (scalar) component of the quaternion.
    double rt_quat_w(void *q);

    /// @brief Multiply two quaternions (composition of rotations): a * b.
    /// @param a The left-hand quaternion (applied second in rotation order).
    /// @param b The right-hand quaternion (applied first in rotation order).
    /// @return A new quaternion representing the Hamilton product a*b,
    ///         equivalent to applying rotation @p b then rotation @p a.
    void *rt_quat_mul(void *a, void *b);

    /// @brief Conjugate of the quaternion: (-x, -y, -z, w).
    /// @param q The quaternion object.
    /// @return A new quaternion with the imaginary components negated.
    ///         For unit quaternions, the conjugate equals the inverse.
    void *rt_quat_conjugate(void *q);

    /// @brief Inverse of the quaternion (conjugate / |q|^2).
    /// @param q The quaternion object.
    /// @return A new quaternion that is the multiplicative inverse of @p q.
    ///         For unit quaternions this is equivalent to the conjugate.
    void *rt_quat_inverse(void *q);

    /// @brief Normalize quaternion to unit length.
    /// @param q The quaternion object.
    /// @return A new unit quaternion in the same direction as @p q
    ///         (|result| = 1).
    void *rt_quat_norm(void *q);

    /// @brief Length (magnitude) of quaternion.
    /// @param q The quaternion object.
    /// @return The Euclidean norm sqrt(x^2 + y^2 + z^2 + w^2).
    double rt_quat_len(void *q);

    /// @brief Squared length of quaternion.
    /// @param q The quaternion object.
    /// @return The squared norm (x^2 + y^2 + z^2 + w^2). Useful for
    ///         checking unit-length without the cost of a square root.
    double rt_quat_len_sq(void *q);

    /// @brief Dot product of two quaternions.
    /// @param a The first quaternion operand.
    /// @param b The second quaternion operand.
    /// @return The four-dimensional dot product
    ///         (a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w).
    double rt_quat_dot(void *a, void *b);

    /// @brief Spherical linear interpolation between two quaternions.
    /// @param a The start quaternion (returned when t = 0).
    /// @param b The end quaternion (returned when t = 1).
    /// @param t Interpolation parameter (0.0 = a, 1.0 = b).
    /// @return A new quaternion representing the shortest-arc spherical
    ///         interpolation between @p a and @p b at parameter @p t.
    void *rt_quat_slerp(void *a, void *b, double t);

    /// @brief Linear interpolation between two quaternions (faster, less accurate).
    /// @param a The start quaternion (returned when t = 0).
    /// @param b The end quaternion (returned when t = 1).
    /// @param t Interpolation parameter (0.0 = a, 1.0 = b).
    /// @return A new normalized quaternion computed by component-wise
    ///         linear interpolation. Faster than slerp but less uniform.
    void *rt_quat_lerp(void *a, void *b, double t);

    /// @brief Rotate a Vec3 by this quaternion: q * v * q^-1.
    /// @param q The unit quaternion representing the rotation.
    /// @param v The Vec3 to rotate.
    /// @return A new Vec3 obtained by rotating @p v by the rotation
    ///         encoded in @p q.
    void *rt_quat_rotate_vec3(void *q, void *v);

    /// @brief Convert quaternion to a 4x4 rotation matrix.
    /// @param q The unit quaternion to convert.
    /// @return A new Mat4 representing the same rotation as @p q.
    void *rt_quat_to_mat4(void *q);

    /// @brief Extract the rotation axis as a Vec3 (undefined for identity).
    /// @param q The quaternion object.
    /// @return A new unit-length Vec3 representing the rotation axis.
    ///         Undefined (degenerate) when @p q is the identity quaternion.
    void *rt_quat_axis(void *q);

    /// @brief Extract the rotation angle in radians.
    /// @param q The quaternion object.
    /// @return The rotation angle in radians, in the range [0, 2*pi).
    double rt_quat_angle(void *q);

#ifdef __cplusplus
}
#endif
