//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_quat.h
// Purpose: Quaternion math utilities for Viper.Quat.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a quaternion from components (x, y, z, w) where w is scalar.
    void *rt_quat_new(double x, double y, double z, double w);

    /// @brief Create the identity quaternion (0, 0, 0, 1).
    void *rt_quat_identity(void);

    /// @brief Create a quaternion from axis-angle representation.
    /// @param axis A Vec3 representing the rotation axis (will be normalized).
    /// @param angle Rotation angle in radians.
    void *rt_quat_from_axis_angle(void *axis, double angle);

    /// @brief Create a quaternion from Euler angles (pitch, yaw, roll) in radians.
    void *rt_quat_from_euler(double pitch, double yaw, double roll);

    /// @brief Get the X component (first imaginary).
    double rt_quat_x(void *q);

    /// @brief Get the Y component (second imaginary).
    double rt_quat_y(void *q);

    /// @brief Get the Z component (third imaginary).
    double rt_quat_z(void *q);

    /// @brief Get the W component (scalar/real part).
    double rt_quat_w(void *q);

    /// @brief Multiply two quaternions (composition of rotations): a * b.
    void *rt_quat_mul(void *a, void *b);

    /// @brief Conjugate of the quaternion: (-x, -y, -z, w).
    void *rt_quat_conjugate(void *q);

    /// @brief Inverse of the quaternion (conjugate / |q|^2).
    void *rt_quat_inverse(void *q);

    /// @brief Normalize quaternion to unit length.
    void *rt_quat_norm(void *q);

    /// @brief Length (magnitude) of quaternion.
    double rt_quat_len(void *q);

    /// @brief Squared length of quaternion.
    double rt_quat_len_sq(void *q);

    /// @brief Dot product of two quaternions.
    double rt_quat_dot(void *a, void *b);

    /// @brief Spherical linear interpolation between two quaternions.
    /// @param t Interpolation parameter (0.0 = a, 1.0 = b).
    void *rt_quat_slerp(void *a, void *b, double t);

    /// @brief Linear interpolation between two quaternions (faster, less accurate).
    void *rt_quat_lerp(void *a, void *b, double t);

    /// @brief Rotate a Vec3 by this quaternion: q * v * q^-1.
    void *rt_quat_rotate_vec3(void *q, void *v);

    /// @brief Convert quaternion to a 4x4 rotation matrix.
    void *rt_quat_to_mat4(void *q);

    /// @brief Extract the rotation axis as a Vec3 (undefined for identity).
    void *rt_quat_axis(void *q);

    /// @brief Extract the rotation angle in radians.
    double rt_quat_angle(void *q);

#ifdef __cplusplus
}
#endif
