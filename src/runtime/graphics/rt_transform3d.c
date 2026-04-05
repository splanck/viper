//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_transform3d.c
// Purpose: Transform3D — position/rotation/scale wrapper with lazy TRS matrix.
//
// Key invariants:
//   - TRS matrix recomputed only when dirty flag is set.
//   - Quaternion is (x,y,z,w), identity = (0,0,0,1).
//   - build_trs mirrors rt_scene3d.c:125-155.
//
// Links: rt_transform3d.h, rt_scene3d.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_transform3d.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
#include "rt_trap.h"
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);
extern void *rt_mat4_new(double m0,
                         double m1,
                         double m2,
                         double m3,
                         double m4,
                         double m5,
                         double m6,
                         double m7,
                         double m8,
                         double m9,
                         double m10,
                         double m11,
                         double m12,
                         double m13,
                         double m14,
                         double m15);

typedef struct {
    void *vptr;
    double position[3];
    double rotation[4]; /* quaternion (x,y,z,w) */
    double scale[3];
    double matrix[16]; /* cached TRS, row-major */
    int8_t dirty;
} rt_transform3d;

static void transform3d_finalizer(void *obj) {
    (void)obj;
}

/// @brief Build TRS matrix from position, quaternion, scale.
/// Mirrors rt_scene3d.c:build_trs_matrix exactly.
static void build_trs(const double *pos, const double *quat, const double *scl, double *out) {
    double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;

    double sx = scl[0], sy = scl[1], sz = scl[2];

    out[0] = (1.0 - (yy + zz)) * sx;
    out[1] = (xy - wz) * sy;
    out[2] = (xz + wy) * sz;
    out[3] = pos[0];

    out[4] = (xy + wz) * sx;
    out[5] = (1.0 - (xx + zz)) * sy;
    out[6] = (yz - wx) * sz;
    out[7] = pos[1];

    out[8] = (xz - wy) * sx;
    out[9] = (yz + wx) * sy;
    out[10] = (1.0 - (xx + yy)) * sz;
    out[11] = pos[2];

    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
}

static void ensure_matrix(rt_transform3d *xf) {
    if (!xf->dirty)
        return;
    build_trs(xf->position, xf->rotation, xf->scale, xf->matrix);
    xf->dirty = 0;
}

/// @brief Create a new Transform3D at the origin with identity rotation and unit scale.
/// @details Transform3D is a standalone TRS (translate-rotate-scale) container.
///          The 4x4 matrix is lazily recomputed only when dirty. Unlike SceneNode3D,
///          transforms have no parent-child hierarchy — they represent a single
///          local-space transformation, typically passed to Canvas3D.DrawMesh.
/// @return Opaque transform handle, or NULL on allocation failure.
void *rt_transform3d_new(void) {
    rt_transform3d *xf = (rt_transform3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_transform3d));
    if (!xf) {
        rt_trap("Transform3D.New: allocation failed");
        return NULL;
    }
    xf->vptr = NULL;
    xf->position[0] = xf->position[1] = xf->position[2] = 0.0;
    xf->rotation[0] = xf->rotation[1] = xf->rotation[2] = 0.0;
    xf->rotation[3] = 1.0; /* identity quaternion */
    xf->scale[0] = xf->scale[1] = xf->scale[2] = 1.0;
    xf->dirty = 1;
    rt_obj_set_finalizer(xf, transform3d_finalizer);
    return xf;
}

/// @brief Set the position component of the transform (marks matrix dirty).
void rt_transform3d_set_position(void *obj, double x, double y, double z) {
    if (!obj)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;
    xf->position[0] = x;
    xf->position[1] = y;
    xf->position[2] = z;
    xf->dirty = 1;
}

/// @brief Get the current position as a new Vec3 (returns origin if NULL).
void *rt_transform3d_get_position(void *obj) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_transform3d *xf = (rt_transform3d *)obj;
    return rt_vec3_new(xf->position[0], xf->position[1], xf->position[2]);
}

