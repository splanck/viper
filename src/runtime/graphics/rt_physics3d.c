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

#define PH3D_MAX_BODIES 256
#define PH3D_SHAPE_AABB 0
#define PH3D_SHAPE_SPHERE 1
#define PH3D_SHAPE_CAPSULE 2

/*==========================================================================
 * Body3D
 *=========================================================================*/

typedef struct {
    void *vptr;
    double position[3];
    double velocity[3];
    double force[3];
    double mass;
    double inv_mass;
    double restitution;
    double friction;
    int64_t collision_layer;
    int64_t collision_mask;
    int32_t shape;
    double half_extents[3];
    double radius;
    double height;
    int8_t is_static;
    int8_t is_trigger;
    int8_t is_grounded;
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

/*==========================================================================
 * Collision detection helpers
 *=========================================================================*/

static void body_aabb(const rt_body3d *b, double *mn, double *mx) {
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

/// @brief Test collision between two bodies. Returns 1 if colliding.
/// Sets normal (A→B push direction) and depth.
/// Uses shape-specific narrow phase for sphere and AABB-sphere pairs.
static int test_collision(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth) {
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

void rt_world3d_step(void *obj, double dt) {
    if (!obj || dt <= 0)
        return;
    rt_world3d *w = (rt_world3d *)obj;

    /* Phase 1: Integration (symplectic Euler) */
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b || b->is_static || b->inv_mass == 0)
            continue;

        b->velocity[0] += (w->gravity[0] + b->force[0] * b->inv_mass) * dt;
        b->velocity[1] += (w->gravity[1] + b->force[1] * b->inv_mass) * dt;
        b->velocity[2] += (w->gravity[2] + b->force[2] * b->inv_mass) * dt;

        b->position[0] += b->velocity[0] * dt;
        b->position[1] += b->velocity[1] * dt;
        b->position[2] += b->velocity[2] * dt;

        b->force[0] = b->force[1] = b->force[2] = 0;
        b->is_grounded = 0;
    }

    /* Phase 2: Collision detection + response (with contact recording) */
    w->contact_count = 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        for (int32_t j = i + 1; j < w->body_count; j++) {
            rt_body3d *a = w->bodies[i], *b = w->bodies[j];
            if (!a || !b)
                continue;
            if (a->is_static && b->is_static)
                continue;

            /* Layer filtering */
            if (!(a->collision_layer & b->collision_mask))
                continue;
            if (!(b->collision_layer & a->collision_mask))
                continue;

            double normal[3], depth;
            if (!test_collision(a, b, normal, &depth))
                continue;

            /* Record contact for event queries */
            if (w->contact_count < PH3D_MAX_CONTACTS) {
                rt_contact3d *c = &w->contacts[w->contact_count++];
                c->body_a = a;
                c->body_b = b;
                c->normal[0] = normal[0];
                c->normal[1] = normal[1];
                c->normal[2] = normal[2];
                c->depth = depth;
            }

            /* Trigger: detect overlap but don't resolve */
            if (a->is_trigger || b->is_trigger)
                continue;

            resolve_collision(a, b, normal, depth);

            /* Ground detection: normal points A→B.
             * n[1] > 0.7: A→B is upward → B rests on A → B is grounded.
             * n[1] < -0.7: A→B is downward → A rests on B → A is grounded. */
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

    /* Phase 3: Joint constraint solving (6 iterations for stability) */
    for (int32_t iter = 0; iter < 6; iter++) {
        for (int32_t j = 0; j < w->joint_count; j++) {
            if (w->joints[j])
                rt_joint3d_solve(w->joints[j], w->joint_types[j], dt);
        }
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
    (void)obj;
}

static void *make_body(int32_t shape, double mass) {
    rt_body3d *b = (rt_body3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body3d));
    if (!b) {
        rt_trap("Physics3D.Body.New: allocation failed");
        return NULL;
    }
    memset(b, 0, sizeof(rt_body3d));
    b->shape = shape;
    b->mass = mass;
    b->inv_mass = mass > 1e-12 ? 1.0 / mass : 0.0;
    b->restitution = 0.3;
    b->friction = 0.5;
    b->collision_layer = 1;
    b->collision_mask = ~(int64_t)0;
    b->is_static = (mass <= 1e-12) ? 1 : 0;
    rt_obj_set_finalizer(b, body3d_finalizer);
    return b;
}

void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_AABB, mass);
    if (b) {
        b->half_extents[0] = hx;
        b->half_extents[1] = hy;
        b->half_extents[2] = hz;
    }
    return b;
}

void *rt_body3d_new_sphere(double radius, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_SPHERE, mass);
    if (b)
        b->radius = radius;
    return b;
}

void *rt_body3d_new_capsule(double radius, double height, double mass) {
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_CAPSULE, mass);
    if (b) {
        b->radius = radius;
        b->height = height;
    }
    return b;
}

void rt_body3d_set_position(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->position[0] = x;
        b->position[1] = y;
        b->position[2] = z;
    }
}

void *rt_body3d_get_position(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->position[0], b->position[1], b->position[2]);
}

void rt_body3d_set_velocity(void *o, double x, double y, double z) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->velocity[0] = x;
        b->velocity[1] = y;
        b->velocity[2] = z;
    }
}

void *rt_body3d_get_velocity(void *o) {
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->velocity[0], b->velocity[1], b->velocity[2]);
}

void rt_body3d_apply_force(void *o, double fx, double fy, double fz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->force[0] += fx;
        b->force[1] += fy;
        b->force[2] += fz;
    }
}

void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz) {
    if (o) {
        rt_body3d *b = (rt_body3d *)o;
        b->velocity[0] += ix * b->inv_mass;
        b->velocity[1] += iy * b->inv_mass;
        b->velocity[2] += iz * b->inv_mass;
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
        b->is_static = s;
        if (s)
            b->inv_mass = 0;
        else
            b->inv_mass = b->mass > 1e-12 ? 1.0 / b->mass : 0.0;
    }
}

int8_t rt_body3d_is_static(void *o) {
    return o ? ((rt_body3d *)o)->is_static : 0;
}

void rt_body3d_set_trigger(void *o, int8_t t) {
    if (o)
        ((rt_body3d *)o)->is_trigger = t;
}

int8_t rt_body3d_is_trigger(void *o) {
    return o ? ((rt_body3d *)o)->is_trigger : 0;
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
    if (other->is_trigger || !other->is_static)
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
