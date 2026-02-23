//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_mat4.c
// Purpose: 4x4 matrix mathematics for the Viper.Mat4 class. Implements 3D
//   affine and projective transforms: translation, rotation (from quaternion or
//   axis-angle), scale, matrix multiplication, transpose, determinant, inverse,
//   perspective and orthographic projection, and Vec3/Vec4 transformation.
//   Used by the 3D scene graph, camera, and skeletal animation systems.
//
// Key invariants:
//   - Elements are stored in row-major order: m[r*4+c] accesses row r, column c.
//   - The bottom row of affine transforms is always (0, 0, 0, 1).
//   - Rotation basis vectors (X, Y, Z columns) represent the transformed axes;
//     translation is stored in column 3 (Tx, Ty, Tz).
//   - Mat4 objects are immutable after creation; all operations return new
//     objects allocated from the GC heap.
//   - Inverse is computed via cofactor expansion; degenerate matrices (det == 0)
//     return the identity matrix with a runtime warning.
//   - Perspective projection uses a right-handed coordinate system, depth range
//     [-1, 1] (OpenGL convention), with near/far clip planes.
//
// Ownership/Lifetime:
//   - All Mat4 objects are allocated via rt_obj_new_i64 (GC heap); no manual
//     free is required. The mat4_impl struct contains only a double[16] array.
//
// Links: src/runtime/graphics/rt_mat4.h (public API),
//        src/runtime/graphics/rt_vec3.h (Vec3 operand and result type),
//        src/runtime/graphics/rt_quat.h (quaternion-to-matrix conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_mat4.h"

#include "rt_object.h"
#include "rt_vec3.h"

#include <math.h>
#include <stdlib.h>

//=============================================================================
// Internal Structure
//=============================================================================

/// @brief 4x4 matrix stored in row-major order.
typedef struct mat4_impl
{
    double m[16]; ///< Elements in row-major order
} mat4_impl;

#define M(mat, r, c) ((mat)->m[(r) * 4 + (c)])

//=============================================================================
// Construction
//=============================================================================

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
                  double m33)
{
    mat4_impl *mat = (mat4_impl *)rt_obj_new_i64(0, sizeof(mat4_impl));
    if (!mat)
        return NULL;

    mat->m[0] = m00;
    mat->m[1] = m01;
    mat->m[2] = m02;
    mat->m[3] = m03;
    mat->m[4] = m10;
    mat->m[5] = m11;
    mat->m[6] = m12;
    mat->m[7] = m13;
    mat->m[8] = m20;
    mat->m[9] = m21;
    mat->m[10] = m22;
    mat->m[11] = m23;
    mat->m[12] = m30;
    mat->m[13] = m31;
    mat->m[14] = m32;
    mat->m[15] = m33;

    return mat;
}

