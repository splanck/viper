//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_mat4.h
// Purpose: 4x4 matrix math for 3D transformations (Viper.Mat4).
// Key invariants: Row-major order, right-multiply with column vectors.
// Ownership/Lifetime: Mat4 objects are runtime-managed and immutable.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Construction
    //=========================================================================

    /// @brief Create a 4x4 matrix with specified elements (row-major).
    void *rt_mat4_new(double m00,
                      double m01,
                      double m02,
                      double m03,
                      double m10,
                      double m11,
                      double m12,
                      double m13,
                      double m20,
                      double m21,
                      double m22,
                      double m23,
                      double m30,
                      double m31,
                      double m32,
                      double m33);

    /// @brief Create a 4x4 identity matrix.
    void *rt_mat4_identity(void);

    /// @brief Create a 4x4 zero matrix.
    void *rt_mat4_zero(void);

    //=========================================================================
    // 3D Transformation Factories
    //=========================================================================

    /// @brief Create a 3D translation matrix.
    void *rt_mat4_translate(double tx, double ty, double tz);

    /// @brief Create a 3D scaling matrix.
    void *rt_mat4_scale(double sx, double sy, double sz);

    /// @brief Create a uniform 3D scaling matrix.
    void *rt_mat4_scale_uniform(double s);

    /// @brief Create a rotation matrix around the X axis.
    /// @param angle Rotation angle in radians.
    void *rt_mat4_rotate_x(double angle);

    /// @brief Create a rotation matrix around the Y axis.
    /// @param angle Rotation angle in radians.
    void *rt_mat4_rotate_y(double angle);

    /// @brief Create a rotation matrix around the Z axis.
    /// @param angle Rotation angle in radians.
    void *rt_mat4_rotate_z(double angle);

    /// @brief Create a rotation matrix around an arbitrary axis.
    /// @param axis Normalized axis vector (Vec3).
    /// @param angle Rotation angle in radians.
    void *rt_mat4_rotate_axis(void *axis, double angle);

    //=========================================================================
    // Projection Matrices
    //=========================================================================

    /// @brief Create a perspective projection matrix.
    /// @param fov Field of view in radians.
    /// @param aspect Aspect ratio (width/height).
    /// @param near Near clipping plane.
    /// @param far Far clipping plane.
    void *rt_mat4_perspective(double fov, double aspect, double near, double far);

    /// @brief Create an orthographic projection matrix.
    void *rt_mat4_ortho(
        double left, double right, double bottom, double top, double near, double far);

    /// @brief Create a look-at view matrix.
    /// @param eye Camera position (Vec3).
    /// @param target Look-at target (Vec3).
    /// @param up Up direction (Vec3).
    void *rt_mat4_look_at(void *eye, void *target, void *up);

    //=========================================================================
    // Element Access
    //=========================================================================

    /// @brief Get element at row, col.
    double rt_mat4_get(void *m, int64_t row, int64_t col);

    //=========================================================================
    // Arithmetic
    //=========================================================================

    /// @brief Matrix addition: a + b.
    void *rt_mat4_add(void *a, void *b);

    /// @brief Matrix subtraction: a - b.
    void *rt_mat4_sub(void *a, void *b);

    /// @brief Matrix multiplication: a * b.
    void *rt_mat4_mul(void *a, void *b);

    /// @brief Matrix-scalar multiplication: m * s.
    void *rt_mat4_mul_scalar(void *m, double s);

    /// @brief Transform a 3D point (applies translation).
    /// @details Uses homogeneous coordinates: [x, y, z, 1].
    void *rt_mat4_transform_point(void *m, void *v);

    /// @brief Transform a 3D vector (ignores translation).
    /// @details Uses homogeneous coordinates: [x, y, z, 0].
    void *rt_mat4_transform_vec(void *m, void *v);

    //=========================================================================
    // Matrix Operations
    //=========================================================================

    /// @brief Transpose the matrix.
    void *rt_mat4_transpose(void *m);

    /// @brief Compute the determinant.
    double rt_mat4_det(void *m);

    /// @brief Compute the inverse matrix (returns identity if singular).
    void *rt_mat4_inverse(void *m);

    /// @brief Negate all elements: -m.
    void *rt_mat4_neg(void *m);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Check if two matrices are approximately equal.
    int8_t rt_mat4_eq(void *a, void *b, double epsilon);

#ifdef __cplusplus
}
#endif
