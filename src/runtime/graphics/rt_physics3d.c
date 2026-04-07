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

typedef struct {
    rt_body3d *body_a;
    rt_body3d *body_b;
    double normal[3];
    double depth;
} rt_contact3d;

typedef struct {
    void *vptr;
    double gravity[3];
    rt_body3d *bodies[PH3D_MAX_BODIES];
    int32_t body_count;
    rt_contact3d contacts[PH3D_MAX_CONTACTS];
    int32_t contact_count;
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
static int test_collision(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth);

/*==========================================================================
 * Collision detection helpers
 *=========================================================================*/

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

static double clampd(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static double vec3_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double vec3_len_sq(const double *v) {
    return vec3_dot(v, v);
}

static double vec3_len(const double *v) {
    return sqrt(vec3_len_sq(v));
}

static void vec3_copy(double *dst, const double *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static void vec3_set(double *dst, double x, double y, double z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

static void vec3_scale_in_place(double *dst, double s) {
    dst[0] *= s;
    dst[1] *= s;
    dst[2] *= s;
}

static void quat_identity(double *q) {
    q[0] = 0.0;
    q[1] = 0.0;
    q[2] = 0.0;
    q[3] = 1.0;
}

static double quat_len_sq(const double *q) {
    return q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
}

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

static void quat_mul(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

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

static void quat_conjugate(const double *q, double *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

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

static void collider_pose_identity(rt_collider_pose *pose) {
    if (!pose)
        return;
    vec3_set(pose->position, 0.0, 0.0, 0.0);
    quat_identity(pose->rotation);
    vec3_set(pose->scale, 1.0, 1.0, 1.0);
}

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

static void body3d_wake_if_dynamic(rt_body3d *b) {
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    b->is_sleeping = 0;
    b->sleep_time = 0.0;
}

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

static void make_temp_sphere(rt_body3d *out, const double *center, double radius) {
    memset(out, 0, sizeof(*out));
    out->shape = PH3D_SHAPE_SPHERE;
    out->position[0] = center[0];
    out->position[1] = center[1];
    out->position[2] = center[2];
    out->radius = radius;
}

static void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c) {
    double half_axis = fmax(b->height * 0.5 - b->radius, 0.0);
    vec3_set(a, b->position[0], b->position[1] - half_axis, b->position[2]);
    vec3_set(c, b->position[0], b->position[1] + half_axis, b->position[2]);
}

static void closest_point_capsule_axis_to_point(const rt_body3d *cap,
                                                const double *point,
                                                double *closest) {
    double a[3], c[3];
    capsule_axis_endpoints(cap, a, c);
    closest[0] = cap->position[0];
    closest[1] = clampd(point[1], a[1], c[1]);
    closest[2] = cap->position[2];
}

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

static int collider_type_is_simple(int64_t type) {
    return type == RT_COLLIDER3D_TYPE_BOX || type == RT_COLLIDER3D_TYPE_SPHERE ||
           type == RT_COLLIDER3D_TYPE_CAPSULE;
}

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

static int test_collider_pair(const rt_body3d *a_body,
                              void *a_collider,
                              const rt_collider_pose *a_pose,
                              const rt_body3d *b_body,
                              void *b_collider,
                              const rt_collider_pose *b_pose,
                              double *normal,
                              double *depth) {
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
                if (test_collider_pair(
                        a_body, child, &child_pose, b_body, b_collider, b_pose, cur_normal, &cur_depth) &&
                    cur_depth > best_depth) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
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
                if (test_collider_pair(
                        a_body, a_collider, a_pose, b_body, child, &child_pose, cur_normal, &cur_depth) &&
                    cur_depth > best_depth) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
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
        return test_simple_collision(&proxy_a, &proxy_b, normal, depth);
    }

    if ((a_type == RT_COLLIDER3D_TYPE_CONVEX_HULL || a_type == RT_COLLIDER3D_TYPE_MESH) &&
        collider_type_is_simple(b_type)) {
        rt_body3d proxy_b;
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(a_collider);
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b) || !mesh)
            return 0;
        if (proxy_b.shape == PH3D_SHAPE_SPHERE)
            return test_meshlike_sphere(mesh, a_pose, &proxy_b, normal, depth);
        if (proxy_b.shape == PH3D_SHAPE_CAPSULE)
            return test_meshlike_capsule(mesh, a_pose, &proxy_b, normal, depth);
        return test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth);
    }

    if (collider_type_is_simple(a_type) &&
        (b_type == RT_COLLIDER3D_TYPE_CONVEX_HULL || b_type == RT_COLLIDER3D_TYPE_MESH)) {
        int hit;
        hit = test_collider_pair(
            b_body, b_collider, b_pose, a_body, a_collider, a_pose, normal, depth);
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
        if (proxy_b.shape == PH3D_SHAPE_SPHERE)
            return test_heightfield_sphere(a_collider, a_pose, &proxy_b, normal, depth);
        if (proxy_b.shape == PH3D_SHAPE_CAPSULE)
            return test_heightfield_capsule(a_collider, a_pose, &proxy_b, normal, depth);
        return test_heightfield_box(a_collider, a_pose, &proxy_b, normal, depth);
    }

    if (collider_type_is_simple(a_type) && b_type == RT_COLLIDER3D_TYPE_HEIGHTFIELD) {
        int hit;
        hit = test_collider_pair(
            b_body, b_collider, b_pose, a_body, a_collider, a_pose, normal, depth);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    return test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth);
}

/// @brief Test collision between two bodies. Returns 1 if colliding.
/// Sets normal (A→B push direction) and depth.
static int test_collision(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth) {
    rt_collider_pose a_pose;
    rt_collider_pose b_pose;
    if (!a || !b || !a->collider || !b->collider)
        return 0;
    collider_pose_from_body(a, &a_pose);
    collider_pose_from_body(b, &b_pose);
    return test_collider_pair(a, a->collider, &a_pose, b, b->collider, &b_pose, normal, depth);
}

/// @brief Apply impulse-based collision response between two bodies.
static void resolve_collision(rt_body3d *a, rt_body3d *b, const double *n, double depth) {
    double rv = (b->velocity[0] - a->velocity[0]) * n[0] +
                (b->velocity[1] - a->velocity[1]) * n[1] + (b->velocity[2] - a->velocity[2]) * n[2];
    if (rv > 0)
        return; /* separating */

    double e = (a->restitution < b->restitution) ? a->restitution : b->restitution;
    double inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum < 1e-12)
        return;

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
}