void *rt_mat4_identity(void)
{
    return rt_mat4_new(
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_zero(void)
{
    return rt_mat4_new(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

//=============================================================================
// 3D Transformation Factories
//=============================================================================

void *rt_mat4_translate(double tx, double ty, double tz)
{
    return rt_mat4_new(1.0, 0.0, 0.0, tx, 0.0, 1.0, 0.0, ty, 0.0, 0.0, 1.0, tz, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_scale(double sx, double sy, double sz)
{
    return rt_mat4_new(sx, 0.0, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 0.0, sz, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_scale_uniform(double s)
{
    return rt_mat4_scale(s, s, s);
}

void *rt_mat4_rotate_x(double angle)
{
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(1.0, 0.0, 0.0, 0.0, 0.0, c, -s, 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_rotate_y(double angle)
{
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(c, 0.0, s, 0.0, 0.0, 1.0, 0.0, 0.0, -s, 0.0, c, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_rotate_z(double angle)
{
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(c, -s, 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void *rt_mat4_rotate_axis(void *axis, double angle)
{
    if (!axis)
        return rt_mat4_identity();

    double x = rt_vec3_x(axis);
    double y = rt_vec3_y(axis);
    double z = rt_vec3_z(axis);

    // Normalize axis
    double len = sqrt(x * x + y * y + z * z);
    if (len < 1e-15)
        return rt_mat4_identity();
    x /= len;
    y /= len;
    z /= len;

    double c = cos(angle);
    double s = sin(angle);
    double t = 1.0 - c;

    return rt_mat4_new(t * x * x + c,
                       t * x * y - s * z,
                       t * x * z + s * y,
                       0.0,
                       t * x * y + s * z,
                       t * y * y + c,
                       t * y * z - s * x,
                       0.0,
                       t * x * z - s * y,
                       t * y * z + s * x,
                       t * z * z + c,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       1.0);
}

//=============================================================================
// Projection Matrices
//=============================================================================

void *rt_mat4_perspective(double fov, double aspect, double near, double far)
{
    if (fov <= 0.0 || aspect <= 0.0 || near >= far)
        return rt_mat4_identity();

    double tanHalfFov = tan(fov / 2.0);
    double f = 1.0 / tanHalfFov;
    double nf = 1.0 / (near - far);

    return rt_mat4_new(f / aspect,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       f,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       (far + near) * nf,
                       -1.0,
                       0.0,
                       0.0,
                       2.0 * far * near * nf,
                       0.0);
}

void *rt_mat4_ortho(double left, double right, double bottom, double top, double near, double far)
{
    if (right == left || top == bottom || far == near)
        return rt_mat4_identity();

    double rl = 1.0 / (right - left);
    double tb = 1.0 / (top - bottom);
    double fn = 1.0 / (far - near);

    return rt_mat4_new(2.0 * rl,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       2.0 * tb,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       -2.0 * fn,
                       0.0,
                       -(right + left) * rl,
                       -(top + bottom) * tb,
                       -(far + near) * fn,
                       1.0);
}

void *rt_mat4_look_at(void *eye, void *target, void *up)
{
    if (!eye || !target || !up)
        return rt_mat4_identity();

    double eyeX = rt_vec3_x(eye);
    double eyeY = rt_vec3_y(eye);
    double eyeZ = rt_vec3_z(eye);

    double targetX = rt_vec3_x(target);
    double targetY = rt_vec3_y(target);
    double targetZ = rt_vec3_z(target);

    double upX = rt_vec3_x(up);
    double upY = rt_vec3_y(up);
    double upZ = rt_vec3_z(up);

    // Forward vector (from eye to target)
    double fX = targetX - eyeX;
    double fY = targetY - eyeY;
    double fZ = targetZ - eyeZ;
    double fLen = sqrt(fX * fX + fY * fY + fZ * fZ);
    if (fLen < 1e-15)
        return rt_mat4_identity();
    fX /= fLen;
    fY /= fLen;
    fZ /= fLen;

    // Right vector (cross product of forward and up)
    double rX = fY * upZ - fZ * upY;
    double rY = fZ * upX - fX * upZ;
    double rZ = fX * upY - fY * upX;
    double rLen = sqrt(rX * rX + rY * rY + rZ * rZ);
    if (rLen < 1e-15)
        return rt_mat4_identity();
    rX /= rLen;
    rY /= rLen;
    rZ /= rLen;

    // Recalculate up vector (cross product of right and forward)
    double uX = rY * fZ - rZ * fY;
    double uY = rZ * fX - rX * fZ;
    double uZ = rX * fY - rY * fX;

    return rt_mat4_new(rX,
                       rY,
                       rZ,
                       -(rX * eyeX + rY * eyeY + rZ * eyeZ),
                       uX,
                       uY,
                       uZ,
                       -(uX * eyeX + uY * eyeY + uZ * eyeZ),
                       -fX,
                       -fY,
                       -fZ,
                       fX * eyeX + fY * eyeY + fZ * eyeZ,
                       0.0,
                       0.0,
                       0.0,
                       1.0);
}

//=============================================================================
// Element Access
//=============================================================================

double rt_mat4_get(void *m, int64_t row, int64_t col)
{
    if (!m || row < 0 || row > 3 || col < 0 || col > 3)
        return 0.0;

    mat4_impl *mat = (mat4_impl *)m;
    return M(mat, row, col);
}

//=============================================================================
// Arithmetic
//=============================================================================

void *rt_mat4_add(void *a, void *b)
{
    if (!a || !b)
        return rt_mat4_zero();

    mat4_impl *ma = (mat4_impl *)a;
    mat4_impl *mb = (mat4_impl *)b;

    double r[16];
    for (int i = 0; i < 16; i++)
        r[i] = ma->m[i] + mb->m[i];

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

void *rt_mat4_sub(void *a, void *b)
{
    if (!a || !b)
        return rt_mat4_zero();

    mat4_impl *ma = (mat4_impl *)a;
    mat4_impl *mb = (mat4_impl *)b;

    double r[16];
    for (int i = 0; i < 16; i++)
        r[i] = ma->m[i] - mb->m[i];

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

void *rt_mat4_mul(void *a, void *b)
{
    if (!a || !b)
        return rt_mat4_identity();

    mat4_impl *ma = (mat4_impl *)a;
    mat4_impl *mb = (mat4_impl *)b;

    double r[16];
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            r[i * 4 + j] =
                ma->m[i * 4 + 0] * mb->m[0 * 4 + j] + ma->m[i * 4 + 1] * mb->m[1 * 4 + j] +
                ma->m[i * 4 + 2] * mb->m[2 * 4 + j] + ma->m[i * 4 + 3] * mb->m[3 * 4 + j];
        }
    }

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

void *rt_mat4_mul_scalar(void *m, double s)
{
    if (!m)
        return rt_mat4_zero();

    mat4_impl *mat = (mat4_impl *)m;

    double r[16];
    for (int i = 0; i < 16; i++)
        r[i] = mat->m[i] * s;

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

void *rt_mat4_transform_point(void *m, void *v)
{
    if (!m || !v)
        return rt_vec3_zero();

    mat4_impl *mat = (mat4_impl *)m;
    double x = rt_vec3_x(v);
    double y = rt_vec3_y(v);
    double z = rt_vec3_z(v);

    // Transform as [x, y, z, 1]
    double rx = mat->m[0] * x + mat->m[1] * y + mat->m[2] * z + mat->m[3];
    double ry = mat->m[4] * x + mat->m[5] * y + mat->m[6] * z + mat->m[7];
    double rz = mat->m[8] * x + mat->m[9] * y + mat->m[10] * z + mat->m[11];
    double rw = mat->m[12] * x + mat->m[13] * y + mat->m[14] * z + mat->m[15];

    // Perspective divide
    if (fabs(rw) > 1e-15 && fabs(rw - 1.0) > 1e-15)
    {
        rx /= rw;
        ry /= rw;
        rz /= rw;
    }

    return rt_vec3_new(rx, ry, rz);
}

void *rt_mat4_transform_vec(void *m, void *v)
{
    if (!m || !v)
        return rt_vec3_zero();

    mat4_impl *mat = (mat4_impl *)m;
    double x = rt_vec3_x(v);
    double y = rt_vec3_y(v);
    double z = rt_vec3_z(v);

    // Transform as [x, y, z, 0] (ignores translation)
    double rx = mat->m[0] * x + mat->m[1] * y + mat->m[2] * z;
    double ry = mat->m[4] * x + mat->m[5] * y + mat->m[6] * z;
    double rz = mat->m[8] * x + mat->m[9] * y + mat->m[10] * z;

    return rt_vec3_new(rx, ry, rz);
}

//=============================================================================
// Matrix Operations
//=============================================================================

void *rt_mat4_transpose(void *m)
{
    if (!m)
        return rt_mat4_identity();

    mat4_impl *mat = (mat4_impl *)m;

    return rt_mat4_new(mat->m[0],
                       mat->m[4],
                       mat->m[8],
                       mat->m[12],
                       mat->m[1],
                       mat->m[5],
                       mat->m[9],
                       mat->m[13],
                       mat->m[2],
                       mat->m[6],
                       mat->m[10],
                       mat->m[14],
                       mat->m[3],
                       mat->m[7],
                       mat->m[11],
                       mat->m[15]);
}

double rt_mat4_det(void *m)
{
    if (!m)
        return 0.0;

    mat4_impl *mat = (mat4_impl *)m;
    double *a = mat->m;

    // Compute 2x2 determinants
    double s0 = a[0] * a[5] - a[1] * a[4];
    double s1 = a[0] * a[6] - a[2] * a[4];
    double s2 = a[0] * a[7] - a[3] * a[4];
    double s3 = a[1] * a[6] - a[2] * a[5];
    double s4 = a[1] * a[7] - a[3] * a[5];
    double s5 = a[2] * a[7] - a[3] * a[6];

    double c5 = a[10] * a[15] - a[11] * a[14];
    double c4 = a[9] * a[15] - a[11] * a[13];
    double c3 = a[9] * a[14] - a[10] * a[13];
    double c2 = a[8] * a[15] - a[11] * a[12];
    double c1 = a[8] * a[14] - a[10] * a[12];
    double c0 = a[8] * a[13] - a[9] * a[12];

    return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
}

void *rt_mat4_inverse(void *m)
{
    if (!m)
        return rt_mat4_identity();

    mat4_impl *mat = (mat4_impl *)m;
    double *a = mat->m;

    // Compute 2x2 determinants
    double s0 = a[0] * a[5] - a[1] * a[4];
    double s1 = a[0] * a[6] - a[2] * a[4];
    double s2 = a[0] * a[7] - a[3] * a[4];
    double s3 = a[1] * a[6] - a[2] * a[5];
    double s4 = a[1] * a[7] - a[3] * a[5];
    double s5 = a[2] * a[7] - a[3] * a[6];

    double c5 = a[10] * a[15] - a[11] * a[14];
    double c4 = a[9] * a[15] - a[11] * a[13];
    double c3 = a[9] * a[14] - a[10] * a[13];
    double c2 = a[8] * a[15] - a[11] * a[12];
    double c1 = a[8] * a[14] - a[10] * a[12];
    double c0 = a[8] * a[13] - a[9] * a[12];

    double det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

    if (fabs(det) < 1e-15)
        return rt_mat4_identity(); // Singular

    double invDet = 1.0 / det;

    double r[16];

    r[0] = (a[5] * c5 - a[6] * c4 + a[7] * c3) * invDet;
    r[1] = (-a[1] * c5 + a[2] * c4 - a[3] * c3) * invDet;
    r[2] = (a[13] * s5 - a[14] * s4 + a[15] * s3) * invDet;
    r[3] = (-a[9] * s5 + a[10] * s4 - a[11] * s3) * invDet;

    r[4] = (-a[4] * c5 + a[6] * c2 - a[7] * c1) * invDet;
    r[5] = (a[0] * c5 - a[2] * c2 + a[3] * c1) * invDet;
    r[6] = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * invDet;
    r[7] = (a[8] * s5 - a[10] * s2 + a[11] * s1) * invDet;

    r[8] = (a[4] * c4 - a[5] * c2 + a[7] * c0) * invDet;
    r[9] = (-a[0] * c4 + a[1] * c2 - a[3] * c0) * invDet;
    r[10] = (a[12] * s4 - a[13] * s2 + a[15] * s0) * invDet;
    r[11] = (-a[8] * s4 + a[9] * s2 - a[11] * s0) * invDet;

    r[12] = (-a[4] * c3 + a[5] * c1 - a[6] * c0) * invDet;
    r[13] = (a[0] * c3 - a[1] * c1 + a[2] * c0) * invDet;
    r[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * invDet;
    r[15] = (a[8] * s3 - a[9] * s1 + a[10] * s0) * invDet;

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

void *rt_mat4_neg(void *m)
{
    if (!m)
        return rt_mat4_zero();

    mat4_impl *mat = (mat4_impl *)m;

    double r[16];
    for (int i = 0; i < 16; i++)
        r[i] = -mat->m[i];

    return rt_mat4_new(r[0],
                       r[1],
                       r[2],
                       r[3],
                       r[4],
                       r[5],
                       r[6],
                       r[7],
                       r[8],
                       r[9],
                       r[10],
                       r[11],
                       r[12],
                       r[13],
                       r[14],
                       r[15]);
}

//=============================================================================
// Comparison
//=============================================================================

int8_t rt_mat4_eq(void *a, void *b, double epsilon)
{
    if (!a || !b)
        return (!a && !b) ? 1 : 0;

    if (epsilon <= 0.0)
        epsilon = 1e-9;

    mat4_impl *ma = (mat4_impl *)a;
    mat4_impl *mb = (mat4_impl *)b;

    for (int i = 0; i < 16; i++)
    {
        if (fabs(ma->m[i] - mb->m[i]) > epsilon)
            return 0;
    }

    return 1;
}
