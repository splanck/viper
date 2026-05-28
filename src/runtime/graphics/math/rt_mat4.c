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
// Thread-local free-list pool (mirrors Vec3 pool pattern)
//=============================================================================
#define MAT4_POOL_CAPACITY 32

static _Thread_local void *mat4_pool_buf_[MAT4_POOL_CAPACITY];
static _Thread_local int mat4_pool_top_ = 0;

/// @brief GC finalizer / pool-return hook for Mat4 objects.
/// @details When the GC is about to reclaim a Mat4 whose reference count has
///   dropped to zero, this function intercepts the reclamation by calling
///   rt_obj_resurrect to cancel the pending free, then re-registers itself as
///   the finalizer so the next release will call it again.  The revived object
///   is pushed onto the thread-local free-list (mat4_pool_buf_) up to
///   MAT4_POOL_CAPACITY entries.  Once the pool is full the function simply
///   returns without resurrecting, allowing the GC to complete the free.
///   This avoids repeated heap allocation for short-lived intermediate matrices
///   that arise in transform chains and animation blending.
static void mat4_pool_return(void *p) {
    if (mat4_pool_top_ < MAT4_POOL_CAPACITY) {
        rt_obj_resurrect(p);
        rt_obj_set_finalizer(p, mat4_pool_return);
        mat4_pool_buf_[mat4_pool_top_++] = p;
    }
}

//=============================================================================
// Internal Structure
//=============================================================================

/// @brief 4x4 matrix stored in row-major order.
typedef struct mat4_impl {
    double m[16]; ///< Elements in row-major order
} mat4_impl;

#define M(mat, r, c) ((mat)->m[(r) * 4 + (c)])

/// @brief Allocate a Mat4 from pool or heap.
static mat4_impl *mat4_alloc(void) {
    mat4_impl *mat;
    if (mat4_pool_top_ > 0) {
        mat = (mat4_impl *)mat4_pool_buf_[--mat4_pool_top_];
    } else {
        mat = (mat4_impl *)rt_obj_new_i64(RT_MAT4_CLASS_ID, sizeof(mat4_impl));
        if (!mat)
            return NULL;
        rt_obj_set_finalizer(mat, mat4_pool_return);
    }
    return mat;
}

/// @brief Safe-cast an opaque handle to mat4_impl.
/// @details With RT_MAT4_INTERNAL_ASSUME_STRUCT_HANDLE the pointer is trusted
///          directly (fast path); otherwise the object's class id is validated
///          against RT_MAT4_CLASS_ID, returning NULL on mismatch or NULL input.
static mat4_impl *mat4_checked(void *m) {
#ifdef RT_MAT4_INTERNAL_ASSUME_STRUCT_HANDLE
    return (mat4_impl *)m;
#else
    if (!m || rt_obj_class_id(m) != RT_MAT4_CLASS_ID)
        return NULL;
    return (mat4_impl *)m;
#endif
}

//=============================================================================
// Construction
//=============================================================================

