//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics3d.c
// Purpose: 3D physics world — AABB/sphere/capsule bodies, symplectic Euler
//   integration, impulse-based collision response with Baumgarte correction,
//   bidirectional layer/mask filtering, and character controller.
//
// Key invariants:
//   - Bodies in topological order not required (flat array, O(n²) broad phase).
//   - Integration: forces→velocity, velocity→position (symplectic Euler).
//   - Collision response: impulse = -(1+e)*rv / (inv_mass_a + inv_mass_b).
//   - Baumgarte stabilization: 40% excess penetration correction, 1% slop.
//   - Character controller: up to 3 slide iterations per move.
//
// Links: rt_physics3d.h, rt_raycast3d.h, plans/3d/20-phase-a-core-game-systems.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_physics3d.h"
#include "rt_joints3d.h"
#include "rt_raycast3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
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

#define PH3D_MAX_BODIES 256
#define PH3D_SHAPE_AABB 0
#define PH3D_SHAPE_SPHERE 1
#define PH3D_SHAPE_CAPSULE 2
#define PH3D_MODE_DYNAMIC 0
#define PH3D_MODE_STATIC 1
#define PH3D_MODE_KINEMATIC 2
#define PH3D_SLEEP_LINEAR_THRESHOLD 0.05
#define PH3D_SLEEP_ANGULAR_THRESHOLD 0.05
#define PH3D_SLEEP_DELAY 0.5
#define PH3D_MAX_CCD_SUBSTEPS 16

/*==========================================================================
 * Body3D
 *=========================================================================*/

typedef struct {
    void *vptr;
    double position[3];
    double orientation[4];
    double velocity[3];
    double angular_velocity[3];
    double force[3];
    double torque[3];
    double mass;
    double inv_mass;
    void *collider;
    double inv_inertia[3];
    double restitution;
    double friction;
    double linear_damping;
    double angular_damping;
    int64_t collision_layer;
    int64_t collision_mask;
    int32_t shape;
    double half_extents[3];
    double radius;
    double height;
    double sleep_time;
    int32_t motion_mode;
    int8_t is_static;
    int8_t is_kinematic;
    int8_t is_trigger;
    int8_t is_grounded;
    int8_t can_sleep;
    int8_t is_sleeping;
    int8_t use_ccd;
    double ground_normal[3];
} rt_body3d;

#define PH3D_MAX_CONTACTS 128
#define PH3D_MAX_QUERY_HITS 256

typedef struct {
    rt_body3d *body_a;
    rt_body3d *body_b;
    void *collider_a;
    void *collider_b;
    double point[3];
    double normal[3];
    double separation;
    double relative_speed;
    double normal_impulse;
    int8_t is_trigger;
} rt_contact3d;

typedef struct {
    void *vptr;
    double gravity[3];
    rt_body3d *bodies[PH3D_MAX_BODIES];
    int32_t body_count;
    rt_contact3d contacts[PH3D_MAX_CONTACTS];
    int32_t contact_count;
    rt_contact3d previous_contacts[PH3D_MAX_CONTACTS];
    int32_t previous_contact_count;
    rt_contact3d enter_events[PH3D_MAX_CONTACTS];
    int32_t enter_event_count;
    rt_contact3d stay_events[PH3D_MAX_CONTACTS];
    int32_t stay_event_count;
    rt_contact3d exit_events[PH3D_MAX_CONTACTS];
    int32_t exit_event_count;
/* Joint constraints */
#define PH3D_MAX_JOINTS 128
    void *joints[PH3D_MAX_JOINTS];
    int32_t joint_types[PH3D_MAX_JOINTS];
    int32_t joint_count;
} rt_world3d;

typedef struct {
    void *vptr;
    rt_body3d *body;
    rt_world3d *world;
    double step_height;
    double slope_limit_cos;
    int8_t is_grounded;
    int8_t was_grounded;
} rt_character3d;

typedef struct {
    double position[3];
    double rotation[4];
    double scale[3];
} rt_collider_pose;

typedef struct {
    rt_body3d *body;
    void *collider;
    double point[3];
    double normal[3];
    double distance;
    double fraction;
    int8_t started_penetrating;
    int8_t is_trigger;
} rt_query_hit3d;

typedef struct {
    void *vptr;
    rt_body3d *body;
    void *collider;
    double point[3];
    double normal[3];
    double distance;
    double fraction;
    int8_t started_penetrating;
    int8_t is_trigger;
} rt_physics_hit3d_obj;

typedef struct {
    void *vptr;
    void **items;
    int64_t count;
} rt_physics_hit_list3d_obj;

typedef struct {
    void *vptr;
    rt_body3d *body_a;
    rt_body3d *body_b;
    void *collider_a;
    void *collider_b;
    double point[3];
    double normal[3];
    double separation;
    double relative_speed;
    double normal_impulse;
    int8_t is_trigger;
} rt_collision_event3d_obj;

typedef struct {
    void *vptr;
    double point[3];
    double normal[3];
    double separation;
} rt_contact_point3d_obj;

static void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose);
static void collider_pose_compose(const rt_collider_pose *parent,
                                  const double *child_position,
                                  const double *child_rotation,
                                  const double *child_scale,
                                  rt_collider_pose *out);
static void transform_point_from_pose(const rt_collider_pose *pose,
                                      const double *local_point,
                                      double *world_point);
static void transform_point_to_local(const rt_collider_pose *pose,
                                     const double *world_point,
                                     double *local_point);
static void body3d_update_shape_cache_from_collider(rt_body3d *body);
static int test_collision(const rt_body3d *a,
                          const rt_body3d *b,
                          double *normal,
                          double *depth,
                          double *point,
                          void **leaf_a_out,
                          void **leaf_b_out);

/*==========================================================================
 * Collision detection helpers
 *=========================================================================*/

/// @brief Compute the world-space AABB of a body, writing min/max into `mn`/`mx`.
///
/// If the body has a custom collider, delegates to `rt_collider3d_*` to
/// compute the bounds in the body's pose. Otherwise falls back to the
/// cached primitive shape (AABB / sphere / capsule). This is the broad-
/// phase primitive used by every collision query.
static void body_aabb(const rt_body3d *b, double *mn, double *mx) {
    if (b->collider) {
        rt_collider_pose pose;
        collider_pose_from_body(b, &pose);
        rt_collider3d_compute_world_aabb_raw(
            b->collider, pose.position, pose.rotation, pose.scale, mn, mx);
        return;
    }
    if (b->shape == PH3D_SHAPE_AABB) {
        mn[0] = b->position[0] - b->half_extents[0];
        mn[1] = b->position[1] - b->half_extents[1];
        mn[2] = b->position[2] - b->half_extents[2];
        mx[0] = b->position[0] + b->half_extents[0];
        mx[1] = b->position[1] + b->half_extents[1];
        mx[2] = b->position[2] + b->half_extents[2];
    } else if (b->shape == PH3D_SHAPE_SPHERE) {
        mn[0] = b->position[0] - b->radius;
        mn[1] = b->position[1] - b->radius;
        mn[2] = b->position[2] - b->radius;
        mx[0] = b->position[0] + b->radius;
        mx[1] = b->position[1] + b->radius;
        mx[2] = b->position[2] + b->radius;
    } else /* capsule */
    {
        double hh = b->height * 0.5;
        mn[0] = b->position[0] - b->radius;
        mn[1] = b->position[1] - hh;
        mn[2] = b->position[2] - b->radius;
        mx[0] = b->position[0] + b->radius;
        mx[1] = b->position[1] + hh;
        mx[2] = b->position[2] + b->radius;
    }
}

// Vector / scalar math helpers — internal-only, kept in this TU so the
// optimizer can inline them aggressively. All `double[3]` arguments are
// treated as plain xyz triples (no SIMD layout constraints).

