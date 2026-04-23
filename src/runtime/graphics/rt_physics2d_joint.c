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
//   - Joint constructors return NULL for NULL/self joints and trap for wrong handle types.
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
static double body_cx(rt_body_impl *b) {
    return b->is_circle ? b->x : (b->x + b->w * 0.5);
}

/// Get body center Y
static double body_cy(rt_body_impl *b) {
    return b->is_circle ? b->y : (b->y + b->h * 0.5);
}

static double finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

static double nonnegative_finite_or_zero(double value) {
    return (isfinite(value) && value > 0.0) ? value : 0.0;
}

static rt_body_impl *checked_body(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (!rt_physics2d_is_body_handle(obj)) {
        rt_trap(api);
        return NULL;
    }
    return (rt_body_impl *)obj;
}

static ph_joint *checked_joint(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (!rt_physics2d_is_joint_handle(obj)) {
        rt_trap(api);
        return NULL;
    }
    return (ph_joint *)obj;
}

static rt_world_impl *checked_world(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (!rt_physics2d_is_world_handle(obj)) {
        rt_trap(api);
        return NULL;
    }
    return (rt_world_impl *)obj;
}

static void joint_finalizer(void *obj) {
    ph_joint *j = (ph_joint *)obj;
    if (!j)
        return;

    if (j->body_a && rt_obj_release_check0(j->body_a))
        rt_obj_free(j->body_a);
    if (j->body_b && rt_obj_release_check0(j->body_b))
        rt_obj_free(j->body_b);
    j->body_a = NULL;
    j->body_b = NULL;
}

/// Set body center position (adjusts x,y for AABBs)
static void body_set_center(rt_body_impl *b, double cx, double cy) {
    if (!b || !isfinite(cx) || !isfinite(cy))
        return;

    double old_x = b->x;
    double old_y = b->y;
    if (b->is_circle) {
        b->x = cx;
        b->y = cy;
    } else {
        b->x = cx - b->w * 0.5;
        b->y = cy - b->h * 0.5;
    }

    double dx = b->x - old_x;
    double dy = b->y - old_y;
    if (isfinite(dx))
        b->prev_x += dx;
    if (isfinite(dy))
        b->prev_y += dy;
}

static ph_joint *alloc_joint(int32_t type, void *body_a, void *body_b) {
    if (!body_a || !body_b || body_a == body_b)
        return NULL;

    rt_body_impl *a = checked_body(body_a, "Physics2D.Joint: expected Physics2D.Body for body A");
    rt_body_impl *b = checked_body(body_b, "Physics2D.Joint: expected Physics2D.Body for body B");
    if (!a || !b)
        return NULL;

    ph_joint *j = (ph_joint *)rt_obj_new_i64(RT_PHYSICS2D_JOINT_CLASS_ID, (int64_t)sizeof(ph_joint));
    if (!j)
        return NULL;

    memset(j, 0, sizeof(ph_joint));
    j->vptr = NULL;
    j->type = type;
    j->body_a = a;
    j->body_b = b;
    j->anchor_x = 0.0;
    j->anchor_y = 0.0;
    j->length = 0.0;
    j->stiffness = 0.0;
    j->damping = 0.0;
    j->active = 1;
    rt_obj_retain_maybe(a);
    rt_obj_retain_maybe(b);
    rt_obj_set_finalizer(j, joint_finalizer);
    return j;
}

//=============================================================================
// Distance Joint
//=============================================================================

/// @brief Create a distance joint that holds bodies `a` and `b` exactly `length` units apart.
/// The constraint is solved via positional correction each step (rigid rod, no springiness).
void *rt_physics2d_distance_joint_new(void *body_a, void *body_b, double length) {
    ph_joint *j = alloc_joint(RT_JOINT_DISTANCE, body_a, body_b);
    if (!j)
        return NULL;
    j->length = nonnegative_finite_or_zero(length);
    return j;
}

/// @brief Distance the joint get length of the physics2d.
double rt_physics2d_distance_joint_get_length(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.DistanceJoint.Length: expected Physics2D.Joint");
    return j ? j->length : 0.0;
}

/// @brief Distance the joint set length of the physics2d.
void rt_physics2d_distance_joint_set_length(void *joint, double length) {
    ph_joint *j = checked_joint(joint, "Physics2D.DistanceJoint.Length.set: expected Physics2D.Joint");
    if (j)
        j->length = nonnegative_finite_or_zero(length);
}

