//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_mat3.c
/// @brief 3x3 matrix math for 2D transformations.
///
/// This file implements a 3x3 matrix type used for 2D affine transformations.
///
/// **Matrix Layout (Row-Major):**
/// ```
/// | m00 m01 m02 |   | a b tx |
/// | m10 m11 m12 | = | c d ty |
/// | m20 m21 m22 |   | 0 0 1  |
///
/// For 2D affine transforms:
/// - a, b, c, d: rotation/scale/shear
/// - tx, ty: translation
/// ```
///
/// **2D Point Transformation:**
/// ```
/// [x']   [a  b  tx] [x]
/// [y'] = [c  d  ty] [y]
/// [1 ]   [0  0  1 ] [1]
///
/// x' = a*x + b*y + tx
/// y' = c*x + d*y + ty
/// ```
///
/// **Thread Safety:** Mat3 objects are immutable after creation.
///
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
typedef struct mat3_impl
{
    double m[9]; ///< Elements in row-major order: [row0][row1][row2]
} mat3_impl;

#define M(mat, r, c) ((mat)->m[(r) * 3 + (c)])

//=============================================================================
// Construction
//=============================================================================

void *rt_mat3_new(double m00,
                  double m01,
                  double m02,
                  double m10,
                  double m11,
                  double m12,
                  double m20,
                  double m21,
                  double m22)
{
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

void *rt_mat3_identity(void)
{
    return rt_mat3_new(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat3_zero(void)
{
    return rt_mat3_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

//=============================================================================
// 2D Transformation Factories
//=============================================================================

void *rt_mat3_translate(double tx, double ty)
{
    return rt_mat3_new(1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0, 1.0);
}

void *rt_mat3_scale(double sx, double sy)
{
    return rt_mat3_new(sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat3_scale_uniform(double s)
{
    return rt_mat3_scale(s, s);
}

void *rt_mat3_rotate(double angle)
{
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat3_new(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat3_shear(double sx, double sy)
{
    return rt_mat3_new(1.0, sx, 0.0, sy, 1.0, 0.0, 0.0, 0.0, 1.0);
}

//=============================================================================
// Element Access
//=============================================================================

double rt_mat3_get(void *m, int64_t row, int64_t col)
{
    if (!m || row < 0 || row > 2 || col < 0 || col > 2)
        return 0.0;

    mat3_impl *mat = (mat3_impl *)m;
    return M(mat, row, col);
}

void *rt_mat3_row(void *m, int64_t row)
{
    if (!m || row < 0 || row > 2)
        return rt_vec3_zero();

    mat3_impl *mat = (mat3_impl *)m;
    return rt_vec3_new(M(mat, row, 0), M(mat, row, 1), M(mat, row, 2));
}

void *rt_mat3_col(void *m, int64_t col)
{
    if (!m || col < 0 || col > 2)
        return rt_vec3_zero();

    mat3_impl *mat = (mat3_impl *)m;
    return rt_vec3_new(M(mat, 0, col), M(mat, 1, col), M(mat, 2, col));
}

//=============================================================================
// Arithmetic
//=============================================================================

void *rt_mat3_add(void *a, void *b)
{
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

void *rt_mat3_sub(void *a, void *b)
{
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

void *rt_mat3_mul(void *a, void *b)
{
    if (!a || !b)
        return rt_mat3_identity();

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    double r[9];
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            r[i * 3 + j] = ma->m[i * 3 + 0] * mb->m[0 * 3 + j] +
                           ma->m[i * 3 + 1] * mb->m[1 * 3 + j] +
                           ma->m[i * 3 + 2] * mb->m[2 * 3 + j];
        }
    }

    return rt_mat3_new(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]);
}

void *rt_mat3_mul_scalar(void *m, double s)
{
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

void *rt_mat3_transform_point(void *m, void *v)
{
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

void *rt_mat3_transform_vec(void *m, void *v)
{
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

void *rt_mat3_transpose(void *m)
{
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

double rt_mat3_det(void *m)
{
    if (!m)
        return 0.0;

    mat3_impl *mat = (mat3_impl *)m;

    // Determinant using cofactor expansion along first row
    return mat->m[0] * (mat->m[4] * mat->m[8] - mat->m[5] * mat->m[7]) -
           mat->m[1] * (mat->m[3] * mat->m[8] - mat->m[5] * mat->m[6]) +
           mat->m[2] * (mat->m[3] * mat->m[7] - mat->m[4] * mat->m[6]);
}

void *rt_mat3_inverse(void *m)
{
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

void *rt_mat3_neg(void *m)
{
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

int8_t rt_mat3_eq(void *a, void *b, double epsilon)
{
    if (!a || !b)
        return (!a && !b) ? 1 : 0;

    if (epsilon <= 0.0)
        epsilon = 1e-9;

    mat3_impl *ma = (mat3_impl *)a;
    mat3_impl *mb = (mat3_impl *)b;

    for (int i = 0; i < 9; i++)
    {
        if (fabs(ma->m[i] - mb->m[i]) > epsilon)
            return 0;
    }

    return 1;
}