/*==========================================================================
 * World3D
 *=========================================================================*/

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
    w->joint_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    memset(w->joints, 0, sizeof(w->joints));
    rt_obj_set_finalizer(w, world3d_finalizer);
    return w;
}

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
                    double normal[3], depth;
                    if (!test_collision(a, b, normal, &depth))
                        continue;

                    if (w->contact_count < PH3D_MAX_CONTACTS) {
                        rt_contact3d *c = &w->contacts[w->contact_count++];
                        c->body_a = a;
                        c->body_b = b;
                        c->normal[0] = normal[0];
                        c->normal[1] = normal[1];
                        c->normal[2] = normal[2];
                        c->depth = depth;
                    }

                    if (!(a->is_trigger || b->is_trigger))
                        resolve_collision(a, b, normal, depth);

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

    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->force[0] = b->force[1] = b->force[2] = 0.0;
        b->torque[0] = b->torque[1] = b->torque[2] = 0.0;
    }
}

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

int64_t rt_world3d_joint_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->joint_count : 0;
}

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

int64_t rt_world3d_body_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->body_count : 0;
}

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

int64_t rt_world3d_get_collision_count(void *obj) {
    return obj ? ((rt_world3d *)obj)->contact_count : 0;
}

void *rt_world3d_get_collision_body_a(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_a;
}

