//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d_joint.c
// Purpose: Joint/constraint implementations for the 2D physics engine.
//   Provides DistanceJoint, SpringJoint, HingeJoint, RopeJoint, circle body
//   creation, and the iterative joint constraint solver.
//
// Key invariants:
//   - Joints use position-based constraint solving with Gauss-Seidel relaxation.
//   - Joint solver runs PH_JOINT_ITERATIONS passes per world step.
//   - Circle bodies store radius; AABB bodies have radius == 0.
//   - All joint constructors return NULL on invalid input (NULL body, self-joint).
//
// Ownership/Lifetime:
//   - Joint objects are GC-managed (rt_obj_new_i64).
//   - World retains joints via add/remove.
//
// Links: rt_physics2d_joint.h, rt_physics2d_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_physics2d_joint.h"
#include "rt_physics2d_internal.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Helpers
//=============================================================================

/// Get body center X
static double body_cx(rt_body_impl *b)
{
    return b->is_circle ? b->x : (b->x + b->w * 0.5);
}

/// Get body center Y
static double body_cy(rt_body_impl *b)
{
    return b->is_circle ? b->y : (b->y + b->h * 0.5);
}

/// Set body center position (adjusts x,y for AABBs)
static void body_set_center(rt_body_impl *b, double cx, double cy)
{
    if (b->is_circle)
    {
        b->x = cx;
        b->y = cy;
    }
    else
    {
        b->x = cx - b->w * 0.5;
        b->y = cy - b->h * 0.5;
    }
}

static ph_joint *alloc_joint(int32_t type, void *body_a, void *body_b)
{
    if (!body_a || !body_b || body_a == body_b)
        return NULL;

    ph_joint *j = (ph_joint *)rt_obj_new_i64(0, (int64_t)sizeof(ph_joint));
    if (!j)
        return NULL;

    j->vptr = NULL;
    j->type = type;
    j->body_a = body_a;
    j->body_b = body_b;
    j->anchor_x = 0.0;
    j->anchor_y = 0.0;
    j->length = 0.0;
    j->stiffness = 0.0;
    j->damping = 0.0;
    j->active = 1;
    return j;
}

//=============================================================================
// Distance Joint
//=============================================================================

void *rt_physics2d_distance_joint_new(void *body_a, void *body_b, double length)
{
    ph_joint *j = alloc_joint(RT_JOINT_DISTANCE, body_a, body_b);
    if (!j)
        return NULL;
    j->length = length < 0.0 ? 0.0 : length;
    return j;
}

double rt_physics2d_distance_joint_get_length(void *joint)
{
    if (!joint)
        return 0.0;
    return ((ph_joint *)joint)->length;
}

void rt_physics2d_distance_joint_set_length(void *joint, double length)
{
    if (!joint)
        return;
    ((ph_joint *)joint)->length = length < 0.0 ? 0.0 : length;
}

//=============================================================================
// Spring Joint
//=============================================================================

void *rt_physics2d_spring_joint_new(void *body_a, void *body_b,
                                    double rest_length, double stiffness,
                                    double damping)
{
    ph_joint *j = alloc_joint(RT_JOINT_SPRING, body_a, body_b);
    if (!j)
        return NULL;
    j->length = rest_length < 0.0 ? 0.0 : rest_length;
    j->stiffness = stiffness < 0.0 ? 0.0 : stiffness;
    j->damping = damping < 0.0 ? 0.0 : damping;
    return j;
}

double rt_physics2d_spring_joint_get_stiffness(void *joint)
{
    if (!joint)
        return 0.0;
    return ((ph_joint *)joint)->stiffness;
}

void rt_physics2d_spring_joint_set_stiffness(void *joint, double stiffness)
{
    if (!joint)
        return;
    ((ph_joint *)joint)->stiffness = stiffness < 0.0 ? 0.0 : stiffness;
}

double rt_physics2d_spring_joint_get_damping(void *joint)
{
    if (!joint)
        return 0.0;
    return ((ph_joint *)joint)->damping;
}

void rt_physics2d_spring_joint_set_damping(void *joint, double damping)
{
    if (!joint)
        return;
    ((ph_joint *)joint)->damping = damping < 0.0 ? 0.0 : damping;
}

//=============================================================================
// Hinge Joint
//=============================================================================