/// @brief Clamp `v` into `[lo, hi]`.
static double clampd(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/// @brief 3D dot product `a · b`.
static double vec3_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// @brief Squared length `|v|²` — avoids the sqrt when the magnitude isn't needed.
static double vec3_len_sq(const double *v) {
    return vec3_dot(v, v);
}

/// @brief Euclidean length `|v|`.
static double vec3_len(const double *v) {
    return sqrt(vec3_len_sq(v));
}

/// @brief Copy `src` xyz into `dst`.
static void vec3_copy(double *dst, const double *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/// @brief Set `dst` to `(x, y, z)`.
static void vec3_set(double *dst, double x, double y, double z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

/// @brief In-place scalar multiply: `dst *= s`.
static void vec3_scale_in_place(double *dst, double s) {
    dst[0] *= s;
    dst[1] *= s;
    dst[2] *= s;
}

/// @brief Component subtract: `out = a - b`.
static void vec3_sub(const double *a, const double *b, double *out) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

/// @brief Negate: `dst = -src`.
static void vec3_negate(const double *src, double *dst) {
    dst[0] = -src[0];
    dst[1] = -src[1];
    dst[2] = -src[2];
}

/// @brief Normalize `v` in place. Returns the original length (0 → no-op).
static double vec3_normalize_in_place(double *v) {
    double len = vec3_len(v);
    if (len > 1e-12) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    return len;
}

// Quaternion helpers — store as `(x, y, z, w)` with `w` last (scalar).
// Used for body orientation and the angular velocity → orientation
// integration step.

/// @brief Set `q` to the identity quaternion (no rotation).
static void quat_identity(double *q) {
    q[0] = 0.0;
    q[1] = 0.0;
    q[2] = 0.0;
    q[3] = 1.0;
}

/// @brief Squared norm `|q|²`.
static double quat_len_sq(const double *q) {
    return q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
}

/// @brief Normalize `q` in place. Falls back to identity for near-zero quats.
///
/// Important for stability: numerical drift over many integration steps
/// gradually denormalizes orientations, which causes geometry to skew.
static void quat_normalize(double *q) {
    double len_sq = quat_len_sq(q);
    if (len_sq < 1e-24) {
        quat_identity(q);
        return;
    }
    double inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

/// @brief Hamilton product `out = a * b` (composes two rotations).
///
/// Convention: applying `a*b` to a vector first applies `b` then `a`,
/// matching the "right-multiply pre-rotation" convention used in
/// `quat_integrate`.
static void quat_mul(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

/// @brief Build a unit quaternion from an axis (direction) and angle (radians).
///
/// Standard formula: `q = (sin(θ/2) * axis_unit, cos(θ/2))`. Returns
/// the identity for zero-length axis or zero angle.
static void quat_from_axis_angle(const double *axis, double angle, double *out) {
    double axis_len = vec3_len(axis);
    if (axis_len < 1e-12 || fabs(angle) < 1e-12) {
        quat_identity(out);
        return;
    }
    double half_angle = angle * 0.5;
    double s = sin(half_angle) / axis_len;
    out[0] = axis[0] * s;
    out[1] = axis[1] * s;
    out[2] = axis[2] * s;
    out[3] = cos(half_angle);
    quat_normalize(out);
}

/// @brief Advance an orientation by the angular velocity over time `dt`.
///
/// Uses the discrete update `q ← Δq * q` where `Δq` is built from
/// `(angular_velocity / |ω|, |ω| * dt)`. Re-normalizes after each step
/// to prevent drift. Skipped when angular velocity or `dt` is zero.
static void quat_integrate(double *orientation, const double *angular_velocity, double dt) {
    double speed = vec3_len(angular_velocity);
    if (speed < 1e-12 || dt <= 0.0)
        return;
    double axis[3] = {
        angular_velocity[0] / speed,
        angular_velocity[1] / speed,
        angular_velocity[2] / speed,
    };
    double delta[4], out[4];
    quat_from_axis_angle(axis, speed * dt, delta);
    quat_mul(delta, orientation, out);
    memcpy(orientation, out, sizeof(out));
    quat_normalize(orientation);
}

/// @brief Conjugate `out = q*` — for unit quaternions equivalent to the inverse.
static void quat_conjugate(const double *q, double *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Rotate vector `v` by quaternion `q`: `out = q * v * q*`.
///
/// The classic sandwich formula. Allocates two temporaries on the stack.
/// For hot inner loops we'd inline a more efficient form, but this is
/// only called once per pose composition so the cost is irrelevant.
static void quat_rotate_vec3(const double *q, const double *v, double *out) {
    double qv[4] = {v[0], v[1], v[2], 0.0};
    double q_conj[4];
    double tmp[4];
    quat_conjugate(q, q_conj);
    quat_mul(q, qv, tmp);
    quat_mul(tmp, q_conj, tmp);
    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
}

// Collider pose helpers — pose = position + orientation + scale, used
// when nesting compound colliders or transforming local-space query
// points into world space.

/// @brief Reset a pose to identity (origin, no rotation, unit scale).
static void collider_pose_identity(rt_collider_pose *pose) {
    if (!pose)
        return;
    vec3_set(pose->position, 0.0, 0.0, 0.0);
    quat_identity(pose->rotation);
    vec3_set(pose->scale, 1.0, 1.0, 1.0);
}

/// @brief Initialize a pose from a body's transform (unit scale).
///
/// Bodies don't carry a per-body scale; the pose's scale stays at unit.
/// Per-collider scale is applied during composition (`collider_pose_compose`).
static void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose) {
    collider_pose_identity(pose);
    if (!body || !pose)
        return;
    vec3_copy(pose->position, body->position);
    pose->rotation[0] = body->orientation[0];
    pose->rotation[1] = body->orientation[1];
    pose->rotation[2] = body->orientation[2];
    pose->rotation[3] = body->orientation[3];
}

/// @brief Compose a child transform with a parent pose.
///
/// Order: scale, rotate, translate (TRS). Multiplies parent and child
/// scales component-wise, then rotates the child position by the parent
/// rotation before adding it to the parent position. Child rotation is
/// pre-multiplied into the parent rotation, then re-normalized.
static void collider_pose_compose(const rt_collider_pose *parent,
                                  const double *child_position,
                                  const double *child_rotation,
                                  const double *child_scale,
                                  rt_collider_pose *out) {
    double scaled[3];
    double rotated[3];
    if (!out)
        return;
    collider_pose_identity(out);
    if (!parent)
        return;
    out->scale[0] = parent->scale[0] * child_scale[0];
    out->scale[1] = parent->scale[1] * child_scale[1];
    out->scale[2] = parent->scale[2] * child_scale[2];
    scaled[0] = child_position[0] * parent->scale[0];
    scaled[1] = child_position[1] * parent->scale[1];
    scaled[2] = child_position[2] * parent->scale[2];
    quat_rotate_vec3(parent->rotation, scaled, rotated);
    out->position[0] = parent->position[0] + rotated[0];
    out->position[1] = parent->position[1] + rotated[1];
    out->position[2] = parent->position[2] + rotated[2];
    quat_mul(parent->rotation, child_rotation, out->rotation);
    quat_normalize(out->rotation);
}

/// @brief Transform a local-space point through a pose into world space.
///
/// Applies scale → rotation → translation. Used to lift collision
/// query points (e.g., raycast hits) back into world coordinates.
static void transform_point_from_pose(const rt_collider_pose *pose,
                                      const double *local_point,
                                      double *world_point) {
    double scaled[3];
    double rotated[3];
    if (!pose || !local_point || !world_point)
        return;
    scaled[0] = local_point[0] * pose->scale[0];
    scaled[1] = local_point[1] * pose->scale[1];
    scaled[2] = local_point[2] * pose->scale[2];
    quat_rotate_vec3(pose->rotation, scaled, rotated);
    world_point[0] = pose->position[0] + rotated[0];
    world_point[1] = pose->position[1] + rotated[1];
    world_point[2] = pose->position[2] + rotated[2];
}

/// @brief Inverse of `transform_point_from_pose`: world → local.
///
/// Applies translation → conjugate rotation → inverse scale. Skips
/// the divide-by-scale step on near-zero axes to avoid divide-by-zero
/// when a body has been collapsed along an axis.
static void transform_point_to_local(const rt_collider_pose *pose,
                                     const double *world_point,
                                     double *local_point) {
    double translated[3];
    double inv_rotation[4];
    if (!pose || !world_point || !local_point)
        return;
    translated[0] = world_point[0] - pose->position[0];
    translated[1] = world_point[1] - pose->position[1];
    translated[2] = world_point[2] - pose->position[2];
    quat_conjugate(pose->rotation, inv_rotation);
    quat_rotate_vec3(inv_rotation, translated, local_point);
    if (fabs(pose->scale[0]) > 1e-12)
        local_point[0] /= pose->scale[0];
    if (fabs(pose->scale[1]) > 1e-12)
        local_point[1] /= pose->scale[1];
    if (fabs(pose->scale[2]) > 1e-12)
        local_point[2] /= pose->scale[2];
}

/// @brief Refresh the body's cached primitive-shape fields from its collider.
///
/// Bodies hold a denormalized cache of `(shape, half_extents, radius,
/// height)` so the inner collision loop doesn't have to virtual-dispatch
/// on every comparison. This needs to be re-run whenever the collider's
/// shape parameters change. Falls back to an AABB derived from the
/// collider's local bounds for any non-primitive shape (mesh, hull, etc.).
static void body3d_update_shape_cache_from_collider(rt_body3d *body) {
    double local_min[3];
    double local_max[3];
    int64_t type;
    if (!body) {
        return;
    }
    body->shape = PH3D_SHAPE_AABB;
    vec3_set(body->half_extents, 0.0, 0.0, 0.0);
    body->radius = 0.0;
    body->height = 0.0;
    if (!body->collider)
        return;

    type = rt_collider3d_get_type(body->collider);
    switch (type) {
    case RT_COLLIDER3D_TYPE_BOX:
        body->shape = PH3D_SHAPE_AABB;
        rt_collider3d_get_box_half_extents_raw(body->collider, body->half_extents);
        break;
    case RT_COLLIDER3D_TYPE_SPHERE:
        body->shape = PH3D_SHAPE_SPHERE;
        body->radius = rt_collider3d_get_radius_raw(body->collider);
        vec3_set(body->half_extents, body->radius, body->radius, body->radius);
        break;
    case RT_COLLIDER3D_TYPE_CAPSULE:
        body->shape = PH3D_SHAPE_CAPSULE;
        body->radius = rt_collider3d_get_radius_raw(body->collider);
        body->height = rt_collider3d_get_height_raw(body->collider);
        vec3_set(body->half_extents, body->radius, body->height * 0.5, body->radius);
        break;
    default:
        rt_collider3d_get_local_bounds_raw(body->collider, local_min, local_max);
        body->shape = PH3D_SHAPE_AABB;
        body->half_extents[0] = fabs(local_max[0] - local_min[0]) * 0.5;
        body->half_extents[1] = fabs(local_max[1] - local_min[1]) * 0.5;
        body->half_extents[2] = fabs(local_max[2] - local_min[2]) * 0.5;
        body->radius = body->half_extents[0];
        if (body->half_extents[1] > body->radius)
            body->radius = body->half_extents[1];
        if (body->half_extents[2] > body->radius)
            body->radius = body->half_extents[2];
        body->height = body->half_extents[1] * 2.0;
        break;
    }
}

/// @brief Compute the diagonal inverse inertia tensor from mass + shape.
///
/// Uses the standard rigid-body formulas:
///   - Box: `I_xx = m(h² + d²)/12`, etc.
///   - Sphere: `I = (2/5) m r²` (uniform on every axis).
///   - Capsule: cylinder approximation, `I_xx = m(3r² + h²)/12`,
///              `I_yy = m r²/2`.
/// Static and kinematic bodies get zero inverse-inertia (they don't
/// rotate from impulses).
static void body3d_compute_inv_inertia(rt_body3d *b) {
    if (!b)
        return;
    vec3_set(b->inv_inertia, 0.0, 0.0, 0.0);
    if (b->mass <= 1e-12 || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;

    if (b->shape == PH3D_SHAPE_AABB) {
        double wx = b->half_extents[0] * 2.0;
        double hy = b->half_extents[1] * 2.0;
        double dz = b->half_extents[2] * 2.0;
        double ixx = b->mass * (hy * hy + dz * dz) / 12.0;
        double iyy = b->mass * (wx * wx + dz * dz) / 12.0;
        double izz = b->mass * (wx * wx + hy * hy) / 12.0;
        b->inv_inertia[0] = ixx > 1e-12 ? 1.0 / ixx : 0.0;
        b->inv_inertia[1] = iyy > 1e-12 ? 1.0 / iyy : 0.0;
        b->inv_inertia[2] = izz > 1e-12 ? 1.0 / izz : 0.0;
        return;
    }

    if (b->shape == PH3D_SHAPE_SPHERE) {
        double inertia = 0.4 * b->mass * b->radius * b->radius;
        double inv = inertia > 1e-12 ? 1.0 / inertia : 0.0;
        vec3_set(b->inv_inertia, inv, inv, inv);
        return;
    }

    {
        double r2 = b->radius * b->radius;
        double h2 = b->height * b->height;
        double ixx = b->mass * (3.0 * r2 + h2) / 12.0;
        double iyy = 0.5 * b->mass * r2;
        double izz = ixx;
        b->inv_inertia[0] = ixx > 1e-12 ? 1.0 / ixx : 0.0;
        b->inv_inertia[1] = iyy > 1e-12 ? 1.0 / iyy : 0.0;
        b->inv_inertia[2] = izz > 1e-12 ? 1.0 / izz : 0.0;
    }
}

/// @brief Synchronize derived motion-mode state with `b->motion_mode`.
///
/// Sets the `is_static`/`is_kinematic` flags, recomputes `inv_mass`
/// (zero for static/kinematic), wipes inverse inertia for non-dynamic
/// bodies, and clears the sleep timer so a mode change doesn't leave a
/// previously-sleeping body inert in the wrong mode.
static void body3d_refresh_motion_mode(rt_body3d *b) {
    if (!b)
        return;
    b->is_static = (b->motion_mode == PH3D_MODE_STATIC) ? 1 : 0;
    b->is_kinematic = (b->motion_mode == PH3D_MODE_KINEMATIC) ? 1 : 0;
    if (b->motion_mode == PH3D_MODE_DYNAMIC && b->mass > 1e-12) {
        b->inv_mass = 1.0 / b->mass;
    } else {
        b->inv_mass = 0.0;
    }
    if (b->motion_mode != PH3D_MODE_DYNAMIC) {
        vec3_set(b->inv_inertia, 0.0, 0.0, 0.0);
        b->is_sleeping = 0;
        b->sleep_time = 0.0;
    } else {
        body3d_compute_inv_inertia(b);
    }
}

/// @brief Validate that a desired motion mode is compatible with the collider.
///
/// Some collider types (e.g., concave triangle meshes) are static-only
/// because the dynamic-collision math doesn't handle them. Traps with
/// `api_name` as the trap message when an invalid combination is
/// requested. Returns 1 when the mode is allowed.
static int body3d_motion_mode_allowed(const rt_body3d *body,
                                      void *collider,
                                      int32_t desired_mode,
                                      const char *api_name) {
    (void)body;
    if (desired_mode == PH3D_MODE_STATIC)
        return 1;
    if (collider && rt_collider3d_is_static_only_raw(collider)) {
        rt_trap(api_name);
        return 0;
    }
    return 1;
}

/// @brief Wake a sleeping dynamic body (no-op for static/kinematic).
///
/// Called whenever something happens that should re-enter the sim:
/// applied force/torque, teleport, manual velocity change, or contact
/// from a non-sleeping neighbour.
static void body3d_wake_if_dynamic(rt_body3d *b) {
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    b->is_sleeping = 0;
    b->sleep_time = 0.0;
}

/// @brief Per-body distance threshold for triggering CCD substeps.
///
/// Returns "half the smallest extent" — a heuristic that matches the
/// fastest a body can move in one step before tunneling becomes a
/// risk. CCD substepping uses this to decide whether the displacement
/// for a step exceeds the body's footprint and needs to be subdivided.
static double body3d_ccd_threshold(const rt_body3d *b) {
    if (!b)
        return 0.0;
    if (b->shape == PH3D_SHAPE_SPHERE || b->shape == PH3D_SHAPE_CAPSULE)
        return b->radius > 1e-6 ? b->radius * 0.5 : 0.0;
    {
        double min_half = b->half_extents[0];
        if (b->half_extents[1] < min_half)
            min_half = b->half_extents[1];
        if (b->half_extents[2] < min_half)
            min_half = b->half_extents[2];
        return min_half > 1e-6 ? min_half * 0.5 : 0.0;
    }
}

/// @brief Build a stack-allocated dummy sphere body for transient queries.
///
/// Used so that overlap / shapecast routines can reuse the body-vs-body
/// collision functions without needing to register a permanent body.
/// Only fields the collision routines read are populated.
static void make_temp_sphere(rt_body3d *out, const double *center, double radius) {
    memset(out, 0, sizeof(*out));
    out->shape = PH3D_SHAPE_SPHERE;
    out->position[0] = center[0];
    out->position[1] = center[1];
    out->position[2] = center[2];
    out->radius = radius;
}

/// @brief Return the two endpoints of a capsule's axis segment.
///
/// Capsules in this engine are Y-aligned with the position at the
/// center. Endpoints sit `(height/2 - radius)` away on each side; the
/// hemispherical caps sit beyond those endpoints.
///
/// DESIGN LIMITATION: this routine ignores `b->orientation`. Rotating a
/// capsule away from vertical produces wrong collision geometry. When
/// `RT_PHYSICS3D_STRICT_CAPSULE_AXIS` is defined at build time we trap on
/// non-identity orientations so the limitation is visible rather than
/// silently corrupting results. Identity quaternion = (x=0,y=0,z=0,w=1).
static void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c) {
#ifdef RT_PHYSICS3D_STRICT_CAPSULE_AXIS
    const double eps = 1e-5;
    if (fabs(b->orientation[0]) > eps || fabs(b->orientation[1]) > eps ||
        fabs(b->orientation[2]) > eps || fabs(b->orientation[3] - 1.0) > eps) {
        rt_trap("Physics3D: capsule axis is Y-aligned; rotating the body has no effect");
    }
#endif
    double half_axis = fmax(b->height * 0.5 - b->radius, 0.0);
    vec3_set(a, b->position[0], b->position[1] - half_axis, b->position[2]);
    vec3_set(c, b->position[0], b->position[1] + half_axis, b->position[2]);
}

/// @brief Project a point onto the capsule's axis segment.
///
/// X/Z are clamped to the capsule's column (axis is Y-aligned), Y is
/// clamped between the segment endpoints. Used as the first step in
/// capsule-vs-anything distance computations.
static void closest_point_capsule_axis_to_point(const rt_body3d *cap,
                                                const double *point,
                                                double *closest) {
    double a[3], c[3];
    capsule_axis_endpoints(cap, a, c);
    closest[0] = cap->position[0];
    closest[1] = clampd(point[1], a[1], c[1]);
    closest[2] = cap->position[2];
}

/// @brief Find the closest point pair on two capsule axes.
///
/// Both capsules are Y-aligned in this engine, so the math reduces to
/// 1D segment-vs-segment along Y: either the segments overlap (use the
/// midpoint of the overlap) or one is entirely above/below the other
/// (use the nearest endpoints). X/Z just take each capsule's center.
static void closest_points_capsule_axes(const rt_body3d *a,
                                        const rt_body3d *b,
                                        double *closest_a,
                                        double *closest_b) {
    double aa[3], ac[3], ba[3], bc[3];
    capsule_axis_endpoints(a, aa, ac);
    capsule_axis_endpoints(b, ba, bc);

    closest_a[0] = a->position[0];
    closest_a[2] = a->position[2];
    closest_b[0] = b->position[0];
    closest_b[2] = b->position[2];

    if (ac[1] < ba[1]) {
        closest_a[1] = ac[1];
        closest_b[1] = ba[1];
    } else if (bc[1] < aa[1]) {
        closest_a[1] = aa[1];
        closest_b[1] = bc[1];
    } else {
        double overlap_min = aa[1] > ba[1] ? aa[1] : ba[1];
        double overlap_max = ac[1] < bc[1] ? ac[1] : bc[1];
        double y = clampd((a->position[1] + b->position[1]) * 0.5, overlap_min, overlap_max);
        closest_a[1] = y;
        closest_b[1] = y;
    }
}

/// @brief Find the point on a capsule's axis closest to a box.
///
/// X/Z take the capsule center; Y picks whichever capsule endpoint
/// overhangs the box (or, if neither does, the projection of the
/// capsule center clamped into the box-axis-overlap range). Used as
/// the first step of capsule-vs-AABB collision.
static void closest_point_capsule_axis_to_aabb(const rt_body3d *cap,
                                               const rt_body3d *box,
                                               double *closest_axis) {
    double mn[3], mx[3], a[3], c[3];
    body_aabb(box, mn, mx);
    capsule_axis_endpoints(cap, a, c);
    closest_axis[0] = cap->position[0];
    closest_axis[2] = cap->position[2];
    if (c[1] < mn[1])
        closest_axis[1] = c[1];
    else if (a[1] > mx[1])
        closest_axis[1] = a[1];
    else
        closest_axis[1] =
            clampd(cap->position[1], mn[1] > a[1] ? mn[1] : a[1], mx[1] < c[1] ? mx[1] : c[1]);
}

/// @brief Layer/mask filter for one-sided queries (raycast, overlap).
///
/// Bidirectional layer/mask filtering applies for body-vs-body collision
/// (`bodies_can_collide`); queries are unidirectional, so we just check
/// the body's layer against the query's mask. A zero mask is treated as
/// "match anything" for ergonomics.
static int query_mask_matches_body(const rt_body3d *body, int64_t mask) {
    int64_t effective_mask = mask;
    if (!body)
        return 0;
    if (effective_mask == 0)
        effective_mask = -1;
    return (body->collision_layer & effective_mask) != 0;
}

/// @brief Raw AABB-vs-AABB overlap test (no shape interpretation).
///
/// SAT on the axis-aligned axes. Returns true if neither axis fully
/// separates the two boxes. This is the broad-phase primitive — the
/// narrow-phase shape tests run only on pairs that pass this check.
static int aabb_overlap_raw(const double *amn, const double *amx, const double *bmn, const double *bmx) {
    return !(amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
             amn[2] > bmx[2] || amx[2] < bmn[2]);
}

/// @brief Compute the AABB swept by translating `[start_min, start_max]` by `delta`.
///
/// Used by CCD to make a single AABB that bounds an entire integration
/// step's motion. If this swept box doesn't overlap a candidate body's
/// AABB, the bodies cannot collide during the step.
static void swept_aabb_from_points(const double *start_min,
                                   const double *start_max,
                                   const double *delta,
                                   double *swept_min,
                                   double *swept_max) {
    double end_min[3] = {start_min[0] + delta[0], start_min[1] + delta[1], start_min[2] + delta[2]};
    double end_max[3] = {start_max[0] + delta[0], start_max[1] + delta[1], start_max[2] + delta[2]};
    swept_min[0] = start_min[0] < end_min[0] ? start_min[0] : end_min[0];
    swept_min[1] = start_min[1] < end_min[1] ? start_min[1] : end_min[1];
    swept_min[2] = start_min[2] < end_min[2] ? start_min[2] : end_min[2];
    swept_max[0] = start_max[0] > end_max[0] ? start_max[0] : end_max[0];
    swept_max[1] = start_max[1] > end_max[1] ? start_max[1] : end_max[1];
    swept_max[2] = start_max[2] > end_max[2] ? start_max[2] : end_max[2];
}

/// @brief Bytewise contact copy. Used to snapshot frame-N contacts for
///        the frame-N+1 enter/stay/exit event diff.
static void contact_snapshot_copy(rt_contact3d *dst, const rt_contact3d *src) {
    if (!dst || !src)
        return;
    memcpy(dst, src, sizeof(*dst));
}

/// @brief Identity test for contacts: same body pair AND same collider pair.
///
/// The collider check matters for compound bodies — same body pair but
/// different child colliders is a different contact.
static int contact_pair_equals(const rt_contact3d *a, const rt_contact3d *b) {
    if (!a || !b)
        return 0;
    return a->body_a == b->body_a && a->body_b == b->body_b &&
           a->collider_a == b->collider_a && a->collider_b == b->collider_b;
}

/// @brief Compute the support point for a collider in a given direction.
///
/// "Support" = farthest point on the shape along `direction`. Closed
/// form for spheres and capsules; recursive for compound colliders
/// (pick the child whose support has the largest dot with `direction`);
/// AABB-based fallback for unrecognized shapes. Used by contact-point
/// reconstruction after narrow-phase collision detection.
static void collider_support_point(void *collider,
                                   const rt_collider_pose *pose,
                                   const double *direction,
                                   double *out_point) {
    double dir[3];
    double mn[3];
    double mx[3];
    int64_t type;
    if (!out_point) {
        return;
    }
    vec3_set(out_point, 0.0, 0.0, 0.0);
    if (!collider || !pose || !direction)
        return;

    vec3_copy(dir, direction);
    if (vec3_normalize_in_place(dir) <= 1e-12)
        vec3_set(dir, 0.0, 1.0, 0.0);
    type = rt_collider3d_get_type(collider);

    switch (type) {
    case RT_COLLIDER3D_TYPE_SPHERE: {
        double radius = rt_collider3d_get_radius_raw(collider);
        double max_scale = pose->scale[0];
        if (pose->scale[1] > max_scale)
            max_scale = pose->scale[1];
        if (pose->scale[2] > max_scale)
            max_scale = pose->scale[2];
        out_point[0] = pose->position[0] + dir[0] * radius * max_scale;
        out_point[1] = pose->position[1] + dir[1] * radius * max_scale;
        out_point[2] = pose->position[2] + dir[2] * radius * max_scale;
        return;
    }
    case RT_COLLIDER3D_TYPE_CAPSULE: {
        double radius = rt_collider3d_get_radius_raw(collider);
        double half_axis = fmax(rt_collider3d_get_height_raw(collider) * 0.5 - radius, 0.0);
        double max_radial_scale = pose->scale[0] > pose->scale[2] ? pose->scale[0] : pose->scale[2];
        out_point[0] = pose->position[0] + dir[0] * radius * max_radial_scale;
        out_point[1] =
            pose->position[1] + (dir[1] >= 0.0 ? half_axis * pose->scale[1] : -half_axis * pose->scale[1]) +
            dir[1] * radius * pose->scale[1];
        out_point[2] = pose->position[2] + dir[2] * radius * max_radial_scale;
        return;
    }
    case RT_COLLIDER3D_TYPE_COMPOUND: {
        int64_t child_count = rt_collider3d_get_child_count_raw(collider);
        double best_dot = -1e300;
        for (int64_t i = 0; i < child_count; ++i) {
            void *child = rt_collider3d_get_child_raw(collider, i);
            double child_pos[3], child_rot[4], child_scale[3];
            rt_collider_pose child_pose;
            double candidate[3];
            rt_collider3d_get_child_transform_raw(collider, i, child_pos, child_rot, child_scale);
            collider_pose_compose(pose, child_pos, child_rot, child_scale, &child_pose);
            collider_support_point(child, &child_pose, dir, candidate);
            {
                double d = vec3_dot(candidate, dir);
                if (d > best_dot) {
                    best_dot = d;
                    vec3_copy(out_point, candidate);
                }
            }
        }
        return;
    }
    default:
        rt_collider3d_compute_world_aabb_raw(collider, pose->position, pose->rotation, pose->scale, mn, mx);
        out_point[0] = dir[0] >= 0.0 ? mx[0] : mn[0];
        out_point[1] = dir[1] >= 0.0 ? mx[1] : mn[1];
        out_point[2] = dir[2] >= 0.0 ? mx[2] : mn[2];
        return;
    }
}

/// @brief Estimate a representative contact point given two leaf colliders + a normal.
///
/// Takes the support point on each leaf in opposite directions
/// (`+normal` for A, `-normal` for B), then averages them. Cheap and
/// stable enough for the impulse solver, which only uses the contact
/// point for friction torque computation.
static void compute_contact_point_from_leafs(void *a_leaf,
                                             const rt_collider_pose *a_pose,
                                             void *b_leaf,
                                             const rt_collider_pose *b_pose,
                                             const double *normal,
                                             double *point_out) {
    double point_a[3];
    double point_b[3];
    double inv_normal[3];
    if (!point_out) {
        return;
    }
    vec3_set(point_out, 0.0, 0.0, 0.0);
    if (!a_leaf || !a_pose || !b_leaf || !b_pose || !normal)
        return;
    vec3_negate(normal, inv_normal);
    collider_support_point(a_leaf, a_pose, normal, point_a);
    collider_support_point(b_leaf, b_pose, inv_normal, point_b);
    point_out[0] = (point_a[0] + point_b[0]) * 0.5;
    point_out[1] = (point_a[1] + point_b[1]) * 0.5;
    point_out[2] = (point_a[2] + point_b[2]) * 0.5;
}

/// @brief Bidirectional layer/mask filter for body-vs-body collision.
///
/// Both bodies must be on a layer the other body's mask accepts. This
/// preserves intuitive behavior: e.g., players (layer P, mask=P|E) and
/// enemies (layer E, mask=P|E) collide; bullets (layer B, mask=E) hit
/// enemies but not players.
static int bodies_can_collide(const rt_body3d *a, const rt_body3d *b) {
    if (!a || !b)
        return 0;
    if (!(a->collision_layer & b->collision_mask))
        return 0;
    if (!(b->collision_layer & a->collision_mask))
        return 0;
    return 1;
}

/* --- Shape-specific narrow-phase collision tests ---
 * Normal always points A→B (matches impulse convention: a.vel -= j*n). */

/// @brief AABB-vs-AABB narrow phase: returns penetration depth and normal.
///
/// SAT picks the axis of minimum overlap as the separation direction.
/// Normal points from A toward B (impulse convention: `a.vel -= j*n`).
static int test_aabb_aabb(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth) {
    double amn[3], amx[3], bmn[3], bmx[3];
    body_aabb(a, amn, amx);
    body_aabb(b, bmn, bmx);
    double ox = (amx[0] < bmx[0] ? amx[0] - bmn[0] : bmx[0] - amn[0]);
    double oy = (amx[1] < bmx[1] ? amx[1] - bmn[1] : bmx[1] - amn[1]);
    double oz = (amx[2] < bmx[2] ? amx[2] - bmn[2] : bmx[2] - amn[2]);
    if (ox <= 0 || oy <= 0 || oz <= 0)
        return 0;
    normal[0] = normal[1] = normal[2] = 0;
    if (ox <= oy && ox <= oz) {
        *depth = ox;
        normal[0] = (a->position[0] < b->position[0]) ? 1.0 : -1.0;
    } else if (oy <= oz) {
        *depth = oy;
        normal[1] = (a->position[1] < b->position[1]) ? 1.0 : -1.0;
    } else {
        *depth = oz;
        normal[2] = (a->position[2] < b->position[2]) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Sphere-vs-sphere: simple distance vs sum-of-radii test.
///
/// Falls back to a +Y push when the centers are exactly coincident
/// (otherwise the normal would be ill-defined and the impulse step
/// would NaN out).
static int test_sphere_sphere(const rt_body3d *a,
                              const rt_body3d *b,
                              double *normal,
                              double *depth) {
    double dx = b->position[0] - a->position[0];
    double dy = b->position[1] - a->position[1];
    double dz = b->position[2] - a->position[2];
    double dist_sq = dx * dx + dy * dy + dz * dz;
    double sum_r = a->radius + b->radius;
    if (dist_sq >= sum_r * sum_r)
        return 0;
    double dist = sqrt(dist_sq);
    if (dist < 1e-12) {
        /* Coincident centers — push along Y */
        normal[0] = 0;
        normal[1] = 1;
        normal[2] = 0;
        *depth = sum_r;
    } else {
        double inv_dist = 1.0 / dist;
        normal[0] = dx * inv_dist;
        normal[1] = dy * inv_dist;
        normal[2] = dz * inv_dist;
        *depth = sum_r - dist;
    }
    return 1;
}

/// @brief AABB-vs-sphere: clamp sphere center into AABB, then sphere-distance test.
///
/// If the sphere's center lies inside the AABB, falls back to the
/// AABB-AABB pushout to avoid a degenerate zero-distance normal. The
/// returned normal points from AABB to sphere.
static int test_aabb_sphere(const rt_body3d *aabb,
                            const rt_body3d *sph,
                            double *normal,
                            double *depth) {
    /* Find closest point on AABB to sphere center */
    double closest[3];
    double amn[3], amx[3];
    body_aabb(aabb, amn, amx);
    for (int i = 0; i < 3; i++) {
        double v = sph->position[i];
        if (v < amn[i])
            v = amn[i];
        if (v > amx[i])
            v = amx[i];
        closest[i] = v;
    }
    double dx = sph->position[0] - closest[0];
    double dy = sph->position[1] - closest[1];
    double dz = sph->position[2] - closest[2];
    double dist_sq = dx * dx + dy * dy + dz * dz;
    if (dist_sq >= sph->radius * sph->radius)
        return 0;
    double dist = sqrt(dist_sq);
    if (dist < 1e-12) {
        /* Sphere center inside AABB — use AABB pushout */
        return test_aabb_aabb(aabb, sph, normal, depth);
    }
    double inv_dist = 1.0 / dist;
    /* Normal points from AABB toward sphere */
    normal[0] = dx * inv_dist;
    normal[1] = dy * inv_dist;
    normal[2] = dz * inv_dist;
    *depth = sph->radius - dist;
    return 1;
}

/// @brief Whether `type` is one of the primitive shapes the narrow phase handles directly.
static int collider_type_is_simple(int64_t type) {
    return type == RT_COLLIDER3D_TYPE_BOX || type == RT_COLLIDER3D_TYPE_SPHERE ||
           type == RT_COLLIDER3D_TYPE_CAPSULE;
}

/// @brief Build a transient `rt_body3d` whose shape mirrors a simple collider.
///
/// Used during compound-vs-compound collision: each leaf-pair produces
/// a transient body so we can reuse the body-level narrow-phase tests.
/// Applies the pose's scale to extents/radius/height. Returns 0 for
/// non-simple shapes (the caller falls back to AABB-vs-AABB).
static int build_simple_proxy(const rt_collider_pose *pose,
                              void *collider,
                              rt_body3d *out) {
    double hx[3];
    double sx = fabs(pose->scale[0]);
    double sy = fabs(pose->scale[1]);
    double sz = fabs(pose->scale[2]);
    int64_t type;
    if (!pose || !collider || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->position[0] = pose->position[0];
    out->position[1] = pose->position[1];
    out->position[2] = pose->position[2];
    out->orientation[0] = pose->rotation[0];
    out->orientation[1] = pose->rotation[1];
    out->orientation[2] = pose->rotation[2];
    out->orientation[3] = pose->rotation[3];
    type = rt_collider3d_get_type(collider);
    switch (type) {
    case RT_COLLIDER3D_TYPE_BOX:
        out->shape = PH3D_SHAPE_AABB;
        rt_collider3d_get_box_half_extents_raw(collider, hx);
        out->half_extents[0] = hx[0] * sx;
        out->half_extents[1] = hx[1] * sy;
        out->half_extents[2] = hx[2] * sz;
        return 1;
    case RT_COLLIDER3D_TYPE_SPHERE:
        out->shape = PH3D_SHAPE_SPHERE;
        out->radius = rt_collider3d_get_radius_raw(collider);
        if (sy > sx)
            sx = sy;
        if (sz > sx)
            sx = sz;
        out->radius *= sx;
        return 1;
    case RT_COLLIDER3D_TYPE_CAPSULE:
        out->shape = PH3D_SHAPE_CAPSULE;
        out->radius = rt_collider3d_get_radius_raw(collider) * (sx > sz ? sx : sz);
        out->height = rt_collider3d_get_height_raw(collider) * sy;
        return 1;
    default:
        return 0;
    }
}

/// @brief Narrow-phase dispatcher for primitive-shape body pairs.
///
/// Cheap broad-phase (AABB overlap) up front, then routes to:
///   - sphere/capsule × sphere/capsule via closest-axis sphere test
///   - AABB × sphere/capsule via clamp-then-distance
///   - AABB × AABB via SAT
/// The capsule shape is reduced to a transient sphere at the closest
/// axis point so the entire matrix collapses to ~3 specialized cases.
/// Always returns the normal pointing from A to B (so symmetric callers
/// flip it when needed).
static int test_simple_collision(const rt_body3d *a,
                                 const rt_body3d *b,
                                 double *normal,
                                 double *depth) {
    /* Broad phase: AABB overlap test */
    double amn[3], amx[3], bmn[3], bmx[3];
    body_aabb(a, amn, amx);
    body_aabb(b, bmn, bmx);
    if (amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
        amn[2] > bmx[2] || amx[2] < bmn[2])
        return 0;

    /* Narrow phase: shape-specific dispatch */
    int sa = a->shape, sb = b->shape;

    /* Sphere/capsule pairs collapse to closest-axis sphere tests. */
    if ((sa == PH3D_SHAPE_SPHERE || sa == PH3D_SHAPE_CAPSULE) &&
        (sb == PH3D_SHAPE_SPHERE || sb == PH3D_SHAPE_CAPSULE)) {
        rt_body3d tmp_a, tmp_b;
        const rt_body3d *sphere_a = a;
        const rt_body3d *sphere_b = b;

        if (sa == PH3D_SHAPE_CAPSULE) {
            double center[3];
            if (sb == PH3D_SHAPE_CAPSULE) {
                double other_center[3];
                closest_points_capsule_axes(a, b, center, other_center);
                make_temp_sphere(&tmp_a, center, a->radius);
                make_temp_sphere(&tmp_b, other_center, b->radius);
                sphere_a = &tmp_a;
                sphere_b = &tmp_b;
                return test_sphere_sphere(sphere_a, sphere_b, normal, depth);
            }
            vec3_copy(center, b->position);
            closest_point_capsule_axis_to_point(a, center, center);
            make_temp_sphere(&tmp_a, center, a->radius);
            sphere_a = &tmp_a;
        }

        if (sb == PH3D_SHAPE_CAPSULE) {
            double center[3];
            vec3_copy(center, a->position);
            closest_point_capsule_axis_to_point(b, center, center);
            make_temp_sphere(&tmp_b, center, b->radius);
            sphere_b = &tmp_b;
        }
        return test_sphere_sphere(sphere_a, sphere_b, normal, depth);
    }

    /* AABB-sphere (order: A=AABB, B=sphere) */
    if (sa == PH3D_SHAPE_AABB && (sb == PH3D_SHAPE_SPHERE || sb == PH3D_SHAPE_CAPSULE)) {
        if (sb == PH3D_SHAPE_CAPSULE) {
            double center[3];
            rt_body3d tmp_sphere;
            closest_point_capsule_axis_to_aabb(b, a, center);
            make_temp_sphere(&tmp_sphere, center, b->radius);
            return test_aabb_sphere(a, &tmp_sphere, normal, depth);
        }
        return test_aabb_sphere(a, b, normal, depth);
    }

    /* Sphere-AABB (reversed — flip normal) */
    if ((sa == PH3D_SHAPE_SPHERE || sa == PH3D_SHAPE_CAPSULE) && sb == PH3D_SHAPE_AABB) {
        int hit;
        if (sa == PH3D_SHAPE_CAPSULE) {
            double center[3];
            rt_body3d tmp_sphere;
            closest_point_capsule_axis_to_aabb(a, b, center);
            make_temp_sphere(&tmp_sphere, center, a->radius);
            hit = test_aabb_sphere(b, &tmp_sphere, normal, depth);
        } else {
            hit = test_aabb_sphere(b, a, normal, depth);
        }
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    /* AABB-AABB fallback */
    return test_aabb_aabb(a, b, normal, depth);
}

/// @brief Generic AABB-vs-AABB overlap with explicit centers (no body required).
///
/// Same SAT logic as `test_aabb_aabb` but takes raw extents and centers
/// so it can be used for compound child colliders that don't have a
/// backing body.
static int test_bounds_overlap(const double *amn,
                               const double *amx,
                               const double *a_center,
                               const double *bmn,
                               const double *bmx,
                               const double *b_center,
                               double *normal,
                               double *depth) {
    double ox = (amx[0] < bmx[0] ? amx[0] - bmn[0] : bmx[0] - amn[0]);
    double oy = (amx[1] < bmx[1] ? amx[1] - bmn[1] : bmx[1] - amn[1]);
    double oz = (amx[2] < bmx[2] ? amx[2] - bmn[2] : bmx[2] - amn[2]);
    if (ox <= 0.0 || oy <= 0.0 || oz <= 0.0)
        return 0;
    normal[0] = normal[1] = normal[2] = 0.0;
    if (ox <= oy && ox <= oz) {
        *depth = ox;
        normal[0] = (a_center[0] < b_center[0]) ? 1.0 : -1.0;
    } else if (oy <= oz) {
        *depth = oy;
        normal[1] = (a_center[1] < b_center[1]) ? 1.0 : -1.0;
    } else {
        *depth = oz;
        normal[2] = (a_center[2] < b_center[2]) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Project point `p` onto triangle `(a, b, c)`, writing the closest point.
///
/// Implements the standard Voronoi-region algorithm from Real-Time
/// Collision Detection (Ericson §5.1.5): tests whether `p` falls into
/// the vertex, edge, or interior region and clamps accordingly.
static void closest_point_on_triangle(const double *p,
                                      const double *a,
                                      const double *b,
                                      const double *c,
                                      double *closest) {
    double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    double d1 = vec3_dot(ab, ap);
    double d2 = vec3_dot(ac, ap);
    double d3, d4, d5, d6;
    if (d1 <= 0.0 && d2 <= 0.0) {
        vec3_copy(closest, a);
        return;
    }

    {
        double bp[3] = {p[0] - b[0], p[1] - b[1], p[2] - b[2]};
        d3 = vec3_dot(ab, bp);
        d4 = vec3_dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3) {
            vec3_copy(closest, b);
            return;
        }
        {
            double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                double v = d1 / (d1 - d3);
                closest[0] = a[0] + ab[0] * v;
                closest[1] = a[1] + ab[1] * v;
                closest[2] = a[2] + ab[2] * v;
                return;
            }
        }
    }

    {
        double cp[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
        d5 = vec3_dot(ab, cp);
        d6 = vec3_dot(ac, cp);
        if (d6 >= 0.0 && d5 <= d6) {
            vec3_copy(closest, c);
            return;
        }
        {
            double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                double w = d2 / (d2 - d6);
                closest[0] = a[0] + ac[0] * w;
                closest[1] = a[1] + ac[1] * w;
                closest[2] = a[2] + ac[2] * w;
                return;
            }
        }
        {
            double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
                double bc[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]};
                double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                closest[0] = b[0] + bc[0] * w;
                closest[1] = b[1] + bc[1] * w;
                closest[2] = b[2] + bc[2] * w;
                return;
            }
        }
    }

    {
        double denom = 1.0 / ((vec3_dot(ab, ab) * vec3_dot(ac, ac)) - vec3_dot(ab, ac) * vec3_dot(ab, ac));
        double dot_ap_ab = vec3_dot(ap, ab);
        double dot_ap_ac = vec3_dot(ap, ac);
        double dot_ab_ab = vec3_dot(ab, ab);
        double dot_ab_ac = vec3_dot(ab, ac);
        double dot_ac_ac = vec3_dot(ac, ac);
        double v = (dot_ac_ac * dot_ap_ab - dot_ab_ac * dot_ap_ac) * denom;
        double w = (dot_ab_ab * dot_ap_ac - dot_ab_ac * dot_ap_ab) * denom;
        closest[0] = a[0] + ab[0] * v + ac[0] * w;
        closest[1] = a[1] + ab[1] * v + ac[1] * w;
        closest[2] = a[2] + ab[2] * v + ac[2] * w;
    }
}

/// @brief Compute a unit normal from three triangle vertices via cross product.
///
/// Falls back to +Y when the triangle is degenerate (zero area). The
/// orientation depends on vertex order — caller may flip if needed.
static void triangle_normal(const double *a, const double *b, const double *c, double *normal) {
    double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    normal[0] = ab[1] * ac[2] - ab[2] * ac[1];
    normal[1] = ab[2] * ac[0] - ab[0] * ac[2];
    normal[2] = ab[0] * ac[1] - ab[1] * ac[0];
    {
        double len = vec3_len(normal);
        if (len > 1e-12) {
            normal[0] /= len;
            normal[1] /= len;
            normal[2] /= len;
        } else {
            normal[0] = 0.0;
            normal[1] = 1.0;
            normal[2] = 0.0;
        }
    }
}

/// @brief Sphere-vs-mesh narrow phase by per-triangle closest-point test.
///
/// Iterates every triangle of `mesh`, transforms its vertices into world
/// space (TODO: this could be cached), and uses
/// `closest_point_on_triangle` + sphere distance. Picks the deepest
/// penetration as the contact. O(n) in triangle count — fine for the
/// kind of low-poly collision meshes typical in games; for higher-poly
/// meshes a BVH would be a future addition.
static int test_meshlike_sphere(rt_mesh3d *mesh,
                                const rt_collider_pose *mesh_pose,
                                const rt_body3d *sphere,
                                double *normal,
                                double *depth) {
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    if (!mesh || !mesh_pose || !sphere || mesh->index_count < 3)
        return 0;
    rt_mesh3d_refresh_bounds(mesh);
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], closest[3];
        double dx, dy, dz, dist_sq;
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        {
            double local[3];
            local[0] = mesh->vertices[i0].pos[0];
            local[1] = mesh->vertices[i0].pos[1];
            local[2] = mesh->vertices[i0].pos[2];
            transform_point_from_pose(mesh_pose, local, a);
            local[0] = mesh->vertices[i1].pos[0];
            local[1] = mesh->vertices[i1].pos[1];
            local[2] = mesh->vertices[i1].pos[2];
            transform_point_from_pose(mesh_pose, local, b);
            local[0] = mesh->vertices[i2].pos[0];
            local[1] = mesh->vertices[i2].pos[1];
            local[2] = mesh->vertices[i2].pos[2];
            transform_point_from_pose(mesh_pose, local, c);
        }
        closest_point_on_triangle(sphere->position, a, b, c, closest);
        dx = sphere->position[0] - closest[0];
        dy = sphere->position[1] - closest[1];
        dz = sphere->position[2] - closest[2];
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq >= sphere->radius * sphere->radius)
            continue;
        {
            double dist = sqrt(dist_sq);
            double cur_depth = sphere->radius - dist;
            double cur_normal[3];
            if (dist > 1e-12) {
                cur_normal[0] = dx / dist;
                cur_normal[1] = dy / dist;
                cur_normal[2] = dz / dist;
            } else {
                double centroid[3] = {(a[0] + b[0] + c[0]) / 3.0,
                                      (a[1] + b[1] + c[1]) / 3.0,
                                      (a[2] + b[2] + c[2]) / 3.0};
                triangle_normal(a, b, c, cur_normal);
                if ((sphere->position[0] - centroid[0]) * cur_normal[0] +
                        (sphere->position[1] - centroid[1]) * cur_normal[1] +
                        (sphere->position[2] - centroid[2]) * cur_normal[2] <
                    0.0) {
                    cur_normal[0] = -cur_normal[0];
                    cur_normal[1] = -cur_normal[1];
                    cur_normal[2] = -cur_normal[2];
                }
            }
            if (cur_depth > best_depth) {
                best_depth = cur_depth;
                vec3_copy(best_normal, cur_normal);
            }
        }
    }
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Capsule-vs-mesh narrow phase via 5 sphere samples along the capsule axis.
///
/// Fakes capsule support by sampling 5 spheres uniformly along the
/// capsule's axis segment and running each through `test_meshlike_sphere`.
/// Picks the deepest penetration. Cheaper than a true capsule-mesh
/// test and good enough for character collision against level geometry.
static int test_meshlike_capsule(rt_mesh3d *mesh,
                                 const rt_collider_pose *mesh_pose,
                                 const rt_body3d *capsule,
                                 double *normal,
                                 double *depth) {
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int samples = half_axis > 1e-9 ? 5 : 1;
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        memset(&sphere, 0, sizeof(sphere));
        sphere.shape = PH3D_SHAPE_SPHERE;
        sphere.position[0] = capsule->position[0];
        sphere.position[1] = capsule->position[1] - half_axis + 2.0 * half_axis * t;
        sphere.position[2] = capsule->position[2];
        sphere.radius = capsule->radius;
        {
            double cur_normal[3];
            double cur_depth;
            if (test_meshlike_sphere(mesh, mesh_pose, &sphere, cur_normal, &cur_depth) &&
                cur_depth > best_depth) {
                best_depth = cur_depth;
                vec3_copy(best_normal, cur_normal);
            }
        }
    }
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Sphere-vs-heightfield narrow phase.
///
/// Transforms the sphere center into local heightfield space, samples
/// the field at (x, z) to get surface height + local normal, and tests
/// whether the sphere bottom is below the surface. Skips the cost of
/// per-cell triangle intersection by using the heightfield's analytical
/// normal directly.
static int test_heightfield_sphere(void *heightfield,
                                   const rt_collider_pose *field_pose,
                                   const rt_body3d *sphere,
                                   double *normal,
                                   double *depth) {
    double local_center[3];
    double surface_height = 0.0;
    double local_normal[3] = {0.0, 1.0, 0.0};
    double penetration;
    transform_point_to_local(field_pose, sphere->position, local_center);
    if (!rt_collider3d_sample_heightfield_raw(
            heightfield, local_center[0], local_center[2], &surface_height, local_normal))
        return 0;
    penetration = surface_height - (local_center[1] - sphere->radius);
    if (penetration <= 0.0)
        return 0;
    quat_rotate_vec3(field_pose->rotation, local_normal, normal);
    {
        double len = vec3_len(normal);
        if (len > 1e-12) {
            normal[0] /= len;
            normal[1] /= len;
            normal[2] /= len;
        } else {
            normal[0] = 0.0;
            normal[1] = 1.0;
            normal[2] = 0.0;
        }
    }
    *depth = penetration * fabs(field_pose->scale[1] > 1e-12 ? field_pose->scale[1] : 1.0);
    return 1;
}

/// @brief Capsule-vs-heightfield narrow phase.
///
/// Only the bottom-end sphere of the capsule's axis is sampled — for
/// gravity-aligned capsule characters that's the contact point of
/// interest. A more accurate version would sample multiple points
/// along the axis as `test_meshlike_capsule` does.
static int test_heightfield_capsule(void *heightfield,
                                    const rt_collider_pose *field_pose,
                                    const rt_body3d *capsule,
                                    double *normal,
                                    double *depth) {
    rt_body3d sphere;
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    memset(&sphere, 0, sizeof(sphere));
    sphere.shape = PH3D_SHAPE_SPHERE;
    sphere.position[0] = capsule->position[0];
    sphere.position[1] = capsule->position[1] - half_axis;
    sphere.position[2] = capsule->position[2];
    sphere.radius = capsule->radius;
    return test_heightfield_sphere(heightfield, field_pose, &sphere, normal, depth);
}

/// @brief Box-vs-heightfield narrow phase via 5-sample bottom probe.
///
/// Samples the heightfield at the box's four bottom corners and its
/// center; the deepest penetration wins. Approximate but cheap;
/// matches the resolution of the heightfield grid in practice.
static int test_heightfield_box(void *heightfield,
                                const rt_collider_pose *field_pose,
                                const rt_body3d *box,
                                double *normal,
                                double *depth) {
    double mn[3], mx[3];
    double samples[5][3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    body_aabb(box, mn, mx);
    vec3_set(samples[0], mn[0], mn[1], mn[2]);
    vec3_set(samples[1], mn[0], mn[1], mx[2]);
    vec3_set(samples[2], mx[0], mn[1], mn[2]);
    vec3_set(samples[3], mx[0], mn[1], mx[2]);
    vec3_set(samples[4], (mn[0] + mx[0]) * 0.5, mn[1], (mn[2] + mx[2]) * 0.5);
    for (int i = 0; i < 5; ++i) {
        double local_point[3];
        double surface_height = 0.0;
        double local_normal[3] = {0.0, 1.0, 0.0};
        transform_point_to_local(field_pose, samples[i], local_point);
        if (!rt_collider3d_sample_heightfield_raw(
                heightfield, local_point[0], local_point[2], &surface_height, local_normal))
            continue;
        {
            double cur_depth = surface_height - local_point[1];
            if (cur_depth > best_depth) {
                best_depth = cur_depth;
                quat_rotate_vec3(field_pose->rotation, local_normal, best_normal);
            }
        }
    }
    if (best_depth <= 0.0)
        return 0;
    {
        double len = vec3_len(best_normal);
        if (len > 1e-12) {
            best_normal[0] /= len;
            best_normal[1] /= len;
            best_normal[2] /= len;
        } else {
            best_normal[0] = 0.0;
            best_normal[1] = 1.0;
            best_normal[2] = 0.0;
        }
    }
    vec3_copy(normal, best_normal);
    *depth = best_depth * fabs(field_pose->scale[1] > 1e-12 ? field_pose->scale[1] : 1.0);
    return 1;
}

/// @brief Recursive collider-vs-collider dispatcher with leaf identification.
///
/// The "real" collision routine — handles every combination of:
///   - compound × anything (recurse into children)
///   - simple × simple (route via `test_simple_collision`)
///   - convex hull / mesh × simple (route via `test_meshlike_*`)
///   - heightfield × simple (route via `test_heightfield_*`)
///   - everything else falls back to AABB-vs-AABB.
///
/// Outputs the leaf colliders that actually touched (so the higher-level
/// contact event can carry the precise child collider, not just the
/// outer compound). Reverses the normal when it has to swap A and B
/// for symmetric dispatch.
static int test_collider_pair(const rt_body3d *a_body,
                              void *a_collider,
                              const rt_collider_pose *a_pose,
                              const rt_body3d *b_body,
                              void *b_collider,
                              const rt_collider_pose *b_pose,
                              double *normal,
                              double *depth,
                              void **leaf_a_out,
                              rt_collider_pose *leaf_a_pose_out,
                              void **leaf_b_out,
                              rt_collider_pose *leaf_b_pose_out) {
    double amn[3], amx[3], bmn[3], bmx[3];
    double a_center[3], b_center[3];
    int64_t a_type;
    int64_t b_type;
    if (!a_collider || !b_collider)
        return 0;

    rt_collider3d_compute_world_aabb_raw(
        a_collider, a_pose->position, a_pose->rotation, a_pose->scale, amn, amx);
    rt_collider3d_compute_world_aabb_raw(
        b_collider, b_pose->position, b_pose->rotation, b_pose->scale, bmn, bmx);
    if (amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
        amn[2] > bmx[2] || amx[2] < bmn[2])
        return 0;
    a_center[0] = (amn[0] + amx[0]) * 0.5;
    a_center[1] = (amn[1] + amx[1]) * 0.5;
    a_center[2] = (amn[2] + amx[2]) * 0.5;
    b_center[0] = (bmn[0] + bmx[0]) * 0.5;
    b_center[1] = (bmn[1] + bmx[1]) * 0.5;
    b_center[2] = (bmn[2] + bmx[2]) * 0.5;

    a_type = rt_collider3d_get_type(a_collider);
    b_type = rt_collider3d_get_type(b_collider);

    if (a_type == RT_COLLIDER3D_TYPE_COMPOUND) {
        double best_depth = 0.0;
        double best_normal[3] = {0.0, 1.0, 0.0};
        int hit = 0;
        int64_t child_count = rt_collider3d_get_child_count_raw(a_collider);
        for (int64_t i = 0; i < child_count; ++i) {
            void *child = rt_collider3d_get_child_raw(a_collider, i);
            double child_pos[3], child_rot[4], child_scale[3];
            rt_collider_pose child_pose;
            rt_collider3d_get_child_transform_raw(
                a_collider, i, child_pos, child_rot, child_scale);
            collider_pose_compose(a_pose, child_pos, child_rot, child_scale, &child_pose);
            {
                double cur_normal[3];
                double cur_depth = 0.0;
                void *cur_leaf_a = NULL;
                void *cur_leaf_b = NULL;
                rt_collider_pose cur_leaf_a_pose;
                rt_collider_pose cur_leaf_b_pose;
                if (test_collider_pair(
                        a_body,
                        child,
                        &child_pose,
                        b_body,
                        b_collider,
                        b_pose,
                        cur_normal,
                        &cur_depth,
                        &cur_leaf_a,
                        &cur_leaf_a_pose,
                        &cur_leaf_b,
                        &cur_leaf_b_pose) &&
                    cur_depth > best_depth) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
                    if (leaf_a_out)
                        *leaf_a_out = cur_leaf_a;
                    if (leaf_a_pose_out)
                        *leaf_a_pose_out = cur_leaf_a_pose;
                    if (leaf_b_out)
                        *leaf_b_out = cur_leaf_b;
                    if (leaf_b_pose_out)
                        *leaf_b_pose_out = cur_leaf_b_pose;
                    hit = 1;
                }
            }
        }
        if (hit) {
            vec3_copy(normal, best_normal);
            *depth = best_depth;
        }
        return hit;
    }

    if (b_type == RT_COLLIDER3D_TYPE_COMPOUND) {
        double best_depth = 0.0;
        double best_normal[3] = {0.0, 1.0, 0.0};
        int hit = 0;
        int64_t child_count = rt_collider3d_get_child_count_raw(b_collider);
        for (int64_t i = 0; i < child_count; ++i) {
            void *child = rt_collider3d_get_child_raw(b_collider, i);
            double child_pos[3], child_rot[4], child_scale[3];
            rt_collider_pose child_pose;
            rt_collider3d_get_child_transform_raw(
                b_collider, i, child_pos, child_rot, child_scale);
            collider_pose_compose(b_pose, child_pos, child_rot, child_scale, &child_pose);
            {
                double cur_normal[3];
                double cur_depth = 0.0;
                void *cur_leaf_a = NULL;
                void *cur_leaf_b = NULL;
                rt_collider_pose cur_leaf_a_pose;
                rt_collider_pose cur_leaf_b_pose;
                if (test_collider_pair(
                        a_body,
                        a_collider,
                        a_pose,
                        b_body,
                        child,
                        &child_pose,
                        cur_normal,
                        &cur_depth,
                        &cur_leaf_a,
                        &cur_leaf_a_pose,
                        &cur_leaf_b,
                        &cur_leaf_b_pose) &&
                    cur_depth > best_depth) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
                    if (leaf_a_out)
                        *leaf_a_out = cur_leaf_a;
                    if (leaf_a_pose_out)
                        *leaf_a_pose_out = cur_leaf_a_pose;
                    if (leaf_b_out)
                        *leaf_b_out = cur_leaf_b;
                    if (leaf_b_pose_out)
                        *leaf_b_pose_out = cur_leaf_b_pose;
                    hit = 1;
                }
            }
        }
        if (hit) {
            vec3_copy(normal, best_normal);
            *depth = best_depth;
        }
        return hit;
    }

    if (collider_type_is_simple(a_type) && collider_type_is_simple(b_type)) {
        rt_body3d proxy_a, proxy_b;
        if (!build_simple_proxy(a_pose, a_collider, &proxy_a) ||
            !build_simple_proxy(b_pose, b_collider, &proxy_b))
            return 0;
        if (!test_simple_collision(&proxy_a, &proxy_b, normal, depth))
            return 0;
        if (leaf_a_out)
            *leaf_a_out = a_collider;
        if (leaf_a_pose_out)
            *leaf_a_pose_out = *a_pose;
        if (leaf_b_out)
            *leaf_b_out = b_collider;
        if (leaf_b_pose_out)
            *leaf_b_pose_out = *b_pose;
        return 1;
    }

    if ((a_type == RT_COLLIDER3D_TYPE_CONVEX_HULL || a_type == RT_COLLIDER3D_TYPE_MESH) &&
        collider_type_is_simple(b_type)) {
        rt_body3d proxy_b;
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(a_collider);
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b) || !mesh)
            return 0;
        if ((proxy_b.shape == PH3D_SHAPE_SPHERE &&
             !test_meshlike_sphere(mesh, a_pose, &proxy_b, normal, depth)) ||
            (proxy_b.shape == PH3D_SHAPE_CAPSULE &&
             !test_meshlike_capsule(mesh, a_pose, &proxy_b, normal, depth)) ||
            ((proxy_b.shape != PH3D_SHAPE_SPHERE && proxy_b.shape != PH3D_SHAPE_CAPSULE) &&
             !test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth))) {
            return 0;
        }
        if (leaf_a_out)
            *leaf_a_out = a_collider;
        if (leaf_a_pose_out)
            *leaf_a_pose_out = *a_pose;
        if (leaf_b_out)
            *leaf_b_out = b_collider;
        if (leaf_b_pose_out)
            *leaf_b_pose_out = *b_pose;
        return 1;
    }

    if (collider_type_is_simple(a_type) &&
        (b_type == RT_COLLIDER3D_TYPE_CONVEX_HULL || b_type == RT_COLLIDER3D_TYPE_MESH)) {
        int hit;
        hit = test_collider_pair(
            b_body,
            b_collider,
            b_pose,
            a_body,
            a_collider,
            a_pose,
            normal,
            depth,
            leaf_b_out,
            leaf_b_pose_out,
            leaf_a_out,
            leaf_a_pose_out);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    if (a_type == RT_COLLIDER3D_TYPE_HEIGHTFIELD && collider_type_is_simple(b_type)) {
        rt_body3d proxy_b;
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b))
            return 0;
        if ((proxy_b.shape == PH3D_SHAPE_SPHERE &&
             !test_heightfield_sphere(a_collider, a_pose, &proxy_b, normal, depth)) ||
            (proxy_b.shape == PH3D_SHAPE_CAPSULE &&
             !test_heightfield_capsule(a_collider, a_pose, &proxy_b, normal, depth)) ||
            ((proxy_b.shape != PH3D_SHAPE_SPHERE && proxy_b.shape != PH3D_SHAPE_CAPSULE) &&
             !test_heightfield_box(a_collider, a_pose, &proxy_b, normal, depth))) {
            return 0;
        }
        if (leaf_a_out)
            *leaf_a_out = a_collider;
        if (leaf_a_pose_out)
            *leaf_a_pose_out = *a_pose;
        if (leaf_b_out)
            *leaf_b_out = b_collider;
        if (leaf_b_pose_out)
            *leaf_b_pose_out = *b_pose;
        return 1;
    }

    if (collider_type_is_simple(a_type) && b_type == RT_COLLIDER3D_TYPE_HEIGHTFIELD) {
        int hit;
        hit = test_collider_pair(
            b_body,
            b_collider,
            b_pose,
            a_body,
            a_collider,
            a_pose,
            normal,
            depth,
            leaf_b_out,
            leaf_b_pose_out,
            leaf_a_out,
            leaf_a_pose_out);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    if (!test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth))
        return 0;
    if (leaf_a_out)
        *leaf_a_out = a_collider;
    if (leaf_a_pose_out)
        *leaf_a_pose_out = *a_pose;
    if (leaf_b_out)
        *leaf_b_out = b_collider;
    if (leaf_b_pose_out)
        *leaf_b_pose_out = *b_pose;
    return 1;
}

