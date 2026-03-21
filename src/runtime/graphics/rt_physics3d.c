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
#include "rt_raycast3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
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

typedef struct
{
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

typedef struct
{
    void *vptr;
    double gravity[3];
    rt_body3d *bodies[PH3D_MAX_BODIES];
    int32_t body_count;
} rt_world3d;

typedef struct
{
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

static void body_aabb(const rt_body3d *b, double *mn, double *mx)
{
    if (b->shape == PH3D_SHAPE_AABB)
    {
        mn[0] = b->position[0] - b->half_extents[0];
        mn[1] = b->position[1] - b->half_extents[1];
        mn[2] = b->position[2] - b->half_extents[2];
        mx[0] = b->position[0] + b->half_extents[0];
        mx[1] = b->position[1] + b->half_extents[1];
        mx[2] = b->position[2] + b->half_extents[2];
    }
    else if (b->shape == PH3D_SHAPE_SPHERE)
    {
        mn[0] = b->position[0] - b->radius;
        mn[1] = b->position[1] - b->radius;
        mn[2] = b->position[2] - b->radius;
        mx[0] = b->position[0] + b->radius;
        mx[1] = b->position[1] + b->radius;
        mx[2] = b->position[2] + b->radius;
    }
    else /* capsule */
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

/// @brief Test collision between two bodies. Returns 1 if colliding.
/// Sets normal (A→B push direction) and depth.
static int test_collision(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth)
{
    /* Broad phase: AABB overlap test */
    double amn[3], amx[3], bmn[3], bmx[3];
    body_aabb(a, amn, amx);
    body_aabb(b, bmn, bmx);
    if (amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
        amn[2] > bmx[2] || amx[2] < bmn[2])
        return 0;

    /* Narrow phase: use AABB penetration for all shapes (simplified) */
    /* Compute overlap per axis */
    double ox = (amx[0] < bmx[0] ? amx[0] - bmn[0] : bmx[0] - amn[0]);
    double oy = (amx[1] < bmx[1] ? amx[1] - bmn[1] : bmx[1] - amn[1]);
    double oz = (amx[2] < bmx[2] ? amx[2] - bmn[2] : bmx[2] - amn[2]);
    if (ox <= 0 || oy <= 0 || oz <= 0)
        return 0;

    /* Push out on axis of minimum overlap.
     * Normal points from A toward B — matches impulse formula convention
     * where a.vel -= j*n pushes A away from B. */
    normal[0] = normal[1] = normal[2] = 0;
    if (ox <= oy && ox <= oz)
    {
        *depth = ox;
        normal[0] = (a->position[0] < b->position[0]) ? 1.0 : -1.0;
    }
    else if (oy <= oz)
    {
        *depth = oy;
        normal[1] = (a->position[1] < b->position[1]) ? 1.0 : -1.0;
    }
    else
    {
        *depth = oz;
        normal[2] = (a->position[2] < b->position[2]) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Apply impulse-based collision response between two bodies.
static void resolve_collision(rt_body3d *a, rt_body3d *b, const double *n, double depth)
{
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
        if (tlen > 1e-8)
        {
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

static void world3d_finalizer(void *obj)
{
    (void)obj; /* bodies are GC-managed */
}

void *rt_world3d_new(double gx, double gy, double gz)
{
    rt_world3d *w = (rt_world3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_world3d));
    if (!w)
    {
        rt_trap("Physics3D.World.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->gravity[0] = gx;
    w->gravity[1] = gy;
    w->gravity[2] = gz;
    w->body_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    rt_obj_set_finalizer(w, world3d_finalizer);
    return w;
}

void rt_world3d_step(void *obj, double dt)
{
    if (!obj || dt <= 0)
        return;
    rt_world3d *w = (rt_world3d *)obj;

    /* Phase 1: Integration (symplectic Euler) */
    for (int32_t i = 0; i < w->body_count; i++)
    {
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

    /* Phase 2: Collision detection + response */
    for (int32_t i = 0; i < w->body_count; i++)
    {
        for (int32_t j = i + 1; j < w->body_count; j++)
        {
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

            /* Trigger: detect overlap but don't resolve */
            if (a->is_trigger || b->is_trigger)
                continue;

            resolve_collision(a, b, normal, depth);

            /* Ground detection: normal points A→B.
             * n[1] > 0.7: A→B is upward → B rests on A → B is grounded.
             * n[1] < -0.7: A→B is downward → A rests on B → A is grounded. */
            if (normal[1] > 0.7)
            {
                b->is_grounded = 1;
                b->ground_normal[0] = normal[0];
                b->ground_normal[1] = normal[1];
                b->ground_normal[2] = normal[2];
            }
            else if (normal[1] < -0.7)
            {
                a->is_grounded = 1;
                a->ground_normal[0] = -normal[0];
                a->ground_normal[1] = -normal[1];
                a->ground_normal[2] = -normal[2];
            }
        }
    }
}

void rt_world3d_add(void *obj, void *body)
{
    if (!obj || !body)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    if (w->body_count >= PH3D_MAX_BODIES)
    {
        rt_trap("Physics3D: max body limit (256) exceeded");
        return;
    }
    w->bodies[w->body_count++] = (rt_body3d *)body;
}

void rt_world3d_remove(void *obj, void *body)
{
    if (!obj || !body)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    for (int32_t i = 0; i < w->body_count; i++)
    {
        if (w->bodies[i] == body)
        {
            w->bodies[i] = w->bodies[--w->body_count];
            w->bodies[w->body_count] = NULL;
            return;
        }
    }
}

int64_t rt_world3d_body_count(void *obj)
{
    return obj ? ((rt_world3d *)obj)->body_count : 0;
}

void rt_world3d_set_gravity(void *obj, double gx, double gy, double gz)
{
    if (!obj)
        return;
    rt_world3d *w = (rt_world3d *)obj;
    w->gravity[0] = gx;
    w->gravity[1] = gy;
    w->gravity[2] = gz;
}

/*==========================================================================
 * Body3D
 *=========================================================================*/

static void body3d_finalizer(void *obj)
{
    (void)obj;
}

static void *make_body(int32_t shape, double mass)
{
    rt_body3d *b = (rt_body3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body3d));
    if (!b)
    {
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

void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass)
{
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_AABB, mass);
    if (b)
    {
        b->half_extents[0] = hx;
        b->half_extents[1] = hy;
        b->half_extents[2] = hz;
    }
    return b;
}

void *rt_body3d_new_sphere(double radius, double mass)
{
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_SPHERE, mass);
    if (b)
        b->radius = radius;
    return b;
}

void *rt_body3d_new_capsule(double radius, double height, double mass)
{
    rt_body3d *b = (rt_body3d *)make_body(PH3D_SHAPE_CAPSULE, mass);
    if (b)
    {
        b->radius = radius;
        b->height = height;
    }
    return b;
}

void rt_body3d_set_position(void *o, double x, double y, double z)
{
    if (o)
    {
        rt_body3d *b = (rt_body3d *)o;
        b->position[0] = x;
        b->position[1] = y;
        b->position[2] = z;
    }
}

void *rt_body3d_get_position(void *o)
{
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->position[0], b->position[1], b->position[2]);
}

void rt_body3d_set_velocity(void *o, double x, double y, double z)
{
    if (o)
    {
        rt_body3d *b = (rt_body3d *)o;
        b->velocity[0] = x;
        b->velocity[1] = y;
        b->velocity[2] = z;
    }
}

void *rt_body3d_get_velocity(void *o)
{
    if (!o)
        return rt_vec3_new(0, 0, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->velocity[0], b->velocity[1], b->velocity[2]);
}

void rt_body3d_apply_force(void *o, double fx, double fy, double fz)
{
    if (o)
    {
        rt_body3d *b = (rt_body3d *)o;
        b->force[0] += fx;
        b->force[1] += fy;
        b->force[2] += fz;
    }
}

void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz)
{
    if (o)
    {
        rt_body3d *b = (rt_body3d *)o;
        b->velocity[0] += ix * b->inv_mass;
        b->velocity[1] += iy * b->inv_mass;
        b->velocity[2] += iz * b->inv_mass;
    }
}

void rt_body3d_set_restitution(void *o, double r)
{
    if (o)
        ((rt_body3d *)o)->restitution = r;
}

double rt_body3d_get_restitution(void *o)
{
    return o ? ((rt_body3d *)o)->restitution : 0;
}

void rt_body3d_set_friction(void *o, double f)
{
    if (o)
        ((rt_body3d *)o)->friction = f;
}

double rt_body3d_get_friction(void *o)
{
    return o ? ((rt_body3d *)o)->friction : 0;
}

void rt_body3d_set_collision_layer(void *o, int64_t l)
{
    if (o)
        ((rt_body3d *)o)->collision_layer = l;
}

int64_t rt_body3d_get_collision_layer(void *o)
{
    return o ? ((rt_body3d *)o)->collision_layer : 0;
}

void rt_body3d_set_collision_mask(void *o, int64_t m)
{
    if (o)
        ((rt_body3d *)o)->collision_mask = m;
}

int64_t rt_body3d_get_collision_mask(void *o)
{
    return o ? ((rt_body3d *)o)->collision_mask : 0;
}

void rt_body3d_set_static(void *o, int8_t s)
{
    if (o)
    {
        rt_body3d *b = (rt_body3d *)o;
        b->is_static = s;
        if (s)
            b->inv_mass = 0;
        else
            b->inv_mass = b->mass > 1e-12 ? 1.0 / b->mass : 0.0;
    }
}

int8_t rt_body3d_is_static(void *o)
{
    return o ? ((rt_body3d *)o)->is_static : 0;
}

void rt_body3d_set_trigger(void *o, int8_t t)
{
    if (o)
        ((rt_body3d *)o)->is_trigger = t;
}

int8_t rt_body3d_is_trigger(void *o)
{
    return o ? ((rt_body3d *)o)->is_trigger : 0;
}

int8_t rt_body3d_is_grounded(void *o)
{
    return o ? ((rt_body3d *)o)->is_grounded : 0;
}

void *rt_body3d_get_ground_normal(void *o)
{
    if (!o)
        return rt_vec3_new(0, 1, 0);
    rt_body3d *b = (rt_body3d *)o;
    return rt_vec3_new(b->ground_normal[0], b->ground_normal[1], b->ground_normal[2]);
}

double rt_body3d_get_mass(void *o)
{
    return o ? ((rt_body3d *)o)->mass : 0;
}

/*==========================================================================
 * Character Controller
 *=========================================================================*/

static void character3d_finalizer(void *obj)
{
    (void)obj;
}

void *rt_character3d_new(double radius, double height, double mass)
{
    rt_character3d *c = (rt_character3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_character3d));
    if (!c)
    {
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

void rt_character3d_move(void *obj, void *velocity_vec, double dt)
{
    if (!obj || !velocity_vec || dt <= 0)
        return;
    rt_character3d *ctrl = (rt_character3d *)obj;
    rt_body3d *body = ctrl->body;
    if (!body)
        return;

    double vx = rt_vec3_x(velocity_vec);
    double vy = rt_vec3_y(velocity_vec);
    double vz = rt_vec3_z(velocity_vec);

    /* Simple movement: apply velocity directly (physics world handles collision) */
    body->velocity[0] = vx;
    body->velocity[1] = vy;
    body->velocity[2] = vz;

    ctrl->was_grounded = ctrl->is_grounded;
    ctrl->is_grounded = body->is_grounded;
}

void rt_character3d_set_step_height(void *o, double h)
{
    if (o)
        ((rt_character3d *)o)->step_height = h;
}

double rt_character3d_get_step_height(void *o)
{
    return o ? ((rt_character3d *)o)->step_height : 0.3;
}

void rt_character3d_set_slope_limit(void *o, double degrees)
{
    if (o)
        ((rt_character3d *)o)->slope_limit_cos = cos(degrees * 3.14159265358979323846 / 180.0);
}

int8_t rt_character3d_is_grounded(void *o)
{
    return o ? ((rt_character3d *)o)->is_grounded : 0;
}

int8_t rt_character3d_just_landed(void *o)
{
    if (!o)
        return 0;
    rt_character3d *c = (rt_character3d *)o;
    return c->is_grounded && !c->was_grounded;
}

void *rt_character3d_get_position(void *o)
{
    if (!o)
        return rt_vec3_new(0, 0, 0);
    return rt_body3d_get_position(((rt_character3d *)o)->body);
}

void rt_character3d_set_position(void *o, double x, double y, double z)
{
    if (o)
        rt_body3d_set_position(((rt_character3d *)o)->body, x, y, z);
}

/*==========================================================================
 * Trigger3D — standalone AABB zone with enter/exit edge detection
 *=========================================================================*/

#define TRG3D_MAX_TRACKED 64

typedef struct
{
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

static void trigger3d_finalizer(void *obj)
{
    (void)obj;
}

void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1)
{
    rt_trigger3d *t = (rt_trigger3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_trigger3d));
    if (!t)
    {
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
int8_t rt_trigger3d_contains(void *obj, void *point)
{
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
static int32_t trigger3d_find_or_add(rt_trigger3d *t, void *body)
{
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
void rt_trigger3d_update(void *obj, void *world_obj)
{
    if (!obj || !world_obj)
        return;
    rt_trigger3d *t = (rt_trigger3d *)obj;
    rt_world3d *w = (rt_world3d *)world_obj;

    /* Swap current → previous */
    for (int32_t i = 0; i < t->tracked_count; i++)
    {
        t->was_inside[i] = t->is_inside[i];
        t->is_inside[i] = 0;
    }
    t->enter_count = 0;
    t->exit_count = 0;

    /* Test every body in the world */
    for (int32_t i = 0; i < w->body_count; i++)
    {
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

int64_t rt_trigger3d_get_enter_count(void *obj)
{
    return obj ? ((rt_trigger3d *)obj)->enter_count : 0;
}

int64_t rt_trigger3d_get_exit_count(void *obj)
{
    return obj ? ((rt_trigger3d *)obj)->exit_count : 0;
}

void rt_trigger3d_set_bounds(
    void *obj, double x0, double y0, double z0, double x1, double y1, double z1)
{
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

#endif /* VIPER_ENABLE_GRAPHICS */