void *rt_physics2d_hinge_joint_new(void *body_a, void *body_b,
                                   double anchor_x, double anchor_y)
{
    ph_joint *j = alloc_joint(RT_JOINT_HINGE, body_a, body_b);
    if (!j)
        return NULL;
    j->anchor_x = anchor_x;
    j->anchor_y = anchor_y;
    return j;
}

double rt_physics2d_hinge_joint_get_angle(void *joint)
{
    if (!joint)
        return 0.0;
    ph_joint *j = (ph_joint *)joint;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return 0.0;
    double dx = body_cx(b) - body_cx(a);
    double dy = body_cy(b) - body_cy(a);
    return atan2(dy, dx);
}

//=============================================================================
// Rope Joint
//=============================================================================

void *rt_physics2d_rope_joint_new(void *body_a, void *body_b, double max_length)
{
    ph_joint *j = alloc_joint(RT_JOINT_ROPE, body_a, body_b);
    if (!j)
        return NULL;
    j->length = max_length < 0.0 ? 0.0 : max_length;
    return j;
}

double rt_physics2d_rope_joint_get_max_length(void *joint)
{
    if (!joint)
        return 0.0;
    return ((ph_joint *)joint)->length;
}

void rt_physics2d_rope_joint_set_max_length(void *joint, double max_length)
{
    if (!joint)
        return;
    ((ph_joint *)joint)->length = max_length < 0.0 ? 0.0 : max_length;
}

//=============================================================================
// Joint Common
//=============================================================================

void *rt_physics2d_joint_get_body_a(void *joint)
{
    if (!joint)
        return NULL;
    return ((ph_joint *)joint)->body_a;
}

void *rt_physics2d_joint_get_body_b(void *joint)
{
    if (!joint)
        return NULL;
    return ((ph_joint *)joint)->body_b;
}

int64_t rt_physics2d_joint_get_type(void *joint)
{
    if (!joint)
        return -1;
    return ((ph_joint *)joint)->type;
}

int8_t rt_physics2d_joint_is_active(void *joint)
{
    if (!joint)
        return 0;
    return ((ph_joint *)joint)->active;
}

//=============================================================================
// World Joint Management
//=============================================================================

void rt_physics2d_world_add_joint(void *world, void *joint)
{
    if (!world || !joint)
        return;
    rt_world_impl *w = (rt_world_impl *)world;
    if (w->joint_count >= PH_MAX_JOINTS)
        return;
    rt_obj_retain_maybe(joint);
    w->joints[w->joint_count++] = (ph_joint *)joint;
}

void rt_physics2d_world_remove_joint(void *world, void *joint)
{
    if (!world || !joint)
        return;
    rt_world_impl *w = (rt_world_impl *)world;
    for (int32_t i = 0; i < w->joint_count; i++)
    {
        if (w->joints[i] == (ph_joint *)joint)
        {
            rt_obj_release_check0(joint);
            w->joints[i] = w->joints[w->joint_count - 1];
            w->joints[w->joint_count - 1] = NULL;
            w->joint_count--;
            return;
        }
    }
}

int64_t rt_physics2d_world_joint_count(void *world)
{
    if (!world)
        return 0;
    return ((rt_world_impl *)world)->joint_count;
}

//=============================================================================
// Constraint Solver
//=============================================================================

static void solve_distance(ph_joint *j, double dt)
{
    (void)dt;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return;

    double dx = body_cx(b) - body_cx(a);
    double dy = body_cy(b) - body_cy(a);
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 1e-8)
        return;

    double diff = (dist - j->length) / dist;
    double total_inv = a->inv_mass + b->inv_mass;
    if (total_inv < 1e-12)
        return;

    double cx_a = dx * diff * (a->inv_mass / total_inv);
    double cy_a = dy * diff * (a->inv_mass / total_inv);
    double cx_b = dx * diff * (b->inv_mass / total_inv);
    double cy_b = dy * diff * (b->inv_mass / total_inv);

    body_set_center(a, body_cx(a) + cx_a, body_cy(a) + cy_a);
    body_set_center(b, body_cx(b) - cx_b, body_cy(b) - cy_b);
}

