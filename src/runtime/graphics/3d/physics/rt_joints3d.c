//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_joints3d.c
// Purpose: 3D physics joint constraints. Distance joints maintain fixed
//   separation via positional correction. Spring joints apply Hooke's law
//   forces with damping. Both operate on Body3D position/velocity directly.
//
// Key invariants:
//   - Distance joint: positional correction pushes bodies to target distance.
//   - Spring joint: force = -stiffness * (dist - rest) - damping * rel_vel.
//   - Both handle zero-distance edge case (coincident centers).
//   - Joints with NULL or non-finite body references are no-ops.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//   - Body references are retained while the joint exists and released by the
//     joint finalizer, so worlds can keep solving after caller locals release.
//
// Links: rt_joints3d.h, rt_physics3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_joints3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_mat4.h"
#include "rt_physics3d.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
#include "rt_trap.h"

#define RT_JOINT3D_MAX_PARAM 1.0e9
#define RT_JOINT3D_MAX_FORCE 1.0e9

/* Body pose/velocity access uses the shared rt_body3d_kinematics view from
 * rt_physics3d.h, whose layout is asserted to match the private rt_body3d there. */

typedef struct {
    double m[16];
} joint3d_mat4_view;

static int joint3d_body_is_finite(const rt_body3d_kinematics *body);

/// @brief Clamp a joint parameter to `[0, RT_JOINT3D_MAX_PARAM]`; non-finite maps to 0.
static double joint3d_sanitize_nonnegative(double value) {
    if (!isfinite(value) || value < 0.0)
        return 0.0;
    return value > RT_JOINT3D_MAX_PARAM ? RT_JOINT3D_MAX_PARAM : value;
}

/// @brief Clamp a force/impulse magnitude to `±RT_JOINT3D_MAX_FORCE`; non-finite maps to 0.
static double joint3d_clamp_force(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > RT_JOINT3D_MAX_FORCE)
        return RT_JOINT3D_MAX_FORCE;
    if (value < -RT_JOINT3D_MAX_FORCE)
        return -RT_JOINT3D_MAX_FORCE;
    return value;
}

static int joint3d_vec3_all_finite(const double *v) {
    return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static void joint3d_vec3_set(double *dst, double x, double y, double z) {
    if (!dst)
        return;
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

static void joint3d_vec3_sub(const double *a, const double *b, double *out) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static double joint3d_vec3_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double joint3d_vec3_len(const double *v) {
    return sqrt(joint3d_vec3_dot(v, v));
}

static int joint3d_vec3_normalize(double *v) {
    double len;
    if (!joint3d_vec3_all_finite(v))
        return 0;
    len = joint3d_vec3_len(v);
    if (!isfinite(len) || len < 1e-12)
        return 0;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return 1;
}

static int joint3d_read_vec3(void *obj, double *out) {
    if (!out || !rt_g3d_is_vec3(obj))
        return 0;
    out[0] = rt_vec3_x(obj);
    out[1] = rt_vec3_y(obj);
    out[2] = rt_vec3_z(obj);
    return joint3d_vec3_all_finite(out);
}

static void joint3d_canonicalize_limits(double *min_v, double *max_v) {
    if (!min_v || !max_v)
        return;
    for (int i = 0; i < 3; i++) {
        if (min_v[i] > max_v[i]) {
            double tmp = min_v[i];
            min_v[i] = max_v[i];
            max_v[i] = tmp;
        }
    }
}

static joint3d_mat4_view *joint3d_mat4_checked(void *obj) {
    if (!obj || !rt_heap_is_payload(obj) || rt_obj_class_id(obj) != RT_MAT4_CLASS_ID)
        return NULL;
    return (joint3d_mat4_view *)obj;
}

static int joint3d_read_mat4_translation(void *obj, double *out) {
    joint3d_mat4_view *m = joint3d_mat4_checked(obj);
    if (!m || !out)
        return 0;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(m->m[i]))
            return 0;
    }
    out[0] = m->m[3];
    out[1] = m->m[7];
    out[2] = m->m[11];
    return 1;
}

static void joint3d_quat_mul(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void joint3d_quat_conjugate(const double *q, double *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

static void joint3d_quat_rotate_vec3(const double *q, const double *v, double *out) {
    double qv[4] = {v[0], v[1], v[2], 0.0};
    double q_conj[4];
    double tmp[4];
    double rotated[4];
    joint3d_quat_conjugate(q, q_conj);
    joint3d_quat_mul(q, qv, tmp);
    joint3d_quat_mul(tmp, q_conj, rotated);
    out[0] = rotated[0];
    out[1] = rotated[1];
    out[2] = rotated[2];
}

static void joint3d_world_anchor(const rt_body3d_kinematics *body,
                                 const double *local_anchor,
                                 double *out) {
    double rotated[3];
    if (!out)
        return;
    if (!body || !local_anchor) {
        joint3d_vec3_set(out, 0.0, 0.0, 0.0);
        return;
    }
    joint3d_quat_rotate_vec3(body->orientation, local_anchor, rotated);
    out[0] = body->position[0] + rotated[0];
    out[1] = body->position[1] + rotated[1];
    out[2] = body->position[2] + rotated[2];
}

static void joint3d_local_from_world(const rt_body3d_kinematics *body,
                                     const double *world_point,
                                     double *out) {
    double delta[3];
    double inv_rotation[4];
    if (!body || !world_point || !out) {
        if (out)
            joint3d_vec3_set(out, 0.0, 0.0, 0.0);
        return;
    }
    joint3d_vec3_sub(world_point, body->position, delta);
    joint3d_quat_conjugate(body->orientation, inv_rotation);
    joint3d_quat_rotate_vec3(inv_rotation, delta, out);
}

static void joint3d_world_axis_from_local(const rt_body3d_kinematics *body,
                                          const double *local_axis,
                                          double *out) {
    if (!body || !local_axis || !out) {
        if (out)
            joint3d_vec3_set(out, 0.0, 1.0, 0.0);
        return;
    }
    joint3d_quat_rotate_vec3(body->orientation, local_axis, out);
    if (!joint3d_vec3_normalize(out))
        joint3d_vec3_set(out, 0.0, 1.0, 0.0);
}

static void joint3d_correct_anchor_pair(rt_body3d_kinematics *body_a,
                                        rt_body3d_kinematics *body_b,
                                        const double *local_anchor_a,
                                        const double *local_anchor_b,
                                        double stiffness) {
    double anchor_a[3];
    double anchor_b[3];
    double delta[3];
    double inv_sum;
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b))
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    joint3d_world_anchor(body_a, local_anchor_a, anchor_a);
    joint3d_world_anchor(body_b, local_anchor_b, anchor_b);
    joint3d_vec3_sub(anchor_b, anchor_a, delta);
    if (!joint3d_vec3_all_finite(delta))
        return;
    if (!isfinite(stiffness) || stiffness <= 0.0)
        stiffness = 1.0;
    if (stiffness > 1.0)
        stiffness = 1.0;
    double scale = stiffness / inv_sum;
    for (int i = 0; i < 3; i++) {
        double correction = delta[i] * scale;
        body_a->position[i] += correction * body_a->inv_mass;
        body_b->position[i] -= correction * body_b->inv_mass;
    }
}

