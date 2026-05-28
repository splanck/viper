//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_mat3.c
// Purpose: 3×3 matrix type for 2D affine transformations in Viper. Supports
//   construction from rotation/scale/translation components, matrix–matrix
//   multiplication (transform concatenation), matrix–vector multiplication
//   (point/direction transform), inverse, transpose, and determinant. Used
//   internally by the camera, scene graph, and sprite systems to compose and
//   apply 2D spatial transforms.
//
// Key invariants:
//   - Matrix layout is row-major in memory:
//
//       | m00 m01 m02 |   | a  b  tx |
//       | m10 m11 m12 | = | c  d  ty |
//       | m20 m21 m22 |   | 0  0  1  |
//
//     For a pure 2D affine transform:  a, b, c, d encode rotation/scale/shear;
//     tx, ty encode translation. The bottom row is always [0, 0, 1].
//
//   - 2D point transformation (homogeneous coordinates):
//
//       x' = a*x + b*y + tx
//       y' = c*x + d*y + ty
//
//   - Rotation by θ (CCW positive):
//       a = cos θ,  b = -sin θ,  c = sin θ,  d = cos θ
//
//   - Transform concatenation: M_combined = M_parent × M_child (left-multiply).
//     Callers must apply transforms in the correct order for their coordinate
//     system convention.
//
//   - Mat3 objects are effectively immutable after creation: all operations
//     return new Mat3 objects rather than mutating the receiver. This makes them
//     safe to share across threads without locking.
//
// Ownership/Lifetime:
//   - Mat3 objects are GC-managed (rt_obj_new_i64). They hold only the 9
//     double fields inline (no external allocations) so no finalizer is needed.
//
// Links: src/runtime/graphics/rt_mat3.h (public API),
//        src/runtime/graphics/rt_vec2.h, rt_vec3.h (operand types),
//        src/runtime/graphics/rt_camera.c (consumer for viewport transforms)
//
//===----------------------------------------------------------------------===//

#include "rt_mat3.h"

#include "rt_object.h"
#include "rt_vec2.h"
#include "rt_vec3.h"

#include <math.h>
#include <stdlib.h>

//=============================================================================
// Internal Structure
//=============================================================================

/// @brief 3x3 matrix stored in row-major order.
typedef struct mat3_impl {
    double m[9]; ///< Elements in row-major order: [row0][row1][row2]
} mat3_impl;

#define M(mat, r, c) ((mat)->m[(r) * 3 + (c)])

//=============================================================================
// Construction
//=============================================================================

/// @brief Construct a 3×3 matrix from 9 row-major scalars (m00 = top-left, m22 = bottom-right).
/// Used for 2D affine transforms (translate/rotate/scale) where the third row provides the
/// homogeneous coordinate. Returns NULL on allocation failure.
void *rt_mat3_new(double m00,
                  double m01,
                  double m02,
                  double m10,
                  double m11,
                  double m12,
                  double m20,
                  double m21,
                  double m22) {
    mat3_impl *mat = (mat3_impl *)rt_obj_new_i64(0, sizeof(mat3_impl));
    if (!mat)
        return NULL;

    mat->m[0] = m00;
    mat->m[1] = m01;
    mat->m[2] = m02;
    mat->m[3] = m10;
    mat->m[4] = m11;
    mat->m[5] = m12;
    mat->m[6] = m20;
    mat->m[7] = m21;
    mat->m[8] = m22;

    return mat;
}

