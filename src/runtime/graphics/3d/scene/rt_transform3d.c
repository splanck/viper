//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_transform3d.c
// Purpose: Transform3D — position/rotation/scale wrapper with lazy TRS matrix.
//
// Key invariants:
//   - TRS matrix recomputed only when dirty flag is set.
//   - Quaternion is (x,y,z,w), identity = (0,0,0,1).
//   - build_trs mirrors rt_scene3d.c:125-155.
//   - Setters mark dirty without computing; reader resolves the matrix lazily.
//
// Ownership/Lifetime:
//   - Transform3D is GC-managed; no finalizer needed (no owned heap allocations).
//
// Links: rt_transform3d.h, rt_scene3d.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_graphics3d_ids.h"
#include "rt_transform3d.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TRANSFORM3D_ABS_MAX 1000000000000.0
#define TRANSFORM3D_TWO_PI 6.28318530717958647692

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

/// @brief Validate @p obj as a Transform3D handle and return its typed pointer (NULL on mismatch).
static rt_transform3d *transform3d_checked(void *obj) {
    return (rt_transform3d *)rt_g3d_checked_or_null(obj, RT_G3D_TRANSFORM3D_CLASS_ID);
}

/// @brief Return @p value if finite, otherwise return @p fallback.
/// @details Local copy of the pattern from rt_scene3d.c. Used to sanitize all
///   incoming transform component values (position, scale) so NaN/Inf from
///   caller errors cannot propagate into the TRS matrix.
static double transform3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp `value` into `[-TRANSFORM3D_ABS_MAX, TRANSFORM3D_ABS_MAX]`, substituting `fallback` when not finite.
static double transform3d_clamp_abs_or(double value, double fallback) {
    value = transform3d_finite_or(value, fallback);
    if (value > TRANSFORM3D_ABS_MAX)
        return TRANSFORM3D_ABS_MAX;
    if (value < -TRANSFORM3D_ABS_MAX)
        return -TRANSFORM3D_ABS_MAX;
    return value;
}

/// @brief Return @p value if finite, or 1.0 as a safe identity scale.
/// @details Finite zero scale is intentional and must be preserved so callers
///   can collapse one or more axes. Only non-finite values are replaced.
static double transform3d_scale_or_unit(double value) {
    if (!isfinite(value))
        return 1.0;
    if (value > TRANSFORM3D_ABS_MAX)
        return TRANSFORM3D_ABS_MAX;
    if (value < -TRANSFORM3D_ABS_MAX)
        return -TRANSFORM3D_ABS_MAX;
    return value;
}

/// @brief Write the identity quaternion (0, 0, 0, 1) into @p q.
/// @details Used as the fallback when `transform3d_quat_normalize` receives a
///   degenerate input. The identity quaternion represents no rotation so a
///   degenerate rotation leaves the transform's orientation unchanged.
/// @param q double[4] output buffer in (x, y, z, w) order.
static void transform3d_quat_identity(double *q) {
    q[0] = 0.0;
    q[1] = 0.0;
    q[2] = 0.0;
    q[3] = 1.0;
}

/// @brief Normalize a double[4] quaternion in-place; substitute identity on any
///        degenerate or non-finite input.
/// @details Two guards: a component NaN/Inf check (which would make the magnitude
///   non-finite) and a near-zero length-squared check (threshold 1e-24 to match
///   the double-precision equivalent of the float 1e-8 threshold in the skeleton
///   code). Falls back to identity rather than trapping so bad rotation inputs from
///   external sources are silently corrected.
/// @param q double[4] quaternion in (x, y, z, w) order; modified in-place.
static void transform3d_quat_normalize(double *q) {
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        transform3d_quat_identity(q);
        return;
    }
    double len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!isfinite(len_sq) || len_sq < 1e-24) {
        transform3d_quat_identity(q);
        return;
    }
    double inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

/// @brief GC finalizer — Transform3D owns no heap allocations, so this is a no-op.
/// @details All fields (`position`, `rotation`, `scale`, cached `matrix`) live
///   inline inside the struct and get freed with the object body. The finalizer
///   still needs to exist so the GC sees this type as "finalizable" rather than
///   plain bytes — useful for uniform lifecycle tracing across 3D object types.
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