static void solve_spring(ph_joint *j, double dt)
{
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b || dt <= 0.0)
        return;

    double dx = body_cx(b) - body_cx(a);
    double dy = body_cy(b) - body_cy(a);
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 1e-8)
        return;

    double nx = dx / dist;
    double ny = dy / dist;

    // Spring force: F = -k * (dist - rest) - d * relVel
    double stretch = dist - j->length;
    double rel_vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
    double force = j->stiffness * stretch + j->damping * rel_vn;

    double fx = force * nx;
    double fy = force * ny;

    if (a->inv_mass > 0.0)
    {
        a->vx += fx * a->inv_mass * dt;
        a->vy += fy * a->inv_mass * dt;
    }
    if (b->inv_mass > 0.0)
    {
        b->vx -= fx * b->inv_mass * dt;
        b->vy -= fy * b->inv_mass * dt;
    }
}

static void solve_hinge(ph_joint *j, double dt)
{
    (void)dt;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return;

    // Both bodies are constrained to the anchor point
    double total_inv = a->inv_mass + b->inv_mass;
    if (total_inv < 1e-12)
        return;

    double acx = body_cx(a), acy = body_cy(a);
    double bcx = body_cx(b), bcy = body_cy(b);
    double mid_x = j->anchor_x;
    double mid_y = j->anchor_y;

    // Pull both bodies toward the anchor
    double da_x = mid_x - acx;
    double da_y = mid_y - acy;
    double db_x = mid_x - bcx;
    double db_y = mid_y - bcy;

    body_set_center(a, acx + da_x * (a->inv_mass / total_inv),
                       acy + da_y * (a->inv_mass / total_inv));
    body_set_center(b, bcx + db_x * (b->inv_mass / total_inv),
                       bcy + db_y * (b->inv_mass / total_inv));
}

static void solve_rope(ph_joint *j, double dt)
{
    (void)dt;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return;

    double dx = body_cx(b) - body_cx(a);
    double dy = body_cy(b) - body_cy(a);
    double dist = sqrt(dx * dx + dy * dy);

    // Rope only constrains when taut (distance > max_length)
    if (dist <= j->length)
        return;

    double diff = (dist - j->length) / dist;
    double total_inv = a->inv_mass + b->inv_mass;
    if (total_inv < 1e-12)
        return;

    double cx_a = dx * diff * (a->inv_mass / total_inv);
    double cy_a = dy * diff * (a->inv_mass / total_inv);
    double cx_b = dx * diff * (b->inv_mass / total_inv);
    double cy_b = dy * diff * (b->inv_mass / total_inv);

    body_set_center(a, body_cx(a) + cx_a, body_cy(a) + cy_a);
    body_set_center(b, body_cx(b) - cx_b, body_cy(b) - cy_b);
}

void rt_physics2d_solve_joints(void *world, double dt)
{
    if (!world)
        return;
    rt_world_impl *w = (rt_world_impl *)world;

    for (int iter = 0; iter < PH_JOINT_ITERATIONS; iter++)
    {
        for (int32_t i = 0; i < w->joint_count; i++)
        {
            ph_joint *j = w->joints[i];
            if (!j || !j->active)
                continue;

            switch (j->type)
            {
            case RT_JOINT_DISTANCE:
                solve_distance(j, dt);
                break;
            case RT_JOINT_SPRING:
                solve_spring(j, dt);
                break;
            case RT_JOINT_HINGE:
                solve_hinge(j, dt);
                break;
            case RT_JOINT_ROPE:
                solve_rope(j, dt);
                break;
            }
        }
    }
}

//=============================================================================
// Circle Bodies
//=============================================================================

void *rt_physics2d_circle_body_new(double cx, double cy, double radius, double mass)
{
    if (radius < 1.0)
        radius = 1.0;

    rt_body_impl *b = (rt_body_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body_impl));
    if (!b)
        return NULL;

    b->vptr = NULL;
    b->x = cx;  // For circles, x/y is center
    b->y = cy;
    b->w = 0.0;
    b->h = 0.0;
    b->vx = 0.0;
    b->vy = 0.0;
    b->fx = 0.0;
    b->fy = 0.0;
    b->mass = mass;
    b->inv_mass = (mass > 0.0) ? (1.0 / mass) : 0.0;
    b->restitution = 0.5;
    b->friction = 0.3;
    b->collision_layer = 1;
    b->collision_mask = 0xFFFFFFFF;
    b->radius = radius;
    b->is_circle = 1;
    return b;
}

double rt_physics2d_body_radius(void *body)
{
    if (!body)
        return 0.0;
    return ((rt_body_impl *)body)->radius;
}

int8_t rt_physics2d_body_is_circle(void *body)
{
    if (!body)
        return 0;
    return ((rt_body_impl *)body)->is_circle;
}
