//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_quat.c
// Purpose: Quaternion mathematics for the Viper.Quat class. Implements Hamilton
//   quaternions for 3D rotation: construction from axis-angle and Euler angles,
//   multiplication (composition), conjugate/inverse, normalization, dot product,
//   spherical linear interpolation (Slerp), Vec3 rotation, and conversion to/from
//   Mat4. Quaternions avoid gimbal lock and provide smooth rotation blending.
//
// Key invariants:
//   - Memory layout is (x, y, z, w) where w is the scalar part; unit quaternions
//     satisfy |q| = 1.0 and represent valid 3D rotations.
//   - All constructor functions call quat_alloc() which may trap on OOM; callers
//     should not pass NULL results to further operations.
//   - Slerp clamps the dot product to [-1, 1] to guard against acos domain errors
//     from floating-point rounding; falls back to linear interpolation when the
//     angle is near zero.
//   - Quat objects are immutable after creation; all operations return new GC
//     heap objects.
//   - FromAxisAngle normalizes the axis vector; a zero-length axis returns the
//     identity quaternion (0, 0, 0, 1).
//
// Ownership/Lifetime:
//   - All Quat objects are allocated via rt_obj_new_i64 (GC heap); the struct
//     contains only four doubles and requires no finalizer.
//
// Links: src/runtime/graphics/rt_quat.h (public API),
//        src/runtime/graphics/rt_vec3.h (axis operand and rotation result type),
//        src/runtime/graphics/rt_mat4.h (rotation matrix conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_quat.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_vec3.h"

#include <math.h>

typedef struct {
    double x;
    double y;
    double z;
    double w;
} ViperQuat;

/// @brief Return whether @p q is a Quat-compatible heap payload.
/// @details Accepts the explicit Quat class id from current constructors and the historical
///          class-id-zero value object layout. Legacy classless payloads must be exactly four
///          doubles so unrelated raw heap values are rejected.
/// @param q Candidate runtime object payload.
/// @return 1 for a compatible quaternion payload, otherwise 0.
static int quat_is_compatible_object(void *q) {
    if (!q)
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(q, &hdr) || !hdr)
        return 0;
    if (hdr->kind != RT_HEAP_OBJECT || hdr->elem_kind != RT_ELEM_NONE)
        return 0;
    if (hdr->class_id == RT_QUAT_CLASS_ID)
        return hdr->cap >= sizeof(ViperQuat);
    return hdr->class_id == 0 && hdr->len == sizeof(ViperQuat) && hdr->cap == sizeof(ViperQuat);
}

/// @brief Validate and cast an opaque handle to a quaternion payload.
/// @details Rejects NULL, non-object heap payloads, incompatible class identifiers, and
///   undersized allocations before quaternion fields are read.
/// @param q Candidate Quat runtime handle.
/// @param op Diagnostic prefix used if validation fails.
/// @return Typed quaternion payload, or NULL after trapping.
static ViperQuat *quat_checked(void *q, const char *op) {
    if (!quat_is_compatible_object(q)) {
        rt_trap(op ? op : "Quat: invalid quaternion");
        return NULL;
    }
    return (ViperQuat *)q;
}

/// @brief Compute a finite, overflow-resistant Euclidean norm for quaternion components.
/// @details Uses `hypot` in a chain so very large finite inputs do not overflow while
///          squaring and very small inputs do not underflow to zero prematurely. Non-finite
///          components return `INFINITY`, giving callers a deterministic fallback path
///          instead of propagating NaN payloads into transform math.
static double quat_safe_len4(double x, double y, double z, double w) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(w))
        return INFINITY;
    return hypot(hypot(x, y), hypot(z, w));
}

/// @brief Compute a finite, overflow-resistant Euclidean norm for a 3D axis.
/// @details This is the axis-specific counterpart to quat_safe_len4 and prevents
///          `FromAxisAngle` from overflowing while normalizing unusually large but
///          otherwise valid direction vectors.
static double quat_safe_len3(double x, double y, double z) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return INFINITY;
    return hypot(hypot(x, y), z);
}

/// @brief Allocate a GC-managed quaternion object and initialize all four components.
/// @return New ViperQuat with the given (x, y, z, w) components, or NULL on OOM.
static ViperQuat *quat_alloc(double x, double y, double z, double w) {
    ViperQuat *q = (ViperQuat *)rt_obj_new_i64(RT_QUAT_CLASS_ID, (int64_t)sizeof(ViperQuat));
    if (!q) {
        rt_trap("Quat: memory allocation failed");
        return NULL;
    }
    q->x = x;
    q->y = y;
    q->z = z;
    q->w = w;
    return q;
}

