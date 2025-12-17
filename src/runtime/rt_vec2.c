//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_vec2.c
// Purpose: Implement 2D vector math for Viper.Vec2.
//
//===----------------------------------------------------------------------===//

#include "rt_vec2.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>

/// @brief Internal Vec2 structure.
typedef struct
{
    double x;
    double y;
} ViperVec2;

/// @brief Allocate a new Vec2 with the given components.
static ViperVec2 *vec2_alloc(double x, double y)
{
    ViperVec2 *v = (ViperVec2 *)rt_obj_new_i64(0, (int64_t)sizeof(ViperVec2));
    if (!v)
    {
        rt_trap("Vec2: memory allocation failed");
        return NULL; // Unreachable after trap
    }
    v->x = x;
    v->y = y;
    return v;
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_vec2_new(double x, double y)
{
    return vec2_alloc(x, y);
}

void *rt_vec2_zero(void)
{
    return vec2_alloc(0.0, 0.0);
}

void *rt_vec2_one(void)
{
    return vec2_alloc(1.0, 1.0);
}

//=============================================================================
// Property Accessors
//=============================================================================

double rt_vec2_x(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.X: null vector");
        return 0.0;
    }
    return ((ViperVec2 *)v)->x;
}

double rt_vec2_y(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.Y: null vector");
        return 0.0;
    }
    return ((ViperVec2 *)v)->y;
}

//=============================================================================
// Arithmetic Operations
//=============================================================================

void *rt_vec2_add(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec2.Add: null vector");
        return NULL;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    return vec2_alloc(va->x + vb->x, va->y + vb->y);
}

void *rt_vec2_sub(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec2.Sub: null vector");
        return NULL;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    return vec2_alloc(va->x - vb->x, va->y - vb->y);
}

void *rt_vec2_mul(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec2.Mul: null vector");
        return NULL;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return vec2_alloc(vec->x * s, vec->y * s);
}

void *rt_vec2_div(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec2.Div: null vector");
        return NULL;
    }
    if (s == 0.0)
    {
        rt_trap("Vec2.Div: division by zero");
        return NULL;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return vec2_alloc(vec->x / s, vec->y / s);
}

void *rt_vec2_neg(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.Neg: null vector");
        return NULL;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return vec2_alloc(-vec->x, -vec->y);
}

//=============================================================================
// Vector Products
//=============================================================================

double rt_vec2_dot(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec2.Dot: null vector");
        return 0.0;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    return va->x * vb->x + va->y * vb->y;
}

double rt_vec2_cross(void *a, void *b)
{
    // 2D cross product returns the scalar z-component of the 3D cross product
    // (treating vectors as 3D with z=0): a.x * b.y - a.y * b.x
    if (!a || !b)
    {
        rt_trap("Vec2.Cross: null vector");
        return 0.0;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    return va->x * vb->y - va->y * vb->x;
}

//=============================================================================
// Length and Distance
//=============================================================================

double rt_vec2_len_sq(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.LenSq: null vector");
        return 0.0;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return vec->x * vec->x + vec->y * vec->y;
}

double rt_vec2_len(void *v)
{
    return sqrt(rt_vec2_len_sq(v));
}

double rt_vec2_dist(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec2.Dist: null vector");
        return 0.0;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    double dx = vb->x - va->x;
    double dy = vb->y - va->y;
    return sqrt(dx * dx + dy * dy);
}

//=============================================================================
// Normalization and Interpolation
//=============================================================================

void *rt_vec2_norm(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.Norm: null vector");
        return NULL;
    }
    double len = rt_vec2_len(v);
    if (len == 0.0)
    {
        // Return zero vector for zero-length input
        return vec2_alloc(0.0, 0.0);
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return vec2_alloc(vec->x / len, vec->y / len);
}

void *rt_vec2_lerp(void *a, void *b, double t)
{
    if (!a || !b)
    {
        rt_trap("Vec2.Lerp: null vector");
        return NULL;
    }
    ViperVec2 *va = (ViperVec2 *)a;
    ViperVec2 *vb = (ViperVec2 *)b;
    // lerp(a, b, t) = a + (b - a) * t = a * (1 - t) + b * t
    double x = va->x + (vb->x - va->x) * t;
    double y = va->y + (vb->y - va->y) * t;
    return vec2_alloc(x, y);
}

//=============================================================================
// Angle and Rotation
//=============================================================================

double rt_vec2_angle(void *v)
{
    if (!v)
    {
        rt_trap("Vec2.Angle: null vector");
        return 0.0;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    return atan2(vec->y, vec->x);
}

void *rt_vec2_rotate(void *v, double angle)
{
    if (!v)
    {
        rt_trap("Vec2.Rotate: null vector");
        return NULL;
    }
    ViperVec2 *vec = (ViperVec2 *)v;
    double c = cos(angle);
    double s = sin(angle);
    double x = vec->x * c - vec->y * s;
    double y = vec->x * s + vec->y * c;
    return vec2_alloc(x, y);
}
