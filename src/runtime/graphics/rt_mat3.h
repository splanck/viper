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
// Links: Viper.Mat3 standard library module; see also rt_vec2.h.
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
    /// @param m00 Element at row 0, column 0.
    /// @param m01 Element at row 0, column 1.
    /// @param m02 Element at row 0, column 2.
    /// @param m10 Element at row 1, column 0.
    /// @param m11 Element at row 1, column 1.
    /// @param m12 Element at row 1, column 2.
    /// @param m20 Element at row 2, column 0.
    /// @param m21 Element at row 2, column 1.
    /// @param m22 Element at row 2, column 2.
    /// @return A new Mat3 object with the specified elements.
    void *rt_mat3_new(double m00,
                      double m01,
                      double m02,
                      double m10,
                      double m11,
                      double m12,
                      double m20,
                      double m21,
                      double m22);

    /// @brief Create a 3x3 identity matrix.
    /// @return A new Mat3 with ones on the diagonal and zeros elsewhere.
    void *rt_mat3_identity(void);

    /// @brief Create a 3x3 zero matrix.
    /// @return A new Mat3 with all elements set to zero.
    void *rt_mat3_zero(void);

    //=========================================================================
    // 2D Transformation Factories
    //=========================================================================

    /// @brief Create a 2D translation matrix.
    /// @param tx Translation in X.
    /// @param ty Translation in Y.
    /// @return A new Mat3 encoding a 2D translation by (@p tx, @p ty).
    void *rt_mat3_translate(double tx, double ty);

    /// @brief Create a 2D scaling matrix.
    /// @param sx Scale factor in X.
    /// @param sy Scale factor in Y.
    /// @return A new Mat3 encoding a 2D scale by (@p sx, @p sy).
    void *rt_mat3_scale(double sx, double sy);

    /// @brief Create a 2D uniform scaling matrix.
    /// @param s Uniform scale factor.
    /// @return A new Mat3 encoding a uniform 2D scale by @p s.
    void *rt_mat3_scale_uniform(double s);

    /// @brief Create a 2D rotation matrix (counter-clockwise).
    /// @param angle Rotation angle in radians.
    /// @return A new Mat3 encoding a counter-clockwise rotation by @p angle.
    void *rt_mat3_rotate(double angle);

    /// @brief Create a 2D shear matrix.
    /// @param sx Shear factor in X (along Y axis).
    /// @param sy Shear factor in Y (along X axis).
    /// @return A new Mat3 encoding a 2D shear transformation.
    void *rt_mat3_shear(double sx, double sy);

    //=========================================================================
    // Element Access
    //=========================================================================

    /// @brief Get element at row, col.
    /// @param m Matrix object.
    /// @param row Row index (0-2).
    /// @param col Column index (0-2).
    /// @return The element value at the specified row and column.
    double rt_mat3_get(void *m, int64_t row, int64_t col);

    /// @brief Get a row as a Vec3.
    /// @param m Matrix object.
    /// @param row Row index (0-2).
    /// @return A new Vec3 containing the three elements of the specified row.
    void *rt_mat3_row(void *m, int64_t row);

    /// @brief Get a column as a Vec3.
    /// @param m Matrix object.
    /// @param col Column index (0-2).
    /// @return A new Vec3 containing the three elements of the specified column.
    void *rt_mat3_col(void *m, int64_t col);

    //=========================================================================
    // Arithmetic
    //=========================================================================

    /// @brief Matrix addition: a + b.
    /// @param a The first Mat3 operand.
    /// @param b The second Mat3 operand.
    /// @return A new Mat3 with each element being the sum of the
    ///         corresponding elements in @p a and @p b.
    void *rt_mat3_add(void *a, void *b);

    /// @brief Matrix subtraction: a - b.
    /// @param a The Mat3 minuend.
    /// @param b The Mat3 subtrahend.
    /// @return A new Mat3 with each element being the difference of the
    ///         corresponding elements in @p a and @p b.
    void *rt_mat3_sub(void *a, void *b);

    /// @brief Matrix multiplication: a * b.
    /// @param a The left-hand Mat3 operand.
    /// @param b The right-hand Mat3 operand.
    /// @return A new Mat3 representing the matrix product a * b.
    void *rt_mat3_mul(void *a, void *b);

    /// @brief Matrix-scalar multiplication: m * s.
    /// @param m The Mat3 operand.
    /// @param s The scalar multiplier.
    /// @return A new Mat3 with every element multiplied by @p s.
    void *rt_mat3_mul_scalar(void *m, double s);

    /// @brief Transform a 2D point (applies translation).
    /// @details Uses homogeneous coordinates: [x, y, 1].
    /// @param m The transformation Mat3.
    /// @param v A Vec2 representing the point to transform.
    /// @return A new Vec2 representing the transformed point, including
    ///         the translation component of the matrix.
    void *rt_mat3_transform_point(void *m, void *v);

    /// @brief Transform a 2D vector (ignores translation).
    /// @details Uses homogeneous coordinates: [x, y, 0].
    /// @param m The transformation Mat3.
    /// @param v A Vec2 representing the direction vector to transform.
    /// @return A new Vec2 representing the transformed direction, with the
    ///         translation component of the matrix ignored.
    void *rt_mat3_transform_vec(void *m, void *v);

    //=========================================================================
    // Matrix Operations
    //=========================================================================

    /// @brief Transpose the matrix.
    /// @param m The Mat3 to transpose.
    /// @return A new Mat3 where rows and columns are swapped (M[i][j] becomes
    ///         M[j][i]).
    void *rt_mat3_transpose(void *m);

    /// @brief Compute the determinant.
    /// @param m The Mat3 to compute the determinant of.
    /// @return The scalar determinant of the matrix. A zero determinant
    ///         indicates a singular (non-invertible) matrix.
    double rt_mat3_det(void *m);

    /// @brief Compute the inverse matrix (returns identity if singular).
    /// @param m The Mat3 to invert.
    /// @return A new Mat3 that is the multiplicative inverse of @p m,
    ///         or the identity matrix if @p m is singular.
    void *rt_mat3_inverse(void *m);

    /// @brief Negate all elements: -m.
    /// @param m The Mat3 to negate.
    /// @return A new Mat3 with every element negated.
    void *rt_mat3_neg(void *m);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Check if two matrices are approximately equal.
    /// @param a The first Mat3 to compare.
    /// @param b The second Mat3 to compare.
    /// @param epsilon Maximum difference per element (default 1e-9).
    /// @return 1 if all corresponding elements differ by at most @p epsilon,
    ///         0 otherwise.
    int8_t rt_mat3_eq(void *a, void *b, double epsilon);

#ifdef __cplusplus
}
#endif