/// @brief Set the rotation from a quaternion (x,y,z,w), marks matrix dirty.
void rt_transform3d_set_rotation(void *obj, void *quat) {
    if (!obj || !quat)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;
    xf->rotation[0] = rt_quat_x(quat);
    xf->rotation[1] = rt_quat_y(quat);
    xf->rotation[2] = rt_quat_z(quat);
    xf->rotation[3] = rt_quat_w(quat);
    xf->dirty = 1;
}

/// @brief Get the current rotation as a new Quat (returns identity if NULL).
void *rt_transform3d_get_rotation(void *obj) {
    if (!obj)
        return rt_quat_new(0, 0, 0, 1);
    rt_transform3d *xf = (rt_transform3d *)obj;
    return rt_quat_new(xf->rotation[0], xf->rotation[1], xf->rotation[2], xf->rotation[3]);
}

/// @brief Set rotation from Euler angles (radians) using ZYX intrinsic convention.
/// @details Converts pitch/yaw/roll to a quaternion internally. ZYX order means
///          yaw is applied first, then pitch, then roll — matching common
///          game engine conventions for character/camera orientation.
void rt_transform3d_set_euler(void *obj, double pitch, double yaw, double roll) {
    if (!obj)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;
    /* Convert Euler angles (radians) to quaternion using ZYX convention */
    double hp = pitch * 0.5, hy = yaw * 0.5, hr = roll * 0.5;
    double cp = cos(hp), sp = sin(hp);
    double cy = cos(hy), sy = sin(hy);
    double cr = cos(hr), sr = sin(hr);
    xf->rotation[0] = sr * cp * cy - cr * sp * sy; /* x */
    xf->rotation[1] = cr * sp * cy + sr * cp * sy; /* y */
    xf->rotation[2] = cr * cp * sy - sr * sp * cy; /* z */
    xf->rotation[3] = cr * cp * cy + sr * sp * sy; /* w */
    xf->dirty = 1;
}

/// @brief Set non-uniform scale factors for each axis (marks matrix dirty).
void rt_transform3d_set_scale(void *obj, double x, double y, double z) {
    if (!obj)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;
    xf->scale[0] = x;
    xf->scale[1] = y;
    xf->scale[2] = z;
    xf->dirty = 1;
}

/// @brief Get the current scale as a new Vec3 (returns (1,1,1) if NULL).
void *rt_transform3d_get_scale(void *obj) {
    if (!obj)
        return rt_vec3_new(1, 1, 1);
    rt_transform3d *xf = (rt_transform3d *)obj;
    return rt_vec3_new(xf->scale[0], xf->scale[1], xf->scale[2]);
}