/// @brief Top-level body-vs-body collision test, including contact point.
///
/// Builds collider poses, delegates to `test_collider_pair` for the
/// actual recursion, then reconstructs an estimated contact point via
/// `compute_contact_point_from_leafs`. Outputs the leaf colliders that
/// touched, so contact events can carry sub-collider identity.
static int test_collision(const rt_body3d *a,
                          const rt_body3d *b,
                          double *normal,
                          double *depth,
                          double *point,
                          void **leaf_a_out,
                          void **leaf_b_out) {
    rt_collider_pose a_pose;
    rt_collider_pose b_pose;
    rt_collider_pose leaf_a_pose;
    rt_collider_pose leaf_b_pose;
    void *leaf_a = NULL;
    void *leaf_b = NULL;
    if (!a || !b || !a->collider || !b->collider)
        return 0;
    collider_pose_from_body(a, &a_pose);
    collider_pose_from_body(b, &b_pose);
    if (!test_collider_pair(a,
                            a->collider,
                            &a_pose,
                            b,
                            b->collider,
                            &b_pose,
                            normal,
                            depth,
                            &leaf_a,
                            &leaf_a_pose,
                            &leaf_b,
                            &leaf_b_pose))
        return 0;
    if (leaf_a_out)
        *leaf_a_out = leaf_a;
    if (leaf_b_out)
        *leaf_b_out = leaf_b;
    if (point)
        compute_contact_point_from_leafs(leaf_a, &leaf_a_pose, leaf_b, &leaf_b_pose, normal, point);
    return 1;
}