/// @brief Construct a 4×4 matrix from 16 row-major scalars (m00 = row 0 col 0, m01 = row 0 col 1,
/// ..., m33 = row 3 col 3). Returns NULL on allocation failure.
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
                  double m33) {
    mat4_impl *mat = mat4_alloc();
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

/// @brief Return the 4×4 identity matrix.
void *rt_mat4_identity(void) {
    return rt_mat4_new(
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Return the 4×4 zero matrix (all entries 0). Useful as an accumulator base.
void *rt_mat4_zero(void) {
    return rt_mat4_new(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

//=============================================================================
// 3D Transformation Factories
//=============================================================================

/// @brief Build a 4×4 translation matrix that moves points by (tx, ty, tz).
void *rt_mat4_translate(double tx, double ty, double tz) {
    return rt_mat4_new(1.0, 0.0, 0.0, tx, 0.0, 1.0, 0.0, ty, 0.0, 0.0, 1.0, tz, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 4×4 non-uniform scaling matrix with per-axis factors (sx, sy, sz).
void *rt_mat4_scale(double sx, double sy, double sz) {
    return rt_mat4_new(sx, 0.0, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 0.0, sz, 0.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 4×4 uniform scaling matrix (same factor on every axis).
void *rt_mat4_scale_uniform(double s) {
    return rt_mat4_scale(s, s, s);
}

/// @brief Build a 4×4 right-handed rotation matrix about the X axis (angle in radians).
void *rt_mat4_rotate_x(double angle) {
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(1.0, 0.0, 0.0, 0.0, 0.0, c, -s, 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 4×4 right-handed rotation matrix about the Y axis (angle in radians).
void *rt_mat4_rotate_y(double angle) {
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(c, 0.0, s, 0.0, 0.0, 1.0, 0.0, 0.0, -s, 0.0, c, 0.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 4×4 right-handed rotation matrix about the Z axis (angle in radians).
void *rt_mat4_rotate_z(double angle) {
    double c = cos(angle);
    double s = sin(angle);
    return rt_mat4_new(c, -s, 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a 4×4 rotation matrix about an arbitrary axis (Rodrigues' rotation formula).
/// `axis` is normalized internally; degenerate (zero-length) axes return identity.
void *rt_mat4_rotate_axis(void *axis, double angle) {
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

/// @brief Build a right-handed perspective projection matrix. `fov` is vertical FOV in radians,
/// `aspect` = width/height. Maps view-space Z to NDC Z in [-1, 1] (OpenGL convention). Returns
/// identity for invalid params (fov ≤ 0, aspect ≤ 0, or near ≥ far).
void *rt_mat4_perspective(double fov, double aspect, double near, double far) {
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
                       2.0 * far * near * nf,
                       0.0,
                       0.0,
                       -1.0,
                       0.0);
}

/// @brief Build an orthographic projection matrix mapping the box [(left, bottom, near),
/// (right, top, far)] to NDC. Returns identity if any axis range is degenerate.
void *rt_mat4_ortho(double left, double right, double bottom, double top, double near, double far) {
    if (right == left || top == bottom || far == near)
        return rt_mat4_identity();

    double rl = 1.0 / (right - left);
    double tb = 1.0 / (top - bottom);
    double fn = 1.0 / (far - near);

    return rt_mat4_new(2.0 * rl,
                       0.0,
                       0.0,
                       -(right + left) * rl,
                       0.0,
                       2.0 * tb,
                       0.0,
                       -(top + bottom) * tb,
                       0.0,
                       0.0,
                       -2.0 * fn,
                       -(far + near) * fn,
                       0.0,
                       0.0,
                       0.0,
                       1.0);
}

/// @brief Build a right-handed view matrix that places the camera at `eye` looking toward
/// `target` with `up` as the world up direction. Standard "look at" formulation. Returns
/// identity if any input is NULL.
void *rt_mat4_look_at(void *eye, void *target, void *up) {
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

/// @brief Read a single matrix element by (row, col) — both in [0, 3]. Returns 0 for null
/// matrix or out-of-range indices.
double rt_mat4_get(void *m, int64_t row, int64_t col) {
    if (!m || row < 0 || row > 3 || col < 0 || col > 3)
        return 0.0;

    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return 0.0;
    return M(mat, row, col);
}

//=============================================================================
// Arithmetic
//=============================================================================

/// @brief Element-wise addition (a + b). Returns identity for NULL inputs.
void *rt_mat4_add(void *a, void *b) {
    mat4_impl *ma = mat4_checked(a);
    mat4_impl *mb = mat4_checked(b);
    if (!ma || !mb)
        return rt_mat4_zero();

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

/// @brief Element-wise subtraction (a - b). Returns identity for NULL inputs.
void *rt_mat4_sub(void *a, void *b) {
    mat4_impl *ma = mat4_checked(a);
    mat4_impl *mb = mat4_checked(b);
    if (!ma || !mb)
        return rt_mat4_zero();

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

/// @brief Standard matrix multiplication (a × b) — composes transforms in left-to-right order:
/// `mul(A, B)` then applied to a point gives `A·B·p`. Returns identity for NULL inputs.
void *rt_mat4_mul(void *a, void *b) {
    mat4_impl *ma = mat4_checked(a);
    mat4_impl *mb = mat4_checked(b);
    if (!ma || !mb)
        return rt_mat4_identity();

    double r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
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

/// @brief Multiply every entry of `m` by scalar `s`. Returns identity for NULL `m`.
void *rt_mat4_mul_scalar(void *m, double s) {
    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_mat4_zero();

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

/// @brief Transform a 3D point through `m` (treats v as homogeneous (x, y, z, 1)). Returns
/// (0,0,0) for NULL inputs. Use `_transform_vec` for direction vectors (no translation).
void *rt_mat4_transform_point(void *m, void *v) {
    if (!m || !v)
        return rt_vec3_zero();

    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_vec3_zero();
    double x = rt_vec3_x(v);
    double y = rt_vec3_y(v);
    double z = rt_vec3_z(v);

    // Transform as [x, y, z, 1]
    double rx = mat->m[0] * x + mat->m[1] * y + mat->m[2] * z + mat->m[3];
    double ry = mat->m[4] * x + mat->m[5] * y + mat->m[6] * z + mat->m[7];
    double rz = mat->m[8] * x + mat->m[9] * y + mat->m[10] * z + mat->m[11];
    double rw = mat->m[12] * x + mat->m[13] * y + mat->m[14] * z + mat->m[15];

    // Perspective divide
    if (fabs(rw) > 1e-15 && fabs(rw - 1.0) > 1e-15) {
        rx /= rw;
        ry /= rw;
        rz /= rw;
    }

    return rt_vec3_new(rx, ry, rz);
}

/// @brief Transform a 3D direction vector through `m` (treats v as homogeneous (x, y, z, 0) —
/// translation is ignored). Use for normals/directions, not absolute positions.
void *rt_mat4_transform_vec(void *m, void *v) {
    if (!m || !v)
        return rt_vec3_zero();

    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_vec3_zero();
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

/// @brief Return the transpose of `m` (rows become columns). Useful for converting between
/// row-major and column-major matrix layouts when interfacing with other math libraries.
void *rt_mat4_transpose(void *m) {
    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_mat4_identity();

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

/// @brief Compute the 4×4 determinant via cofactor expansion. Returns 0 for NULL input. A
/// near-zero determinant indicates a singular matrix that has no well-defined inverse.
double rt_mat4_det(void *m) {
    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return 0.0;

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

/// @brief Compute the 4×4 inverse via the cofactor / adjugate formula. Returns identity for
/// NULL input or a singular matrix (det ≈ 0). For affine-only transforms, often faster to
/// invert the rotation+translation directly.
void *rt_mat4_inverse(void *m) {
    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_mat4_identity();

    const double *a = mat->m;
    double inv[16];
    double det;
    double inv_det;

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!isfinite(det) || fabs(det) < 1e-15)
        return rt_mat4_identity(); // Singular

    inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        inv[i] *= inv_det;

    return rt_mat4_new(inv[0],
                       inv[1],
                       inv[2],
                       inv[3],
                       inv[4],
                       inv[5],
                       inv[6],
                       inv[7],
                       inv[8],
                       inv[9],
                       inv[10],
                       inv[11],
                       inv[12],
                       inv[13],
                       inv[14],
                       inv[15]);
}

/// @brief Element-wise negation (-m). Returns identity for NULL input.
void *rt_mat4_neg(void *m) {
    mat4_impl *mat = mat4_checked(m);
    if (!mat)
        return rt_mat4_zero();

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

/// @brief Returns 1 if every element of `a` and `b` differs by no more than `epsilon`. Use a
/// small epsilon (e.g., 1e-6) for floating-point tolerance comparison.
int8_t rt_mat4_eq(void *a, void *b, double epsilon) {
    if (!a || !b)
        return (!a && !b) ? 1 : 0;

    if (epsilon <= 0.0)
        epsilon = 1e-9;

    mat4_impl *ma = mat4_checked(a);
    mat4_impl *mb = mat4_checked(b);
    if (!ma || !mb)
        return 0;

    for (int i = 0; i < 16; i++) {
        if (fabs(ma->m[i] - mb->m[i]) > epsilon)
            return 0;
    }

    return 1;
}
