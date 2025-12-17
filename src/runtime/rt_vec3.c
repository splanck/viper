//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_vec3.c
// Purpose: Implement 3D vector math for Viper.Vec3.
//
//===----------------------------------------------------------------------===//

#include "rt_vec3.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>

/// @brief Internal Vec3 structure.
typedef struct
{
    double x;
    double y;
    double z;
} ViperVec3;

/// @brief Allocate a new Vec3 with the given components.
static ViperVec3 *vec3_alloc(double x, double y, double z)
{
    ViperVec3 *v = (ViperVec3 *)rt_obj_new_i64(0, (int64_t)sizeof(ViperVec3));
    if (!v)
    {
        rt_trap("Vec3: memory allocation failed");
        return NULL; // Unreachable after trap
    }
    v->x = x;
    v->y = y;
    v->z = z;
    return v;
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_vec3_new(double x, double y, double z)
{
    return vec3_alloc(x, y, z);
}

void *rt_vec3_zero(void)
{
    return vec3_alloc(0.0, 0.0, 0.0);
}

void *rt_vec3_one(void)
{
    return vec3_alloc(1.0, 1.0, 1.0);
}

//=============================================================================
// Property Accessors
//=============================================================================

double rt_vec3_x(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.X: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->x;
}

double rt_vec3_y(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Y: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->y;
}

double rt_vec3_z(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Z: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->z;
}

//=============================================================================
// Arithmetic Operations
//=============================================================================

void *rt_vec3_add(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Add: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return vec3_alloc(va->x + vb->x, va->y + vb->y, va->z + vb->z);
}

void *rt_vec3_sub(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Sub: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return vec3_alloc(va->x - vb->x, va->y - vb->y, va->z - vb->z);
}

void *rt_vec3_mul(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec3.Mul: null vector");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x * s, vec->y * s, vec->z * s);
}

void *rt_vec3_div(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec3.Div: null vector");
        return NULL;
    }
    if (s == 0.0)
    {
        rt_trap("Vec3.Div: division by zero");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x / s, vec->y / s, vec->z / s);
}

void *rt_vec3_neg(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Neg: null vector");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(-vec->x, -vec->y, -vec->z);
}

//=============================================================================
// Vector Products
//=============================================================================

double rt_vec3_dot(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Dot: null vector");
        return 0.0;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return va->x * vb->x + va->y * vb->y + va->z * vb->z;
}

void *rt_vec3_cross(void *a, void *b)
{
    // 3D cross product: a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)
    if (!a || !b)
    {
        rt_trap("Vec3.Cross: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    double x = va->y * vb->z - va->z * vb->y;
    double y = va->z * vb->x - va->x * vb->z;
    double z = va->x * vb->y - va->y * vb->x;
    return vec3_alloc(x, y, z);
}

//=============================================================================
// Length and Distance
//=============================================================================

double rt_vec3_len_sq(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.LenSq: null vector");
        return 0.0;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec->x * vec->x + vec->y * vec->y + vec->z * vec->z;
}

double rt_vec3_len(void *v)
{
    return sqrt(rt_vec3_len_sq(v));
}

double rt_vec3_dist(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Dist: null vector");
        return 0.0;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    double dx = vb->x - va->x;
    double dy = vb->y - va->y;
    double dz = vb->z - va->z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

//=============================================================================
// Normalization and Interpolation
//=============================================================================

void *rt_vec3_norm(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Norm: null vector");
        return NULL;
    }
    double len = rt_vec3_len(v);
    if (len == 0.0)
    {
        // Return zero vector for zero-length input
        return vec3_alloc(0.0, 0.0, 0.0);
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x / len, vec->y / len, vec->z / len);
}

void *rt_vec3_lerp(void *a, void *b, double t)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Lerp: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    // lerp(a, b, t) = a + (b - a) * t = a * (1 - t) + b * t
    double x = va->x + (vb->x - va->x) * t;
    double y = va->y + (vb->y - va->y) * t;
    double z = va->z + (vb->z - va->z) * t;
    return vec3_alloc(x, y, z);
}