/// @brief Return the 3×3 identity matrix.
void *rt_mat3_identity(void) {
    return rt_mat3_new(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Return the 3×3 zero matrix.
void *rt_mat3_zero(void) {
    return rt_mat3_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

//=============================================================================
// 2D Transformation Factories
//=============================================================================

/// @brief Build a 2D translation matrix that moves points by (tx, ty).
void *rt_mat3_translate(double tx, double ty) {
    return rt_mat3_new(1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0, 1.0);
}

/// @brief Build a 2D non-uniform scaling matrix with axis factors (sx, sy).
void *rt_mat3_scale(double sx, double sy) {
    return rt_mat3_new(sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 2D uniform scaling matrix (same factor on both axes).
void *rt_mat3_scale_uniform(double s) {
    return rt_mat3_scale(s, s);
}

/// @brief Build a 2D rotation matrix (counter-clockwise, angle in radians).
void *rt_mat3_rotate(double angle) {
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat3_new(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 2D shear matrix: `sx` shears X by Y, `sy` shears Y by X. Useful for slant
/// effects (italic text, parallelogram skew).
void *rt_mat3_shear(double sx, double sy) {
    return rt_mat3_new(1.0, sx, 0.0, sy, 1.0, 0.0, 0.0, 0.0, 1.0);
}

//=============================================================================
// Element Access
//=============================================================================

/// @brief Read a single matrix element by (row, col), both in [0, 2]. Returns 0 for null
/// matrix or out-of-range indices.
double rt_mat3_get(void *m, int64_t row, int64_t col) {
    if (!m || row < 0 || row > 2 || col < 0 || col > 2)
        return 0.0;

    mat3_impl *mat = (mat3_impl *)m;
    return M(mat, row, col);
}

/// @brief Extract the i-th row as a fresh Vec3. Returns (0,0,0) for invalid input.
void *rt_mat3_row(void *m, int64_t row) {
    if (!m || row < 0 || row > 2)
        return rt_vec3_zero();

    mat3_impl *mat = (mat3_impl *)m;
    return rt_vec3_new(M(mat, row, 0), M(mat, row, 1), M(mat, row, 2));
}

/// @brief Extract the i-th column as a fresh Vec3. Returns (0,0,0) for invalid input.
void *rt_mat3_col(void *m, int64_t col) {
    if (!m || col < 0 || col > 2)
        return rt_vec3_zero();

    mat3_impl *mat = (mat3_impl *)m;
    return rt_vec3_new(M(mat, 0, col), M(mat, 1, col), M(mat, 2, col));
}

//=============================================================================
// Arithmetic
//=============================================================================

/// @brief Element-wise addition (a + b). Returns identity for NULL inputs.
void *rt_mat3_add(void *a, void *b) {
    if (!a || !b)
        return rt_mat3_zero();

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    return rt_mat3_new(ma->m[0] + mb->m[0],
                       ma->m[1] + mb->m[1],
                       ma->m[2] + mb->m[2],
                       ma->m[3] + mb->m[3],
                       ma->m[4] + mb->m[4],
                       ma->m[5] + mb->m[5],
                       ma->m[6] + mb->m[6],
                       ma->m[7] + mb->m[7],
                       ma->m[8] + mb->m[8]);
}

/// @brief Element-wise subtraction (a - b). Returns identity for NULL inputs.
void *rt_mat3_sub(void *a, void *b) {
    if (!a || !b)
        return rt_mat3_zero();

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    return rt_mat3_new(ma->m[0] - mb->m[0],
                       ma->m[1] - mb->m[1],
                       ma->m[2] - mb->m[2],
                       ma->m[3] - mb->m[3],
                       ma->m[4] - mb->m[4],
                       ma->m[5] - mb->m[5],
                       ma->m[6] - mb->m[6],
                       ma->m[7] - mb->m[7],
                       ma->m[8] - mb->m[8]);
}

/// @brief Standard matrix multiplication (a × b). Composes 2D affine transforms left-to-right:
/// `mul(translate, rotate)` applied to a point first rotates then translates. NULL→identity.
void *rt_mat3_mul(void *a, void *b) {
    if (!a || !b)
        return rt_mat3_identity();

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    double r[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i * 3 + j] = ma->m[i * 3 + 0] * mb->m[0 * 3 + j] +
                           ma->m[i * 3 + 1] * mb->m[1 * 3 + j] +
                           ma->m[i * 3 + 2] * mb->m[2 * 3 + j];
        }
    }

    return rt_mat3_new(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]);
}

/// @brief Multiply every entry of `m` by scalar `s`.
void *rt_mat3_mul_scalar(void *m, double s) {
    if (!m)
        return rt_mat3_zero();

    mat3_impl *mat = (mat3_impl *)m;

    return rt_mat3_new(mat->m[0] * s,
                       mat->m[1] * s,
                       mat->m[2] * s,
                       mat->m[3] * s,
                       mat->m[4] * s,
                       mat->m[5] * s,
                       mat->m[6] * s,
                       mat->m[7] * s,
                       mat->m[8] * s);
}

/// @brief Transform a 2D point (x, y) through `m` (treats v as homogeneous (x, y, 1)). The
/// returned Vec3's third component is the homogeneous w; usually 1 for affine transforms.
void *rt_mat3_transform_point(void *m, void *v) {
    if (!m || !v)
        return rt_vec2_zero();

    mat3_impl *mat = (mat3_impl *)m;
    double x = rt_vec2_x(v);
    double y = rt_vec2_y(v);

    // Transform as [x, y, 1]
    double rx = mat->m[0] * x + mat->m[1] * y + mat->m[2];
    double ry = mat->m[3] * x + mat->m[4] * y + mat->m[5];

    return rt_vec2_new(rx, ry);
}

/// @brief Transform a 2D direction through `m` (treats v as homogeneous (x, y, 0) — translation
/// is ignored). Use for normals/directions, not absolute positions.
void *rt_mat3_transform_vec(void *m, void *v) {
    if (!m || !v)
        return rt_vec2_zero();

    mat3_impl *mat = (mat3_impl *)m;
    double x = rt_vec2_x(v);
    double y = rt_vec2_y(v);

    // Transform as [x, y, 0] (ignores translation)
    double rx = mat->m[0] * x + mat->m[1] * y;
    double ry = mat->m[3] * x + mat->m[4] * y;

    return rt_vec2_new(rx, ry);
}

//=============================================================================
// Matrix Operations
//=============================================================================

/// @brief Return the transpose of `m` (rows become columns).
void *rt_mat3_transpose(void *m) {
    if (!m)
        return rt_mat3_identity();

    mat3_impl *mat = (mat3_impl *)m;

    return rt_mat3_new(mat->m[0],
                       mat->m[3],
                       mat->m[6],
                       mat->m[1],
                       mat->m[4],
                       mat->m[7],
                       mat->m[2],
                       mat->m[5],
                       mat->m[8]);
}

/// @brief Compute the 3×3 determinant via cofactor expansion. 0 indicates a singular matrix.
double rt_mat3_det(void *m) {
    if (!m)
        return 0.0;

    mat3_impl *mat = (mat3_impl *)m;

    // Determinant using cofactor expansion along first row
    return mat->m[0] * (mat->m[4] * mat->m[8] - mat->m[5] * mat->m[7]) -
           mat->m[1] * (mat->m[3] * mat->m[8] - mat->m[5] * mat->m[6]) +
           mat->m[2] * (mat->m[3] * mat->m[7] - mat->m[4] * mat->m[6]);
}

/// @brief Compute the 3×3 inverse via the cofactor / adjugate formula. Returns identity on
/// NULL input or singular matrix (det ≈ 0).
void *rt_mat3_inverse(void *m) {
    if (!m)
        return rt_mat3_identity();

    mat3_impl *mat = (mat3_impl *)m;
    double det = rt_mat3_det(m);

    if (fabs(det) < 1e-15)
        return rt_mat3_identity(); // Singular matrix

    double invDet = 1.0 / det;

    // Cofactor matrix (transposed)
    double c00 = mat->m[4] * mat->m[8] - mat->m[5] * mat->m[7];
    double c01 = mat->m[2] * mat->m[7] - mat->m[1] * mat->m[8];
    double c02 = mat->m[1] * mat->m[5] - mat->m[2] * mat->m[4];

    double c10 = mat->m[5] * mat->m[6] - mat->m[3] * mat->m[8];
    double c11 = mat->m[0] * mat->m[8] - mat->m[2] * mat->m[6];
    double c12 = mat->m[2] * mat->m[3] - mat->m[0] * mat->m[5];

    double c20 = mat->m[3] * mat->m[7] - mat->m[4] * mat->m[6];
    double c21 = mat->m[1] * mat->m[6] - mat->m[0] * mat->m[7];
    double c22 = mat->m[0] * mat->m[4] - mat->m[1] * mat->m[3];

    return rt_mat3_new(c00 * invDet,
                       c01 * invDet,
                       c02 * invDet,
                       c10 * invDet,
                       c11 * invDet,
                       c12 * invDet,
                       c20 * invDet,
                       c21 * invDet,
                       c22 * invDet);
}

/// @brief Element-wise negation (-m).
void *rt_mat3_neg(void *m) {
    if (!m)
        return rt_mat3_zero();

    mat3_impl *mat = (mat3_impl *)m;

    return rt_mat3_new(-mat->m[0],
                       -mat->m[1],
                       -mat->m[2],
                       -mat->m[3],
                       -mat->m[4],
                       -mat->m[5],
                       -mat->m[6],
                       -mat->m[7],
                       -mat->m[8]);
}

//=============================================================================
// Comparison
//=============================================================================

/// @brief Returns 1 if every element of `a` and `b` differs by no more than `epsilon`.
int8_t rt_mat3_eq(void *a, void *b, double epsilon) {
    if (!a || !b)
        return (!a && !b) ? 1 : 0;

    if (epsilon <= 0.0)
        epsilon = 1e-9;

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    for (int i = 0; i < 9; i++) {
        if (fabs(ma->m[i] - mb->m[i]) > epsilon)
            return 0;
    }

    return 1;
}