static void joint3d_correct_anchor_pair_limited(rt_body3d_kinematics *body_a,
                                                rt_body3d_kinematics *body_b,
                                                const double *local_anchor_a,
                                                const double *local_anchor_b,
                                                const double *linear_min,
                                                const double *linear_max) {
    double anchor_a[3];
    double anchor_b[3];
    double delta[3];
    double violation[3] = {0.0, 0.0, 0.0};
    double inv_sum;
    int has_violation = 0;
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b) || !linear_min ||
        !linear_max)
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    joint3d_world_anchor(body_a, local_anchor_a, anchor_a);
    joint3d_world_anchor(body_b, local_anchor_b, anchor_b);
    joint3d_vec3_sub(anchor_b, anchor_a, delta);
    if (!joint3d_vec3_all_finite(delta))
        return;
    for (int i = 0; i < 3; i++) {
        if (delta[i] < linear_min[i]) {
            violation[i] = delta[i] - linear_min[i];
            has_violation = 1;
        } else if (delta[i] > linear_max[i]) {
            violation[i] = delta[i] - linear_max[i];
            has_violation = 1;
        }
    }
    if (!has_violation)
        return;
    for (int i = 0; i < 3; i++) {
        double correction = violation[i] / inv_sum;
        body_a->position[i] += correction * body_a->inv_mass;
        body_b->position[i] -= correction * body_b->inv_mass;
    }
}

static void joint3d_remove_relative_linear_velocity(rt_body3d_kinematics *body_a,
                                                    rt_body3d_kinematics *body_b,
                                                    double amount) {
    double inv_sum;
    double rel[3];
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b))
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    if (!isfinite(amount) || amount <= 0.0)
        amount = 1.0;
    if (amount > 1.0)
        amount = 1.0;
    joint3d_vec3_sub(body_b->velocity, body_a->velocity, rel);
    if (!joint3d_vec3_all_finite(rel))
        return;
    for (int i = 0; i < 3; i++) {
        double correction = rel[i] * amount / inv_sum;
        body_a->velocity[i] += correction * body_a->inv_mass;
        body_b->velocity[i] -= correction * body_b->inv_mass;
    }
}

static void joint3d_remove_relative_linear_velocity_locked_axes(rt_body3d_kinematics *body_a,
                                                                rt_body3d_kinematics *body_b,
                                                                const double *linear_min,
                                                                const double *linear_max) {
    double inv_sum;
    double rel[3];
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b) || !linear_min ||
        !linear_max)
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    joint3d_vec3_sub(body_b->velocity, body_a->velocity, rel);
    if (!joint3d_vec3_all_finite(rel))
        return;
    for (int i = 0; i < 3; i++) {
        if (fabs(linear_max[i] - linear_min[i]) > 1e-12)
            continue;
        double correction = rel[i] / inv_sum;
        body_a->velocity[i] += correction * body_a->inv_mass;
        body_b->velocity[i] -= correction * body_b->inv_mass;
    }
}

static void joint3d_project_relative_angular_velocity_limits(rt_body3d_kinematics *body_a,
                                                             rt_body3d_kinematics *body_b,
                                                             const double *angular_min,
                                                             const double *angular_max) {
    double rel[3];
    double violation[3] = {0.0, 0.0, 0.0};
    double inv_sum;
    int has_violation = 0;
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b) || !angular_min ||
        !angular_max)
        return;
    if (!joint3d_vec3_all_finite(body_a->angular_velocity) ||
        !joint3d_vec3_all_finite(body_b->angular_velocity))
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    joint3d_vec3_sub(body_b->angular_velocity, body_a->angular_velocity, rel);
    if (!joint3d_vec3_all_finite(rel))
        return;
    for (int i = 0; i < 3; i++) {
        if (rel[i] < angular_min[i]) {
            violation[i] = rel[i] - angular_min[i];
            has_violation = 1;
        } else if (rel[i] > angular_max[i]) {
            violation[i] = rel[i] - angular_max[i];
            has_violation = 1;
        }
    }
    if (!has_violation)
        return;
    for (int i = 0; i < 3; i++) {
        double correction = violation[i] / inv_sum;
        body_a->angular_velocity[i] += correction * body_a->inv_mass;
        body_b->angular_velocity[i] -= correction * body_b->inv_mass;
    }
}

