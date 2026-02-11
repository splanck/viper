//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_quat.c
/// @brief Quaternion mathematics for the Viper.Quat class.
///
/// Implements Hamilton quaternions for 3D rotation representation.
/// Quaternions avoid gimbal lock and provide smooth interpolation (SLERP)
/// compared to Euler angles.
///
/// Memory layout: (x, y, z, w) where w is the scalar part.
/// Unit quaternions represent rotations: |q| = 1.
/// Thread Safety: Quaternion objects are immutable after creation.
///
//===----------------------------------------------------------------------===//

#include "rt_quat.h"

#include "rt_internal.h"
#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_vec3.h"

#include <math.h>

typedef struct
{
    double x;
    double y;
    double z;
    double w;
} ViperQuat;

static ViperQuat *quat_alloc(double x, double y, double z, double w)
{
    ViperQuat *q = (ViperQuat *)rt_obj_new_i64(0, (int64_t)sizeof(ViperQuat));
    if (!q)
    {
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

void *rt_quat_new(double x, double y, double z, double w)
{
    return quat_alloc(x, y, z, w);
}

void *rt_quat_identity(void)
{
    return quat_alloc(0.0, 0.0, 0.0, 1.0);
}

void *rt_quat_from_axis_angle(void *axis, double angle)
{
    if (!axis)
    {
        rt_trap("Quat.FromAxisAngle: null axis");
        return NULL;
    }
    double ax = rt_vec3_x(axis);
    double ay = rt_vec3_y(axis);
    double az = rt_vec3_z(axis);
    double len = sqrt(ax * ax + ay * ay + az * az);
    if (len == 0.0)
        return quat_alloc(0.0, 0.0, 0.0, 1.0);

    ax /= len;
    ay /= len;
    az /= len;
    double half = angle * 0.5;
    double s = sin(half);
    return quat_alloc(ax * s, ay * s, az * s, cos(half));
}

void *rt_quat_from_euler(double pitch, double yaw, double roll)
{
    double cp = cos(pitch * 0.5);
    double sp = sin(pitch * 0.5);
    double cy = cos(yaw * 0.5);
    double sy = sin(yaw * 0.5);
    double cr = cos(roll * 0.5);
    double sr = sin(roll * 0.5);

    double w = cr * cp * cy + sr * sp * sy;
    double x = sr * cp * cy - cr * sp * sy;
    double y = cr * sp * cy + sr * cp * sy;
    double z = cr * cp * sy - sr * sp * cy;
    return quat_alloc(x, y, z, w);
}

//=============================================================================
// Property Accessors
//=============================================================================

double rt_quat_x(void *q)
{
    if (!q)
    {
        rt_trap("Quat.X: null quaternion");
        return 0.0;
    }
    return ((ViperQuat *)q)->x;
}

double rt_quat_y(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Y: null quaternion");
        return 0.0;
    }
    return ((ViperQuat *)q)->y;
}

double rt_quat_z(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Z: null quaternion");
        return 0.0;
    }
    return ((ViperQuat *)q)->z;
}

double rt_quat_w(void *q)
{
    if (!q)
    {
        rt_trap("Quat.W: null quaternion");
        return 0.0;
    }
    return ((ViperQuat *)q)->w;
}

//=============================================================================
// Operations
//=============================================================================

void *rt_quat_mul(void *a, void *b)
{
    if (!a || !b)
    {
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

void *rt_quat_conjugate(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Conjugate: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return quat_alloc(-qv->x, -qv->y, -qv->z, qv->w);
}

void *rt_quat_inverse(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Inverse: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double len_sq = qv->x * qv->x + qv->y * qv->y + qv->z * qv->z + qv->w * qv->w;
    if (len_sq == 0.0)
    {
        rt_trap("Quat.Inverse: zero-length quaternion");
        return NULL;
    }
    double inv = 1.0 / len_sq;
    return quat_alloc(-qv->x * inv, -qv->y * inv, -qv->z * inv, qv->w * inv);
}

void *rt_quat_norm(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Norm: null quaternion");
        return NULL;
    }
    ViperQuat *qv = (ViperQuat *)q;
    double len = sqrt(qv->x * qv->x + qv->y * qv->y + qv->z * qv->z + qv->w * qv->w);
    if (len == 0.0)
        return quat_alloc(0.0, 0.0, 0.0, 0.0);
    double inv = 1.0 / len;
    return quat_alloc(qv->x * inv, qv->y * inv, qv->z * inv, qv->w * inv);
}

double rt_quat_len(void *q)
{
    if (!q)
    {
        rt_trap("Quat.Len: null quaternion");
        return 0.0;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return sqrt(qv->x * qv->x + qv->y * qv->y + qv->z * qv->z + qv->w * qv->w);
}

double rt_quat_len_sq(void *q)
{
    if (!q)
    {
        rt_trap("Quat.LenSq: null quaternion");
        return 0.0;
    }
    ViperQuat *qv = (ViperQuat *)q;
    return qv->x * qv->x + qv->y * qv->y + qv->z * qv->z + qv->w * qv->w;
}

double rt_quat_dot(void *a, void *b)
{
    if (!a || !b)
    {
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

void *rt_quat_slerp(void *a, void *b, double t)
{
    if (!a || !b)
    {
        rt_trap("Quat.Slerp: null quaternion");
        return NULL;
    }
    ViperQuat *qa = (ViperQuat *)a;
    ViperQuat *qb = (ViperQuat *)b;

    double dot = qa->x * qb->x + qa->y * qb->y + qa->z * qb->z + qa->w * qb->w;

    /* If dot < 0, negate one to take the shorter arc. */
    double bx = qb->x;
    double by = qb->y;
    double bz = qb->z;
    double bw = qb->w;
    if (dot < 0.0)
    {
        dot = -dot;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }

    double s0, s1;
    if (dot > 0.9995)
    {
        /* Nearly identical — use linear interpolation to avoid division by ~0. */
        s0 = 1.0 - t;
        s1 = t;
    }
    else
    {
        double theta = acos(dot);
        double sin_theta = sin(theta);
        s0 = sin((1.0 - t) * theta) / sin_theta;
        s1 = sin(t * theta) / sin_theta;
    }

    return quat_alloc(
        s0 * qa->x + s1 * bx, s0 * qa->y + s1 * by, s0 * qa->z + s1 * bz, s0 * qa->w + s1 * bw);
}

void *rt_quat_lerp(void *a, void *b, double t)
{
    if (!a || !b)
    {
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
    double len = sqrt(x * x + y * y + z * z + w * w);
    if (len == 0.0)
        return quat_alloc(0.0, 0.0, 0.0, 1.0);
    double inv = 1.0 / len;
    return quat_alloc(x * inv, y * inv, z * inv, w * inv);
}

//=============================================================================
// Rotation
//=============================================================================

void *rt_quat_rotate_vec3(void *q, void *v)
{
    if (!q || !v)
    {
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

void *rt_quat_to_mat4(void *q)
{
    if (!q)
    {
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

void *rt_quat_axis(void *q)
{
    if (!q)
    {
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

double rt_quat_angle(void *q)
{
    if (!q)
    {
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