void *rt_world3d_get_collision_body_b(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_b;
}

void *rt_world3d_get_collision_normal(void *obj, int64_t index) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(
        w->contacts[index].normal[0], w->contacts[index].normal[1], w->contacts[index].normal[2]);
}

double rt_world3d_get_collision_depth(void *obj, int64_t index) {
    if (!obj)
        return 0;
    rt_world3d *w = (rt_world3d *)obj;
    if (index < 0 || index >= w->contact_count)
        return 0;
    return w->contacts[index].depth;
}

/*==========================================================================
 * Body3D
 *=========================================================================*/

static void body3d_finalizer(void *obj) {
    rt_body3d *b = (rt_body3d *)obj;
    if (!b)
        return;
    if (b->collider && rt_obj_release_check0(b->collider))
        rt_obj_free(b->collider);
    b->collider = NULL;
}

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

void *rt_body3d_new(double mass) {
    return make_body(mass);
}

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

void rt_body3d_set_collider(void *o, void *collider) {
    if (!o)
        return;
    body3d_assign_collider(
        (rt_body3d *)o, collider, "Physics3DBody.SetCollider: collider requires a static body");
}

void *rt_body3d_get_collider(void *o) {
    return o ? ((rt_body3d *)o)->collider : NULL;
}

void rt_body3d_set_position(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->position[0] = x;
        b->position[1] = y;
        b->position[2] = z;
        body3d_wake_if_dynamic(b);
    }
}

void *rt_body3d_get_position(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->position[0], b->position[1], b->position[2]);
}

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

void *rt_body3d_get_orientation(void *o) {
    if (!o)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    {
        rt_body3d *b = (rt_body3d *)o;
        return rt_quat_new(
            b->orientation[0], b->orientation[1], b->orientation[2], b->orientation[3]);
    }
}

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

void *rt_body3d_get_velocity(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->velocity[0], b->velocity[1], b->velocity[2]);
}

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

void *rt_body3d_get_angular_velocity(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    {
        rt_body3d *b = (rt_body3d *)o;
        return rt_vec3_new(
            b->angular_velocity[0], b->angular_velocity[1], b->angular_velocity[2]);
    }
}

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

void rt_body3d_set_restitution(void *o, double r) {
    if (o)
        ((rt_body3d *)o)->restitution = r;
}

double rt_body3d_get_restitution(void *o) {
    return o ? ((rt_body3d *)o)->restitution : 0;
}

void rt_body3d_set_friction(void *o, double f) {
    if (o)
        ((rt_body3d *)o)->friction = f;
}

double rt_body3d_get_friction(void *o) {
    return o ? ((rt_body3d *)o)->friction : 0;
}

void rt_body3d_set_linear_damping(void *o, double d) {
    if (o)
        ((rt_body3d *)o)->linear_damping = d > 0.0 ? d : 0.0;
}

double rt_body3d_get_linear_damping(void *o) {
    return o ? ((rt_body3d *)o)->linear_damping : 0.0;
}

void rt_body3d_set_angular_damping(void *o, double d) {
    if (o)
        ((rt_body3d *)o)->angular_damping = d > 0.0 ? d : 0.0;
}

double rt_body3d_get_angular_damping(void *o) {
    return o ? ((rt_body3d *)o)->angular_damping : 0.0;
}

void rt_body3d_set_collision_layer(void *o, int64_t l) {
    if (o)
        ((rt_body3d *)o)->collision_layer = l;
}

int64_t rt_body3d_get_collision_layer(void *o) {
    return o ? ((rt_body3d *)o)->collision_layer : 0;
}

void rt_body3d_set_collision_mask(void *o, int64_t m) {
    if (o)
        ((rt_body3d *)o)->collision_mask = m;
}

int64_t rt_body3d_get_collision_mask(void *o) {
    return o ? ((rt_body3d *)o)->collision_mask : 0;
}

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