//=============================================================================
// Constructors
//=============================================================================

/// @brief Construct a quaternion from raw components (x, y, z, w). For unit quaternions:
/// (x, y, z) is the imaginary part, w is the real part. Use `_from_axis_angle` or `_from_euler`
/// for higher-level construction.
void *rt_quat_new(double x, double y, double z, double w) {
    return quat_alloc(x, y, z, w);
}

/// @brief Return the identity quaternion (0, 0, 0, 1) — represents "no rotation".
void *rt_quat_identity(void) {
    return quat_alloc(0.0, 0.0, 0.0, 1.0);
}

/// @brief Build a unit quaternion representing a rotation of `angle` radians about `axis`. The
/// axis is normalized internally; a zero-length axis or non-finite angle returns identity.
void *rt_quat_from_axis_angle(void *axis, double angle) {
    if (!axis) {
        rt_trap("Quat.FromAxisAngle: null axis");
        return NULL;
    }
    if (!isfinite(angle))
        return quat_alloc(0.0, 0.0, 0.0, 1.0);
    double ax = rt_vec3_x(axis);
    double ay = rt_vec3_y(axis);
    double az = rt_vec3_z(axis);
    double len = quat_safe_len3(ax, ay, az);
    if (len == 0.0 || !isfinite(len))
        return quat_alloc(0.0, 0.0, 0.0, 1.0);

    ax /= len;
    ay /= len;
    az /= len;
    double half = angle * 0.5;
    double s = sin(half);
    return quat_alloc(ax * s, ay * s, az * s, cos(half));
}

/// @brief Build a unit quaternion from Euler angles (radians). Convention: pitch about X, yaw
/// about Y, roll about Z, composed in ZYX intrinsic order (yaw, then pitch, then roll). This is
/// the same convention as Viper.Graphics3D.Transform3D.SetEuler so every Euler-consuming API in
/// the engine agrees on axes.
void *rt_quat_from_euler(double pitch, double yaw, double roll) {
    if (!isfinite(pitch) || !isfinite(yaw) || !isfinite(roll))
        return quat_alloc(0.0, 0.0, 0.0, 1.0);
    double cp = cos(pitch * 0.5);
    double sp = sin(pitch * 0.5);
    double cy = cos(yaw * 0.5);
    double sy = sin(yaw * 0.5);
    double cr = cos(roll * 0.5);
    double sr = sin(roll * 0.5);

    double x = sp * cy * cr - cp * sy * sr;
    double y = cp * sy * cr + sp * cy * sr;
    double z = cp * cy * sr - sp * sy * cr;
    double w = cp * cy * cr + sp * sy * sr;
    return quat_alloc(x, y, z, w);
}

//=============================================================================
// Property Accessors
//=============================================================================

/// @brief X the quat.
double rt_quat_x(void *q) {
    ViperQuat *quat = quat_checked(q, "Quat.X: invalid quaternion");
    if (!quat)
        return 0.0;
    return quat->x;
}

/// @brief Y the quat.
double rt_quat_y(void *q) {
    ViperQuat *quat = quat_checked(q, "Quat.Y: invalid quaternion");
    if (!quat)
        return 0.0;
    return quat->y;
}

/// @brief Z the quat.
double rt_quat_z(void *q) {
    ViperQuat *quat = quat_checked(q, "Quat.Z: invalid quaternion");
    if (!quat)
        return 0.0;
    return quat->z;
}

/// @brief W the quat.
double rt_quat_w(void *q) {
    ViperQuat *quat = quat_checked(q, "Quat.W: invalid quaternion");
    if (!quat)
        return 0.0;
    return quat->w;
}

//=============================================================================
// Operations
//=============================================================================

/// @brief Hamilton product (a × b) — composes rotations: applying `mul(a, b)` rotates the
/// vector by b first, then by a. Traps on null input.
void *rt_quat_mul(void *a, void *b) {
    if (!a || !b) {
        rt_trap("Quat.Mul: null quaternion");
        return NULL;
    }
    ViperQuat *qa = (ViperQuat *)a;
    ViperQuat *qb = (ViperQuat *)b;
    double w = qa->w * qb->w - qa->x * qb->x - qa->y * qb->y - qa->z * qb->z;
    double x = qa->w * qb->x + qa->x * qb->w + qa->y * qb->z - qa->z * qb->y;
    double y = qa->w * qb->y - qa->x * qb->z + qa->y * qb->w + qa->z * qb->x;
    double z = qa->w * qb->z + qa->x * qb->y - qa->y * qb->x + qa->z * qb->w;
    return quat_alloc(x, y, z, w);
}