//=============================================================================
// Spring Joint
//=============================================================================

/// @brief Create a spring joint between two bodies — applies Hooke's law force proportional to
/// `stiffness` × displacement from `rest_length`, with `damping` proportional to relative velocity.
void *rt_physics2d_spring_joint_new(
    void *body_a, void *body_b, double rest_length, double stiffness, double damping) {
    ph_joint *j = alloc_joint(RT_JOINT_SPRING, body_a, body_b);
    if (!j)
        return NULL;
    j->length = nonnegative_finite_or_zero(rest_length);
    j->stiffness = nonnegative_finite_or_zero(stiffness);
    j->damping = nonnegative_finite_or_zero(damping);
    return j;
}

/// @brief Spring the joint get stiffness of the physics2d.
double rt_physics2d_spring_joint_get_stiffness(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.SpringJoint.Stiffness: expected Physics2D.Joint");
    return j ? j->stiffness : 0.0;
}

/// @brief Spring the joint set stiffness of the physics2d.
void rt_physics2d_spring_joint_set_stiffness(void *joint, double stiffness) {
    ph_joint *j = checked_joint(joint, "Physics2D.SpringJoint.Stiffness.set: expected Physics2D.Joint");
    if (j)
        j->stiffness = nonnegative_finite_or_zero(stiffness);
}

/// @brief Spring the joint get damping of the physics2d.
double rt_physics2d_spring_joint_get_damping(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.SpringJoint.Damping: expected Physics2D.Joint");
    return j ? j->damping : 0.0;
}

/// @brief Spring the joint set damping of the physics2d.
void rt_physics2d_spring_joint_set_damping(void *joint, double damping) {
    ph_joint *j = checked_joint(joint, "Physics2D.SpringJoint.Damping.set: expected Physics2D.Joint");
    if (j)
        j->damping = nonnegative_finite_or_zero(damping);
}

//=============================================================================
// Hinge Joint
//=============================================================================

/// @brief Create a hinge (pin) joint that locks two bodies to share a common world-space anchor
/// point (anchor_x, anchor_y). Bodies remain free to rotate about the anchor.
void *rt_physics2d_hinge_joint_new(void *body_a, void *body_b, double anchor_x, double anchor_y) {
    ph_joint *j = alloc_joint(RT_JOINT_HINGE, body_a, body_b);
    if (!j)
        return NULL;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    j->anchor_x = finite_or(anchor_x, body_cx(a));
    j->anchor_y = finite_or(anchor_y, body_cy(a));
    j->anchor_ax = j->anchor_x - body_cx(a);
    j->anchor_ay = j->anchor_y - body_cy(a);
    j->anchor_bx = j->anchor_x - body_cx(b);
    j->anchor_by = j->anchor_y - body_cy(b);
    return j;
}

/// @brief Hinge the joint get angle of the physics2d.
double rt_physics2d_hinge_joint_get_angle(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.HingeJoint.Angle: expected Physics2D.Joint");
    if (!j)
        return 0.0;
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

/// @brief Create a rope joint — distance constraint that's only active when bodies exceed
/// `max_length` apart. Bodies can be closer freely (chain/rope behavior, not rigid rod).
void *rt_physics2d_rope_joint_new(void *body_a, void *body_b, double max_length) {
    ph_joint *j = alloc_joint(RT_JOINT_ROPE, body_a, body_b);
    if (!j)
        return NULL;
    j->length = nonnegative_finite_or_zero(max_length);
    return j;
}

/// @brief Rope the joint get max length of the physics2d.
double rt_physics2d_rope_joint_get_max_length(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.RopeJoint.MaxLength: expected Physics2D.Joint");
    return j ? j->length : 0.0;
}

/// @brief Rope the joint set max length of the physics2d.
void rt_physics2d_rope_joint_set_max_length(void *joint, double max_length) {
    ph_joint *j = checked_joint(joint, "Physics2D.RopeJoint.MaxLength.set: expected Physics2D.Joint");
    if (j)
        j->length = nonnegative_finite_or_zero(max_length);
}

//=============================================================================
// Joint Common
//=============================================================================

/// @brief Borrow the first body referenced by the joint (caller must NOT release).
void *rt_physics2d_joint_get_body_a(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.Joint.BodyA: expected Physics2D.Joint");
    return j ? j->body_a : NULL;
}

/// @brief Borrow the second body referenced by the joint (caller must NOT release).
void *rt_physics2d_joint_get_body_b(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.Joint.BodyB: expected Physics2D.Joint");
    return j ? j->body_b : NULL;
}

/// @brief Joint the get type of the physics2d.
int64_t rt_physics2d_joint_get_type(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.Joint.Type: expected Physics2D.Joint");
    return j ? j->type : -1;
}

/// @brief Joint the is active of the physics2d.
int8_t rt_physics2d_joint_is_active(void *joint) {
    ph_joint *j = checked_joint(joint, "Physics2D.Joint.Active: expected Physics2D.Joint");
    return j ? j->active : 0;
}

//=============================================================================
// World Joint Management
//=============================================================================

static int8_t world_has_body(rt_world_impl *w, void *body) {
    if (!w || !body)
        return 0;
    for (int64_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == (rt_body_impl *)body)
            return 1;
    }
    return 0;
}