/// @brief Apply impulse-based collision response between two bodies.
///
/// Standard rigid-body impulse: `j = -(1+e)·rv·(1/(m_a+m_b))` along the
/// contact normal, plus a tangential Coulomb friction impulse capped at
/// `μ·j_normal`. Then a Baumgarte positional correction (40% of excess
/// penetration past 1% slop) bleeds drift back to zero. Wakes both
/// bodies if either impulse component is significant. Returns the
/// magnitude of the normal impulse (used by `relative_speed_out` and
/// the impulse-as-event payload).
static double resolve_collision(rt_body3d *a,
                                rt_body3d *b,
                                const double *n,
                                double depth,
                                double *relative_speed_out) {
    double rv = (b->velocity[0] - a->velocity[0]) * n[0] +
                (b->velocity[1] - a->velocity[1]) * n[1] + (b->velocity[2] - a->velocity[2]) * n[2];
    if (relative_speed_out)
        *relative_speed_out = fabs(rv);
    if (rv > 0)
        return 0.0; /* separating */

    double e = (a->restitution < b->restitution) ? a->restitution : b->restitution;
    double inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum < 1e-12)
        return 0.0;

    double j = -(1.0 + e) * rv / inv_sum;
    int woke = 0;

    a->velocity[0] -= j * a->inv_mass * n[0];
    a->velocity[1] -= j * a->inv_mass * n[1];
    a->velocity[2] -= j * a->inv_mass * n[2];
    b->velocity[0] += j * b->inv_mass * n[0];
    b->velocity[1] += j * b->inv_mass * n[1];
    b->velocity[2] += j * b->inv_mass * n[2];

    /* Coulomb friction — tangential impulse capped by mu * normal impulse */
    {
        double mu = sqrt(a->friction * b->friction);
        double rvx = b->velocity[0] - a->velocity[0];
        double rvy = b->velocity[1] - a->velocity[1];
        double rvz = b->velocity[2] - a->velocity[2];
        double rv_n = rvx * n[0] + rvy * n[1] + rvz * n[2];
        double tx = rvx - rv_n * n[0];
        double ty = rvy - rv_n * n[1];
        double tz = rvz - rv_n * n[2];
        double tlen = sqrt(tx * tx + ty * ty + tz * tz);
        if (tlen > 1e-8) {
            tx /= tlen;
            ty /= tlen;
            tz /= tlen;
            double jt = -(rvx * tx + rvy * ty + rvz * tz) / inv_sum;
            if (fabs(jt) > mu * j)
                jt = (jt > 0 ? 1.0 : -1.0) * mu * j;
            a->velocity[0] -= jt * a->inv_mass * tx;
            a->velocity[1] -= jt * a->inv_mass * ty;
            a->velocity[2] -= jt * a->inv_mass * tz;
            b->velocity[0] += jt * b->inv_mass * tx;
            b->velocity[1] += jt * b->inv_mass * ty;
            b->velocity[2] += jt * b->inv_mass * tz;
            if (fabs(jt) > 1e-8)
                woke = 1;
        }
    }

    /* Baumgarte positional correction (40%, 1% slop) */
    double slop = 0.01;
    double correction = fmax(depth - slop, 0.0) * 0.4 / inv_sum;
    a->position[0] -= correction * a->inv_mass * n[0];
    a->position[1] -= correction * a->inv_mass * n[1];
    a->position[2] -= correction * a->inv_mass * n[2];
    b->position[0] += correction * b->inv_mass * n[0];
    b->position[1] += correction * b->inv_mass * n[1];
    b->position[2] += correction * b->inv_mass * n[2];

    if (fabs(j) > 1e-8 || woke) {
        body3d_wake_if_dynamic(a);
        body3d_wake_if_dynamic(b);
    }
    return fabs(j);
}

// GC finalizers for the boxed query-result objects exposed to Zia.
// Each releases the retained body / collider references so dropping
// the result frees the underlying physics objects when no other
// holder remains.

/// @brief GC finalizer for `PhysicsHit3D` — release referenced body/collider.
static void physics_hit3d_finalizer(void *obj) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)obj;
    if (!hit)
        return;
    if (hit->body && rt_obj_release_check0(hit->body))
        rt_obj_free(hit->body);
    if (hit->collider && rt_obj_release_check0(hit->collider))
        rt_obj_free(hit->collider);
    hit->body = NULL;
    hit->collider = NULL;
}

/// @brief GC finalizer for `PhysicsHitList3D` — release each item then the array.
static void physics_hit_list3d_finalizer(void *obj) {
    rt_physics_hit_list3d_obj *list = (rt_physics_hit_list3d_obj *)obj;
    if (!list)
        return;
    if (list->items) {
        for (int64_t i = 0; i < list->count; ++i) {
            if (list->items[i] && rt_obj_release_check0(list->items[i]))
                rt_obj_free(list->items[i]);
        }
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
}

/// @brief GC finalizer for `CollisionEvent3D` — release both bodies and both colliders.
static void collision_event3d_finalizer(void *obj) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    if (!event)
        return;
    if (event->body_a && rt_obj_release_check0(event->body_a))
        rt_obj_free(event->body_a);
    if (event->body_b && rt_obj_release_check0(event->body_b))
        rt_obj_free(event->body_b);
    if (event->collider_a && rt_obj_release_check0(event->collider_a))
        rt_obj_free(event->collider_a);
    if (event->collider_b && rt_obj_release_check0(event->collider_b))
        rt_obj_free(event->collider_b);
    event->body_a = NULL;
    event->body_b = NULL;
    event->collider_a = NULL;
    event->collider_b = NULL;
}

/// @brief Box a transient `rt_query_hit3d` into a GC-managed `PhysicsHit3D`.
///
/// Copies all hit fields and retains the body/collider pointers so the
/// hit object can outlive the originating query call.
static void *physics_hit3d_new(const rt_query_hit3d *src) {
    rt_physics_hit3d_obj *hit =
        (rt_physics_hit3d_obj *)rt_obj_new_i64(0, (int64_t)sizeof(rt_physics_hit3d_obj));
    if (!hit) {
        rt_trap("PhysicsHit3D: allocation failed");
        return NULL;
    }
    memset(hit, 0, sizeof(*hit));
    if (src) {
        hit->body = src->body;
        hit->collider = src->collider;
        vec3_copy(hit->point, src->point);
        vec3_copy(hit->normal, src->normal);
        hit->distance = src->distance;
        hit->fraction = src->fraction;
        hit->started_penetrating = src->started_penetrating;
        hit->is_trigger = src->is_trigger;
        if (hit->body)
            rt_obj_retain_maybe(hit->body);
        if (hit->collider)
            rt_obj_retain_maybe(hit->collider);
    }
    rt_obj_set_finalizer(hit, physics_hit3d_finalizer);
    return hit;
}

/// @brief Build a `PhysicsHitList3D` from a transient hit array.
///
/// Allocates an items array of length `count`, boxes each hit via
/// `physics_hit3d_new`, and returns the GC-managed wrapper. Returns
/// NULL when `count <= 0` so callers can pass empty results back as
/// a NULL Zia value.
static void *physics_hit_list3d_new(const rt_query_hit3d *hits, int32_t count) {
    rt_physics_hit_list3d_obj *list;
    if (count <= 0)
        return NULL;
    list = (rt_physics_hit_list3d_obj *)rt_obj_new_i64(0, (int64_t)sizeof(*list));
    if (!list) {
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    memset(list, 0, sizeof(*list));
    list->items = (void **)calloc((size_t)count, sizeof(void *));
    if (!list->items) {
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    list->count = count;
    for (int32_t i = 0; i < count; ++i) {
        list->items[i] = physics_hit3d_new(&hits[i]);
    }
    rt_obj_set_finalizer(list, physics_hit_list3d_finalizer);
    return list;
}

/// @brief Box a contact's geometric data into a `ContactPoint3D` for Zia.
///
/// Only point/normal/separation are exposed — `body_*` and impulse
/// belong to the higher-level `CollisionEvent3D`.
static void *contact_point3d_new_from_contact(const rt_contact3d *contact) {
    rt_contact_point3d_obj *point;
    if (!contact)
        return NULL;
    point = (rt_contact_point3d_obj *)rt_obj_new_i64(0, (int64_t)sizeof(*point));
    if (!point) {
        rt_trap("ContactPoint3D: allocation failed");
        return NULL;
    }
    memset(point, 0, sizeof(*point));
    vec3_copy(point->point, contact->point);
    vec3_copy(point->normal, contact->normal);
    point->separation = contact->separation;
    return point;
}

/// @brief Box a contact into a `CollisionEvent3D` for OnEnter/Stay/Exit dispatch.
///
/// Includes both bodies, both colliders, contact geometry, relative
/// speed, and the resolved normal impulse so Zia handlers can branch
/// on collision strength, identify the colliding parties, etc. Retains
/// every reference so the event survives past the world step.
static void *collision_event3d_new_from_contact(const rt_contact3d *contact) {
    rt_collision_event3d_obj *event;
    if (!contact)
        return NULL;
    event = (rt_collision_event3d_obj *)rt_obj_new_i64(0, (int64_t)sizeof(*event));
    if (!event) {
        rt_trap("CollisionEvent3D: allocation failed");
        return NULL;
    }
    memset(event, 0, sizeof(*event));
    event->body_a = contact->body_a;
    event->body_b = contact->body_b;
    event->collider_a = contact->collider_a;
    event->collider_b = contact->collider_b;
    vec3_copy(event->point, contact->point);
    vec3_copy(event->normal, contact->normal);
    event->separation = contact->separation;
    event->relative_speed = contact->relative_speed;
    event->normal_impulse = contact->normal_impulse;
    event->is_trigger = contact->is_trigger;
    if (event->body_a)
        rt_obj_retain_maybe(event->body_a);
    if (event->body_b)
        rt_obj_retain_maybe(event->body_b);
    if (event->collider_a)
        rt_obj_retain_maybe(event->collider_a);
    if (event->collider_b)
        rt_obj_retain_maybe(event->collider_b);
    rt_obj_set_finalizer(event, collision_event3d_finalizer);
    return event;
}

/// @brief Diff this frame's contacts against the previous frame to fire enter/stay/exit events.
///
/// For each contact that exists this frame:
///   - in previous → `stay`
///   - not in previous → `enter`
/// For each previous contact not present now → `exit`. Then snapshot
/// this frame's contacts as the new "previous" for the next call. The
/// 128-event cap silently truncates extreme cases (consistent with the
/// other PH3D arrays).
static void world3d_build_event_buffers(rt_world3d *w) {
    if (!w)
        return;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = 0;
        for (int32_t j = 0; j < w->previous_contact_count; ++j) {
            if (contact_pair_equals(&w->contacts[i], &w->previous_contacts[j])) {
                found = 1;
                break;
            }
        }
        if (found) {
            if (w->stay_event_count < PH3D_MAX_CONTACTS)
                contact_snapshot_copy(&w->stay_events[w->stay_event_count++], &w->contacts[i]);
        } else {
            if (w->enter_event_count < PH3D_MAX_CONTACTS)
                contact_snapshot_copy(&w->enter_events[w->enter_event_count++], &w->contacts[i]);
        }
    }

    for (int32_t i = 0; i < w->previous_contact_count; ++i) {
        int found = 0;
        for (int32_t j = 0; j < w->contact_count; ++j) {
            if (contact_pair_equals(&w->previous_contacts[i], &w->contacts[j])) {
                found = 1;
                break;
            }
        }
        if (!found && w->exit_event_count < PH3D_MAX_CONTACTS)
            contact_snapshot_copy(&w->exit_events[w->exit_event_count++], &w->previous_contacts[i]);
    }

    w->previous_contact_count = w->contact_count;
    for (int32_t i = 0; i < w->contact_count; ++i)
        contact_snapshot_copy(&w->previous_contacts[i], &w->contacts[i]);
}

/*==========================================================================
 * World3D
 *=========================================================================*/

/// @brief GC finalizer for `World3D` — release every body and joint.
///
/// Walks the body and joint arrays releasing each owned reference. The
/// world has strong refs to its bodies/joints so dropping the world
/// frees the entire simulation state.
static void world3d_finalizer(void *obj) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] && rt_obj_release_check0(w->bodies[i]))
            rt_obj_free(w->bodies[i]);
        w->bodies[i] = NULL;
    }
    for (int32_t i = 0; i < w->joint_count; i++) {
        if (w->joints[i] && rt_obj_release_check0(w->joints[i]))
            rt_obj_free(w->joints[i]);
        w->joints[i] = NULL;
    }
    w->body_count = 0;
    w->joint_count = 0;
}

