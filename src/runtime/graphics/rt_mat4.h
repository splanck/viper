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
// Links: Viper.Mat4 standard library module; see also rt_vec3.h, rt_quat.h.
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
    /// @param m00 Element at row 0, column 0.
    /// @param m01 Element at row 0, column 1.
    /// @param m02 Element at row 0, column 2.
    /// @param m03 Element at row 0, column 3.
    /// @param m10 Element at row 1, column 0.
    /// @param m11 Element at row 1, column 1.
    /// @param m12 Element at row 1, column 2.
    /// @param m13 Element at row 1, column 3.
    /// @param m20 Element at row 2, column 0.
    /// @param m21 Element at row 2, column 1.
    /// @param m22 Element at row 2, column 2.
    /// @param m23 Element at row 2, column 3.
    /// @param m30 Element at row 3, column 0.
    /// @param m31 Element at row 3, column 1.
    /// @param m32 Element at row 3, column 2.
    /// @param m33 Element at row 3, column 3.
    /// @return A new Mat4 object with the specified elements.
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
    /// @return A new Mat4 with ones on the diagonal and zeros elsewhere.
    void *rt_mat4_identity(void);

    /// @brief Create a 4x4 zero matrix.
    /// @return A new Mat4 with all elements set to zero.
    void *rt_mat4_zero(void);

    //=========================================================================
    // 3D Transformation Factories
    //=========================================================================

    /// @brief Create a 3D translation matrix.
    /// @param tx Translation along the X axis.
    /// @param ty Translation along the Y axis.
    /// @param tz Translation along the Z axis.
    /// @return A new Mat4 encoding a translation by (@p tx, @p ty, @p tz).
    void *rt_mat4_translate(double tx, double ty, double tz);

    /// @brief Create a 3D scaling matrix.
    /// @param sx Scale factor along the X axis.
    /// @param sy Scale factor along the Y axis.
    /// @param sz Scale factor along the Z axis.
    /// @return A new Mat4 encoding a non-uniform scale by (@p sx, @p sy, @p sz).
    void *rt_mat4_scale(double sx, double sy, double sz);

    /// @brief Create a uniform 3D scaling matrix.
    /// @param s Uniform scale factor applied to all three axes.
    /// @return A new Mat4 encoding a uniform scale by @p s.
    void *rt_mat4_scale_uniform(double s);

    /// @brief Create a rotation matrix around the X axis.
    /// @param angle Rotation angle in radians.
    /// @return A new Mat4 encoding a rotation about the X axis by @p angle.
    void *rt_mat4_rotate_x(double angle);

    /// @brief Create a rotation matrix around the Y axis.
    /// @param angle Rotation angle in radians.
    /// @return A new Mat4 encoding a rotation about the Y axis by @p angle.
    void *rt_mat4_rotate_y(double angle);

    /// @brief Create a rotation matrix around the Z axis.
    /// @param angle Rotation angle in radians.
    /// @return A new Mat4 encoding a rotation about the Z axis by @p angle.
    void *rt_mat4_rotate_z(double angle);

    /// @brief Create a rotation matrix around an arbitrary axis.
    /// @param axis Normalized axis vector (Vec3).
    /// @param angle Rotation angle in radians.
    /// @return A new Mat4 encoding a rotation of @p angle radians about
    ///         @p axis (Rodrigues' rotation formula).
    void *rt_mat4_rotate_axis(void *axis, double angle);

    //=========================================================================
    // Projection Matrices
    //=========================================================================

    /// @brief Create a perspective projection matrix.
    /// @param fov Vertical field of view in radians.
    /// @param aspect Aspect ratio (width / height).
    /// @param near Distance to the near clipping plane (must be positive).
    /// @param far Distance to the far clipping plane (must be > near).
    /// @return A new Mat4 encoding a symmetric perspective projection that
    ///         maps the view frustum to normalized device coordinates.
    void *rt_mat4_perspective(double fov, double aspect, double near, double far);

    /// @brief Create an orthographic projection matrix.
    /// @param left Left boundary of the view volume.
    /// @param right Right boundary of the view volume.
    /// @param bottom Bottom boundary of the view volume.
    /// @param top Top boundary of the view volume.
    /// @param near Near clipping plane distance.
    /// @param far Far clipping plane distance.
    /// @return A new Mat4 encoding an orthographic projection that maps
    ///         the specified box to normalized device coordinates.
    void *rt_mat4_ortho(
        double left, double right, double bottom, double top, double near, double far);

    /// @brief Create a look-at view matrix.
    /// @param eye Camera position (Vec3).
    /// @param target Look-at target point (Vec3).
    /// @param up Up direction vector (Vec3, need not be unit length).
    /// @return A new Mat4 that transforms world coordinates into the camera's
    ///         view space, with the camera at @p eye looking toward @p target.
    void *rt_mat4_look_at(void *eye, void *target, void *up);

    //=========================================================================
    // Element Access
    //=========================================================================

    /// @brief Get element at row, col.
    /// @param m The Mat4 object.
    /// @param row Row index (0-3).
    /// @param col Column index (0-3).
    /// @return The element value at the specified row and column.
    double rt_mat4_get(void *m, int64_t row, int64_t col);

    //=========================================================================
    // Arithmetic
    //=========================================================================

    /// @brief Matrix addition: a + b.
    /// @param a The first Mat4 operand.
    /// @param b The second Mat4 operand.
    /// @return A new Mat4 with each element being the sum of the
    ///         corresponding elements in @p a and @p b.
    void *rt_mat4_add(void *a, void *b);

    /// @brief Matrix subtraction: a - b.
    /// @param a The Mat4 minuend.
    /// @param b The Mat4 subtrahend.
    /// @return A new Mat4 with each element being the difference of the
    ///         corresponding elements in @p a and @p b.
    void *rt_mat4_sub(void *a, void *b);

    /// @brief Matrix multiplication: a * b.
    /// @param a The left-hand Mat4 operand.
    /// @param b The right-hand Mat4 operand.
    /// @return A new Mat4 representing the matrix product a * b.
    void *rt_mat4_mul(void *a, void *b);

    /// @brief Matrix-scalar multiplication: m * s.
    /// @param m The Mat4 operand.
    /// @param s The scalar multiplier.
    /// @return A new Mat4 with every element multiplied by @p s.
    void *rt_mat4_mul_scalar(void *m, double s);

    /// @brief Transform a 3D point (applies translation).
    /// @details Uses homogeneous coordinates: [x, y, z, 1].
    /// @param m The transformation Mat4.
    /// @param v A Vec3 representing the point to transform.
    /// @return A new Vec3 representing the transformed point, including
    ///         the translation component of the matrix.
    void *rt_mat4_transform_point(void *m, void *v);

    /// @brief Transform a 3D vector (ignores translation).
    /// @details Uses homogeneous coordinates: [x, y, z, 0].
    /// @param m The transformation Mat4.
    /// @param v A Vec3 representing the direction vector to transform.
    /// @return A new Vec3 representing the transformed direction, with the
    ///         translation component of the matrix ignored.
    void *rt_mat4_transform_vec(void *m, void *v);

    //=========================================================================
    // Matrix Operations
    //=========================================================================

    /// @brief Transpose the matrix.
    /// @param m The Mat4 to transpose.
    /// @return A new Mat4 where rows and columns are swapped (M[i][j] becomes
    ///         M[j][i]).
    void *rt_mat4_transpose(void *m);

    /// @brief Compute the determinant.
    /// @param m The Mat4 to compute the determinant of.
    /// @return The scalar determinant of the matrix. A zero determinant
    ///         indicates a singular (non-invertible) matrix.
    double rt_mat4_det(void *m);

    /// @brief Compute the inverse matrix (returns identity if singular).
    /// @param m The Mat4 to invert.
    /// @return A new Mat4 that is the multiplicative inverse of @p m,
    ///         or the identity matrix if @p m is singular.
    void *rt_mat4_inverse(void *m);

    /// @brief Negate all elements: -m.
    /// @param m The Mat4 to negate.
    /// @return A new Mat4 with every element negated.
    void *rt_mat4_neg(void *m);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Check if two matrices are approximately equal.
    /// @param a The first Mat4 to compare.
    /// @param b The second Mat4 to compare.
    /// @param epsilon Maximum allowed absolute difference per element.
    /// @return 1 if all corresponding elements differ by at most @p epsilon,
    ///         0 otherwise.
    int8_t rt_mat4_eq(void *a, void *b, double epsilon);

#ifdef __cplusplus
}
#endif
