//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_joints3d.c
// Purpose: 3D physics joint constraints. Distance joints maintain fixed
//   separation via positional correction. Spring joints apply Hooke's law
//   forces with damping. Both operate on Body3D position/velocity directly.
//
// Key invariants:
//   - Distance joint: positional correction pushes bodies to target distance.
//   - Spring joint: force = -stiffness * (dist - rest) - damping * rel_vel.
//   - Both handle zero-distance edge case (coincident centers).
//   - Joints with NULL body references are no-ops (safe after GC collection).
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: rt_joints3d.h, rt_physics3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_joints3d.h"
#include "rt_physics3d.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);

/* Access body internals — these match the rt_body3d struct layout in rt_physics3d.c */
typedef struct {
    void *vptr;
    double position[3];
    double velocity[3];
    double force[3];
    double mass;
    double inv_mass;
} rt_body3d_view;

/*==========================================================================
 * Distance Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_view *body_a;
    rt_body3d_view *body_b;
    double target_distance;
} rt_distance_joint3d;

static void distance_joint_finalizer(void *obj) {
    (void)obj;
}

/// @brief Create a distance joint that constrains two bodies to a fixed separation.
/// @details The joint applies positional correction and velocity damping each
///          physics step to maintain the target distance. Both bodies must be
///          non-null. If both are static (zero inverse mass), the joint is inert.
/// @param body_a   First body handle.
/// @param body_b   Second body handle.
/// @param distance Target separation distance in world units.
/// @return Opaque joint handle, or NULL on failure.
void *rt_distance_joint3d_new(void *body_a, void *body_b, double distance) {
    if (!body_a || !body_b) {
        rt_trap("DistanceJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    rt_distance_joint3d *j =
        (rt_distance_joint3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_distance_joint3d));
    if (!j) {
        rt_trap("DistanceJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_view *)body_a;
    j->body_b = (rt_body3d_view *)body_b;
    j->target_distance = distance;
    rt_obj_set_finalizer(j, distance_joint_finalizer);
    return j;
}

/// @brief Get the target distance of a distance joint.
double rt_distance_joint3d_get_distance(void *joint) {
    return joint ? ((rt_distance_joint3d *)joint)->target_distance : 0;
}

/// @brief Change the target distance of a distance joint at runtime.
void rt_distance_joint3d_set_distance(void *joint, double distance) {
    if (joint)
        ((rt_distance_joint3d *)joint)->target_distance = distance;
}

static void solve_distance(rt_distance_joint3d *j, double dt) {
    if (!j || !j->body_a || !j->body_b)
        return;
    (void)dt;

    double dx = j->body_b->position[0] - j->body_a->position[0];
    double dy = j->body_b->position[1] - j->body_a->position[1];
    double dz = j->body_b->position[2] - j->body_a->position[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (dist < 1e-12)
        return; /* coincident — can't determine direction */

    double error = dist - j->target_distance;
    double inv_dist = 1.0 / dist;
    double nx = dx * inv_dist;
    double ny = dy * inv_dist;
    double nz = dz * inv_dist;

    double inv_sum = j->body_a->inv_mass + j->body_b->inv_mass;
    if (inv_sum < 1e-12)
        return; /* both static */

    /* Positional correction: move each body proportional to inverse mass */
    double correction = error / inv_sum;

    j->body_a->position[0] += correction * j->body_a->inv_mass * nx;
    j->body_a->position[1] += correction * j->body_a->inv_mass * ny;
    j->body_a->position[2] += correction * j->body_a->inv_mass * nz;
    j->body_b->position[0] -= correction * j->body_b->inv_mass * nx;
    j->body_b->position[1] -= correction * j->body_b->inv_mass * ny;
    j->body_b->position[2] -= correction * j->body_b->inv_mass * nz;

    /* Velocity correction: remove relative velocity along constraint axis */
    double rvx = j->body_b->velocity[0] - j->body_a->velocity[0];
    double rvy = j->body_b->velocity[1] - j->body_a->velocity[1];
    double rvz = j->body_b->velocity[2] - j->body_a->velocity[2];
    double rv_along = rvx * nx + rvy * ny + rvz * nz;

    double jn = rv_along / inv_sum;
    j->body_a->velocity[0] += jn * j->body_a->inv_mass * nx;
    j->body_a->velocity[1] += jn * j->body_a->inv_mass * ny;
    j->body_a->velocity[2] += jn * j->body_a->inv_mass * nz;
    j->body_b->velocity[0] -= jn * j->body_b->inv_mass * nx;
    j->body_b->velocity[1] -= jn * j->body_b->inv_mass * ny;
    j->body_b->velocity[2] -= jn * j->body_b->inv_mass * nz;
}