/// @brief Lazily rebuild the cached 4x4 matrix from TRS components.
/// @details Every mutating setter (`set_position`, `set_rotation`, `set_scale`)
///   flips `dirty = 1` without doing any math; this routine is the single
///   chokepoint where the cost is paid, right before a consumer reads
///   `matrix`. Skipping when `dirty == 0` means repeated reads between
///   mutations are O(1). The invariant is: after this call returns,
///   `xf->matrix` equals `T * R * S` built from the current component
///   fields, and `xf->dirty == 0`.
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
    rt_transform3d *xf = (rt_transform3d *)rt_obj_new_i64(RT_G3D_TRANSFORM3D_CLASS_ID, (int64_t)sizeof(rt_transform3d));
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
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return;
    xf->position[0] = transform3d_clamp_abs_or(x, 0.0);
    xf->position[1] = transform3d_clamp_abs_or(y, 0.0);
    xf->position[2] = transform3d_clamp_abs_or(z, 0.0);
    xf->dirty = 1;
}

/// @brief Get the current position as a new Vec3 (returns origin if NULL).
void *rt_transform3d_get_position(void *obj) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(xf->position[0], xf->position[1], xf->position[2]);
}

/// @brief Set the rotation from a quaternion (x,y,z,w), marks matrix dirty.
void rt_transform3d_set_rotation(void *obj, void *quat) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf || !rt_g3d_is_quat(quat))
        return;
    xf->rotation[0] = rt_quat_x(quat);
    xf->rotation[1] = rt_quat_y(quat);
    xf->rotation[2] = rt_quat_z(quat);
    xf->rotation[3] = rt_quat_w(quat);
    transform3d_quat_normalize(xf->rotation);
    xf->dirty = 1;
}

/// @brief Get the current rotation as a new Quat (returns identity if NULL).
void *rt_transform3d_get_rotation(void *obj) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return rt_quat_new(0, 0, 0, 1);
    return rt_quat_new(xf->rotation[0], xf->rotation[1], xf->rotation[2], xf->rotation[3]);
}

/// @brief Set rotation from Euler angles (degrees) using ZYX intrinsic convention.
/// @details Converts pitch/yaw/roll to a quaternion internally. ZYX order means
///          yaw is applied first, then pitch, then roll — matching common
///          game engine conventions for character/camera orientation.
void rt_transform3d_set_euler(void *obj, double pitch, double yaw, double roll) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return;
    pitch = transform3d_finite_or(pitch, 0.0);
    yaw = transform3d_finite_or(yaw, 0.0);
    roll = transform3d_finite_or(roll, 0.0);
    pitch = fmod(pitch, 360.0);
    yaw = fmod(yaw, 360.0);
    roll = fmod(roll, 360.0);
    if (!isfinite(pitch) || !isfinite(yaw) || !isfinite(roll))
        return;
    double hp = pitch * (M_PI / 180.0) * 0.5;
    double hy = yaw * (M_PI / 180.0) * 0.5;
    double hr = roll * (M_PI / 180.0) * 0.5;
    double cp = cos(hp), sp = sin(hp); /* pitch: X */
    double cy = cos(hy), sy = sin(hy); /* yaw: Y */
    double cr = cos(hr), sr = sin(hr); /* roll: Z */
    xf->rotation[0] = sp * cy * cr - cp * sy * sr;
    xf->rotation[1] = cp * sy * cr + sp * cy * sr;
    xf->rotation[2] = cp * cy * sr - sp * sy * cr;
    xf->rotation[3] = cp * cy * cr + sp * sy * sr;
    transform3d_quat_normalize(xf->rotation);
    xf->dirty = 1;
}

/// @brief Set non-uniform scale factors for each axis (marks matrix dirty).
void rt_transform3d_set_scale(void *obj, double x, double y, double z) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return;
    xf->scale[0] = transform3d_scale_or_unit(x);
    xf->scale[1] = transform3d_scale_or_unit(y);
    xf->scale[2] = transform3d_scale_or_unit(z);
    xf->dirty = 1;
}

/// @brief Get the current scale as a new Vec3 (returns (1,1,1) if NULL).
void *rt_transform3d_get_scale(void *obj) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return rt_vec3_new(1, 1, 1);
    return rt_vec3_new(xf->scale[0], xf->scale[1], xf->scale[2]);
}