/// @brief `Physics3D.World.New(gx, gy, gz)` — construct an empty world with gravity.
///
/// Allocates an `rt_world3d`, initializes the gravity vector, and zeros
/// every contact / event / joint table. The world has fixed maximum
/// capacities (256 bodies, 128 joints, 128 contacts). Hooks the GC
/// finalizer so dropping the world frees the simulation.
///
/// @param gx,gy,gz Initial world gravity acceleration (m/s²).
/// @return Owned `World3D` handle.
void *rt_world3d_new(double gx, double gy, double gz) {
    rt_world3d *w = (rt_world3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_world3d));
    if (!w) {
        rt_trap("Physics3D.World.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->gravity[0] = gx;
    w->gravity[1] = gy;
    w->gravity[2] = gz;
    w->body_count = 0;
    w->contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    memset(w->contacts, 0, sizeof(w->contacts));
    memset(w->previous_contacts, 0, sizeof(w->previous_contacts));
    memset(w->enter_events, 0, sizeof(w->enter_events));
    memset(w->stay_events, 0, sizeof(w->stay_events));
    memset(w->exit_events, 0, sizeof(w->exit_events));
    memset(w->joints, 0, sizeof(w->joints));
    rt_obj_set_finalizer(w, world3d_finalizer);
    return w;
}

/// @brief Decide how many CCD substeps to take this frame.
///
/// Walks every CCD-enabled dynamic body, estimates the maximum
/// distance it will move in `dt` (current speed plus one step of
/// gravity-plus-applied-force acceleration), and computes the number
/// of substeps needed to keep each body's per-substep displacement
/// below its `body3d_ccd_threshold` (≈ half the smallest extent).
/// Clamped to `[1, PH3D_MAX_CCD_SUBSTEPS]`.
static int world3d_compute_substeps(const rt_world3d *w, double dt) {
    int substeps = 1;
    if (!w || dt <= 0.0)
        return substeps;
    for (int32_t i = 0; i < w->body_count; i++) {
        const rt_body3d *b = w->bodies[i];
        if (!b || !b->use_ccd || b->motion_mode == PH3D_MODE_STATIC)
            continue;
        double threshold = body3d_ccd_threshold(b);
        if (threshold <= 1e-6)
            continue;
        double speed = vec3_len(b->velocity);
        if (b->motion_mode == PH3D_MODE_DYNAMIC && b->inv_mass > 0.0) {
            double accel[3] = {
                w->gravity[0] + b->force[0] * b->inv_mass,
                w->gravity[1] + b->force[1] * b->inv_mass,
                w->gravity[2] + b->force[2] * b->inv_mass,
            };
            speed += vec3_len(accel) * dt;
        }
        {
            int needed = (int)ceil((speed * dt) / threshold);
            if (needed > substeps)
                substeps = needed;
        }
    }
    if (substeps < 1)
        substeps = 1;
    if (substeps > PH3D_MAX_CCD_SUBSTEPS)
        substeps = PH3D_MAX_CCD_SUBSTEPS;
    return substeps;
}

/// @brief `World3D.Step(dt)` — advance the simulation by one frame.
///
/// Per-substep:
///   1. **Integration**: forces → velocity (symplectic Euler), apply
///      gravity + linear/angular damping, then advance position +
///      orientation. Sleeping bodies skip the step entirely.
///   2. **Collision detection**: O(n²) broad phase, narrow phase via
///      `test_collision`, append contacts, run `resolve_collision` for
///      non-trigger pairs (with a 6-iteration joint solver per substep).
///   3. **Grounded flag** is set when a contact normal points sufficiently
///      upward (Y > 0.7 ≈ 45° slope), used by character controllers.
/// After the substeps, the contact list is diffed against the previous
/// frame to fire enter/stay/exit events, and per-step forces/torques
/// are zeroed for the next frame.
///
/// @param obj `World3D` handle.
/// @param dt  Step duration (seconds). No-op for `dt <= 0`.
void rt_world3d_step(void *obj, double dt) {
    if (!obj || dt <= 0)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    int substeps = world3d_compute_substeps(w, dt);
    double sub_dt = dt / (double)substeps;

    for (int substep = 0; substep < substeps; substep++) {
        /* Phase 1: Integration */
        for (int32_t i = 0; i < w->body_count; i++) {
            rt_body3d *b = w->bodies[i];
            double linear_scale;
            double angular_scale;
            if (!b)
                continue;

            b->is_grounded = 0;

            if (b->motion_mode == PH3D_MODE_STATIC) {
                continue;
            }

            if (b->motion_mode == PH3D_MODE_DYNAMIC) {
                if (b->is_sleeping) {
                    continue;
                }

                b->velocity[0] += (w->gravity[0] + b->force[0] * b->inv_mass) * sub_dt;
                b->velocity[1] += (w->gravity[1] + b->force[1] * b->inv_mass) * sub_dt;
                b->velocity[2] += (w->gravity[2] + b->force[2] * b->inv_mass) * sub_dt;
                b->angular_velocity[0] += b->torque[0] * b->inv_inertia[0] * sub_dt;
                b->angular_velocity[1] += b->torque[1] * b->inv_inertia[1] * sub_dt;
                b->angular_velocity[2] += b->torque[2] * b->inv_inertia[2] * sub_dt;

                linear_scale = fmax(0.0, 1.0 - b->linear_damping * sub_dt);
                angular_scale = fmax(0.0, 1.0 - b->angular_damping * sub_dt);
                vec3_scale_in_place(b->velocity, linear_scale);
                vec3_scale_in_place(b->angular_velocity, angular_scale);

                if (b->can_sleep) {
                    double linear_sq = vec3_len_sq(b->velocity);
                    double angular_sq = vec3_len_sq(b->angular_velocity);
                    double linear_thresh =
                        PH3D_SLEEP_LINEAR_THRESHOLD * PH3D_SLEEP_LINEAR_THRESHOLD;
                    double angular_thresh =
                        PH3D_SLEEP_ANGULAR_THRESHOLD * PH3D_SLEEP_ANGULAR_THRESHOLD;
                    if (linear_sq <= linear_thresh && angular_sq <= angular_thresh) {
                        b->sleep_time += sub_dt;
                        if (b->sleep_time >= PH3D_SLEEP_DELAY) {
                            b->is_sleeping = 1;
                            vec3_set(b->velocity, 0.0, 0.0, 0.0);
                            vec3_set(b->angular_velocity, 0.0, 0.0, 0.0);
                        }
                    } else {
                        b->sleep_time = 0.0;
                    }
                }
            }

            b->position[0] += b->velocity[0] * sub_dt;
            b->position[1] += b->velocity[1] * sub_dt;
            b->position[2] += b->velocity[2] * sub_dt;
            quat_integrate(b->orientation, b->angular_velocity, sub_dt);
        }

        /* Phase 2: Collision detection + response (last substep contacts kept) */
        w->contact_count = 0;
        for (int32_t i = 0; i < w->body_count; i++) {
            for (int32_t j = i + 1; j < w->body_count; j++) {
                rt_body3d *a = w->bodies[i], *b = w->bodies[j];
                if (!a || !b)
                    continue;
                if (a->motion_mode != PH3D_MODE_DYNAMIC && b->motion_mode != PH3D_MODE_DYNAMIC)
                    continue;

                if (!(a->collision_layer & b->collision_mask))
                    continue;
                if (!(b->collision_layer & a->collision_mask))
                    continue;

                {
                    double normal[3], depth, point[3];
                    double relative_speed = 0.0;
                    double normal_impulse = 0.0;
                    void *leaf_a = NULL;
                    void *leaf_b = NULL;
                    if (!test_collision(a, b, normal, &depth, point, &leaf_a, &leaf_b))
                        continue;

                    if (w->contact_count < PH3D_MAX_CONTACTS) {
                        rt_contact3d *c = &w->contacts[w->contact_count++];
                        c->body_a = a;
                        c->body_b = b;
                        c->collider_a = leaf_a ? leaf_a : a->collider;
                        c->collider_b = leaf_b ? leaf_b : b->collider;
                        c->point[0] = point[0];
                        c->point[1] = point[1];
                        c->point[2] = point[2];
                        c->normal[0] = normal[0];
                        c->normal[1] = normal[1];
                        c->normal[2] = normal[2];
                        c->separation = -depth;
                        c->is_trigger = (a->is_trigger || b->is_trigger) ? 1 : 0;
                        if (c->is_trigger) {
                            relative_speed = fabs((b->velocity[0] - a->velocity[0]) * normal[0] +
                                                  (b->velocity[1] - a->velocity[1]) * normal[1] +
                                                  (b->velocity[2] - a->velocity[2]) * normal[2]);
                        } else {
                            normal_impulse = resolve_collision(a, b, normal, depth, &relative_speed);
                        }
                        c->relative_speed = relative_speed;
                        c->normal_impulse = normal_impulse;
                    } else if (!(a->is_trigger || b->is_trigger)) {
                        (void)resolve_collision(a, b, normal, depth, NULL);
                    }

                    if (normal[1] > 0.7) {
                        b->is_grounded = 1;
                        b->ground_normal[0] = normal[0];
                        b->ground_normal[1] = normal[1];
                        b->ground_normal[2] = normal[2];
                    } else if (normal[1] < -0.7) {
                        a->is_grounded = 1;
                        a->ground_normal[0] = -normal[0];
                        a->ground_normal[1] = -normal[1];
                        a->ground_normal[2] = -normal[2];
                    }
                }
            }
        }

        for (int32_t iter = 0; iter < 6; iter++) {
            for (int32_t j = 0; j < w->joint_count; j++) {
                if (w->joints[j])
                    rt_joint3d_solve(w->joints[j], w->joint_types[j], sub_dt);
            }
        }
    }

    world3d_build_event_buffers(w);

    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->force[0] = b->force[1] = b->force[2] = 0.0;
        b->torque[0] = b->torque[1] = b->torque[2] = 0.0;
    }
}

/// @brief `World3D.AddJoint(joint, type)` — register a constraint.
///
/// Retains the joint and stores its type tag so `rt_joint3d_solve` can
/// dispatch correctly. Traps when the 128-joint cap is exceeded.
void rt_world3d_add_joint(void *obj, void *joint, int64_t joint_type) {
    if (!obj || !joint)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    if (w->joint_count >= PH3D_MAX_JOINTS) {
        rt_trap("Physics3D: max joint limit (128) exceeded");
        return;
    }
    rt_obj_retain_maybe(joint);
    w->joints[w->joint_count] = joint;
    w->joint_types[w->joint_count] = (int32_t)joint_type;
    w->joint_count++;
}

/// @brief `World3D.RemoveJoint(joint)` — unregister a constraint.
///
/// Linear scan; on hit, swaps in the last entry to compact the array
/// and releases the world's reference. Silent no-op when not found.
void rt_world3d_remove_joint(void *obj, void *joint) {
    if (!obj || !joint)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    for (int32_t i = 0; i < w->joint_count; i++) {
        if (w->joints[i] == joint) {
            void *removed = w->joints[i];
            w->joints[i] = w->joints[--w->joint_count];
            w->joint_types[i] = w->joint_types[w->joint_count];
            w->joints[w->joint_count] = NULL;
            if (removed && rt_obj_release_check0(removed))
                rt_obj_free(removed);
            return;
        }
    }
}

/// @brief `World3D.JointCount` — number of registered joints (0 for NULL).
int64_t rt_world3d_joint_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->joint_count : 0;
}

/// @brief `World3D.Add(body)` — register a body.
///
/// Retains the body and stores it in the bodies array. Traps when the
/// 256-body cap is exceeded.
void rt_world3d_add(void *obj, void *body) {
    if (!obj || !body)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    if (w->body_count >= PH3D_MAX_BODIES) {
        rt_trap("Physics3D: max body limit (256) exceeded");
        return;
    }
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = (rt_body3d *)body;
}

/// @brief `World3D.Remove(body)` — unregister a body.
///
/// Same swap-with-last compaction as `RemoveJoint`. Releases the world's
/// reference, which may free the body if no other holder remains.
void rt_world3d_remove(void *obj, void *body) {
    if (!obj || !body)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == body) {
            void *removed = w->bodies[i];
            w->bodies[i] = w->bodies[--w->body_count];
            w->bodies[w->body_count] = NULL;
            if (removed && rt_obj_release_check0(removed))
                rt_obj_free(removed);
            return;
        }
    }
}

/// @brief `World3D.BodyCount` — number of registered bodies (0 for NULL).
int64_t rt_world3d_body_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->body_count : 0;
}

/// @brief `World3D.SetGravity(gx, gy, gz)` — change the world gravity vector.
///
/// Takes effect on the next `Step`. Existing in-flight velocities are
/// not modified.
void rt_world3d_set_gravity(void *obj, double gx, double gy, double gz) {
    if (!obj)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    w->gravity[0] = gx;
    w->gravity[1] = gy;
    w->gravity[2] = gz;
}

/*==========================================================================
 * Collision event queries
 *=========================================================================*/

// Collision-query accessors — expose the latest frame's contact array
// to Zia. All return safe defaults (0 / NULL / origin Vec3) for NULL
// receivers and out-of-range indices so call sites can iterate without
// extra guards.

/// @brief `World3D.CollisionCount` — number of contacts this frame.
int64_t rt_world3d_get_collision_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->contact_count : 0;
}

/// @brief `World3D.CollisionBodyA(i)` — borrowed reference to body A in contact `i`.
void *rt_world3d_get_collision_body_a(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_a;
}

/// @brief `World3D.CollisionBodyB(i)` — borrowed reference to body B in contact `i`.
void *rt_world3d_get_collision_body_b(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_b;
}

/// @brief `World3D.CollisionNormal(i)` — fresh `Vec3` of the contact normal.
void *rt_world3d_get_collision_normal(void *obj, int64_t index) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(
        w->contacts[index].normal[0], w->contacts[index].normal[1], w->contacts[index].normal[2]);
}

/// @brief `World3D.CollisionDepth(i)` — penetration depth of contact `i`.
///
/// Returned as a positive value (the internal `separation` is stored
/// negative; this flips it back).
double rt_world3d_get_collision_depth(void *obj, int64_t index) {
    if (!obj)
        return 0;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return 0;
    return -w->contacts[index].separation;
}

/// @brief `World3D.CollisionEventCount` — number of collision events this frame.
///
/// Currently aliased to `contact_count` (one event per contact); kept
/// as a separate accessor in case the engine ever splits "live contacts"
/// from "events emitted this step".
int64_t rt_world3d_get_collision_event_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->contact_count : 0;
}

/// @brief `World3D.CollisionEvent(i)` — boxed `CollisionEvent3D` for contact `i`.
///
/// Returns NULL on out-of-range. The returned event is owned by the
/// caller (unlike the borrowed-reference body accessors).
void *rt_world3d_get_collision_event(void *obj, int64_t index) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w || index < 0 || index >= w->contact_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->contacts[index]);
}

// Enter/Stay/Exit event accessors — populated by the contact-diff in
// `world3d_build_event_buffers`. These mirror the "live contacts"
// accessors above but only fire on transitions (enter, exit) or
// continuations (stay) — useful for trigger events like OnPlayerEnter.

/// @brief `World3D.EnterEventCount` — contacts that began this frame.
int64_t rt_world3d_get_enter_event_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->enter_event_count : 0;
}

/// @brief `World3D.EnterEvent(i)` — boxed event for the i-th new contact.
void *rt_world3d_get_enter_event(void *obj, int64_t index) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w || index < 0 || index >= w->enter_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->enter_events[index]);
}

/// @brief `World3D.StayEventCount` — contacts that persisted from the previous frame.
int64_t rt_world3d_get_stay_event_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->stay_event_count : 0;
}

/// @brief `World3D.StayEvent(i)` — boxed event for the i-th continuing contact.
void *rt_world3d_get_stay_event(void *obj, int64_t index) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w || index < 0 || index >= w->stay_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->stay_events[index]);
}

/// @brief `World3D.ExitEventCount` — contacts that ended this frame.
int64_t rt_world3d_get_exit_event_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->exit_event_count : 0;
}

/// @brief `World3D.ExitEvent(i)` — boxed event for the i-th newly-ended contact.
void *rt_world3d_get_exit_event(void *obj, int64_t index) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w || index < 0 || index >= w->exit_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->exit_events[index]);
}

// Boxed `CollisionEvent3D` field accessors — exposed to Zia as
// properties on the event object. Borrowed references for body /
// collider (the event already holds a retained ref).

/// @brief `CollisionEvent3D.BodyA` — first body in the collision pair.
void *rt_collision_event3d_get_body_a(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->body_a : NULL;
}

/// @brief `CollisionEvent3D.BodyB` — second body in the collision pair.
void *rt_collision_event3d_get_body_b(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->body_b : NULL;
}

/// @brief `CollisionEvent3D.ColliderA` — leaf collider on body A (for compounds).
void *rt_collision_event3d_get_collider_a(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->collider_a : NULL;
}

/// @brief `CollisionEvent3D.ColliderB` — leaf collider on body B (for compounds).
void *rt_collision_event3d_get_collider_b(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->collider_b : NULL;
}

/// @brief `CollisionEvent3D.IsTrigger` — whether either party is a trigger.
int8_t rt_collision_event3d_get_is_trigger(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->is_trigger : 0;
}

/// @brief `CollisionEvent3D.ContactCount` — currently 1 per event (single-point contacts).
///
/// Reserved for future multi-point contact manifolds; today every event
/// carries exactly one representative contact point.
int64_t rt_collision_event3d_get_contact_count(void *obj) {
    return obj ? 1 : 0;
}

/// @brief `CollisionEvent3D.RelativeSpeed` — closing speed along the contact normal.
double rt_collision_event3d_get_relative_speed(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->relative_speed : 0.0;
}

/// @brief `CollisionEvent3D.NormalImpulse` — magnitude of the resolved normal impulse.
///
/// Useful for damage models or audio: louder/heavier sound on bigger
/// impulses. Always 0 for trigger events (no impulse is applied).
double rt_collision_event3d_get_normal_impulse(void *obj) {
    return obj ? ((rt_collision_event3d_obj *)obj)->normal_impulse : 0.0;
}

/// @brief `CollisionEvent3D.Contact(i)` — boxed contact-point sub-object.
///
/// Currently only `i == 0` is valid (single-point contacts). Reconstructs
/// a transient `rt_contact3d` from the event's stored fields and boxes
/// it via `contact_point3d_new_from_contact`.
void *rt_collision_event3d_get_contact(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    rt_contact3d contact;
    if (!event || index != 0)
        return NULL;
    memset(&contact, 0, sizeof(contact));
    contact.body_a = event->body_a;
    contact.body_b = event->body_b;
    contact.collider_a = event->collider_a;
    contact.collider_b = event->collider_b;
    vec3_copy(contact.point, event->point);
    vec3_copy(contact.normal, event->normal);
    contact.separation = event->separation;
    return contact_point3d_new_from_contact(&contact);
}