/// @brief World the add joint of the physics2d.
void rt_physics2d_world_add_joint(void *world, void *joint) {
    if (!world || !joint)
        return;
    rt_world_impl *w = checked_world(world, "Physics2D.World.AddJoint: expected Physics2D.World");
    ph_joint *j = checked_joint(joint, "Physics2D.World.AddJoint: expected Physics2D.Joint");
    if (!w || !j)
        return;
    for (int32_t i = 0; i < w->joint_count; i++) {
        if (w->joints[i] == j)
            return;
    }
    if (!world_has_body(w, j->body_a) || !world_has_body(w, j->body_b)) {
        rt_trap("Physics2D.World.AddJoint: both joint bodies must be added to the same world first");
        return;
    }
    if (w->joint_count >= PH_MAX_JOINTS) {
        rt_trap("Physics2D.World.AddJoint: joint limit exceeded (max " RT_PH_MAX_JOINTS_STR
                "); increase PH_MAX_JOINTS and recompile");
        return;
    }
    j->active = 1;
    rt_obj_retain_maybe(joint);
    w->joints[w->joint_count++] = j;
}

/// @brief World the remove joint of the physics2d.
void rt_physics2d_world_remove_joint(void *world, void *joint) {
    if (!world || !joint)
        return;
    rt_world_impl *w = checked_world(world, "Physics2D.World.RemoveJoint: expected Physics2D.World");
    ph_joint *j = checked_joint(joint, "Physics2D.World.RemoveJoint: expected Physics2D.Joint");
    if (!w || !j)
        return;
    for (int32_t i = 0; i < w->joint_count; i++) {
        if (w->joints[i] == j) {
            j->active = 0;
            if (rt_obj_release_check0(j))
                rt_obj_free(j);
            w->joints[i] = w->joints[w->joint_count - 1];
            w->joints[w->joint_count - 1] = NULL;
            w->joint_count--;
            return;
        }
    }
}

/// @brief Return the count of elements in the physics2d.
int64_t rt_physics2d_world_joint_count(void *world) {
    rt_world_impl *w = checked_world(world, "Physics2D.World.JointCount: expected Physics2D.World");
    return w ? w->joint_count : 0;
}

//=============================================================================
// Constraint Solver
//=============================================================================

static void solve_distance(ph_joint *j, double dt) {
    (void)dt;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return;

    double total_inv = a->inv_mass + b->inv_mass;
    if (total_inv < 1e-12)
        return;

    double dx = body_cx(b) - body_cx(a);
    double dy = body_cy(b) - body_cy(a);
    double dist = sqrt(dx * dx + dy * dy);
    double nx = 1.0;
    double ny = 0.0;
    if (dist >= 1e-8) {
        nx = dx / dist;
        ny = dy / dist;
    } else if (j->length <= 0.0) {
        return;
    } else {
        dist = 0.0;
    }

    double error = dist - j->length;
    double cx_a = nx * error * (a->inv_mass / total_inv);
    double cy_a = ny * error * (a->inv_mass / total_inv);
    double cx_b = nx * error * (b->inv_mass / total_inv);
    double cy_b = ny * error * (b->inv_mass / total_inv);

    body_set_center(a, body_cx(a) + cx_a, body_cy(a) + cy_a);
    body_set_center(b, body_cx(b) - cx_b, body_cy(b) - cy_b);
}

static void solve_spring(ph_joint *j, double dt) {
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

    if (a->inv_mass > 0.0) {
        a->vx += fx * a->inv_mass * dt;
        a->vy += fy * a->inv_mass * dt;
    }
    if (b->inv_mass > 0.0) {
        b->vx -= fx * b->inv_mass * dt;
        b->vy -= fy * b->inv_mass * dt;
    }
}