/// @brief Get the combined TRS matrix as a new Mat4 (lazily recomputed if dirty).
/// @details The matrix is built as Translate * Rotate * Scale in row-major order,
///          matching the scene graph convention. Returns identity if NULL.
void *rt_transform3d_get_matrix(void *obj) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf)
        return rt_mat4_new(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
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
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf || !rt_g3d_is_vec3(delta))
        return;
    double nx = xf->position[0] + transform3d_finite_or(rt_vec3_x(delta), 0.0);
    double ny = xf->position[1] + transform3d_finite_or(rt_vec3_y(delta), 0.0);
    double nz = xf->position[2] + transform3d_finite_or(rt_vec3_z(delta), 0.0);
    xf->position[0] = transform3d_clamp_abs_or(nx, 0.0);
    xf->position[1] = transform3d_clamp_abs_or(ny, 0.0);
    xf->position[2] = transform3d_clamp_abs_or(nz, 0.0);
    xf->dirty = 1;
}

/// @brief Apply an incremental rotation around an arbitrary axis (radians).
/// @details Builds a quaternion from the axis-angle, then left-multiplies it
///          onto the current rotation: current = new_rot * current. The axis
///          vector is normalized internally.
void rt_transform3d_rotate(void *obj, void *axis, double angle) {
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf || !rt_g3d_is_vec3(axis))
        return;

    /* Build quaternion from axis-angle */
    double ax = rt_vec3_x(axis), ay = rt_vec3_y(axis), az = rt_vec3_z(axis);
    if (!isfinite(ax) || !isfinite(ay) || !isfinite(az) || !isfinite(angle))
        return;
    double len = sqrt(ax * ax + ay * ay + az * az);
    if (!isfinite(len) || len < 1e-12)
        return;
    ax /= len;
    ay /= len;
    az /= len;

    angle = fmod(angle, TRANSFORM3D_TWO_PI);
    if (!isfinite(angle))
        return;
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
    transform3d_quat_normalize(xf->rotation);
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
    rt_transform3d *xf = transform3d_checked(obj);
    if (!xf || !rt_g3d_is_vec3(target))
        return;

    double target_x = rt_vec3_x(target);
    double target_y = rt_vec3_y(target);
    double target_z = rt_vec3_z(target);
    if (!isfinite(target_x) || !isfinite(target_y) || !isfinite(target_z))
        return;
    double tx = target_x - xf->position[0];
    double ty = target_y - xf->position[1];
    double tz = target_z - xf->position[2];
    double flen = sqrt(tx * tx + ty * ty + tz * tz);
    if (!isfinite(flen) || flen < 1e-12)
        return;
    tx /= flen;
    ty /= flen;
    tz /= flen;

    double ux = 0.0, uy = 1.0, uz = 0.0;
    if (rt_g3d_is_vec3(up_vec)) {
        ux = rt_vec3_x(up_vec);
        uy = rt_vec3_y(up_vec);
        uz = rt_vec3_z(up_vec);
        if (!isfinite(ux) || !isfinite(uy) || !isfinite(uz)) {
            ux = 0.0;
            uy = 1.0;
            uz = 0.0;
        }
    }

    /* Right = normalize(cross(forward, up)) */
    double rx = ty * uz - tz * uy, ry = tz * ux - tx * uz, rz = tx * uy - ty * ux;
    double rlen = sqrt(rx * rx + ry * ry + rz * rz);
    if (!isfinite(rlen) || rlen < 1e-12) {
        if (fabs(ty) < 0.9) {
            ux = 0.0;
            uy = 1.0;
            uz = 0.0;
        } else {
            ux = 1.0;
            uy = 0.0;
            uz = 0.0;
        }
        rx = ty * uz - tz * uy;
        ry = tz * ux - tx * uz;
        rz = tx * uy - ty * ux;
        rlen = sqrt(rx * rx + ry * ry + rz * rz);
        if (!isfinite(rlen) || rlen < 1e-12)
            return;
    }
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;

    /* True up = cross(right, forward) */
    double tux = ry * tz - rz * ty, tuy = rz * tx - rx * tz, tuz = rx * ty - ry * tx;

    /* Extract quaternion from full 3x3 rotation matrix (Shepperd method).
     * Matrix columns: right(r), true_up(tu), local back(-forward) because -Z faces target. */
    double m00 = rx, m01 = tux, m02 = -tx;
    double m10 = ry, m11 = tuy, m12 = -ty;
    double m20 = rz, m21 = tuz, m22 = -tz;
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
    transform3d_quat_normalize(xf->rotation);
    xf->dirty = 1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