/// @brief `CollisionEvent3D.ContactPoint(i)` — fresh `Vec3` for the contact point.
void *rt_collision_event3d_get_contact_point(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    if (!event || index != 0)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(event->point[0], event->point[1], event->point[2]);
}

/// @brief `CollisionEvent3D.ContactNormal(i)` — fresh `Vec3` for the contact normal.
void *rt_collision_event3d_get_contact_normal(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    if (!event || index != 0)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(event->normal[0], event->normal[1], event->normal[2]);
}

/// @brief `CollisionEvent3D.ContactSeparation(i)` — signed separation distance.
///
/// Negative means penetration; zero or positive means touching/just-separated.
double rt_collision_event3d_get_contact_separation(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    if (!event || index != 0)
        return 0.0;
    return event->separation;
}

/// @brief `ContactPoint3D.Point` — world-space contact location.
void *rt_contact_point3d_get_point(void *obj) {
    rt_contact_point3d_obj *contact = (rt_contact_point3d_obj *)obj;
    if (!contact)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(contact->point[0], contact->point[1], contact->point[2]);
}

/// @brief `ContactPoint3D.Normal` — world-space contact normal (A→B).
void *rt_contact_point3d_get_normal(void *obj) {
    rt_contact_point3d_obj *contact = (rt_contact_point3d_obj *)obj;
    if (!contact)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(contact->normal[0], contact->normal[1], contact->normal[2]);
}

/// @brief `ContactPoint3D.Separation` — signed gap (negative = penetrating).
double rt_contact_point3d_get_separation(void *obj) {
    return obj ? ((rt_contact_point3d_obj *)obj)->separation : 0.0;
}

// PhysicsHit3D field accessors — exposed as Zia properties on the
// boxed query result. All borrowed references / safe defaults on NULL.

/// @brief `PhysicsHit3D.Distance` — world-space distance from query origin.
double rt_physics_hit3d_get_distance(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->distance : 0.0;
}

/// @brief `PhysicsHit3D.Body` — the body that was hit.
void *rt_physics_hit3d_get_body(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->body : NULL;
}

/// @brief `PhysicsHit3D.Collider` — the leaf collider that was hit.
void *rt_physics_hit3d_get_collider(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->collider : NULL;
}

/// @brief `PhysicsHit3D.Point` — world-space hit location.
void *rt_physics_hit3d_get_point(void *obj) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)obj;
    if (!hit)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(hit->point[0], hit->point[1], hit->point[2]);
}

/// @brief `PhysicsHit3D.Normal` — world-space surface normal at the hit.
void *rt_physics_hit3d_get_normal(void *obj) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)obj;
    if (!hit)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(hit->normal[0], hit->normal[1], hit->normal[2]);
}

/// @brief `PhysicsHit3D.Fraction` — t along the swept path (0..1).
double rt_physics_hit3d_get_fraction(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->fraction : 0.0;
}

/// @brief `PhysicsHit3D.StartedPenetrating` — true if the query started inside the collider.
int8_t rt_physics_hit3d_get_started_penetrating(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->started_penetrating : 0;
}

/// @brief `PhysicsHit3D.IsTrigger` — true if the hit collider belongs to a trigger body.
int8_t rt_physics_hit3d_get_is_trigger(void *obj) {
    return obj ? ((rt_physics_hit3d_obj *)obj)->is_trigger : 0;
}

/// @brief `PhysicsHitList3D.Count` — number of hits in the list.
int64_t rt_physics_hit_list3d_get_count(void *obj) {
    return obj ? ((rt_physics_hit_list3d_obj *)obj)->count : 0;
}

/// @brief `PhysicsHitList3D[i]` — borrowed `PhysicsHit3D` reference.
void *rt_physics_hit_list3d_get(void *obj, int64_t index) {
    rt_physics_hit_list3d_obj *list = (rt_physics_hit_list3d_obj *)obj;
    if (!list || index < 0 || index >= list->count)
        return NULL;
    return list->items[index];
}

/// @brief Insert `hit` into a distance-sorted hit array, keeping the order.
///
/// O(n) insertion (linear shift). Acceptable because `RaycastAll` /
/// `OverlapAll` queries are bounded by `PH3D_MAX_QUERY_HITS` (256).
static int query_hit_insert_sorted(rt_query_hit3d *hits, int32_t count, const rt_query_hit3d *hit) {
    int32_t pos = count;
    if (!hits || !hit)
        return count;
    while (pos > 0 && hits[pos - 1].distance > hit->distance) {
        hits[pos] = hits[pos - 1];
        pos--;
    }
    hits[pos] = *hit;
    return count + 1;
}

/// @brief Stack-init a transient body with a collider for use by query helpers.
///
/// Static motion mode (so impulse code never touches it), identity
/// orientation, and the supplied position. `make_temp_sphere` is the
/// sphere-only variant used elsewhere; this is the general form.
static void init_temp_query_body(rt_body3d *body, void *collider, const double *position) {
    memset(body, 0, sizeof(*body));
    quat_identity(body->orientation);
    body->motion_mode = PH3D_MODE_STATIC;
    body->collider = collider;
    if (position)
        vec3_copy(body->position, position);
}

/// @brief Test a transient query body for overlap against a registered body.
///
/// Used by overlap queries — fills `out_hit` with `started_penetrating=1`
/// since the query body starts already touching the other body.
static int overlap_query_body_against_body(rt_body3d *query_body,
                                           rt_body3d *other,
                                           rt_query_hit3d *out_hit) {
    double normal[3], depth, point[3];
    void *leaf_other = NULL;
    if (!query_body || !other || !other->collider)
        return 0;
    if (!test_collision(query_body, other, normal, &depth, point, NULL, &leaf_other))
        return 0;
    if (out_hit) {
        memset(out_hit, 0, sizeof(*out_hit));
        out_hit->body = other;
        out_hit->collider = leaf_other ? leaf_other : other->collider;
        vec3_copy(out_hit->point, point);
        vec3_copy(out_hit->normal, normal);
        out_hit->distance = 0.0;
        out_hit->fraction = 0.0;
        out_hit->started_penetrating = 1;
        out_hit->is_trigger = other->is_trigger;
    }
    return 1;
}

/// @brief Sweep a sphere along `delta` and find first contact with `other`.
///
/// First checks initial overlap (early-out for already-penetrating).
/// Then broad-phases against the swept AABB. Then steps along the sweep
/// in `radius/4`-sized increments looking for the first overlap, and
/// refines the impact `t` via 14 iterations of bisection. The sub-step
/// size is clamped to `[0.02, 0.5]` so very small or huge bodies still
/// terminate in a reasonable number of iterations.
static int sweep_sphere_against_body(void *sphere_collider,
                                     const double *start_center,
                                     double radius,
                                     const double *delta,
                                     rt_body3d *other,
                                     double max_distance,
                                     rt_query_hit3d *out_hit) {
    rt_body3d query_body;
    rt_query_hit3d cur_hit;
    double query_min[3], query_max[3], swept_min[3], swept_max[3];
    double other_min[3], other_max[3];
    double delta_len;
    double step_dist;
    int steps;
    if (!sphere_collider || !start_center || !delta || !other || !other->collider)
        return 0;

    init_temp_query_body(&query_body, sphere_collider, start_center);
    body3d_update_shape_cache_from_collider(&query_body);
    if (overlap_query_body_against_body(&query_body, other, out_hit))
        return 1;

    body_aabb(&query_body, query_min, query_max);
    body_aabb(other, other_min, other_max);
    swept_aabb_from_points(query_min, query_max, delta, swept_min, swept_max);
    if (!aabb_overlap_raw(swept_min, swept_max, other_min, other_max))
        return 0;

    delta_len = vec3_len(delta);
    if (delta_len <= 1e-12 || max_distance <= 0.0)
        return 0;

    {
        double other_extent_x = other_max[0] - other_min[0];
        double other_extent_y = other_max[1] - other_min[1];
        double other_extent_z = other_max[2] - other_min[2];
        double body_extent = other_extent_x;
        if (other_extent_y > body_extent)
            body_extent = other_extent_y;
        if (other_extent_z > body_extent)
            body_extent = other_extent_z;
        step_dist = radius > 1e-3 ? radius * 0.25 : body_extent * 0.1;
        if (step_dist < 0.02)
            step_dist = 0.02;
        if (step_dist > 0.5)
            step_dist = 0.5;
    }

    steps = (int)ceil(delta_len / step_dist);
    if (steps < 1)
        steps = 1;
    if (steps > 1024)
        steps = 1024;

    {
        double prev_t = 0.0;
        for (int s = 1; s <= steps; ++s) {
            double t = (double)s / (double)steps;
            double center[3] = {
                start_center[0] + delta[0] * t,
                start_center[1] + delta[1] * t,
                start_center[2] + delta[2] * t,
            };
            init_temp_query_body(&query_body, sphere_collider, center);
            body3d_update_shape_cache_from_collider(&query_body);
            if (!overlap_query_body_against_body(&query_body, other, &cur_hit)) {
                prev_t = t;
                continue;
            }
            {
                double lo = prev_t;
                double hi = t;
                rt_query_hit3d best = cur_hit;
                for (int iter = 0; iter < 14; ++iter) {
                    double mid = (lo + hi) * 0.5;
                    double mid_center[3] = {
                        start_center[0] + delta[0] * mid,
                        start_center[1] + delta[1] * mid,
                        start_center[2] + delta[2] * mid,
                    };
                    init_temp_query_body(&query_body, sphere_collider, mid_center);
                    body3d_update_shape_cache_from_collider(&query_body);
                    if (overlap_query_body_against_body(&query_body, other, &cur_hit)) {
                        hi = mid;
                        best = cur_hit;
                    } else {
                        lo = mid;
                    }
                }
                best.distance = hi * max_distance;
                best.fraction = hi;
                best.started_penetrating = 0;
                if (out_hit)
                    *out_hit = best;
                return 1;
            }
        }
    }
    return 0;
}

/// @brief Sweep a capsule (axis from `a` to `b`, radius `radius`) along `delta`.
///
/// Approximates the capsule sweep as up to 7 sphere-sweeps sampled
/// along the axis. Picks the closest hit. Cheaper than a true capsule
/// sweep and works well for character motion against level geometry.
static int sweep_capsule_against_body(const double *a,
                                      const double *b,
                                      double radius,
                                      const double *delta,
                                      rt_body3d *other,
                                      double max_distance,
                                      rt_query_hit3d *out_hit) {
    double axis[3];
    double axis_len;
    int samples;
    int hit = 0;
    rt_query_hit3d best;
    void *sphere_collider;
    if (!a || !b || !delta || !other || !other->collider)
        return 0;
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return 0;
    vec3_sub(b, a, axis);
    axis_len = vec3_len(axis);
    samples = axis_len <= radius ? 1 : (int)ceil(axis_len / (radius > 1e-6 ? radius : 0.25)) + 1;
    if (samples < 1)
        samples = 1;
    if (samples > 7)
        samples = 7;
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        double center[3] = {
            a[0] + axis[0] * t,
            a[1] + axis[1] * t,
            a[2] + axis[2] * t,
        };
        rt_query_hit3d cur_hit;
        if (!sweep_sphere_against_body(sphere_collider, center, radius, delta, other, max_distance, &cur_hit))
            continue;
        if (!hit || cur_hit.distance < best.distance) {
            best = cur_hit;
            hit = 1;
        }
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    if (hit && out_hit)
        *out_hit = best;
    return hit;
}

