//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_physics2d.c
/// @brief Simple 2D physics engine with AABB collision and impulse resolution.
///
/// Fixed-timestep Euler integration. Bodies are axis-aligned bounding boxes.
/// Mass == 0 means static (immovable). Impulse-based collision response
/// with configurable restitution (bounce) and friction.
///
//===----------------------------------------------------------------------===//

#include "rt_physics2d.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PH_MAX_BODIES 256

//=============================================================================
// Internal types
//=============================================================================

typedef struct
{
    void *vptr;
    double x, y;
    double w, h;
    double vx, vy;
    double fx, fy;
    double mass;
    double inv_mass;
    double restitution;
    double friction;
} rt_body_impl;

typedef struct
{
    void *vptr;
    double gravity_x;
    double gravity_y;
    rt_body_impl *bodies[PH_MAX_BODIES];
    int64_t body_count;
} rt_world_impl;

//=============================================================================
// Collision detection & resolution
//=============================================================================

static int8_t aabb_overlap(rt_body_impl *a, rt_body_impl *b,
                            double *nx, double *ny, double *pen)
{
    double ax1 = a->x, ay1 = a->y;
    double ax2 = a->x + a->w, ay2 = a->y + a->h;
    double bx1 = b->x, by1 = b->y;
    double bx2 = b->x + b->w, by2 = b->y + b->h;
    double ox, oy;

    if (ax2 <= bx1 || bx2 <= ax1 || ay2 <= by1 || by2 <= ay1)
        return 0;

    /* Calculate overlap on each axis */
    ox = (ax2 < bx2 ? ax2 - bx1 : bx2 - ax1);
    oy = (ay2 < by2 ? ay2 - by1 : by2 - ay1);

    /* Use minimum overlap axis as contact normal */
    if (ox < oy)
    {
        *pen = ox;
        *ny = 0.0;
        *nx = ((a->x + a->w * 0.5) < (b->x + b->w * 0.5)) ? 1.0 : -1.0;
    }
    else
    {
        *pen = oy;
        *nx = 0.0;
        *ny = ((a->y + a->h * 0.5) < (b->y + b->h * 0.5)) ? 1.0 : -1.0;
    }
    return 1;
}

static void resolve_collision(rt_body_impl *a, rt_body_impl *b,
                               double nx, double ny, double pen)
{
    double rvx, rvy, vel_along_n, e, j, total_inv, correction;

    /* Both static — skip */
    if (a->inv_mass == 0.0 && b->inv_mass == 0.0)
        return;

    /* Relative velocity of B w.r.t. A */
    rvx = b->vx - a->vx;
    rvy = b->vy - a->vy;

    /* Velocity along contact normal */
    vel_along_n = rvx * nx + rvy * ny;

    /* Skip if separating */
    if (vel_along_n > 0.0)
        return;

    /* Coefficient of restitution (use minimum) */
    e = a->restitution < b->restitution ? a->restitution : b->restitution;

    /* Impulse scalar */
    total_inv = a->inv_mass + b->inv_mass;
    j = -(1.0 + e) * vel_along_n / total_inv;

    /* Apply impulse */
    a->vx -= j * a->inv_mass * nx;
    a->vy -= j * a->inv_mass * ny;
    b->vx += j * b->inv_mass * nx;
    b->vy += j * b->inv_mass * ny;

    /* Friction impulse (tangent direction) */
    {
        double tx = rvx - vel_along_n * nx;
        double ty = rvy - vel_along_n * ny;
        double t_len = sqrt(tx * tx + ty * ty);
        if (t_len > 1e-9)
        {
            double mu, jt, vel_along_t;
            tx /= t_len;
            ty /= t_len;
            vel_along_t = rvx * tx + rvy * ty;
            mu = (a->friction + b->friction) * 0.5;
            jt = -vel_along_t / total_inv;
            /* Clamp friction impulse (Coulomb) */
            if (jt > j * mu)
                jt = j * mu;
            else if (jt < -j * mu)
                jt = -j * mu;
            a->vx -= jt * a->inv_mass * tx;
            a->vy -= jt * a->inv_mass * ty;
            b->vx += jt * b->inv_mass * tx;
            b->vy += jt * b->inv_mass * ty;
        }
    }

    /* Positional correction to prevent sinking */
    {
        double slop = 0.01;
        double pct = 0.4;
        correction = (pen - slop > 0.0 ? pen - slop : 0.0) * pct / total_inv;
        a->x -= correction * a->inv_mass * nx;
        a->y -= correction * a->inv_mass * ny;
        b->x += correction * b->inv_mass * nx;
        b->y += correction * b->inv_mass * ny;
    }
}

//=============================================================================
// World finalization
//=============================================================================

static void world_finalizer(void *obj)
{
    rt_world_impl *w = (rt_world_impl *)obj;
    if (w)
    {
        int64_t i;
        for (i = 0; i < w->body_count; i++)
        {
            if (w->bodies[i])
                rt_obj_release_check0(w->bodies[i]);
        }
        w->body_count = 0;
    }
}

//=============================================================================
// Public API - World
//=============================================================================

