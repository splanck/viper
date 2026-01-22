//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_mat3.h
// Purpose: 3x3 matrix math for 2D transformations (Viper.Mat3).
// Key invariants: Row-major order, right-multiply with column vectors.
// Ownership/Lifetime: Mat3 objects are runtime-managed and immutable.
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

    /// @brief Create a 3x3 matrix with specified elements (row-major).
    /// @details Elements are laid out as:
    ///          | m00 m01 m02 |
    ///          | m10 m11 m12 |
    ///          | m20 m21 m22 |
    void *rt_mat3_new(double m00, double m01, double m02,
                      double m10, double m11, double m12,
                      double m20, double m21, double m22);

    /// @brief Create a 3x3 identity matrix.
    void *rt_mat3_identity(void);

    /// @brief Create a 3x3 zero matrix.
    void *rt_mat3_zero(void);

    //=========================================================================
    // 2D Transformation Factories
    //=========================================================================

    /// @brief Create a 2D translation matrix.
    /// @param tx Translation in X.
    /// @param ty Translation in Y.
    void *rt_mat3_translate(double tx, double ty);

    /// @brief Create a 2D scaling matrix.
    /// @param sx Scale factor in X.
    /// @param sy Scale factor in Y.
    void *rt_mat3_scale(double sx, double sy);

    /// @brief Create a 2D uniform scaling matrix.
    /// @param s Uniform scale factor.
    void *rt_mat3_scale_uniform(double s);

    /// @brief Create a 2D rotation matrix (counter-clockwise).
    /// @param angle Rotation angle in radians.
    void *rt_mat3_rotate(double angle);

    /// @brief Create a 2D shear matrix.
    /// @param sx Shear factor in X (along Y axis).
    /// @param sy Shear factor in Y (along X axis).
    void *rt_mat3_shear(double sx, double sy);

    //=========================================================================
    // Element Access
    //=========================================================================

    /// @brief Get element at row, col.
    /// @param m Matrix object.
    /// @param row Row index (0-2).
    /// @param col Column index (0-2).
    double rt_mat3_get(void *m, int64_t row, int64_t col);

    /// @brief Get a row as a Vec3.
    void *rt_mat3_row(void *m, int64_t row);

    /// @brief Get a column as a Vec3.
    void *rt_mat3_col(void *m, int64_t col);

    //=========================================================================
    // Arithmetic
    //=========================================================================

    /// @brief Matrix addition: a + b.
    void *rt_mat3_add(void *a, void *b);

    /// @brief Matrix subtraction: a - b.
    void *rt_mat3_sub(void *a, void *b);

    /// @brief Matrix multiplication: a * b.
    void *rt_mat3_mul(void *a, void *b);

    /// @brief Matrix-scalar multiplication: m * s.
    void *rt_mat3_mul_scalar(void *m, double s);

    /// @brief Transform a 2D point (applies translation).
    /// @details Uses homogeneous coordinates: [x, y, 1].
    void *rt_mat3_transform_point(void *m, void *v);

    /// @brief Transform a 2D vector (ignores translation).
    /// @details Uses homogeneous coordinates: [x, y, 0].
    void *rt_mat3_transform_vec(void *m, void *v);

    //=========================================================================
    // Matrix Operations
    //=========================================================================

    /// @brief Transpose the matrix.
    void *rt_mat3_transpose(void *m);

    /// @brief Compute the determinant.
    double rt_mat3_det(void *m);

    /// @brief Compute the inverse matrix (returns identity if singular).
    void *rt_mat3_inverse(void *m);

    /// @brief Negate all elements: -m.
    void *rt_mat3_neg(void *m);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Check if two matrices are approximately equal.
    /// @param epsilon Maximum difference per element (default 1e-9).
    int8_t rt_mat3_eq(void *a, void *b, double epsilon);

#ifdef __cplusplus
}
#endif