static void solve_hinge(ph_joint *j, double dt) {
    (void)dt;
    rt_body_impl *a = (rt_body_impl *)j->body_a;
    rt_body_impl *b = (rt_body_impl *)j->body_b;
    if (!a || !b)
        return;

    double total_inv = a->inv_mass + b->inv_mass;
    if (total_inv < 1e-12)
        return;

    double acx = body_cx(a), acy = body_cy(a);
    double bcx = body_cx(b), bcy = body_cy(b);
    double a_anchor_x = acx + j->anchor_ax;
    double a_anchor_y = acy + j->anchor_ay;
    double b_anchor_x = bcx + j->anchor_bx;
    double b_anchor_y = bcy + j->anchor_by;
    double dx = b_anchor_x - a_anchor_x;
    double dy = b_anchor_y - a_anchor_y;

    body_set_center(a,
                    acx + dx * (a->inv_mass / total_inv),
                    acy + dy * (a->inv_mass / total_inv));
    body_set_center(b,
                    bcx - dx * (b->inv_mass / total_inv),
                    bcy - dy * (b->inv_mass / total_inv));
    j->anchor_x = (a_anchor_x + b_anchor_x) * 0.5;
    j->anchor_y = (a_anchor_y + b_anchor_y) * 0.5;
}

static void solve_rope(ph_joint *j, double dt) {
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

void rt_physics2d_solve_spring_joints(void *world, double dt) {
    rt_world_impl *w = checked_world(world, "Physics2D.World: expected Physics2D.World");
    if (!w)
        return;

    for (int32_t i = 0; i < w->joint_count; i++) {
        ph_joint *j = w->joints[i];
        if (!j || !j->active || j->type != RT_JOINT_SPRING)
            continue;
        solve_spring(j, dt);
    }
}

void rt_physics2d_solve_position_joints(void *world, double dt) {
    rt_world_impl *w = checked_world(world, "Physics2D.World: expected Physics2D.World");
    if (!w)
        return;

    for (int iter = 0; iter < PH_JOINT_ITERATIONS; iter++) {
        for (int32_t i = 0; i < w->joint_count; i++) {
            ph_joint *j = w->joints[i];
            if (!j || !j->active)
                continue;

            switch (j->type) {
                case RT_JOINT_DISTANCE:
                    solve_distance(j, dt);
                    break;
                case RT_JOINT_SPRING:
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

/// @brief Solve the joints of the physics2d.
void rt_physics2d_solve_joints(void *world, double dt) {
    rt_physics2d_solve_spring_joints(world, dt);
    rt_physics2d_solve_position_joints(world, dt);
}

//=============================================================================
// Circle Bodies
//=============================================================================

/// @brief Construct a circle-shaped rigid body centered at (cx, cy) with `radius` and `mass`.
/// Otherwise behaves like `_body_new` (default restitution 0.5, friction 0.3, layer 1, mask 0xFF...).
void *rt_physics2d_circle_body_new(double cx, double cy, double radius, double mass) {
    cx = finite_or(cx, 0.0);
    cy = finite_or(cy, 0.0);
    if (!isfinite(radius) || radius < 1.0)
        radius = 1.0;
    mass = (isfinite(mass) && mass > 0.0) ? mass : 0.0;

    rt_body_impl *b =
        (rt_body_impl *)rt_obj_new_i64(RT_PHYSICS2D_BODY_CLASS_ID, (int64_t)sizeof(rt_body_impl));
    if (!b)
        return NULL;

    b->vptr = NULL;
    b->x = cx; // For circles, x/y is center
    b->y = cy;
    b->prev_x = cx;
    b->prev_y = cy;
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
    b->collision_mask = INT64_C(-1);
    b->radius = radius;
    b->is_circle = 1;
    return b;
}

/// @brief Body the radius of the physics2d.
double rt_physics2d_body_radius(void *body) {
    rt_body_impl *b = checked_body(body, "Physics2D.Body.Radius: expected Physics2D.Body");
    return b ? b->radius : 0.0;
}

/// @brief Body the is circle of the physics2d.
int8_t rt_physics2d_body_is_circle(void *body) {
    rt_body_impl *b = checked_body(body, "Physics2D.Body.IsCircle: expected Physics2D.Body");
    return b ? b->is_circle : 0;
}