void *rt_physics2d_world_new(double gravity_x, double gravity_y)
{
    rt_world_impl *w = (rt_world_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_world_impl));
    if (!w)
    {
        rt_trap("Physics2D.World: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->gravity_x = gravity_x;
    w->gravity_y = gravity_y;
    w->body_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    rt_obj_set_finalizer(w, world_finalizer);
    return w;
}

void rt_physics2d_world_step(void *obj, double dt)
{
    rt_world_impl *w;
    int64_t i, j;
    if (!obj || dt <= 0.0)
        return;
    w = (rt_world_impl *)obj;

    /* 1. Apply gravity and integrate forces -> velocity */
    for (i = 0; i < w->body_count; i++)
    {
        rt_body_impl *b = w->bodies[i];
        if (!b || b->inv_mass == 0.0)
            continue;
        b->vx += (b->fx * b->inv_mass + w->gravity_x) * dt;
        b->vy += (b->fy * b->inv_mass + w->gravity_y) * dt;
        b->fx = 0.0;
        b->fy = 0.0;
    }

    /* 2. Integrate velocity -> position */
    for (i = 0; i < w->body_count; i++)
    {
        rt_body_impl *b = w->bodies[i];
        if (!b || b->inv_mass == 0.0)
            continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
    }

    /* 3. Detect and resolve collisions (N^2 narrow phase) */
    for (i = 0; i < w->body_count; i++)
    {
        for (j = i + 1; j < w->body_count; j++)
        {
            double nx, ny, pen;
            if (!w->bodies[i] || !w->bodies[j])
                continue;
            if (aabb_overlap(w->bodies[i], w->bodies[j], &nx, &ny, &pen))
            {
                resolve_collision(w->bodies[i], w->bodies[j], nx, ny, pen);
            }
        }
    }
}

void rt_physics2d_world_add(void *obj, void *body)
{
    rt_world_impl *w;
    if (!obj || !body)
        return;
    w = (rt_world_impl *)obj;
    if (w->body_count >= PH_MAX_BODIES)
        return;
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = (rt_body_impl *)body;
}

void rt_physics2d_world_remove(void *obj, void *body)
{
    rt_world_impl *w;
    int64_t i;
    if (!obj || !body)
        return;
    w = (rt_world_impl *)obj;
    for (i = 0; i < w->body_count; i++)
    {
        if (w->bodies[i] == (rt_body_impl *)body)
        {
            rt_obj_release_check0(w->bodies[i]);
            w->bodies[i] = w->bodies[w->body_count - 1];
            w->bodies[w->body_count - 1] = NULL;
            w->body_count--;
            return;
        }
    }
}

int64_t rt_physics2d_world_body_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_world_impl *)obj)->body_count;
}

void rt_physics2d_world_set_gravity(void *obj, double gx, double gy)
{
    if (!obj)
        return;
    ((rt_world_impl *)obj)->gravity_x = gx;
    ((rt_world_impl *)obj)->gravity_y = gy;
}

//=============================================================================
// Public API - Body
//=============================================================================

void *rt_physics2d_body_new(double x, double y, double w, double h, double mass)
{
    rt_body_impl *b = (rt_body_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body_impl));
    if (!b)
    {
        rt_trap("Physics2D.Body: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->vx = 0.0;
    b->vy = 0.0;
    b->fx = 0.0;
    b->fy = 0.0;
    b->mass = mass;
    b->inv_mass = (mass > 0.0) ? (1.0 / mass) : 0.0;
    b->restitution = 0.5;
    b->friction = 0.3;
    return b;
}

double rt_physics2d_body_x(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->x : 0.0;
}
double rt_physics2d_body_y(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->y : 0.0;
}
double rt_physics2d_body_w(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->w : 0.0;
}
double rt_physics2d_body_h(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->h : 0.0;
}
double rt_physics2d_body_vx(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->vx : 0.0;
}
double rt_physics2d_body_vy(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->vy : 0.0;
}

void rt_physics2d_body_set_pos(void *obj, double x, double y)
{
    if (!obj)
        return;
    ((rt_body_impl *)obj)->x = x;
    ((rt_body_impl *)obj)->y = y;
}

void rt_physics2d_body_set_vel(void *obj, double vx, double vy)
{
    if (!obj)
        return;
    ((rt_body_impl *)obj)->vx = vx;
    ((rt_body_impl *)obj)->vy = vy;
}

void rt_physics2d_body_apply_force(void *obj, double fx, double fy)
{
    if (!obj)
        return;
    ((rt_body_impl *)obj)->fx += fx;
    ((rt_body_impl *)obj)->fy += fy;
}

void rt_physics2d_body_apply_impulse(void *obj, double ix, double iy)
{
    rt_body_impl *b;
    if (!obj)
        return;
    b = (rt_body_impl *)obj;
    if (b->inv_mass == 0.0)
        return;
    b->vx += ix * b->inv_mass;
    b->vy += iy * b->inv_mass;
}

double rt_physics2d_body_restitution(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->restitution : 0.0;
}
void rt_physics2d_body_set_restitution(void *obj, double r)
{
    if (obj)
        ((rt_body_impl *)obj)->restitution = r;
}
double rt_physics2d_body_friction(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->friction : 0.0;
}
void rt_physics2d_body_set_friction(void *obj, double f)
{
    if (obj)
        ((rt_body_impl *)obj)->friction = f;
}
int8_t rt_physics2d_body_is_static(void *obj)
{
    return (obj && ((rt_body_impl *)obj)->inv_mass == 0.0) ? 1 : 0;
}
double rt_physics2d_body_mass(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->mass : 0.0;
}