static void joint3d_remove_relative_angular_velocity(rt_body3d_kinematics *body_a,
                                                     rt_body3d_kinematics *body_b,
                                                     const double *allowed_axis) {
    double rel[3];
    double remove[3];
    double inv_sum;
    if (!joint3d_body_is_finite(body_a) || !joint3d_body_is_finite(body_b))
        return;
    if (!joint3d_vec3_all_finite(body_a->angular_velocity) ||
        !joint3d_vec3_all_finite(body_b->angular_velocity))
        return;
    inv_sum = body_a->inv_mass + body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    joint3d_vec3_sub(body_b->angular_velocity, body_a->angular_velocity, rel);
    remove[0] = rel[0];
    remove[1] = rel[1];
    remove[2] = rel[2];
    if (allowed_axis && joint3d_vec3_all_finite(allowed_axis)) {
        double axial = joint3d_vec3_dot(rel, allowed_axis);
        remove[0] -= allowed_axis[0] * axial;
        remove[1] -= allowed_axis[1] * axial;
        remove[2] -= allowed_axis[2] * axial;
    }
    if (!joint3d_vec3_all_finite(remove))
        return;
    for (int i = 0; i < 3; i++) {
        double correction = remove[i] / inv_sum;
        body_a->angular_velocity[i] += correction * body_a->inv_mass;
        body_b->angular_velocity[i] -= correction * body_b->inv_mass;
    }
}

/// @brief True if a body view is solvable: finite, non-negative inverse mass and
///        finite position/velocity. Guards the joint solver against NaN bodies.
static int joint3d_body_is_finite(const rt_body3d_kinematics *body) {
    if (!body || !isfinite(body->inv_mass) || body->inv_mass < 0.0)
        return 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(body->position[i]) || !isfinite(body->velocity[i]) ||
            !isfinite(body->orientation[i]) || !isfinite(body->angular_velocity[i]))
            return 0;
    }
    if (!isfinite(body->orientation[3]))
        return 0;
    return 1;
}

/// @brief Release the GC reference held in `*slot` (if any) and null the slot. Idempotent.
static void joint3d_release_body_ref(rt_body3d_kinematics **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/*==========================================================================
 * Distance Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_kinematics *body_a;
    rt_body3d_kinematics *body_b;
    double target_distance;
} rt_distance_joint3d;

/// @brief GC finalizer — release the bodies retained by this distance joint.
static void distance_joint_finalizer(void *obj) {
    rt_distance_joint3d *j = (rt_distance_joint3d *)obj;
    if (!j)
        return;
    joint3d_release_body_ref(&j->body_a);
    joint3d_release_body_ref(&j->body_b);
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
    if (!rt_g3d_has_class(body_a, RT_G3D_BODY3D_CLASS_ID) ||
        !rt_g3d_has_class(body_b, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("DistanceJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    rt_distance_joint3d *j = (rt_distance_joint3d *)rt_obj_new_i64(
        RT_G3D_DISTANCEJOINT3D_CLASS_ID, (int64_t)sizeof(rt_distance_joint3d));
    if (!j) {
        rt_trap("DistanceJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_kinematics *)body_a;
    j->body_b = (rt_body3d_kinematics *)body_b;
    rt_obj_retain_maybe(body_a);
    rt_obj_retain_maybe(body_b);
    j->target_distance = joint3d_sanitize_nonnegative(distance);
    rt_obj_set_finalizer(j, distance_joint_finalizer);
    return j;
}

/// @brief Get the target distance of a distance joint.
double rt_distance_joint3d_get_distance(void *joint) {
    rt_distance_joint3d *j =
        (rt_distance_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_DISTANCEJOINT3D_CLASS_ID);
    return j ? j->target_distance : 0;
}

/// @brief Change the target distance of a distance joint at runtime.
void rt_distance_joint3d_set_distance(void *joint, double distance) {
    rt_distance_joint3d *j =
        (rt_distance_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_DISTANCEJOINT3D_CLASS_ID);
    if (j)
        j->target_distance = joint3d_sanitize_nonnegative(distance);
}

/// @brief Enforce a rigid distance constraint between two bodies for one step.
/// @details Implements a two-pass hard constraint: a position correction that
///   teleports the bodies so their separation matches `target_distance`, then
///   a velocity correction that zeros out the relative velocity along the
///   constraint axis so the bodies don't immediately separate again next
///   frame. Both corrections are mass-weighted by inverse mass — infinite-mass
///   (static) bodies contribute zero to the split, so a dynamic body hitched
///   to a static one moves the full correction distance itself. `dt` is
///   currently unused because this is a position-based constraint; leaving
///   it in the signature keeps parity with `solve_spring` and reserves it
///   for future XPBD-style step-size-aware variants.
///   Early-outs: coincident bodies (no defined direction) and two static
///   bodies (both inv_mass = 0) both skip the step cleanly.
static void solve_distance(rt_distance_joint3d *j, double dt) {
    if (!j || !joint3d_body_is_finite(j->body_a) || !joint3d_body_is_finite(j->body_b))
        return;
    (void)dt;

    double dx = j->body_b->position[0] - j->body_a->position[0];
    double dy = j->body_b->position[1] - j->body_a->position[1];
    double dz = j->body_b->position[2] - j->body_a->position[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (!isfinite(dist) || dist < 1e-12)
        return; /* coincident — can't determine direction */

    double error = dist - j->target_distance;
    double inv_dist = 1.0 / dist;
    double nx = dx * inv_dist;
    double ny = dy * inv_dist;
    double nz = dz * inv_dist;

    double inv_sum = j->body_a->inv_mass + j->body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return; /* both static */

    /* Positional correction: move each body proportional to inverse mass */
    double correction = error / inv_sum;
    if (!isfinite(correction))
        return;

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
    if (!isfinite(rv_along))
        return;

    double jn = rv_along / inv_sum;
    if (!isfinite(jn))
        return;
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
    rt_body3d_kinematics *body_a;
    rt_body3d_kinematics *body_b;
    double rest_length;
    double stiffness;
    double damping;
} rt_spring_joint3d;