/// @brief `World3D.OverlapSphere(center, radius, mask)` — list bodies overlapping a sphere.
///
/// Builds a transient sphere collider, then tests every world body
/// (after layer/mask filter) for overlap. Returns up to
/// `PH3D_MAX_QUERY_HITS` (256) hits as a `PhysicsHitList3D`. The
/// transient collider is released before returning.
void *rt_world3d_overlap_sphere(void *obj, void *center_obj, double radius, int64_t mask) {
    rt_world3d *w = (rt_world3d *)obj;
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    double center[3];
    rt_body3d query_body;
    void *sphere_collider;
    if (!w || !center_obj || radius < 0.0)
        return NULL;
    center[0] = rt_vec3_x(center_obj);
    center[1] = rt_vec3_y(center_obj);
    center[2] = rt_vec3_z(center_obj);
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return NULL;
    init_temp_query_body(&query_body, sphere_collider, center);
    body3d_update_shape_cache_from_collider(&query_body);
    for (int32_t i = 0; i < w->body_count && hit_count < PH3D_MAX_QUERY_HITS; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (overlap_query_body_against_body(&query_body, body, &hit))
            hits[hit_count++] = hit;
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    return physics_hit_list3d_new(hits, hit_count);
}

/// @brief `World3D.OverlapAabb(min, max, mask)` — list bodies overlapping a box.
///
/// Same pattern as `OverlapSphere` but uses a transient box collider
/// sized from the (min, max) corners. The half-extents are derived
/// from the corner spread; the center is the midpoint.
void *rt_world3d_overlap_aabb(void *obj, void *min_obj, void *max_obj, int64_t mask) {
    rt_world3d *w = (rt_world3d *)obj;
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    double mn[3], mx[3], center[3], half[3];
    rt_body3d query_body;
    void *box_collider;
    if (!w || !min_obj || !max_obj)
        return NULL;
    mn[0] = rt_vec3_x(min_obj);
    mn[1] = rt_vec3_y(min_obj);
    mn[2] = rt_vec3_z(min_obj);
    mx[0] = rt_vec3_x(max_obj);
    mx[1] = rt_vec3_y(max_obj);
    mx[2] = rt_vec3_z(max_obj);
    center[0] = (mn[0] + mx[0]) * 0.5;
    center[1] = (mn[1] + mx[1]) * 0.5;
    center[2] = (mn[2] + mx[2]) * 0.5;
    half[0] = fabs(mx[0] - mn[0]) * 0.5;
    half[1] = fabs(mx[1] - mn[1]) * 0.5;
    half[2] = fabs(mx[2] - mn[2]) * 0.5;
    box_collider = rt_collider3d_new_box(half[0], half[1], half[2]);
    if (!box_collider)
        return NULL;
    init_temp_query_body(&query_body, box_collider, center);
    body3d_update_shape_cache_from_collider(&query_body);
    for (int32_t i = 0; i < w->body_count && hit_count < PH3D_MAX_QUERY_HITS; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (overlap_query_body_against_body(&query_body, body, &hit))
            hits[hit_count++] = hit;
    }
    if (rt_obj_release_check0(box_collider))
        rt_obj_free(box_collider);
    return physics_hit_list3d_new(hits, hit_count);
}

/// @brief `World3D.SweepSphere(center, radius, delta, mask)` — first hit along a sphere sweep.
///
/// Returns the closest hit as a `PhysicsHit3D`, or NULL if the sweep
/// reaches `delta` without contact. Used for trajectory predictions
/// and projectile collision.
void *rt_world3d_sweep_sphere(void *obj, void *center_obj, double radius, void *delta_obj, int64_t mask) {
    rt_world3d *w = (rt_world3d *)obj;
    rt_query_hit3d best_hit;
    int found = 0;
    double center[3], delta[3];
    double max_distance;
    void *sphere_collider;
    if (!w || !center_obj || !delta_obj || radius < 0.0)
        return NULL;
    center[0] = rt_vec3_x(center_obj);
    center[1] = rt_vec3_y(center_obj);
    center[2] = rt_vec3_z(center_obj);
    delta[0] = rt_vec3_x(delta_obj);
    delta[1] = rt_vec3_y(delta_obj);
    delta[2] = rt_vec3_z(delta_obj);
    max_distance = vec3_len(delta);
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return NULL;
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (!sweep_sphere_against_body(sphere_collider, center, radius, delta, body, max_distance, &hit))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

/// @brief `World3D.SweepCapsule(a, b, radius, delta, mask)` — first hit along a capsule sweep.
///
/// Like `SweepSphere` but for capsule queries — primary use case is
/// character-controller motion against world geometry.
void *rt_world3d_sweep_capsule(void *obj,
                               void *a_obj,
                               void *b_obj,
                               double radius,
                               void *delta_obj,
                               int64_t mask) {
    rt_world3d *w = (rt_world3d *)obj;
    rt_query_hit3d best_hit;
    int found = 0;
    double a[3], b[3], delta[3];
    double max_distance;
    if (!w || !a_obj || !b_obj || !delta_obj || radius < 0.0)
        return NULL;
    a[0] = rt_vec3_x(a_obj);
    a[1] = rt_vec3_y(a_obj);
    a[2] = rt_vec3_z(a_obj);
    b[0] = rt_vec3_x(b_obj);
    b[1] = rt_vec3_y(b_obj);
    b[2] = rt_vec3_z(b_obj);
    delta[0] = rt_vec3_x(delta_obj);
    delta[1] = rt_vec3_y(delta_obj);
    delta[2] = rt_vec3_z(delta_obj);
    max_distance = vec3_len(delta);
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (!sweep_capsule_against_body(a, b, radius, delta, body, max_distance, &hit))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

/// @brief `World3D.Raycast(origin, direction, maxDistance, mask)` — first hit along a ray.
///
/// Implemented as a `SweepSphere` with a 1mm radius — gives reasonable
/// results for hitscan-style queries (line of sight, click-to-pick)
/// without needing a separate ray-vs-collider code path. Returns NULL
/// when the direction is zero or `maxDistance <= 0`.
void *rt_world3d_raycast(void *obj, void *origin_obj, void *direction_obj, double max_distance, int64_t mask) {
    double dir[3];
    void *delta;
    void *hit;
    if (!origin_obj || !direction_obj || max_distance <= 0.0)
        return NULL;
    dir[0] = rt_vec3_x(direction_obj);
    dir[1] = rt_vec3_y(direction_obj);
    dir[2] = rt_vec3_z(direction_obj);
    if (vec3_normalize_in_place(dir) <= 1e-12)
        return NULL;
    delta = rt_vec3_new(dir[0] * max_distance, dir[1] * max_distance, dir[2] * max_distance);
    hit = rt_world3d_sweep_sphere(obj, origin_obj, 1e-3, delta, mask);
    return hit;
}

/// @brief `World3D.RaycastAll(origin, direction, maxDistance, mask)` — all hits along a ray, sorted.
///
/// Like `Raycast` but doesn't stop at the first hit — every body
/// the ray pierces is recorded. Results come back sorted by distance.
/// Bounded by `PH3D_MAX_QUERY_HITS` (256).
void *rt_world3d_raycast_all(void *obj,
                             void *origin_obj,
                             void *direction_obj,
                             double max_distance,
                             int64_t mask) {
    rt_world3d *w = (rt_world3d *)obj;
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    double origin[3], dir[3], delta[3];
    int32_t hit_count = 0;
    void *sphere_collider;
    if (!w || !origin_obj || !direction_obj || max_distance <= 0.0)
        return NULL;
    origin[0] = rt_vec3_x(origin_obj);
    origin[1] = rt_vec3_y(origin_obj);
    origin[2] = rt_vec3_z(origin_obj);
    dir[0] = rt_vec3_x(direction_obj);
    dir[1] = rt_vec3_y(direction_obj);
    dir[2] = rt_vec3_z(direction_obj);
    if (vec3_normalize_in_place(dir) <= 1e-12)
        return NULL;
    delta[0] = dir[0] * max_distance;
    delta[1] = dir[1] * max_distance;
    delta[2] = dir[2] * max_distance;
    sphere_collider = rt_collider3d_new_sphere(1e-3);
    if (!sphere_collider)
        return NULL;
    for (int32_t i = 0; i < w->body_count && hit_count < PH3D_MAX_QUERY_HITS; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (sweep_sphere_against_body(sphere_collider, origin, 1e-3, delta, body, max_distance, &hit))
            hit_count = query_hit_insert_sorted(hits, hit_count, &hit);
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    return physics_hit_list3d_new(hits, hit_count);
}

/*==========================================================================
 * Body3D
 *=========================================================================*/

/// @brief GC finalizer for `Body3D` — release the owned collider.
///
/// The body holds a strong reference to its collider; this drops it.
/// World-level body removal (`rt_world3d_remove`) drops the world's
/// reference, which in most cases is the last one and triggers this.
static void body3d_finalizer(void *obj) {
    rt_body3d *b = (rt_body3d *)obj;
    if (!b)
        return;
    if (b->collider && rt_obj_release_check0(b->collider))
        rt_obj_free(b->collider);
    b->collider = NULL;
}

/// @brief Swap the body's collider, refreshing all derived state.
///
/// Validates that the new collider is allowed for the body's current
/// motion mode (`body3d_motion_mode_allowed`). On success, retains the
/// new collider, releases the old, refreshes the cached shape and
/// inverse-mass derivations, and wakes the body if dynamic. No-op
/// when the same collider is assigned twice.
static int body3d_assign_collider(rt_body3d *body, void *collider, const char *api_name) {
    if (!body)
        return 0;
    if (collider == body->collider)
        return 1;
    if (!body3d_motion_mode_allowed(
            body, collider, body->motion_mode, api_name))
        return 0;
    if (collider)
        rt_obj_retain_maybe(collider);
    if (body->collider && rt_obj_release_check0(body->collider))
        rt_obj_free(body->collider);
    body->collider = collider;
    body3d_update_shape_cache_from_collider(body);
    body3d_refresh_motion_mode(body);
    body3d_wake_if_dynamic(body);
    return 1;
}

/// @brief Allocate a fresh body with sensible defaults.
///
/// Defaults: restitution 0.3 (mostly inelastic), friction 0.5 (typical
/// rubber/concrete blend), layer 1, mask all (collides with everything),
/// motion mode = `dynamic` if mass > 0 else `static`, sleeping enabled.
/// The collider is left NULL for the caller to assign via
/// `body3d_assign_collider`.
static void *make_body(double mass) {
    rt_body3d *b = (rt_body3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body3d));
    if (!b) {
        rt_trap("Physics3D.Body.New: allocation failed");
        return NULL;
    }
    memset(b, 0, sizeof(rt_body3d));
    b->mass = mass;
    b->restitution = 0.3;
    b->friction = 0.5;
    b->collision_layer = 1;
    b->collision_mask = ~(int64_t)0;
    b->motion_mode = (mass <= 1e-12) ? PH3D_MODE_STATIC : PH3D_MODE_DYNAMIC;
    b->can_sleep = 1;
    b->ground_normal[1] = 1.0;
    quat_identity(b->orientation);
    body3d_refresh_motion_mode(b);
    rt_obj_set_finalizer(b, body3d_finalizer);
    return b;
}

/// @brief `Body3D.New(mass)` — bare body without a collider.
///
/// Caller must assign a collider via `SetCollider` before adding to a
/// world (otherwise body-vs-body tests skip it).
void *rt_body3d_new(double mass) {
    return make_body(mass);
}

/// @brief `Body3D.NewAABB(hx, hy, hz, mass)` — body with a fresh box collider.
///
/// Half-extents specify the box. The transient collider returned by
/// `rt_collider3d_new_box` is released after the body retains it.
void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(mass);
    void *collider = rt_collider3d_new_box(hx, hy, hz);
    if (!b || !collider) {
        if (collider && rt_obj_release_check0(collider))
            rt_obj_free(collider);
        if (b && rt_obj_release_check0(b))
            rt_obj_free(b);
        return NULL;
    }
    body3d_assign_collider(b, collider, "Physics3DBody.NewAABB: failed to assign collider");
    if (rt_obj_release_check0(collider))
        rt_obj_free(collider);
    return b;
}

/// @brief `Body3D.NewSphere(radius, mass)` — body with a fresh sphere collider.
void *rt_body3d_new_sphere(double radius, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(mass);
    void *collider = rt_collider3d_new_sphere(radius);
    if (!b || !collider) {
        if (collider && rt_obj_release_check0(collider))
            rt_obj_free(collider);
        if (b && rt_obj_release_check0(b))
            rt_obj_free(b);
        return NULL;
    }
    body3d_assign_collider(b, collider, "Physics3DBody.NewSphere: failed to assign collider");
    if (rt_obj_release_check0(collider))
        rt_obj_free(collider);
    return b;
}

/// @brief `Body3D.NewCapsule(radius, height, mass)` — Y-aligned capsule body.
///
/// Total height includes both hemispherical caps. The cylinder portion
/// has length `max(0, height - 2*radius)`.
void *rt_body3d_new_capsule(double radius, double height, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(mass);
    void *collider = rt_collider3d_new_capsule(radius, height);
    if (!b || !collider) {
        if (collider && rt_obj_release_check0(collider))
            rt_obj_free(collider);
        if (b && rt_obj_release_check0(b))
            rt_obj_free(b);
        return NULL;
    }
    body3d_assign_collider(b, collider, "Physics3DBody.NewCapsule: failed to assign collider");
    if (rt_obj_release_check0(collider))
        rt_obj_free(collider);
    return b;
}

/// @brief `Body3D.SetCollider(collider)` — swap the body's collider.
///
/// Traps if the collider requires a static-only body but the body is
/// dynamic/kinematic (e.g., concave triangle meshes).
void rt_body3d_set_collider(void *o, void *collider) {
    if (!o)
        return;
    body3d_assign_collider(
        (rt_body3d *)o, collider, "Physics3DBody.SetCollider: collider requires a static body");
}

/// @brief `Body3D.GetCollider` — borrowed reference to the body's collider.
void *rt_body3d_get_collider(void *o) {
    return o ? ((rt_body3d *)o)->collider : NULL;
}

/// @brief `Body3D.SetPosition(x, y, z)` — teleport (also wakes if dynamic).
///
/// Bypasses collision response, so use sparingly during simulation
/// (per-step adjustments are usually better via velocity).
void rt_body3d_set_position(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->position[0] = x;
        b->position[1] = y;
        b->position[2] = z;
        body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetPosition` — fresh `Vec3` of the body's world position.
void *rt_body3d_get_position(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->position[0], b->position[1], b->position[2]);
}

/// @brief `Body3D.SetOrientation(quat)` — set the body's orientation quaternion.
///
/// NULL `quat` resets to identity. Always normalizes to defend against
/// caller-passed unnormalized quaternions.
void rt_body3d_set_orientation(void *o, void *quat) {
    if (!o)
        return;
    {
        rt_body3d *b = (rt_body3d *)o;
        if (!quat) {
            quat_identity(b->orientation);
        } else {
            b->orientation[0] = rt_quat_x(quat);
            b->orientation[1] = rt_quat_y(quat);
            b->orientation[2] = rt_quat_z(quat);
            b->orientation[3] = rt_quat_w(quat);
            quat_normalize(b->orientation);
        }
        body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetOrientation` — fresh `Quat` of the body's orientation.
void *rt_body3d_get_orientation(void *o) {
    if (!o)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    {
        rt_body3d *b = (rt_body3d *)o;
        return rt_quat_new(
            b->orientation[0], b->orientation[1], b->orientation[2], b->orientation[3]);
    }
}

/// @brief `Body3D.SetVelocity(x, y, z)` — set linear velocity (m/s).
///
/// Wakes the body if dynamic and the new velocity is non-zero.
void rt_body3d_set_velocity(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->velocity[0] = x;
        b->velocity[1] = y;
        b->velocity[2] = z;
        if (vec3_len_sq(b->velocity) > 1e-12)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetVelocity` — fresh `Vec3` of linear velocity.
void *rt_body3d_get_velocity(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->velocity[0], b->velocity[1], b->velocity[2]);
}

// Body3D physical-state accessors — set/get pairs for velocity,
// angular velocity, force, and torque. Setters wake the body if
// dynamic; getters return safe defaults on NULL.

/// @brief `Body3D.SetAngularVelocity(x, y, z)` — set angular velocity (radians/sec).
void rt_body3d_set_angular_velocity(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->angular_velocity[0] = x;
        b->angular_velocity[1] = y;
        b->angular_velocity[2] = z;
        if (vec3_len_sq(b->angular_velocity) > 1e-12)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetAngularVelocity` — fresh `Vec3` of angular velocity.
void *rt_body3d_get_angular_velocity(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    {
        rt_body3d *b = (rt_body3d *)o;
        return rt_vec3_new(
            b->angular_velocity[0], b->angular_velocity[1], b->angular_velocity[2]);
    }
}

/// @brief `Body3D.ApplyForce(fx, fy, fz)` — accumulate a continuous force (N).
///
/// The force is consumed by the next `World3D.Step` and zeroed at end
/// of step. Apply once per frame for sustained force, every frame for
/// sustained acceleration. No-op for non-dynamic bodies.
void rt_body3d_apply_force(void *o, double fx, double fy, double fz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        b->force[0] += fx;
        b->force[1] += fy;
        b->force[2] += fz;
        if (fx != 0.0 || fy != 0.0 || fz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.ApplyImpulse(ix, iy, iz)` — instantaneous velocity change.
///
/// Equivalent to `Δv = impulse / mass`. Applied immediately, not at
/// step time — use for jumps, hits, explosions. No-op for non-dynamic.
void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        b->velocity[0] += ix * b->inv_mass;
        b->velocity[1] += iy * b->inv_mass;
        b->velocity[2] += iz * b->inv_mass;
        if (ix != 0.0 || iy != 0.0 || iz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.ApplyTorque(tx, ty, tz)` — accumulate a continuous torque (N·m).
///
/// Same lifecycle as `ApplyForce`: consumed and reset by the next step.
void rt_body3d_apply_torque(void *o, double tx, double ty, double tz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        b->torque[0] += tx;
        b->torque[1] += ty;
        b->torque[2] += tz;
        if (tx != 0.0 || ty != 0.0 || tz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.ApplyAngularImpulse(ix, iy, iz)` — instant angular velocity change.
///
/// Δω = impulse · I⁻¹ (per axis, since the inertia tensor is diagonal).
void rt_body3d_apply_angular_impulse(void *o, double ix, double iy, double iz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        b->angular_velocity[0] += ix * b->inv_inertia[0];
        b->angular_velocity[1] += iy * b->inv_inertia[1];
        b->angular_velocity[2] += iz * b->inv_inertia[2];
        if (ix != 0.0 || iy != 0.0 || iz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

// Body3D material/filter property setters and getters — pure assigns
// with NULL-guards. Damping setters clamp negatives to zero.

/// @brief `Body3D.SetRestitution(r)` — set bounciness (0=no bounce, 1=elastic).
void rt_body3d_set_restitution(void *o, double r) {
    if (o)
        ((rt_body3d *)o)->restitution = r;
}

/// @brief `Body3D.GetRestitution` — read bounciness.
double rt_body3d_get_restitution(void *o) {
    return o ? ((rt_body3d *)o)->restitution : 0;
}

/// @brief `Body3D.SetFriction(f)` — set friction coefficient.
///
/// Pair friction is `sqrt(a.friction * b.friction)` — geometric mean
/// matches typical material-interaction tables.
void rt_body3d_set_friction(void *o, double f) {
    if (o)
        ((rt_body3d *)o)->friction = f;
}

/// @brief `Body3D.GetFriction` — read friction coefficient.
double rt_body3d_get_friction(void *o) {
    return o ? ((rt_body3d *)o)->friction : 0;
}

/// @brief `Body3D.SetLinearDamping(d)` — per-second linear velocity decay.
///
/// Each step scales velocity by `max(0, 1 - d*dt)`. Useful as a soft
/// air-resistance proxy. Negative values clamp to 0.
void rt_body3d_set_linear_damping(void *o, double d) {
    if (o)
        ((rt_body3d *)o)->linear_damping = d > 0.0 ? d : 0.0;
}

/// @brief `Body3D.GetLinearDamping` — read linear damping coefficient.
double rt_body3d_get_linear_damping(void *o) {
    return o ? ((rt_body3d *)o)->linear_damping : 0.0;
}

/// @brief `Body3D.SetAngularDamping(d)` — per-second angular velocity decay.
void rt_body3d_set_angular_damping(void *o, double d) {
    if (o)
        ((rt_body3d *)o)->angular_damping = d > 0.0 ? d : 0.0;
}

/// @brief `Body3D.GetAngularDamping` — read angular damping coefficient.
double rt_body3d_get_angular_damping(void *o) {
    return o ? ((rt_body3d *)o)->angular_damping : 0.0;
}

/// @brief `Body3D.SetCollisionLayer(l)` — bitmask labeling this body's category.
void rt_body3d_set_collision_layer(void *o, int64_t l) {
    if (o)
        ((rt_body3d *)o)->collision_layer = l;
}

/// @brief `Body3D.GetCollisionLayer` — read this body's layer bitmask.
int64_t rt_body3d_get_collision_layer(void *o) {
    return o ? ((rt_body3d *)o)->collision_layer : 0;
}

/// @brief `Body3D.SetCollisionMask(m)` — bitmask of layers this body collides with.
void rt_body3d_set_collision_mask(void *o, int64_t m) {
    if (o)
        ((rt_body3d *)o)->collision_mask = m;
}

/// @brief `Body3D.GetCollisionMask` — read this body's collision mask.
int64_t rt_body3d_get_collision_mask(void *o) {
    return o ? ((rt_body3d *)o)->collision_mask : 0;
}

/// @brief `Body3D.SetStatic(s)` — toggle static motion mode.
///
/// Traps if the body's collider is incompatible with the requested
/// mode (e.g., switching a triangle-mesh-collider body to dynamic).
/// Refreshes derived state (inv_mass, inv_inertia) on success.
void rt_body3d_set_static(void *o, int8_t s) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        int32_t desired_mode = s ? PH3D_MODE_STATIC : PH3D_MODE_DYNAMIC;
        if (!body3d_motion_mode_allowed(
                b,
                b->collider,
                desired_mode,
                "Physics3DBody.set_Static: collider requires a static body"))
            return;
        b->motion_mode = desired_mode;
        body3d_refresh_motion_mode(b);
    }
}

/// @brief `Body3D.IsStatic` — true if the body is in static motion mode.
int8_t rt_body3d_is_static(void *o) {
    return o ? ((rt_body3d *)o)->is_static : 0;
}

/// @brief `Body3D.SetKinematic(k)` — toggle kinematic motion mode.
///
/// Kinematic bodies move under script control (position/velocity set
/// directly) but are treated as infinitely massive by the impulse
/// solver — they push dynamic bodies but aren't pushed in return.
void rt_body3d_set_kinematic(void *o, int8_t k) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        int32_t desired_mode = k ? PH3D_MODE_KINEMATIC : PH3D_MODE_DYNAMIC;
        if (!body3d_motion_mode_allowed(
                b,
                b->collider,
                desired_mode,
                "Physics3DBody.set_Kinematic: collider requires a static body"))
            return;
        b->motion_mode = desired_mode;
        body3d_refresh_motion_mode(b);
    }
}

/// @brief `Body3D.IsKinematic` — true if the body is in kinematic motion mode.
int8_t rt_body3d_is_kinematic(void *o) {
    return o ? ((rt_body3d *)o)->is_kinematic : 0;
}

/// @brief `Body3D.SetTrigger(t)` — toggle "report contacts but don't physically respond".
///
/// Trigger bodies fire collision events but don't apply impulses or
/// positional correction — useful for damage volumes, pickup zones, etc.
void rt_body3d_set_trigger(void *o, int8_t t) {
    if (o)
        ((rt_body3d *)o)->is_trigger = t;
}

/// @brief `Body3D.IsTrigger` — true if the body is in trigger-only mode.
int8_t rt_body3d_is_trigger(void *o) {
    return o ? ((rt_body3d *)o)->is_trigger : 0;
}

/// @brief `Body3D.SetCanSleep(canSleep)` — toggle automatic sleep eligibility.
///
/// When false, the body never enters sleep mode (always integrated).
/// Disabling sleep on a quiet body wakes it immediately so the next
/// step processes it normally.
void rt_body3d_set_can_sleep(void *o, int8_t can_sleep) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->can_sleep = can_sleep ? 1 : 0;
        if (!b->can_sleep) {
            b->is_sleeping = 0;
            b->sleep_time = 0.0;
        }
    }
}

/// @brief `Body3D.CanSleep` — read sleep eligibility flag.
int8_t rt_body3d_can_sleep(void *o) {
    return o ? ((rt_body3d *)o)->can_sleep : 0;
}

/// @brief `Body3D.IsSleeping` — true when the body is currently quiescent.
int8_t rt_body3d_is_sleeping(void *o) {
    return o ? ((rt_body3d *)o)->is_sleeping : 0;
}

/// @brief `Body3D.Wake` — force the body awake (no-op for non-dynamic).
void rt_body3d_wake(void *o) {
    if (o)
        body3d_wake_if_dynamic((rt_body3d *)o);
}

/// @brief `Body3D.Sleep` — manually put the body to sleep.
///
/// Zeros velocity / angular velocity / accumulated forces. No-op
/// when the body can't sleep or isn't dynamic. Bypasses the normal
/// sleep-delay countdown.
void rt_body3d_sleep(void *o) {
    if (!o)
        return;
    {
        rt_body3d *b = (rt_body3d *)o;
        if (b->motion_mode != PH3D_MODE_DYNAMIC || !b->can_sleep)
            return;
        b->is_sleeping = 1;
        b->sleep_time = PH3D_SLEEP_DELAY;
        vec3_set(b->velocity, 0.0, 0.0, 0.0);
        vec3_set(b->angular_velocity, 0.0, 0.0, 0.0);
        vec3_set(b->force, 0.0, 0.0, 0.0);
        vec3_set(b->torque, 0.0, 0.0, 0.0);
    }
}

/// @brief `Body3D.SetUseCcd(useCcd)` — opt this body into CCD substeps.
///
/// CCD subdivides high-speed steps to prevent tunneling through thin
/// geometry. Enable for fast-moving bodies (bullets, balls); leave off
/// otherwise to save CPU.
void rt_body3d_set_use_ccd(void *o, int8_t use_ccd) {
    if (o)
        ((rt_body3d *)o)->use_ccd = use_ccd ? 1 : 0;
}

/// @brief `Body3D.GetUseCcd` — read CCD opt-in flag.
int8_t rt_body3d_get_use_ccd(void *o) {
    return o ? ((rt_body3d *)o)->use_ccd : 0;
}

/// @brief `Body3D.IsGrounded` — true when the body has an upward contact this frame.
///
/// "Upward" = contact normal Y > 0.7 (~45° max slope). Set/cleared by
/// `World3D.Step`; queryable from frame to frame.
int8_t rt_body3d_is_grounded(void *o) {
    return o ? ((rt_body3d *)o)->is_grounded : 0;
}

/// @brief `Body3D.GroundNormal` — fresh `Vec3` of the latest ground normal.
///
/// Defaults to +Y when not grounded.
void *rt_body3d_get_ground_normal(void *o) {
    if (!o)
        return rt_vec3_new(0, 1, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->ground_normal[0], b->ground_normal[1], b->ground_normal[2]);
}

/// @brief `Body3D.Mass` — read the body's mass (zero for static).
double rt_body3d_get_mass(void *o) {
    return o ? ((rt_body3d *)o)->mass : 0;
}

/*==========================================================================
 * Character Controller
 *=========================================================================*/

typedef struct {
    rt_body3d *body;
    double normal[3];
    double depth;
    double fraction;
    int8_t hit;
} rt_character_hit3d;

// Character controller — built on top of Body3D with custom motion
// resolution: kinematic-style sweeps + slide along surfaces, optional
// step-up over small obstacles, ground probing for "is grounded" state.

/// @brief Update the controller's grounded flag and store the latest ground normal.
///
/// Negates the contact normal because the contact normal points from
/// the body toward the ground; we want the surface normal pointing up.
static void character3d_set_ground_state(rt_character3d *ctrl,
                                         int8_t grounded,
                                         const double *normal) {
    if (!ctrl || !ctrl->body)
        return;
    ctrl->is_grounded = grounded;
    ctrl->body->is_grounded = grounded;
    if (grounded && normal) {
        ctrl->body->ground_normal[0] = -normal[0];
        ctrl->body->ground_normal[1] = -normal[1];
        ctrl->body->ground_normal[2] = -normal[2];
    } else {
        ctrl->body->ground_normal[0] = 0.0;
        ctrl->body->ground_normal[1] = 1.0;
        ctrl->body->ground_normal[2] = 0.0;
    }
}

/// @brief True if the surface (negated contact normal) is below the slope limit.
///
/// `slope_limit_cos = cos(max_slope_angle)`; a "walkable" surface has
/// `normal_y >= cos(angle)`. Used to gate ground-snapping and step-up.
static int character3d_normal_is_walkable(const rt_character3d *ctrl, const double *normal) {
    return ctrl && normal && (-normal[1] >= ctrl->slope_limit_cos);
}

/// @brief Filter for which world bodies the controller should slide against.
///
/// Excludes self, triggers, and dynamic bodies (the controller is
/// kinematic so we don't want it pushing dynamics around as solid
/// blockers). Honors the standard layer/mask filter.
static int character3d_candidate_body(const rt_character3d *ctrl, const rt_body3d *other) {
    if (!ctrl || !ctrl->body || !ctrl->world || !other)
        return 0;
    if (other == ctrl->body)
        return 0;
    if (other->is_trigger || other->motion_mode == PH3D_MODE_DYNAMIC)
        return 0;
    return bodies_can_collide(ctrl->body, other);
}

/// @brief Probe what the controller would collide with at a given position.
///
/// Temporarily moves the body to `pos`, runs the standard narrow-phase
/// against every candidate body, restores the original position, and
/// returns the deepest contact (if any). Used for both penetration
/// resolution and binary-searched sweeps.
static int character3d_test_position(rt_character3d *ctrl,
                                     const double *pos,
                                     rt_character_hit3d *out_hit) {
    if (!ctrl || !ctrl->body || !ctrl->world)
        return 0;

    rt_body3d *body = ctrl->body;
    double saved[3] = {body->position[0], body->position[1], body->position[2]};
    body->position[0] = pos[0];
    body->position[1] = pos[1];
    body->position[2] = pos[2];

    rt_character_hit3d best;
    memset(&best, 0, sizeof(best));
    for (int32_t i = 0; i < ctrl->world->body_count; i++) {
        rt_body3d *other = ctrl->world->bodies[i];
        double normal[3], depth;
        if (!character3d_candidate_body(ctrl, other))
            continue;
        if (!test_collision(body, other, normal, &depth, NULL, NULL, NULL))
            continue;
        if (!best.hit || depth > best.depth) {
            best.hit = 1;
            best.body = other;
            best.depth = depth;
            vec3_copy(best.normal, normal);
        }
    }

    body->position[0] = saved[0];
    body->position[1] = saved[1];
    body->position[2] = saved[2];

    if (best.hit && out_hit)
        *out_hit = best;
    return best.hit;
}

/// @brief Push the controller out of any penetration it currently has.
///
/// Up to 6 iterations: probe at current position, push along the
/// deepest normal by `depth + 1e-4` epsilon, repeat. Bails early
/// when no penetration remains. Bounded so degenerate stuck cases
/// terminate quickly.
static void character3d_resolve_penetration(rt_character3d *ctrl) {
    if (!ctrl || !ctrl->body)
        return;
    for (int iter = 0; iter < 6; iter++) {
        rt_character_hit3d hit;
        double pos[3] = {ctrl->body->position[0], ctrl->body->position[1], ctrl->body->position[2]};
        if (!character3d_test_position(ctrl, pos, &hit))
            return;
        ctrl->body->position[0] -= hit.normal[0] * (hit.depth + 1e-4);
        ctrl->body->position[1] -= hit.normal[1] * (hit.depth + 1e-4);
        ctrl->body->position[2] -= hit.normal[2] * (hit.depth + 1e-4);
    }
}

/// @brief Sweep the controller along `delta`, stopping at the first contact.
///
/// Steps in `radius/4`-sized substeps; on the first substep that hits,
/// 14 iterations of bisection refine the impact `t`. On no-hit, body
/// is moved to the end position and the function returns 0. Step count
/// is bounded to 128.
static int character3d_sweep(rt_character3d *ctrl,
                             const double *delta,
                             rt_character_hit3d *out_hit) {
    if (!ctrl || !ctrl->body || vec3_len_sq(delta) < 1e-12)
        return 0;

    rt_body3d *body = ctrl->body;
    double start[3] = {body->position[0], body->position[1], body->position[2]};
    double end[3] = {start[0] + delta[0], start[1] + delta[1], start[2] + delta[2]};
    double move_len = sqrt(vec3_len_sq(delta));
    double step_dist = body->radius > 1e-6 ? body->radius * 0.25 : 0.05;
    int steps = (int)ceil(move_len / (step_dist > 0.05 ? step_dist : 0.05));
    double prev_t = 0.0;
    rt_character_hit3d hit;

    if (steps < 1)
        steps = 1;
    if (steps > 128)
        steps = 128;

    for (int s = 1; s <= steps; s++) {
        double t = (double)s / (double)steps;
        double pos[3] = {start[0] + delta[0] * t, start[1] + delta[1] * t, start[2] + delta[2] * t};
        if (!character3d_test_position(ctrl, pos, &hit)) {
            prev_t = t;
            continue;
        }

        {
            double lo = prev_t;
            double hi = t;
            rt_character_hit3d best_hit = hit;
            for (int iter = 0; iter < 14; iter++) {
                double mid = (lo + hi) * 0.5;
                double mid_pos[3] = {start[0] + delta[0] * mid,
                                     start[1] + delta[1] * mid,
                                     start[2] + delta[2] * mid};
                if (character3d_test_position(ctrl, mid_pos, &hit)) {
                    hi = mid;
                    best_hit = hit;
                } else {
                    lo = mid;
                }
            }

            body->position[0] = start[0] + delta[0] * lo;
            body->position[1] = start[1] + delta[1] * lo;
            body->position[2] = start[2] + delta[2] * lo;
            best_hit.hit = 1;
            best_hit.fraction = lo;
            if (out_hit)
                *out_hit = best_hit;
            return 1;
        }
    }

    body->position[0] = end[0];
    body->position[1] = end[1];
    body->position[2] = end[2];
    if (out_hit)
        memset(out_hit, 0, sizeof(*out_hit));
    return 0;
}

/// @brief Drop the controller 5cm and check for a walkable surface.
///
/// Used to detect grounded state when the controller is just barely
/// above the floor (after a small jump or when sliding down a slight
/// slope). Updates the body's grounded flag accordingly.
static int character3d_probe_ground(rt_character3d *ctrl) {
    if (!ctrl || !ctrl->body)
        return 0;
    double probe_pos[3] = {
        ctrl->body->position[0], ctrl->body->position[1] - 0.05, ctrl->body->position[2]};
    rt_character_hit3d hit;
    if (character3d_test_position(ctrl, probe_pos, &hit) &&
        character3d_normal_is_walkable(ctrl, hit.normal)) {
        character3d_set_ground_state(ctrl, 1, hit.normal);
        return 1;
    }
    character3d_set_ground_state(ctrl, 0, NULL);
    return 0;
}

/// @brief Attempt to step up over a small obstacle (FPS-style stair climb).
///
/// Three-phase test:
///   1. Sweep up by `step_height`. If blocked, abort.
///   2. Sweep horizontally by the leftover delta. If blocked, abort.
///   3. Sweep down (slightly past `step_height`) onto the new surface.
///      If the new surface is walkable, commit and mark grounded.
/// On any failure the controller is restored to its original position.
static int character3d_try_step(rt_character3d *ctrl, const double *horizontal_delta) {
    if (!ctrl || !ctrl->body || ctrl->step_height <= 1e-6 || vec3_len_sq(horizontal_delta) < 1e-12)
        return 0;

    double start[3] = {ctrl->body->position[0], ctrl->body->position[1], ctrl->body->position[2]};
    double up[3] = {0.0, ctrl->step_height, 0.0};
    rt_character_hit3d hit;

    if (character3d_sweep(ctrl, up, &hit)) {
        ctrl->body->position[0] = start[0];
        ctrl->body->position[1] = start[1];
        ctrl->body->position[2] = start[2];
        return 0;
    }
    character3d_resolve_penetration(ctrl);

    if (character3d_sweep(ctrl, horizontal_delta, &hit)) {
        ctrl->body->position[0] = start[0];
        ctrl->body->position[1] = start[1];
        ctrl->body->position[2] = start[2];
        return 0;
    }
    character3d_resolve_penetration(ctrl);

    {
        double down[3] = {0.0, -(ctrl->step_height + 0.05), 0.0};
        if (character3d_sweep(ctrl, down, &hit) &&
            character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            return 1;
        }
    }

    ctrl->body->position[0] = start[0];
    ctrl->body->position[1] = start[1];
    ctrl->body->position[2] = start[2];
    return 0;
}

/// @brief Slide-and-iterate motion solver — the heart of the controller's `Move`.
///
/// Up to 4 iterations of: resolve penetration → sweep → if hit,
/// project leftover motion onto the contact plane (or try step-up
/// for non-walkable hits) → continue with the leftover motion. This
/// gives the "slide along walls" feel typical of FPS controllers.
/// Vertical hits onto walkable surfaces also set the grounded flag
/// so gravity stops compounding.
static void character3d_move_axis(rt_character3d *ctrl,
                                  const double *initial_delta,
                                  int allow_step) {
    double remaining[3] = {initial_delta[0], initial_delta[1], initial_delta[2]};
    for (int iter = 0; iter < 4; iter++) {
        rt_character_hit3d hit;
        double leftover[3];

        if (vec3_len_sq(remaining) < 1e-12)
            return;

        character3d_resolve_penetration(ctrl);
        if (!character3d_sweep(ctrl, remaining, &hit))
            return;

        leftover[0] = remaining[0] * (1.0 - hit.fraction);
        leftover[1] = remaining[1] * (1.0 - hit.fraction);
        leftover[2] = remaining[2] * (1.0 - hit.fraction);

        if (allow_step && !character3d_normal_is_walkable(ctrl, hit.normal) &&
            character3d_try_step(ctrl, leftover))
            return;

        if (remaining[1] < 0.0 && character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            return;
        }

        {
            double into = vec3_dot(leftover, hit.normal);
            if (into > 0.0) {
                leftover[0] -= hit.normal[0] * into;
                leftover[1] -= hit.normal[1] * into;
                leftover[2] -= hit.normal[2] * into;
            } else {
                leftover[0] = leftover[1] = leftover[2] = 0.0;
            }
        }

        remaining[0] = leftover[0];
        remaining[1] = leftover[1];
        remaining[2] = leftover[2];
    }
}

/// @brief GC finalizer for `Character3D` — release the body and the world ref.
static void character3d_finalizer(void *obj) {
    rt_character3d *c = (rt_character3d *)obj;
    if (!c)
        return;
    if (c->body && rt_obj_release_check0(c->body))
        rt_obj_free(c->body);
    c->body = NULL;
    if (c->world && rt_obj_release_check0(c->world))
        rt_obj_free(c->world);
    c->world = NULL;
}

/// @brief `Physics3D.Character.New(radius, height, mass)` — make a capsule character.
///
/// Creates an internally-owned capsule body and wraps it. Defaults:
/// 30cm step height, 45° max walkable slope. The character is not
/// added to a world automatically — call `SetWorld` before using
/// `Move`.
void *rt_character3d_new(double radius, double height, double mass) {
    rt_character3d *c = (rt_character3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_character3d));
    if (!c) {
        rt_trap("Physics3D.Character.New: allocation failed");
        return NULL;
    }
    c->vptr = NULL;
    c->body = (rt_body3d *)rt_body3d_new_capsule(radius, height, mass);
    c->world = NULL;
    c->step_height = 0.3;
    c->slope_limit_cos = cos(45.0 * 3.14159265358979323846 / 180.0);
    c->is_grounded = 0;
    c->was_grounded = 0;
    rt_obj_set_finalizer(c, character3d_finalizer);
    return c;
}

/// @brief `Character3D.Move(velocity, dt)` — kinematic move with sliding.
///
/// Splits the velocity into horizontal (allows step-up) and vertical
/// (does not), runs `character3d_move_axis` for each, then probes the
/// ground if not already grounded. Updates the body's velocity to the
/// actual achieved displacement / dt — useful for animation systems
/// that read velocity off the controller.
void rt_character3d_move(void *obj, void *velocity_vec, double dt) {
    if (!obj || !velocity_vec || dt <= 0)
        return;
    rt_character3d *ctrl = (rt_character3d *)obj;
    rt_body3d *body = ctrl->body;
    if (!body)
        return;

    double vx = rt_vec3_x(velocity_vec);
    double vy = rt_vec3_y(velocity_vec);
    double vz = rt_vec3_z(velocity_vec);

    ctrl->was_grounded = ctrl->is_grounded;
    character3d_set_ground_state(ctrl, 0, NULL);

    {
        double start[3] = {body->position[0], body->position[1], body->position[2]};
        double horizontal[3] = {vx * dt, 0.0, vz * dt};
        double vertical[3] = {0.0, vy * dt, 0.0};

        character3d_resolve_penetration(ctrl);
        character3d_move_axis(ctrl, horizontal, 1);
        character3d_move_axis(ctrl, vertical, 0);
        character3d_resolve_penetration(ctrl);
        if (!ctrl->is_grounded)
            character3d_probe_ground(ctrl);

        body->velocity[0] = (body->position[0] - start[0]) / dt;
        body->velocity[1] = (body->position[1] - start[1]) / dt;
        body->velocity[2] = (body->position[2] - start[2]) / dt;
    }
}

/// @brief `Character3D.SetStepHeight(h)` — max obstacle height the controller can step over.
void rt_character3d_set_step_height(void *o, double h) {
    if (o)
        ((rt_character3d *)o)->step_height = h;
}

/// @brief `Character3D.GetStepHeight` — read the configured step height.
double rt_character3d_get_step_height(void *o) {
    return o ? ((rt_character3d *)o)->step_height : 0.3;
}

/// @brief `Character3D.SetSlopeLimit(degrees)` — max walkable slope angle.
///
/// Stored as `cos(angle)` to make the per-step "is this surface walkable"
/// test a single comparison (no trig in the hot path).
void rt_character3d_set_slope_limit(void *o, double degrees) {
    if (o)
        ((rt_character3d *)o)->slope_limit_cos = cos(degrees * 3.14159265358979323846 / 180.0);
}

/// @brief `Character3D.SetWorld(world)` — bind the character to a physics world.
///
/// Required before `Move` will collide against anything. Releases any
/// previous world reference and retains the new one. NULL detaches.
void rt_character3d_set_world(void *o, void *world) {
    if (!o)
        return;
    rt_character3d *ctrl = (rt_character3d *)o;
    if (ctrl->world == world)
        return;
    if (world)
        rt_obj_retain_maybe(world);
    if (ctrl->world && rt_obj_release_check0(ctrl->world))
        rt_obj_free(ctrl->world);
    ctrl->world = (rt_world3d *)world;
}

/// @brief `Character3D.GetWorld` — borrowed reference to the bound world.
void *rt_character3d_get_world(void *o) {
    return o ? ((rt_character3d *)o)->world : NULL;
}

/// @brief `Character3D.IsGrounded` — true when standing on a walkable surface.
int8_t rt_character3d_is_grounded(void *o) {
    return o ? ((rt_character3d *)o)->is_grounded : 0;
}

/// @brief `Character3D.JustLanded` — edge-detect: true on the first frame after landing.
///
/// Compares this frame's grounded state to the previous frame's. Useful
/// for landing animations, fall-damage triggers, dust puffs, etc.
int8_t rt_character3d_just_landed(void *o) {
    if (!o)
        return 0;
    rt_character3d *c = (rt_character3d *)o;
    return c->is_grounded && !c->was_grounded;
}

/// @brief `Character3D.GetPosition` — fresh `Vec3` of the body's position.
void *rt_character3d_get_position(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    return rt_body3d_get_position(((rt_character3d *)o)->body);
}

/// @brief `Character3D.SetPosition(x, y, z)` — teleport the controller.
///
/// Direct delegation to the underlying body. Caller is responsible for
/// avoiding teleports into geometry.
void rt_character3d_set_position(void *o, double x, double y, double z) {
    if (o)
        rt_body3d_set_position(((rt_character3d *)o)->body, x, y, z);
}

/*==========================================================================
 * Trigger3D — standalone AABB zone with enter/exit edge detection
 *=========================================================================*/

#define TRG3D_MAX_TRACKED 64

typedef struct {
    void *vptr;
    double bounds_min[3];
    double bounds_max[3];
    void *tracked_bodies[TRG3D_MAX_TRACKED];
    int8_t was_inside[TRG3D_MAX_TRACKED];
    int8_t is_inside[TRG3D_MAX_TRACKED];
    int32_t tracked_count;
    int32_t enter_count;
    int32_t exit_count;
} rt_trigger3d;

/// @brief GC finalizer for `Trigger3D` — no-op (tracked-body refs are weak).
///
/// Tracked bodies are stored as raw pointers (we don't retain them
/// because the trigger is only an observer). Weak refs is fine here:
/// if a tracked body is destroyed the next `Update` will discover the
/// stale pointer and clean it up.
static void trigger3d_finalizer(void *obj) {
    (void)obj;
}

/// @brief `Trigger3D.New(x0, y0, z0, x1, y1, z1)` — make an axis-aligned trigger zone.
///
/// Auto-orders the corners so caller can pass them in any order. Up to
/// 64 bodies are tracked for enter/exit edge detection.
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1) {
    rt_trigger3d *t = (rt_trigger3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_trigger3d));
    if (!t) {
        rt_trap("Trigger3D.New: allocation failed");
        return NULL;
    }
    memset(t, 0, sizeof(rt_trigger3d));
    t->bounds_min[0] = x0 < x1 ? x0 : x1;
    t->bounds_min[1] = y0 < y1 ? y0 : y1;
    t->bounds_min[2] = z0 < z1 ? z0 : z1;
    t->bounds_max[0] = x0 > x1 ? x0 : x1;
    t->bounds_max[1] = y0 > y1 ? y0 : y1;
    t->bounds_max[2] = z0 > z1 ? z0 : z1;
    rt_obj_set_finalizer(t, trigger3d_finalizer);
    return t;
}

/// @brief `Trigger3D.Contains(point)` — point-in-AABB test for a `Vec3`.
///
/// Synchronous query; doesn't update enter/exit state. Use this for
/// ad-hoc "is the player in the safe zone" checks; use `Update` +
/// `EnterCount`/`ExitCount` for transition-based logic.
int8_t rt_trigger3d_contains(void *obj, void *point) {
    if (!obj || !point)
        return 0;
    rt_trigger3d *t = (rt_trigger3d *)obj;
    double px = rt_vec3_x(point), py = rt_vec3_y(point), pz = rt_vec3_z(point);
    return (px >= t->bounds_min[0] && px <= t->bounds_max[0] && py >= t->bounds_min[1] &&
            py <= t->bounds_max[1] && pz >= t->bounds_min[2] && pz <= t->bounds_max[2])
               ? 1
               : 0;
}

/// @brief Find a tracked body's slot, or claim a new slot if the table has room.
///
/// Returns -1 when the 64-slot table is full; the caller skips the
/// body for this frame in that case.
static int32_t trigger3d_find_or_add(rt_trigger3d *t, void *body) {
    for (int32_t i = 0; i < t->tracked_count; i++)
        if (t->tracked_bodies[i] == body)
            return i;
    if (t->tracked_count >= TRG3D_MAX_TRACKED)
        return -1;
    int32_t idx = t->tracked_count++;
    t->tracked_bodies[idx] = body;
    t->was_inside[idx] = 0;
    t->is_inside[idx] = 0;
    return idx;
}

/// @brief `Trigger3D.Update(world)` — recompute occupancy and edge counts.
///
/// Tests every body in `world` for point-in-AABB inclusion (using body
/// center as the query point). Diffs current frame vs. previous to
/// produce `enter_count` and `exit_count` totals — no per-body events
/// are stored, so callers learn "how many entered" but not "which".
/// Run once per frame after `World3D.Step`.
void rt_trigger3d_update(void *obj, void *world_obj) {
    if (!obj || !world_obj)
        return;
    rt_trigger3d *t = (rt_trigger3d *)obj;
    rt_world3d *w = (rt_world3d *)world_obj;

    /* Swap current → previous */
    for (int32_t i = 0; i < t->tracked_count; i++) {
        t->was_inside[i] = t->is_inside[i];
        t->is_inside[i] = 0;
    }
    t->enter_count = 0;
    t->exit_count = 0;

    /* Test every body in the world */
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;

        /* Point-in-AABB test using body center */
        int8_t inside = (b->position[0] >= t->bounds_min[0] && b->position[0] <= t->bounds_max[0] &&
                         b->position[1] >= t->bounds_min[1] && b->position[1] <= t->bounds_max[1] &&
                         b->position[2] >= t->bounds_min[2] && b->position[2] <= t->bounds_max[2])
                            ? 1
                            : 0;

        int32_t idx = trigger3d_find_or_add(t, b);
        if (idx < 0)
            continue; /* tracking full */

        t->is_inside[idx] = inside;
        if (inside && !t->was_inside[idx])
            t->enter_count++;
        if (!inside && t->was_inside[idx])
            t->exit_count++;
    }
}

/// @brief `Trigger3D.EnterCount` — bodies that entered this trigger this frame.
int64_t rt_trigger3d_get_enter_count(void *obj) {
    return obj ? ((rt_trigger3d *)obj)->enter_count : 0;
}

/// @brief `Trigger3D.ExitCount` — bodies that left this trigger this frame.
int64_t rt_trigger3d_get_exit_count(void *obj) {
    return obj ? ((rt_trigger3d *)obj)->exit_count : 0;
}

/// @brief `Trigger3D.SetBounds(x0..z1)` — replace the trigger's AABB.
///
/// Auto-orders the corners. Tracked-body state is preserved across the
/// resize, so a body that was inside the old box and is also inside
/// the new box remains "in" without firing an enter event.
void rt_trigger3d_set_bounds(
    void *obj, double x0, double y0, double z0, double x1, double y1, double z1) {
    if (!obj)
        return;
    rt_trigger3d *t = (rt_trigger3d *)obj;
    t->bounds_min[0] = x0 < x1 ? x0 : x1;
    t->bounds_min[1] = y0 < y1 ? y0 : y1;
    t->bounds_min[2] = z0 < z1 ? z0 : z1;
    t->bounds_max[0] = x0 > x1 ? x0 : x1;
    t->bounds_max[1] = y0 > y1 ? y0 : y1;
    t->bounds_max[2] = z0 > z1 ? z0 : z1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