/// @brief Get the combined TRS matrix as a new Mat4 (lazily recomputed if dirty).
/// @details The matrix is built as Translate * Rotate * Scale in row-major order,
///          matching the scene graph convention. Returns identity if NULL.
void *rt_transform3d_get_matrix(void *obj) {
    if (!obj)
        return rt_mat4_new(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    rt_transform3d *xf = (rt_transform3d *)obj;
    ensure_matrix(xf);
    return rt_mat4_new(xf->matrix[0],
                       xf->matrix[1],
                       xf->matrix[2],
                       xf->matrix[3],
                       xf->matrix[4],
                       xf->matrix[5],
                       xf->matrix[6],
                       xf->matrix[7],
                       xf->matrix[8],
                       xf->matrix[9],
                       xf->matrix[10],
                       xf->matrix[11],
                       xf->matrix[12],
                       xf->matrix[13],
                       xf->matrix[14],
                       xf->matrix[15]);
}

/// @brief Add a displacement vector to the current position (incremental move).
void rt_transform3d_translate(void *obj, void *delta) {
    if (!obj || !delta)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;
    xf->position[0] += rt_vec3_x(delta);
    xf->position[1] += rt_vec3_y(delta);
    xf->position[2] += rt_vec3_z(delta);
    xf->dirty = 1;
}

/// @brief Apply an incremental rotation around an arbitrary axis (radians).
/// @details Builds a quaternion from the axis-angle, then left-multiplies it
///          onto the current rotation: current = new_rot * current. The axis
///          vector is normalized internally.
void rt_transform3d_rotate(void *obj, void *axis, double angle) {
    if (!obj || !axis)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;

    /* Build quaternion from axis-angle */
    double ax = rt_vec3_x(axis), ay = rt_vec3_y(axis), az = rt_vec3_z(axis);
    double len = sqrt(ax * ax + ay * ay + az * az);
    if (len < 1e-12)
        return;
    ax /= len;
    ay /= len;
    az /= len;

    double ha = angle * 0.5;
    double sa = sin(ha), ca = cos(ha);
    double qx = ax * sa, qy = ay * sa, qz = az * sa, qw = ca;

    /* Multiply: current = new_rot * current */
    double cx = xf->rotation[0], cy = xf->rotation[1];
    double cz = xf->rotation[2], cw = xf->rotation[3];
    xf->rotation[0] = qw * cx + qx * cw + qy * cz - qz * cy;
    xf->rotation[1] = qw * cy - qx * cz + qy * cw + qz * cx;
    xf->rotation[2] = qw * cz + qx * cy - qy * cx + qz * cw;
    xf->rotation[3] = qw * cw - qx * cx - qy * cy - qz * cz;
    xf->dirty = 1;
}

/// @brief Orient the transform to face a target point.
/// @details Computes a forward vector from position to target, derives a
///          right-handed orthonormal basis (right, true-up, forward), then
///          extracts a quaternion from the 3x3 rotation matrix using the
///          Shepperd method. The position is not modified — only rotation.
/// @param obj    Transform handle.
/// @param target Vec3 point to face toward.
/// @param up_vec Vec3 up hint (defaults to world Y if NULL).
void rt_transform3d_look_at(void *obj, void *target, void *up_vec) {
    if (!obj || !target)
        return;
    rt_transform3d *xf = (rt_transform3d *)obj;

    double tx = rt_vec3_x(target) - xf->position[0];
    double ty = rt_vec3_y(target) - xf->position[1];
    double tz = rt_vec3_z(target) - xf->position[2];
    double flen = sqrt(tx * tx + ty * ty + tz * tz);
    if (flen < 1e-12)
        return;
    tx /= flen;
    ty /= flen;
    tz /= flen;

    double ux = 0.0, uy = 1.0, uz = 0.0;
    if (up_vec) {
        ux = rt_vec3_x(up_vec);
        uy = rt_vec3_y(up_vec);
        uz = rt_vec3_z(up_vec);
    }

    /* Right = normalize(cross(forward, up)) */
    double rx = ty * uz - tz * uy, ry = tz * ux - tx * uz, rz = tx * uy - ty * ux;
    double rlen = sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen < 1e-12)
        return;
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;

    /* True up = cross(right, forward) */
    double tux = ry * tz - rz * ty, tuy = rz * tx - rx * tz, tuz = rx * ty - ry * tx;

    /* Extract quaternion from full 3x3 rotation matrix (Shepperd method).
     * Matrix columns: right(r), true_up(tu), forward(f) */
    double m00 = rx, m01 = tux, m02 = tx;
    double m10 = ry, m11 = tuy, m12 = ty;
    double m20 = rz, m21 = tuz, m22 = tz;
    double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        double s = sqrt(trace + 1.0) * 2.0;
        xf->rotation[3] = 0.25 * s;
        xf->rotation[0] = (m21 - m12) / s;
        xf->rotation[1] = (m02 - m20) / s;
        xf->rotation[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
        xf->rotation[3] = (m21 - m12) / s;
        xf->rotation[0] = 0.25 * s;
        xf->rotation[1] = (m01 + m10) / s;
        xf->rotation[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
        xf->rotation[3] = (m02 - m20) / s;
        xf->rotation[0] = (m01 + m10) / s;
        xf->rotation[1] = 0.25 * s;
        xf->rotation[2] = (m12 + m21) / s;
    } else {
        double s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
        xf->rotation[3] = (m10 - m01) / s;
        xf->rotation[0] = (m02 + m20) / s;
        xf->rotation[1] = (m12 + m21) / s;
        xf->rotation[2] = 0.25 * s;
    }
    xf->dirty = 1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