/// @brief GC finalizer — release the bodies retained by this spring joint.
static void spring_joint_finalizer(void *obj) {
    rt_spring_joint3d *j = (rt_spring_joint3d *)obj;
    if (!j)
        return;
    joint3d_release_body_ref(&j->body_a);
    joint3d_release_body_ref(&j->body_b);
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
void *rt_spring_joint3d_new(
    void *body_a, void *body_b, double rest_length, double stiffness, double damping) {
    if (!rt_g3d_has_class(body_a, RT_G3D_BODY3D_CLASS_ID) ||
        !rt_g3d_has_class(body_b, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("SpringJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    rt_spring_joint3d *j = (rt_spring_joint3d *)rt_obj_new_i64(RT_G3D_SPRINGJOINT3D_CLASS_ID,
                                                               (int64_t)sizeof(rt_spring_joint3d));
    if (!j) {
        rt_trap("SpringJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_kinematics *)body_a;
    j->body_b = (rt_body3d_kinematics *)body_b;
    rt_obj_retain_maybe(body_a);
    rt_obj_retain_maybe(body_b);
    j->rest_length = joint3d_sanitize_nonnegative(rest_length);
    j->stiffness = joint3d_sanitize_nonnegative(stiffness);
    j->damping = joint3d_sanitize_nonnegative(damping);
    rt_obj_set_finalizer(j, spring_joint_finalizer);
    return j;
}

/// @brief Get the spring constant k.
double rt_spring_joint3d_get_stiffness(void *joint) {
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    return j ? j->stiffness : 0;
}

/// @brief Set the spring constant k at runtime.
void rt_spring_joint3d_set_stiffness(void *joint, double stiffness) {
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    if (j)
        j->stiffness = joint3d_sanitize_nonnegative(stiffness);
}

/// @brief Get the velocity damping coefficient.
double rt_spring_joint3d_get_damping(void *joint) {
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    return j ? j->damping : 0;
}

/// @brief Set the velocity damping coefficient at runtime.
void rt_spring_joint3d_set_damping(void *joint, double damping) {
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    if (j)
        j->damping = joint3d_sanitize_nonnegative(damping);
}

/// @brief Get the spring's natural (zero-force) length.
double rt_spring_joint3d_get_rest_length(void *joint) {
    rt_spring_joint3d *j =
        (rt_spring_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    return j ? j->rest_length : 0;
}

/// @brief Integrate spring + damping forces into both bodies' velocities.
/// @details Applies Hooke's law `F = -k * (dist - rest)` along the axis from
///   A to B, plus a velocity-damping term `-c * (rel_vel · axis)` to bleed
///   oscillation. The two forces are added into equal-and-opposite impulses
///   of magnitude `F * dt` scaled by each body's inverse mass, preserving
///   momentum between the pair. Unlike `solve_distance`, this is a soft
///   constraint: it never corrects positions directly, so over-stiff springs
///   can oscillate or explode at the current fixed step — tune `stiffness`
///   below the stability ceiling for the caller's step frequency.
static void solve_spring(rt_spring_joint3d *j, double dt) {
    if (!j || !joint3d_body_is_finite(j->body_a) || !joint3d_body_is_finite(j->body_b) ||
        !isfinite(dt) || dt <= 0.0)
        return;

    double dx = j->body_b->position[0] - j->body_a->position[0];
    double dy = j->body_b->position[1] - j->body_a->position[1];
    double dz = j->body_b->position[2] - j->body_a->position[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (!isfinite(dist) || dist < 1e-12)
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
    if (!isfinite(rv_along))
        return;
    double damp_force = -j->damping * rv_along;

    double total_force = joint3d_clamp_force(spring_force + damp_force);

    /* Apply force to both bodies (equal and opposite) */
    double fx = joint3d_clamp_force(total_force * nx);
    double fy = joint3d_clamp_force(total_force * ny);
    double fz = joint3d_clamp_force(total_force * nz);

    /* F = ma → a = F * inv_mass, v += a * dt */
    j->body_a->velocity[0] -= fx * j->body_a->inv_mass * dt;
    j->body_a->velocity[1] -= fy * j->body_a->inv_mass * dt;
    j->body_a->velocity[2] -= fz * j->body_a->inv_mass * dt;
    j->body_b->velocity[0] += fx * j->body_b->inv_mass * dt;
    j->body_b->velocity[1] += fy * j->body_b->inv_mass * dt;
    j->body_b->velocity[2] += fz * j->body_b->inv_mass * dt;
}

/*==========================================================================
 * Hinge Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_kinematics *body_a;
    rt_body3d_kinematics *body_b;
    double local_anchor_a[3];
    double local_anchor_b[3];
    double local_axis_a[3];
    double ref_perp_a[3]; /* a perpendicular-to-axis reference, in each body's local frame */
    double ref_perp_b[3]; /* coincident at creation, so the hinge angle starts at 0 */
    int8_t motor_enabled;
    double motor_target_velocity;
    double motor_max_impulse;
    int8_t has_limits;
    double angle_min;
    double angle_max;
} rt_hinge_joint3d;

static void hinge_joint_finalizer(void *obj) {
    rt_hinge_joint3d *j = (rt_hinge_joint3d *)obj;
    if (!j)
        return;
    joint3d_release_body_ref(&j->body_a);
    joint3d_release_body_ref(&j->body_b);
}

void *rt_hinge_joint3d_new(void *body_a, void *body_b, void *anchor, void *axis) {
    double anchor_world[3];
    double axis_world[3];
    double inv_a_rotation[4];
    if (!rt_g3d_has_class(body_a, RT_G3D_BODY3D_CLASS_ID) ||
        !rt_g3d_has_class(body_b, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("HingeJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    if (!joint3d_read_vec3(anchor, anchor_world)) {
        rt_trap("HingeJoint3D.New: anchor must be a finite Vec3");
        return NULL;
    }
    if (!joint3d_read_vec3(axis, axis_world) || !joint3d_vec3_normalize(axis_world)) {
        rt_trap("HingeJoint3D.New: axis must be a non-zero finite Vec3");
        return NULL;
    }

    rt_hinge_joint3d *j = (rt_hinge_joint3d *)rt_obj_new_i64(
        RT_G3D_HINGEJOINT3D_CLASS_ID, (int64_t)sizeof(rt_hinge_joint3d));
    if (!j) {
        rt_trap("HingeJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_kinematics *)body_a;
    j->body_b = (rt_body3d_kinematics *)body_b;
    joint3d_local_from_world(j->body_a, anchor_world, j->local_anchor_a);
    joint3d_local_from_world(j->body_b, anchor_world, j->local_anchor_b);
    joint3d_quat_conjugate(j->body_a->orientation, inv_a_rotation);
    joint3d_quat_rotate_vec3(inv_a_rotation, axis_world, j->local_axis_a);
    if (!joint3d_vec3_normalize(j->local_axis_a))
        joint3d_vec3_set(j->local_axis_a, 0.0, 1.0, 0.0);
    /* Store a perpendicular-to-axis reference in each body's local frame; both
     * map to the same world direction now, so the hinge angle starts at 0. */
    {
        double seed[3];
        double perp_world[3];
        double inv_b_rotation[4];
        double d;
        if (fabs(axis_world[0]) < 0.9)
            joint3d_vec3_set(seed, 1.0, 0.0, 0.0);
        else
            joint3d_vec3_set(seed, 0.0, 1.0, 0.0);
        d = joint3d_vec3_dot(seed, axis_world);
        perp_world[0] = seed[0] - axis_world[0] * d;
        perp_world[1] = seed[1] - axis_world[1] * d;
        perp_world[2] = seed[2] - axis_world[2] * d;
        if (!joint3d_vec3_normalize(perp_world))
            joint3d_vec3_set(perp_world, 1.0, 0.0, 0.0);
        joint3d_quat_rotate_vec3(inv_a_rotation, perp_world, j->ref_perp_a);
        joint3d_quat_conjugate(j->body_b->orientation, inv_b_rotation);
        joint3d_quat_rotate_vec3(inv_b_rotation, perp_world, j->ref_perp_b);
    }
    j->motor_enabled = 0;
    j->motor_target_velocity = 0.0;
    j->motor_max_impulse = 0.0;
    j->has_limits = 0;
    j->angle_min = 0.0;
    j->angle_max = 0.0;
    rt_obj_retain_maybe(body_a);
    rt_obj_retain_maybe(body_b);
    rt_obj_set_finalizer(j, hinge_joint_finalizer);
    return j;
}

/// @brief Current signed hinge angle (radians) between the bodies' stored
///   perpendicular references, projected onto the world hinge axis.
static double hinge_joint_current_angle(const rt_hinge_joint3d *j) {
    double axis_world[3];
    double ra[3];
    double rb[3];
    double cross[3];
    double da;
    double db;
    joint3d_world_axis_from_local(j->body_a, j->local_axis_a, axis_world);
    joint3d_quat_rotate_vec3(j->body_a->orientation, j->ref_perp_a, ra);
    joint3d_quat_rotate_vec3(j->body_b->orientation, j->ref_perp_b, rb);
    da = joint3d_vec3_dot(ra, axis_world);
    ra[0] -= axis_world[0] * da;
    ra[1] -= axis_world[1] * da;
    ra[2] -= axis_world[2] * da;
    db = joint3d_vec3_dot(rb, axis_world);
    rb[0] -= axis_world[0] * db;
    rb[1] -= axis_world[1] * db;
    rb[2] -= axis_world[2] * db;
    if (!joint3d_vec3_normalize(ra) || !joint3d_vec3_normalize(rb))
        return 0.0;
    cross[0] = ra[1] * rb[2] - ra[2] * rb[1];
    cross[1] = ra[2] * rb[0] - ra[0] * rb[2];
    cross[2] = ra[0] * rb[1] - ra[1] * rb[0];
    return atan2(joint3d_vec3_dot(cross, axis_world), joint3d_vec3_dot(ra, rb));
}

/// @brief When the hinge angle reaches a configured limit, remove the axial
///   relative angular velocity that would carry it further past the bound, so
///   the joint stops at the limit (overrides the motor at the stop).
static void hinge_joint_apply_limits(rt_hinge_joint3d *j, const double *axis_world) {
    rt_body3d_kinematics *a = j->body_a;
    rt_body3d_kinematics *b = j->body_b;
    double angle;
    double rel[3];
    double w_rel;
    double inv_sum;
    double correction;
    if (!joint3d_body_is_finite(a) || !joint3d_body_is_finite(b))
        return;
    if (!joint3d_vec3_all_finite(a->angular_velocity) ||
        !joint3d_vec3_all_finite(b->angular_velocity))
        return;
    angle = hinge_joint_current_angle(j);
    joint3d_vec3_sub(b->angular_velocity, a->angular_velocity, rel);
    w_rel = joint3d_vec3_dot(rel, axis_world);
    if (!((angle >= j->angle_max && w_rel > 0.0) || (angle <= j->angle_min && w_rel < 0.0)))
        return;
    inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum <= 1e-9)
        return;
    correction = w_rel / inv_sum; /* drive axial relative velocity to 0 */
    for (int i = 0; i < 3; i++) {
        a->angular_velocity[i] += axis_world[i] * correction * a->inv_mass;
        b->angular_velocity[i] -= axis_world[i] * correction * b->inv_mass;
    }
}

/// @brief Drive the relative angular velocity about the hinge axis toward the
///   motor target velocity, with the per-step change bounded by
///   motor_max_impulse (the motor's strength). The inv-mass terms cancel so an
///   unbounded motor reaches the target in one step; the clamp models a finite
///   motor. Runs after the perpendicular-twist constraint, which leaves the
///   axial component free for the motor to drive.
static void hinge_joint_apply_motor(rt_hinge_joint3d *j, const double *axis_world) {
    rt_body3d_kinematics *a = j->body_a;
    rt_body3d_kinematics *b = j->body_b;
    double rel[3];
    double w_rel;
    double violation;
    double inv_sum;
    double correction;
    if (!joint3d_body_is_finite(a) || !joint3d_body_is_finite(b))
        return;
    if (!joint3d_vec3_all_finite(a->angular_velocity) ||
        !joint3d_vec3_all_finite(b->angular_velocity))
        return;
    joint3d_vec3_sub(b->angular_velocity, a->angular_velocity, rel);
    w_rel = joint3d_vec3_dot(rel, axis_world);
    violation = w_rel - j->motor_target_velocity;
    inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum <= 1e-9)
        return;
    correction = violation / inv_sum;
    if (correction > j->motor_max_impulse)
        correction = j->motor_max_impulse;
    else if (correction < -j->motor_max_impulse)
        correction = -j->motor_max_impulse;
    for (int i = 0; i < 3; i++) {
        a->angular_velocity[i] += axis_world[i] * correction * a->inv_mass;
        b->angular_velocity[i] -= axis_world[i] * correction * b->inv_mass;
    }
}

static void solve_hinge(rt_hinge_joint3d *j, double dt) {
    double axis_world[3];
    (void)dt;
    if (!j)
        return;
    joint3d_correct_anchor_pair(j->body_a, j->body_b, j->local_anchor_a, j->local_anchor_b, 1.0);
    joint3d_remove_relative_linear_velocity(j->body_a, j->body_b, 1.0);
    joint3d_world_axis_from_local(j->body_a, j->local_axis_a, axis_world);
    joint3d_remove_relative_angular_velocity(j->body_a, j->body_b, axis_world);
    if (j->motor_enabled)
        hinge_joint_apply_motor(j, axis_world);
    if (j->has_limits)
        hinge_joint_apply_limits(j, axis_world);
}

/// @brief Current signed hinge angle (radians) about the axis, measured between
///   the two bodies' stored perpendicular references. Reads 0 at creation; grows
///   right-handed about the hinge axis as body B rotates relative to body A.
double rt_hinge_joint3d_get_angle(void *joint) {
    if (!rt_g3d_has_class(joint, RT_G3D_HINGEJOINT3D_CLASS_ID))
        return 0.0;
    return hinge_joint_current_angle((const rt_hinge_joint3d *)joint);
}

/// @brief Constrain the hinge to [min, max] radians (swapped if reversed); the
///   solver stops rotation at the bounds. Non-finite limits disable the limit.
void rt_hinge_joint3d_set_limits(void *joint, double min_angle, double max_angle) {
    rt_hinge_joint3d *j;
    if (!rt_g3d_has_class(joint, RT_G3D_HINGEJOINT3D_CLASS_ID))
        return;
    j = (rt_hinge_joint3d *)joint;
    if (!isfinite(min_angle) || !isfinite(max_angle)) {
        j->has_limits = 0;
        return;
    }
    if (min_angle > max_angle) {
        double tmp = min_angle;
        min_angle = max_angle;
        max_angle = tmp;
    }
    j->angle_min = min_angle;
    j->angle_max = max_angle;
    j->has_limits = 1;
}

/// @brief Enable/configure a hinge motor that drives rotation about the hinge
///   axis toward @p target_velocity (rad/s), bounded by @p max_impulse strength.
void rt_hinge_joint3d_set_motor(void *joint,
                                int8_t enabled,
                                double target_velocity,
                                double max_impulse) {
    rt_hinge_joint3d *j;
    if (!rt_g3d_has_class(joint, RT_G3D_HINGEJOINT3D_CLASS_ID))
        return;
    j = (rt_hinge_joint3d *)joint;
    j->motor_enabled = enabled ? 1 : 0;
    j->motor_target_velocity = isfinite(target_velocity) ? target_velocity : 0.0;
    j->motor_max_impulse = (isfinite(max_impulse) && max_impulse > 0.0) ? max_impulse : 0.0;
}

/*==========================================================================
 * Rope Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_kinematics *body_a;
    rt_body3d_kinematics *body_b;
    double max_length;
} rt_rope_joint3d;

static void rope_joint_finalizer(void *obj) {
    rt_rope_joint3d *j = (rt_rope_joint3d *)obj;
    if (!j)
        return;
    joint3d_release_body_ref(&j->body_a);
    joint3d_release_body_ref(&j->body_b);
}

void *rt_rope_joint3d_new(void *body_a, void *body_b, double max_length) {
    if (!rt_g3d_has_class(body_a, RT_G3D_BODY3D_CLASS_ID) ||
        !rt_g3d_has_class(body_b, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("RopeJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    rt_rope_joint3d *j = (rt_rope_joint3d *)rt_obj_new_i64(
        RT_G3D_ROPEJOINT3D_CLASS_ID, (int64_t)sizeof(rt_rope_joint3d));
    if (!j) {
        rt_trap("RopeJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_kinematics *)body_a;
    j->body_b = (rt_body3d_kinematics *)body_b;
    j->max_length = joint3d_sanitize_nonnegative(max_length);
    rt_obj_retain_maybe(body_a);
    rt_obj_retain_maybe(body_b);
    rt_obj_set_finalizer(j, rope_joint_finalizer);
    return j;
}

double rt_rope_joint3d_get_max_length(void *joint) {
    rt_rope_joint3d *j =
        (rt_rope_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_ROPEJOINT3D_CLASS_ID);
    return j ? j->max_length : 0.0;
}

void rt_rope_joint3d_set_max_length(void *joint, double max_length) {
    rt_rope_joint3d *j =
        (rt_rope_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_ROPEJOINT3D_CLASS_ID);
    if (j)
        j->max_length = joint3d_sanitize_nonnegative(max_length);
}

static void solve_rope(rt_rope_joint3d *j, double dt) {
    double delta[3];
    double dist;
    double inv_sum;
    double n[3];
    double rel_velocity[3];
    double rel_along;
    (void)dt;
    if (!j || !joint3d_body_is_finite(j->body_a) || !joint3d_body_is_finite(j->body_b))
        return;
    joint3d_vec3_sub(j->body_b->position, j->body_a->position, delta);
    if (!joint3d_vec3_all_finite(delta))
        return;
    dist = joint3d_vec3_len(delta);
    if (!isfinite(dist) || dist <= j->max_length || dist < 1e-12)
        return;
    inv_sum = j->body_a->inv_mass + j->body_b->inv_mass;
    if (!isfinite(inv_sum) || inv_sum < 1e-12)
        return;
    n[0] = delta[0] / dist;
    n[1] = delta[1] / dist;
    n[2] = delta[2] / dist;

    double error = dist - j->max_length;
    double correction = error / inv_sum;
    for (int i = 0; i < 3; i++) {
        j->body_a->position[i] += correction * j->body_a->inv_mass * n[i];
        j->body_b->position[i] -= correction * j->body_b->inv_mass * n[i];
    }

    joint3d_vec3_sub(j->body_b->velocity, j->body_a->velocity, rel_velocity);
    rel_along = joint3d_vec3_dot(rel_velocity, n);
    if (!isfinite(rel_along) || rel_along <= 0.0)
        return;
    double impulse = rel_along / inv_sum;
    for (int i = 0; i < 3; i++) {
        j->body_a->velocity[i] += impulse * j->body_a->inv_mass * n[i];
        j->body_b->velocity[i] -= impulse * j->body_b->inv_mass * n[i];
    }
}

/*==========================================================================
 * SixDof Joint
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_body3d_kinematics *body_a;
    rt_body3d_kinematics *body_b;
    double local_anchor_a[3];
    double local_anchor_b[3];
    double linear_min[3];
    double linear_max[3];
    double angular_min[3];
    double angular_max[3];
    int8_t linear_motor_enabled;
    double linear_motor_velocity[3];
    double linear_motor_max_impulse;
} rt_sixdof_joint3d;

static void sixdof_joint_finalizer(void *obj) {
    rt_sixdof_joint3d *j = (rt_sixdof_joint3d *)obj;
    if (!j)
        return;
    joint3d_release_body_ref(&j->body_a);
    joint3d_release_body_ref(&j->body_b);
}

void *rt_sixdof_joint3d_new(void *body_a, void *body_b, void *frame_a, void *frame_b) {
    double local_anchor_a[3];
    double local_anchor_b[3];
    if (!rt_g3d_has_class(body_a, RT_G3D_BODY3D_CLASS_ID) ||
        !rt_g3d_has_class(body_b, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("SixDofJoint3D.New: both bodies must be non-null");
        return NULL;
    }
    if (!joint3d_read_mat4_translation(frame_a, local_anchor_a) ||
        !joint3d_read_mat4_translation(frame_b, local_anchor_b)) {
        rt_trap("SixDofJoint3D.New: frames must be finite Mat4 values");
        return NULL;
    }
    rt_sixdof_joint3d *j = (rt_sixdof_joint3d *)rt_obj_new_i64(
        RT_G3D_SIXDOFJOINT3D_CLASS_ID, (int64_t)sizeof(rt_sixdof_joint3d));
    if (!j) {
        rt_trap("SixDofJoint3D.New: allocation failed");
        return NULL;
    }
    j->vptr = NULL;
    j->body_a = (rt_body3d_kinematics *)body_a;
    j->body_b = (rt_body3d_kinematics *)body_b;
    memcpy(j->local_anchor_a, local_anchor_a, sizeof(j->local_anchor_a));
    memcpy(j->local_anchor_b, local_anchor_b, sizeof(j->local_anchor_b));
    joint3d_vec3_set(j->linear_min, 0.0, 0.0, 0.0);
    joint3d_vec3_set(j->linear_max, 0.0, 0.0, 0.0);
    joint3d_vec3_set(j->angular_min, 0.0, 0.0, 0.0);
    joint3d_vec3_set(j->angular_max, 0.0, 0.0, 0.0);
    j->linear_motor_enabled = 0;
    joint3d_vec3_set(j->linear_motor_velocity, 0.0, 0.0, 0.0);
    j->linear_motor_max_impulse = 0.0;
    rt_obj_retain_maybe(body_a);
    rt_obj_retain_maybe(body_b);
    rt_obj_set_finalizer(j, sixdof_joint_finalizer);
    return j;
}

/// @brief Drive the relative linear velocity along each *unlocked* axis toward
///   the motor target (locked axes are held by the limit solver), bounded by the
///   motor's max-impulse strength. Powers sliders/pistons/elevators.
static void sixdof_joint_apply_linear_motor(rt_sixdof_joint3d *j) {
    rt_body3d_kinematics *a = j->body_a;
    rt_body3d_kinematics *b = j->body_b;
    double rel[3];
    double inv_sum;
    if (!joint3d_body_is_finite(a) || !joint3d_body_is_finite(b))
        return;
    if (!joint3d_vec3_all_finite(a->velocity) || !joint3d_vec3_all_finite(b->velocity))
        return;
    inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum <= 1e-9)
        return;
    joint3d_vec3_sub(b->velocity, a->velocity, rel);
    for (int i = 0; i < 3; i++) {
        double violation;
        double correction;
        if (fabs(j->linear_max[i] - j->linear_min[i]) <= 1e-12)
            continue; /* axis is locked — leave it to the limit solver */
        violation = rel[i] - j->linear_motor_velocity[i];
        correction = violation / inv_sum;
        if (correction > j->linear_motor_max_impulse)
            correction = j->linear_motor_max_impulse;
        else if (correction < -j->linear_motor_max_impulse)
            correction = -j->linear_motor_max_impulse;
        a->velocity[i] += correction * a->inv_mass;
        b->velocity[i] -= correction * b->inv_mass;
    }
}

/// @brief Enable/configure the SixDof linear motor (target relative velocity per
///   world axis, bounded by max_impulse). Non-Vec3 velocity is ignored.
void rt_sixdof_joint3d_set_linear_motor(void *joint,
                                        int8_t enabled,
                                        void *velocity,
                                        double max_impulse) {
    double vel[3];
    rt_sixdof_joint3d *j =
        (rt_sixdof_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SIXDOFJOINT3D_CLASS_ID);
    if (!j)
        return;
    if (!joint3d_read_vec3(velocity, vel))
        return;
    j->linear_motor_enabled = enabled ? 1 : 0;
    memcpy(j->linear_motor_velocity, vel, sizeof(j->linear_motor_velocity));
    j->linear_motor_max_impulse = (isfinite(max_impulse) && max_impulse > 0.0) ? max_impulse : 0.0;
}

void rt_sixdof_joint3d_set_linear_limits(void *joint, void *min_obj, void *max_obj) {
    double min_v[3];
    double max_v[3];
    rt_sixdof_joint3d *j =
        (rt_sixdof_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SIXDOFJOINT3D_CLASS_ID);
    if (!j)
        return;
    if (!joint3d_read_vec3(min_obj, min_v) || !joint3d_read_vec3(max_obj, max_v)) {
        rt_trap("SixDofJoint3D.SetLinearLimits: min and max must be finite Vec3 values");
        return;
    }
    joint3d_canonicalize_limits(min_v, max_v);
    memcpy(j->linear_min, min_v, sizeof(j->linear_min));
    memcpy(j->linear_max, max_v, sizeof(j->linear_max));
}

void rt_sixdof_joint3d_set_angular_limits(void *joint, void *min_obj, void *max_obj) {
    double min_v[3];
    double max_v[3];
    rt_sixdof_joint3d *j =
        (rt_sixdof_joint3d *)rt_g3d_checked_or_null(joint, RT_G3D_SIXDOFJOINT3D_CLASS_ID);
    if (!j)
        return;
    if (!joint3d_read_vec3(min_obj, min_v) || !joint3d_read_vec3(max_obj, max_v)) {
        rt_trap("SixDofJoint3D.SetAngularLimits: min and max must be finite Vec3 values");
        return;
    }
    joint3d_canonicalize_limits(min_v, max_v);
    memcpy(j->angular_min, min_v, sizeof(j->angular_min));
    memcpy(j->angular_max, max_v, sizeof(j->angular_max));
}

static void solve_sixdof(rt_sixdof_joint3d *j, double dt) {
    (void)dt;
    if (!j)
        return;
    joint3d_correct_anchor_pair_limited(
        j->body_a, j->body_b, j->local_anchor_a, j->local_anchor_b, j->linear_min, j->linear_max);
    joint3d_remove_relative_linear_velocity_locked_axes(
        j->body_a, j->body_b, j->linear_min, j->linear_max);
    joint3d_project_relative_angular_velocity_limits(
        j->body_a, j->body_b, j->angular_min, j->angular_max);
    if (j->linear_motor_enabled)
        sixdof_joint_apply_linear_motor(j);
}

/*==========================================================================
 * Generic joint solver dispatch
 *=========================================================================*/

/// @brief Dispatch the constraint solver for a joint based on its type.
/// @details Called by the physics world during each step. Dispatches to
///          the concrete joint solver based on joint_type.
/// @param joint      Opaque joint handle.
/// @param joint_type RT_JOINT_* type code.
/// @param dt         Physics timestep in seconds.
void rt_joint3d_solve(void *joint, int32_t joint_type, double dt) {
    if (!joint)
        return;
    if (joint_type == RT_JOINT_DISTANCE && rt_g3d_has_class(joint, RT_G3D_DISTANCEJOINT3D_CLASS_ID))
        solve_distance((rt_distance_joint3d *)joint, dt);
    else if (joint_type == RT_JOINT_SPRING &&
             rt_g3d_has_class(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID))
        solve_spring((rt_spring_joint3d *)joint, dt);
    else if (joint_type == RT_JOINT_HINGE &&
             rt_g3d_has_class(joint, RT_G3D_HINGEJOINT3D_CLASS_ID))
        solve_hinge((rt_hinge_joint3d *)joint, dt);
    else if (joint_type == RT_JOINT_ROPE &&
             rt_g3d_has_class(joint, RT_G3D_ROPEJOINT3D_CLASS_ID))
        solve_rope((rt_rope_joint3d *)joint, dt);
    else if (joint_type == RT_JOINT_SIXDOF &&
             rt_g3d_has_class(joint, RT_G3D_SIXDOFJOINT3D_CLASS_ID))
        solve_sixdof((rt_sixdof_joint3d *)joint, dt);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