/*==========================================================================
 * Spring Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_view *body_a;
    rt_body3d_view *body_b;
    double rest_length;
    double stiffness;
    double damping;
} rt_spring_joint3d;

static void spring_joint_finalizer(void *obj) {
    (void)obj;
}

/// @brief Create a spring joint that applies Hooke's law forces between two bodies.
/// @details Unlike the distance joint (hard constraint), the spring joint applies
///          continuous forces: F = -k*(dist - rest) + damping. This produces
///          bouncy, elastic behavior. Damping reduces oscillation over time.
/// @param body_a      First body handle.
/// @param body_b      Second body handle.
/// @param rest_length Natural length at which the spring exerts zero force.
/// @param stiffness   Spring constant k (higher = stiffer, less stretch).
/// @param damping     Velocity damping coefficient (higher = less oscillation).
/// @return Opaque joint handle, or NULL on failure.
void *rt_spring_joint3d_new(void *body_a, void *body_b, double rest_length,
                            double stiffness, double damping) {
    if (!body_a || !body_b) {
        rt_trap("SpringJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_spring_joint3d));
    if (!j) {
        rt_trap("SpringJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_view *)body_a;
    j->body_b = (rt_body3d_view *)body_b;
    j->rest_length = rest_length;
    j->stiffness = stiffness;
    j->damping = damping;
    rt_obj_set_finalizer(j, spring_joint_finalizer);
    return j;
}

/// @brief Get the spring constant k.
double rt_spring_joint3d_get_stiffness(void *joint) {
    return joint ? ((rt_spring_joint3d *)joint)->stiffness : 0;
}

/// @brief Set the spring constant k at runtime.
void rt_spring_joint3d_set_stiffness(void *joint, double stiffness) {
    if (joint)
        ((rt_spring_joint3d *)joint)->stiffness = stiffness;
}

/// @brief Get the velocity damping coefficient.
double rt_spring_joint3d_get_damping(void *joint) {
    return joint ? ((rt_spring_joint3d *)joint)->damping : 0;
}

/// @brief Set the velocity damping coefficient at runtime.
void rt_spring_joint3d_set_damping(void *joint, double damping) {
    if (joint)
        ((rt_spring_joint3d *)joint)->damping = damping;
}

/// @brief Get the spring's natural (zero-force) length.
double rt_spring_joint3d_get_rest_length(void *joint) {
    return joint ? ((rt_spring_joint3d *)joint)->rest_length : 0;
}

static void solve_spring(rt_spring_joint3d *j, double dt) {
    if (!j || !j->body_a || !j->body_b || dt <= 0)
        return;

    double dx = j->body_b->position[0] - j->body_a->position[0];
    double dy = j->body_b->position[1] - j->body_a->position[1];
    double dz = j->body_b->position[2] - j->body_a->position[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (dist < 1e-12)
        return;

    double inv_dist = 1.0 / dist;
    double nx = dx * inv_dist;
    double ny = dy * inv_dist;
    double nz = dz * inv_dist;

    /* Hooke's law: F = -k * (dist - rest) */
    double displacement = dist - j->rest_length;
    double spring_force = -j->stiffness * displacement;

    /* Damping: F_damp = -c * relative_velocity_along_axis */
    double rvx = j->body_b->velocity[0] - j->body_a->velocity[0];
    double rvy = j->body_b->velocity[1] - j->body_a->velocity[1];
    double rvz = j->body_b->velocity[2] - j->body_a->velocity[2];
    double rv_along = rvx * nx + rvy * ny + rvz * nz;
    double damp_force = -j->damping * rv_along;

    double total_force = spring_force + damp_force;

    /* Apply force to both bodies (equal and opposite) */
    double fx = total_force * nx;
    double fy = total_force * ny;
    double fz = total_force * nz;

    /* F = ma → a = F * inv_mass, v += a * dt */
    j->body_a->velocity[0] -= fx * j->body_a->inv_mass * dt;
    j->body_a->velocity[1] -= fy * j->body_a->inv_mass * dt;
    j->body_a->velocity[2] -= fz * j->body_a->inv_mass * dt;
    j->body_b->velocity[0] += fx * j->body_b->inv_mass * dt;
    j->body_b->velocity[1] += fy * j->body_b->inv_mass * dt;
    j->body_b->velocity[2] += fz * j->body_b->inv_mass * dt;
}

/*==========================================================================
 * Generic joint solver dispatch
 *=========================================================================*/

/// @brief Dispatch the constraint solver for a joint based on its type.
/// @details Called by the physics world during each step. Dispatches to
///          solve_distance (hard positional constraint) or solve_spring
///          (Hooke's law force application) based on joint_type.
/// @param joint      Opaque joint handle (distance or spring).
/// @param joint_type RT_JOINT_DISTANCE or RT_JOINT_SPRING.
/// @param dt         Physics timestep in seconds.
void rt_joint3d_solve(void *joint, int32_t joint_type, double dt) {
    if (!joint)
        return;
    if (joint_type == RT_JOINT_DISTANCE)
        solve_distance((rt_distance_joint3d *)joint, dt);
    else if (joint_type == RT_JOINT_SPRING)
        solve_spring((rt_spring_joint3d *)joint, dt);
}

#endif /* VIPER_ENABLE_GRAPHICS */