/// @brief Quaternion conjugate (negates the imaginary part: x, y, z → -x, -y, -z; w stays).
/// For unit quaternions, conjugate equals inverse and represents the opposite rotation.
void *rt_quat_conjugate(void *q) {
    if (!q) {
        rt_trap("Quat.Conjugate: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return quat_alloc(-qv->x, -qv->y, -qv->z, qv->w);
}

/// @brief Quaternion inverse (conjugate / |q|²). For unit quaternions matches `_conjugate`
/// but is safer for general use. Traps on null or zero-length/non-finite input.
void *rt_quat_inverse(void *q) {
    if (!q) {
        rt_trap("Quat.Inverse: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double len = quat_safe_len4(qv->x, qv->y, qv->z, qv->w);
    if (len == 0.0 || !isfinite(len)) {
        rt_trap("Quat.Inverse: invalid quaternion length");
        return NULL;
    }
    double len_sq = len * len;
    double inv = 1.0 / len_sq;
    return quat_alloc(-qv->x * inv, -qv->y * inv, -qv->z * inv, qv->w * inv);
}

/// @brief Normalize `q` to unit length. Returns the zero quaternion (0,0,0,0) if `q` is zero
/// to avoid divide-by-zero. Re-normalize periodically when chaining many multiplies to prevent
/// drift from accumulated floating-point error.
void *rt_quat_norm(void *q) {
    if (!q) {
        rt_trap("Quat.Norm: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double len = quat_safe_len4(qv->x, qv->y, qv->z, qv->w);
    if (len == 0.0 || !isfinite(len))
        return quat_alloc(0.0, 0.0, 0.0, 0.0);
    double inv = 1.0 / len;
    return quat_alloc(qv->x * inv, qv->y * inv, qv->z * inv, qv->w * inv);
}

/// @brief Return the number of elements in the quat.
double rt_quat_len(void *q) {
    if (!q) {
        rt_trap("Quat.Len: null quaternion");
        return 0.0;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return quat_safe_len4(qv->x, qv->y, qv->z, qv->w);
}

/// @brief Len the sq of the quat.
double rt_quat_len_sq(void *q) {
    if (!q) {
        rt_trap("Quat.LenSq: null quaternion");
        return 0.0;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return qv->x * qv->x + qv->y * qv->y + qv->z * qv->z + qv->w * qv->w;
}

/// @brief Dot the quat.
double rt_quat_dot(void *a, void *b) {
    if (!a || !b) {
        rt_trap("Quat.Dot: null quaternion");
        return 0.0;
    }
    ViperQuat *qa = (ViperQuat *)a;
    ViperQuat *qb = (ViperQuat *)b;
    return qa->x * qb->x + qa->y * qb->y + qa->z * qb->z + qa->w * qb->w;
}

//=============================================================================
// Interpolation
//=============================================================================

/// @brief Spherical linear interpolation between unit quaternions `a` and `b`. `t` ∈ [0, 1]
/// (0 = a, 1 = b). Picks the shorter arc by negating one operand if the dot product is < 0.
/// Falls back to `_lerp` for nearly-aligned inputs to avoid numerical instability.
void *rt_quat_slerp(void *a, void *b, double t) {
    if (!isfinite(t)) {
        rt_trap("Quat.Slerp: non-finite interpolation parameter");
        return NULL;
    }
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    ViperQuat *qa = quat_checked(a, "Quat.Slerp: invalid start quaternion");
    ViperQuat *qb = quat_checked(b, "Quat.Slerp: invalid end quaternion");
    if (!qa || !qb)
        return NULL;

    double dot = qa->x * qb->x + qa->y * qb->y + qa->z * qb->z + qa->w * qb->w;

    /* If dot < 0, negate one to take the shorter arc. */
    double bx = qb->x;
    double by = qb->y;
    double bz = qb->z;
    double bw = qb->w;
    if (dot < 0.0) {
        dot = -dot;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }
    if (!isfinite(dot)) {
        rt_trap("Quat.Slerp: non-finite quaternion dot");
        return quat_alloc(0.0, 0.0, 0.0, 1.0);
    }
    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;

    double s0, s1;
    if (dot > 0.9995) {
        /* Nearly identical — use linear interpolation to avoid division by ~0. */
        s0 = 1.0 - t;
        s1 = t;
    } else {
        double theta = acos(dot);
        double sin_theta = sin(theta);
        s0 = sin((1.0 - t) * theta) / sin_theta;
        s1 = sin(t * theta) / sin_theta;
    }

    return quat_alloc(
        s0 * qa->x + s1 * bx, s0 * qa->y + s1 * by, s0 * qa->z + s1 * bz, s0 * qa->w + s1 * bw);
}

/// @brief Linear quaternion interpolation (component-wise) between `a` and `b`. Faster than
/// slerp but constant angular velocity is not preserved — use only for small angle deltas.
/// Result is *not* automatically normalized.
void *rt_quat_lerp(void *a, void *b, double t) {
    if (!a || !b) {
        rt_trap("Quat.Lerp: null quaternion");
        return NULL;
    }
    ViperQuat *qa = (ViperQuat *)a;
    ViperQuat *qb = (ViperQuat *)b;
    double omt = 1.0 - t;
    double x = omt * qa->x + t * qb->x;
    double y = omt * qa->y + t * qb->y;
    double z = omt * qa->z + t * qb->z;
    double w = omt * qa->w + t * qb->w;
    double len = quat_safe_len4(x, y, z, w);
    if (len == 0.0 || !isfinite(len))
        return quat_alloc(0.0, 0.0, 0.0, 1.0);
    double inv = 1.0 / len;
    return quat_alloc(x * inv, y * inv, z * inv, w * inv);
}

//=============================================================================
// Rotation
//=============================================================================

/// @brief Apply rotation `q` to vector `v` (returns a new Vec3). Computes `q · v · q*` using
/// the optimized formula `v + 2 * cross(qxyz, cross(qxyz, v) + qw * v)`.
void *rt_quat_rotate_vec3(void *q, void *v) {
    if (!q || !v) {
        rt_trap("Quat.RotateVec3: null argument");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double vx = rt_vec3_x(v);
    double vy = rt_vec3_y(v);
    double vz = rt_vec3_z(v);

    /* Optimized q * v * q^-1 for unit quaternions (avoids full multiplication). */
    double tx = 2.0 * (qv->y * vz - qv->z * vy);
    double ty = 2.0 * (qv->z * vx - qv->x * vz);
    double tz = 2.0 * (qv->x * vy - qv->y * vx);

    double rx = vx + qv->w * tx + (qv->y * tz - qv->z * ty);
    double ry = vy + qv->w * ty + (qv->z * tx - qv->x * tz);
    double rz = vz + qv->w * tz + (qv->x * ty - qv->y * tx);

    return rt_vec3_new(rx, ry, rz);
}

/// @brief Convert the quaternion to an equivalent 4×4 rotation matrix (translation = 0,
/// scale = 1). Useful for shader uniforms that prefer matrix uniforms over quaternion math.
void *rt_quat_to_mat4(void *q) {
    if (!q) {
        rt_trap("Quat.ToMat4: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double x = qv->x;
    double y = qv->y;
    double z = qv->z;
    double w = qv->w;

    double x2 = x + x;
    double y2 = y + y;
    double z2 = z + z;
    double xx = x * x2;
    double xy = x * y2;
    double xz = x * z2;
    double yy = y * y2;
    double yz = y * z2;
    double zz = z * z2;
    double wx = w * x2;
    double wy = w * y2;
    double wz = w * z2;

    /* Row-major 4x4 rotation matrix. */
    return rt_mat4_new(1.0 - (yy + zz),
                       xy - wz,
                       xz + wy,
                       0.0,
                       xy + wz,
                       1.0 - (xx + zz),
                       yz - wx,
                       0.0,
                       xz - wy,
                       yz + wx,
                       1.0 - (xx + yy),
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       1.0);
}

/// @brief Extract the rotation axis from a unit quaternion (the inverse of `_from_axis_angle`).
/// Returns (1, 0, 0) for an identity-or-degenerate quaternion (no meaningful axis).
void *rt_quat_axis(void *q) {
    if (!q) {
        rt_trap("Quat.Axis: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double s_sq = 1.0 - qv->w * qv->w;
    if (s_sq <= 0.0)
        return rt_vec3_new(0.0, 0.0, 1.0); /* Identity — arbitrary axis. */
    double inv_s = 1.0 / sqrt(s_sq);
    return rt_vec3_new(qv->x * inv_s, qv->y * inv_s, qv->z * inv_s);
}

/// @brief Angle the quat.
double rt_quat_angle(void *q) {
    if (!q) {
        rt_trap("Quat.Angle: null quaternion");
        return 0.0;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double w_clamped = qv->w;
    if (w_clamped > 1.0)
        w_clamped = 1.0;
    if (w_clamped < -1.0)
        w_clamped = -1.0;
    return 2.0 * acos(w_clamped);
}