int8_t rt_body3d_is_static(void *o) {
    return o ? ((rt_body3d *)o)->is_static : 0;
}

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

int8_t rt_body3d_is_kinematic(void *o) {
    return o ? ((rt_body3d *)o)->is_kinematic : 0;
}

void rt_body3d_set_trigger(void *o, int8_t t) {
    if (o)
        ((rt_body3d *)o)->is_trigger = t;
}

int8_t rt_body3d_is_trigger(void *o) {
    return o ? ((rt_body3d *)o)->is_trigger : 0;
}

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

int8_t rt_body3d_can_sleep(void *o) {
    return o ? ((rt_body3d *)o)->can_sleep : 0;
}

int8_t rt_body3d_is_sleeping(void *o) {
    return o ? ((rt_body3d *)o)->is_sleeping : 0;
}

void rt_body3d_wake(void *o) {
    if (o)
        body3d_wake_if_dynamic((rt_body3d *)o);
}

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

void rt_body3d_set_use_ccd(void *o, int8_t use_ccd) {
    if (o)
        ((rt_body3d *)o)->use_ccd = use_ccd ? 1 : 0;
}

int8_t rt_body3d_get_use_ccd(void *o) {
    return o ? ((rt_body3d *)o)->use_ccd : 0;
}

int8_t rt_body3d_is_grounded(void *o) {
    return o ? ((rt_body3d *)o)->is_grounded : 0;
}

void *rt_body3d_get_ground_normal(void *o) {
    if (!o)
        return rt_vec3_new(0, 1, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->ground_normal[0], b->ground_normal[1], b->ground_normal[2]);
}

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

static int character3d_normal_is_walkable(const rt_character3d *ctrl, const double *normal) {
    return ctrl && normal && (-normal[1] >= ctrl->slope_limit_cos);
}

static int character3d_candidate_body(const rt_character3d *ctrl, const rt_body3d *other) {
    if (!ctrl || !ctrl->body || !ctrl->world || !other)
        return 0;
    if (other == ctrl->body)
        return 0;
    if (other->is_trigger || other->motion_mode == PH3D_MODE_DYNAMIC)
        return 0;
    return bodies_can_collide(ctrl->body, other);
}

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
        if (!test_collision(body, other, normal, &depth))
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

void rt_character3d_set_step_height(void *o, double h) {
    if (o)
        ((rt_character3d *)o)->step_height = h;
}

double rt_character3d_get_step_height(void *o) {
    return o ? ((rt_character3d *)o)->step_height : 0.3;
}

void rt_character3d_set_slope_limit(void *o, double degrees) {
    if (o)
        ((rt_character3d *)o)->slope_limit_cos = cos(degrees * 3.14159265358979323846 / 180.0);
}

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

void *rt_character3d_get_world(void *o) {
    return o ? ((rt_character3d *)o)->world : NULL;
}

int8_t rt_character3d_is_grounded(void *o) {
    return o ? ((rt_character3d *)o)->is_grounded : 0;
}

int8_t rt_character3d_just_landed(void *o) {
    if (!o)
        return 0;
    rt_character3d *c = (rt_character3d *)o;
    return c->is_grounded && !c->was_grounded;
}

void *rt_character3d_get_position(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    return rt_body3d_get_position(((rt_character3d *)o)->body);
}

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

static void trigger3d_finalizer(void *obj) {
    (void)obj;
}

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

/// @brief Test if a Vec3 point is inside the trigger AABB.
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

/// @brief Find or insert a body in the tracked list. Returns index or -1 if full.
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

/// @brief Check all bodies in a Physics3D world against this trigger.
/// Computes enter/exit counts by comparing current vs. previous frame.
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

int64_t rt_trigger3d_get_enter_count(void *obj) {
    return obj ? ((rt_trigger3d *)obj)->enter_count : 0;
}

int64_t rt_trigger3d_get_exit_count(void *obj) {
    return obj ? ((rt_trigger3d *)obj)->exit_count : 0;
}

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
