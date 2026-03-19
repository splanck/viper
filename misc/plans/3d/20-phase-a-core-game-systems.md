# Phase A: Core Game Systems

## Goal

Add 3D physics, trigger zones, and math helpers — transforming Viper from a rendering engine into a game engine where objects have gravity, collide with walls, and interact with trigger volumes.

## Dependencies

- Phase 13 complete (frustum culling, AABB infrastructure)
- Ray3D / AABB3D collision detection (already implemented in `rt_raycast3d.c`)
- Physics2D patterns in `src/runtime/graphics/rt_physics2d.c` (reference implementation)
- Vec3 math in `src/runtime/graphics/rt_vec3.c`

---

## A0. Prerequisite: Additional Collision Primitives (~150 LOC)

Before Physics3D can be implemented, these collision tests must be added to `rt_raycast3d.c`. The existing file has ray-triangle, ray-mesh, ray-AABB, ray-sphere, AABB-AABB overlap, and AABB penetration — but is missing shape-shape tests needed for physics.

### New functions in `src/runtime/graphics/rt_raycast3d.c`:

```c
/* Sphere-Sphere overlap */
int8_t rt_sphere3d_overlaps(void *center_a, double radius_a,
                             void *center_b, double radius_b);
// Algorithm: dist(a,b) < ra + rb

/* Sphere-Sphere penetration (push-out vector) */
void *rt_sphere3d_penetration(void *center_a, double radius_a,
                               void *center_b, double radius_b);
// Algorithm: dir = normalize(b-a), depth = (ra+rb) - dist, return dir * depth

/* AABB-Sphere overlap */
int8_t rt_aabb3d_sphere_overlaps(void *aabb_min, void *aabb_max,
                                  void *center, double radius);
// Algorithm: closest point on AABB to center, check dist < radius

/* Closest point on AABB to a point */
void *rt_aabb3d_closest_point(void *aabb_min, void *aabb_max, void *point);
// Algorithm: clamp point.xyz to [min.xyz, max.xyz]

/* Closest point on line segment to a point (needed for capsule) */
void *rt_segment3d_closest_point(void *seg_a, void *seg_b, void *point);
// Algorithm: project point onto segment, clamp t to [0,1]

/* Capsule-AABB overlap (capsule = segment + radius) */
int8_t rt_capsule3d_aabb_overlaps(void *cap_a, void *cap_b, double radius,
                                   void *aabb_min, void *aabb_max);
// Algorithm: closest point on segment to closest point on AABB, check dist < radius

/* Capsule-Sphere overlap */
int8_t rt_capsule3d_sphere_overlaps(void *cap_a, void *cap_b, double cap_radius,
                                     void *sphere_center, double sphere_radius);
// Algorithm: closest point on segment to sphere center, check dist < cap_r + sph_r

/* Swept AABB (for character movement) */
double rt_sweep_aabb3d(void *pos, void *velocity, double dt,
                        void *aabb_half, void *obstacle_min, void *obstacle_max,
                        void *out_normal);
// Algorithm: Minkowski sum expansion, ray-AABB test on expanded box
// Returns: fraction of movement [0,1] before collision, or 1.0 if no hit
// out_normal: collision surface normal (Vec3)
```

### runtime.def additions: 8 RT_FUNC
### Stubs: return 0/false/NULL
### Tests: 8 tests (one per function)

---

## A1. Physics3D + CharacterController (~800 LOC)

### New Files

**`src/runtime/graphics/rt_physics3d.h`** (~80 LOC)
**`src/runtime/graphics/rt_physics3d.c`** (~700 LOC)

### Namespace: `Viper.Game.Physics3D.*`

Following the Physics2D pattern (`Viper.Game.Physics2D.*` in runtime.def lines 4347-4412), Physics3D uses the `Viper.Game.Physics3D` namespace, NOT `Viper.Graphics3D`.

### Data Structures

```c
#define PH3D_MAX_BODIES 256
#define PH3D_SHAPE_AABB    0
#define PH3D_SHAPE_SPHERE  1
#define PH3D_SHAPE_CAPSULE 2

/* Internal body structure (mirrors Physics2D rt_body_impl pattern) */
typedef struct {
    void *vptr;               /* GC dispatch — MUST be first field */
    double position[3];       /* world position (center of shape) */
    double velocity[3];       /* linear velocity (units/sec) */
    double force[3];          /* accumulated force per-frame (cleared after step) */
    double mass;              /* 0 = static/infinite mass */
    double inv_mass;          /* 1/mass or 0 for static */
    double restitution;       /* bounciness [0,1] (default 0.3) */
    double friction;          /* kinetic friction [0,1] (default 0.5) */
    int64_t collision_layer;  /* bitmask: which layers this body occupies (default 1) */
    int64_t collision_mask;   /* bitmask: which layers to collide with (default ~0) */
    int32_t shape;            /* PH3D_SHAPE_AABB/SPHERE/CAPSULE */
    double half_extents[3];   /* AABB half-size (x,y,z) */
    double radius;            /* sphere/capsule radius */
    double height;            /* capsule total height (including hemispherical caps) */
    int8_t is_static;         /* 1 = infinite mass, doesn't move */
    int8_t is_trigger;        /* 1 = overlap-only, no physics response */
    int8_t is_grounded;       /* set by step: touching ground this frame */
    double ground_normal[3];  /* surface normal of ground contact (if grounded) */
} rt_body3d;

/* World structure (mirrors Physics2D rt_world_impl) */
typedef struct {
    void *vptr;               /* GC dispatch */
    double gravity[3];        /* world gravity (default 0, -9.8, 0) */
    rt_body3d *bodies[PH3D_MAX_BODIES]; /* fixed-size body array */
    int32_t body_count;
} rt_world3d;
```

### Physics Step Algorithm

Following Physics2D's Symplectic Euler integration and impulse-based collision response:

```c
void rt_world3d_step(void *obj, double dt) {
    rt_world3d *w = (rt_world3d *)obj;

    /* Phase 1: Integration (Symplectic Euler) */
    for (int i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (b->is_static || b->inv_mass == 0) continue;

        /* Accumulate gravity */
        b->velocity[0] += (w->gravity[0] + b->force[0] * b->inv_mass) * dt;
        b->velocity[1] += (w->gravity[1] + b->force[1] * b->inv_mass) * dt;
        b->velocity[2] += (w->gravity[2] + b->force[2] * b->inv_mass) * dt;

        /* Move */
        b->position[0] += b->velocity[0] * dt;
        b->position[1] += b->velocity[1] * dt;
        b->position[2] += b->velocity[2] * dt;

        /* Clear accumulated force */
        b->force[0] = b->force[1] = b->force[2] = 0;
        b->is_grounded = 0;
    }

    /* Phase 2: Collision detection + resolution */
    for (int i = 0; i < w->body_count; i++) {
        for (int j = i + 1; j < w->body_count; j++) {
            rt_body3d *a = w->bodies[i], *b = w->bodies[j];
            if (a->is_static && b->is_static) continue;

            /* Layer filtering (same as Physics2D) */
            if (!(a->collision_layer & b->collision_mask)) continue;
            if (!(b->collision_layer & a->collision_mask)) continue;

            /* Shape-pair dispatch */
            double normal[3], depth;
            if (!test_collision_pair(a, b, normal, &depth)) continue;

            /* Skip physics response for triggers */
            if (a->is_trigger || b->is_trigger) continue;

            /* Impulse-based collision response (same as Physics2D) */
            resolve_collision(a, b, normal, depth);

            /* Ground detection: if normal points mostly upward */
            if (normal[1] > 0.7) {
                if (a->position[1] < b->position[1]) a->is_grounded = 1;
                else b->is_grounded = 1;
            }
        }
    }
}
```

### Collision Pair Dispatch

```c
static int test_collision_pair(rt_body3d *a, rt_body3d *b,
                                double *normal, double *depth) {
    /* 6 shape combinations: AABB×AABB, AABB×Sphere, AABB×Capsule,
     * Sphere×Sphere, Sphere×Capsule, Capsule×Capsule */
    int sa = a->shape, sb = b->shape;
    if (sa > sb) { /* ensure consistent order */ swap(a, b); sa = a->shape; sb = b->shape; }

    switch (sa * 3 + sb) {
    case 0: /* AABB × AABB */
        return aabb_aabb_test(a, b, normal, depth);
    case 1: /* AABB × Sphere */
        return aabb_sphere_test(a, b, normal, depth);
    case 2: /* AABB × Capsule */
        return aabb_capsule_test(a, b, normal, depth);
    case 4: /* Sphere × Sphere */
        return sphere_sphere_test(a, b, normal, depth);
    case 5: /* Sphere × Capsule */
        return sphere_capsule_test(a, b, normal, depth);
    case 8: /* Capsule × Capsule */
        return capsule_capsule_test(a, b, normal, depth);
    }
    return 0;
}
```

### Impulse Resolution

Mirrors Physics2D (lines 29-31 of rt_physics2d.c):
```c
static void resolve_collision(rt_body3d *a, rt_body3d *b,
                               const double *n, double depth) {
    /* Relative velocity along collision normal */
    double rv = (b->velocity[0] - a->velocity[0]) * n[0] +
                (b->velocity[1] - a->velocity[1]) * n[1] +
                (b->velocity[2] - a->velocity[2]) * n[2];
    if (rv > 0) return; /* separating */

    /* Restitution (min of both bodies) */
    double e = (a->restitution < b->restitution) ? a->restitution : b->restitution;

    /* Impulse magnitude */
    double j = -(1.0 + e) * rv / (a->inv_mass + b->inv_mass);

    /* Apply impulse */
    a->velocity[0] -= j * a->inv_mass * n[0];
    a->velocity[1] -= j * a->inv_mass * n[1];
    a->velocity[2] -= j * a->inv_mass * n[2];
    b->velocity[0] += j * b->inv_mass * n[0];
    b->velocity[1] += j * b->inv_mass * n[1];
    b->velocity[2] += j * b->inv_mass * n[2];

    /* Positional correction (Baumgarte stabilization: 40% of excess penetration) */
    double slop = 0.01;
    double correction = fmax(depth - slop, 0.0) * 0.4 / (a->inv_mass + b->inv_mass);
    a->position[0] -= correction * a->inv_mass * n[0];
    a->position[1] -= correction * a->inv_mass * n[1];
    a->position[2] -= correction * a->inv_mass * n[2];
    b->position[0] += correction * b->inv_mass * n[0];
    b->position[1] += correction * b->inv_mass * n[1];
    b->position[2] += correction * b->inv_mass * n[2];
}
```

### Character Controller

```c
typedef struct {
    void *vptr;
    rt_body3d *body;          /* capsule body (already in world) */
    double step_height;       /* max step-up height (default 0.3) */
    double slope_limit_cos;   /* cos(max walkable slope angle) (default cos(45°)) */
    int8_t is_grounded;       /* computed each move */
    int8_t was_grounded;      /* previous frame */
} rt_character3d;
```

MoveAndSlide uses swept AABB for simplified character movement:
```c
void rt_character3d_move(void *obj, void *velocity_vec, double dt) {
    rt_character3d *ctrl = (rt_character3d *)obj;
    rt_body3d *body = ctrl->body;

    double vx = rt_vec3_x(velocity_vec);
    double vy = rt_vec3_y(velocity_vec);
    double vz = rt_vec3_z(velocity_vec);

    /* Apply gravity from the world */
    /* (caller should add gravity to velocity before calling move) */

    /* Swept collision: up to 3 slide iterations */
    for (int iter = 0; iter < 3; iter++) {
        double remaining_dt = dt;
        /* For each body in the world, test swept AABB */
        double earliest_t = 1.0;
        double hit_normal[3] = {0, 0, 0};

        /* ... find earliest collision fraction ... */

        /* Move by fraction */
        body->position[0] += vx * remaining_dt * earliest_t;
        body->position[1] += vy * remaining_dt * earliest_t;
        body->position[2] += vz * remaining_dt * earliest_t;

        if (earliest_t >= 1.0) break; /* no collision */

        /* Slide: remove velocity component along normal */
        double dot = vx * hit_normal[0] + vy * hit_normal[1] + vz * hit_normal[2];
        vx -= dot * hit_normal[0];
        vy -= dot * hit_normal[1];
        vz -= dot * hit_normal[2];

        dt *= (1.0 - earliest_t);
    }

    /* Ground detection: short raycast downward */
    ctrl->was_grounded = ctrl->is_grounded;
    ctrl->is_grounded = body->is_grounded;
}
```

### Public API

```c
/* World */
void   *rt_world3d_new(double gx, double gy, double gz);
void    rt_world3d_step(void *world, double dt);
void    rt_world3d_add(void *world, void *body);
void    rt_world3d_remove(void *world, void *body);
int64_t rt_world3d_body_count(void *world);
void    rt_world3d_set_gravity(void *world, double gx, double gy, double gz);

/* Body */
void   *rt_body3d_new_aabb(double hx, double hy, double hz, double mass);
void   *rt_body3d_new_sphere(double radius, double mass);
void   *rt_body3d_new_capsule(double radius, double height, double mass);
void    rt_body3d_set_position(void *body, double x, double y, double z);
void   *rt_body3d_get_position(void *body);  /* returns Vec3 */
void    rt_body3d_set_velocity(void *body, double vx, double vy, double vz);
void   *rt_body3d_get_velocity(void *body);  /* returns Vec3 */
void    rt_body3d_apply_force(void *body, double fx, double fy, double fz);
void    rt_body3d_apply_impulse(void *body, double ix, double iy, double iz);
void    rt_body3d_set_restitution(void *body, double r);
double  rt_body3d_get_restitution(void *body);
void    rt_body3d_set_friction(void *body, double f);
double  rt_body3d_get_friction(void *body);
void    rt_body3d_set_collision_layer(void *body, int64_t layer);
int64_t rt_body3d_get_collision_layer(void *body);
void    rt_body3d_set_collision_mask(void *body, int64_t mask);
int64_t rt_body3d_get_collision_mask(void *body);
void    rt_body3d_set_static(void *body, int8_t is_static);
int8_t  rt_body3d_is_static(void *body);
void    rt_body3d_set_trigger(void *body, int8_t is_trigger);
int8_t  rt_body3d_is_trigger(void *body);
int8_t  rt_body3d_is_grounded(void *body);
void   *rt_body3d_get_ground_normal(void *body);  /* returns Vec3 */
double  rt_body3d_get_mass(void *body);

/* Character Controller */
void   *rt_character3d_new(double radius, double height, double mass);
void    rt_character3d_move(void *ctrl, void *velocity, double dt);
void    rt_character3d_set_step_height(void *ctrl, double h);
double  rt_character3d_get_step_height(void *ctrl);
void    rt_character3d_set_slope_limit(void *ctrl, double degrees);
int8_t  rt_character3d_is_grounded(void *ctrl);
int8_t  rt_character3d_just_landed(void *ctrl); /* was_grounded=0, is_grounded=1 */
void   *rt_character3d_get_position(void *ctrl); /* returns Vec3 */
void    rt_character3d_set_position(void *ctrl, double x, double y, double z);
```

### GC Finalizer Pattern

Following Physics2D (`rt_physics2d.c:259`):
```c
static void world3d_finalizer(void *obj) {
    rt_world3d *w = (rt_world3d *)obj;
    /* Bodies are GC-managed — just clear the array */
    w->body_count = 0;
}

static void body3d_finalizer(void *obj) {
    (void)obj; /* no heap allocations */
}
```

### RuntimeClasses.hpp additions (after RTCLS_Audio3D):
```cpp
RTCLS_Physics3DWorld,
RTCLS_Physics3DBody,
RTCLS_Character3D,
```

### CMakeLists.txt: add after `graphics/rt_physics2d_joint.c`:
```cmake
graphics/rt_physics3d.c
```

### runtime.def: ~35 RT_FUNC + 3 RT_CLASS

Example entries:
```
RT_FUNC(Physics3DWorldNew, rt_world3d_new, "Viper.Game.Physics3D.World.New", "obj(f64,f64,f64)")
RT_FUNC(Physics3DWorldStep, rt_world3d_step, "Viper.Game.Physics3D.World.Step", "void(obj,f64)")
RT_FUNC(Physics3DBodyNewAABB, rt_body3d_new_aabb, "Viper.Game.Physics3D.Body.NewAABB", "obj(f64,f64,f64,f64)")
RT_FUNC(Character3DNew, rt_character3d_new, "Viper.Game.Physics3D.Character.New", "obj(f64,f64,f64)")
RT_FUNC(Character3DMove, rt_character3d_move, "Viper.Game.Physics3D.Character.Move", "void(obj,obj,f64)")

RT_CLASS_BEGIN("Viper.Game.Physics3D.World", Physics3DWorld, "obj", Physics3DWorldNew)
    RT_PROP("BodyCount", "i64", Physics3DWorldBodyCount, none)
    RT_METHOD("Step", "void(f64)", Physics3DWorldStep)
    RT_METHOD("Add", "void(obj)", Physics3DWorldAdd)
    RT_METHOD("Remove", "void(obj)", Physics3DWorldRemove)
    RT_METHOD("SetGravity", "void(f64,f64,f64)", Physics3DWorldSetGravity)
RT_CLASS_END()
```

### Stubs: ~35 functions in rt_graphics_stubs.c
### RuntimeSignatures.cpp: add `#include "rt_physics3d.h"`

---

## A2. Trigger Zones + Collision Callbacks (~200 LOC)

### Implemented within `rt_physics3d.c` (not a separate file)

Trigger zones are bodies with `is_trigger = 1`. During `world3d_step`, trigger overlaps are detected but no impulse is applied. Enter/exit edge detection:

```c
/* Per-trigger, per-body tracking (allocated on first trigger check) */
typedef struct {
    int64_t body_id;
    int8_t was_inside;
    int8_t is_inside;
} trigger_state_t;
```

### Standalone Trigger API (for simple AABB zones without full physics):

```c
typedef struct {
    void *vptr;
    double min[3], max[3];
    /* Track up to 64 bodies for enter/exit */
    void *tracked_bodies[64];
    int8_t inside_flags[64];
    int32_t tracked_count;
    int32_t enter_count;  /* this frame */
    int32_t exit_count;   /* this frame */
} rt_trigger3d;

void   *rt_trigger3d_new(double x0, double y0, double z0,
                          double x1, double y1, double z1);
int8_t  rt_trigger3d_contains(void *trigger, void *point);     /* Vec3 */
void    rt_trigger3d_update(void *trigger, void *bodies_world); /* check all bodies */
int64_t rt_trigger3d_get_enter_count(void *trigger);
int64_t rt_trigger3d_get_exit_count(void *trigger);
void    rt_trigger3d_set_bounds(void *trigger, double x0, double y0, double z0,
                                 double x1, double y1, double z1);
```

### RuntimeClasses.hpp: add `RTCLS_Trigger3D`
### runtime.def: 6 RT_FUNC + 1 RT_CLASS
### Stubs: 6 functions

---

## A3. Vec3/Math Helpers (~100 LOC)

### Modified File: `src/runtime/graphics/rt_vec3.c`

All new functions follow the existing Vec3 pattern:
- Take `void*` Vec3 parameters
- Return new `void*` Vec3 (or double for scalar results)
- Use thread-local pool for allocation (`vec3_pool_buf_`)
- NULL-safe (return zero/default on NULL input)

```c
void *rt_vec3_reflect(void *v, void *normal) {
    /* v - 2*(v·n)*n */
    if (!v || !normal) return rt_vec3_new(0, 0, 0);
    double vx = X(v), vy = Y(v), vz = Z(v);
    double nx = X(normal), ny = Y(normal), nz = Z(normal);
    double d = 2.0 * (vx*nx + vy*ny + vz*nz);
    return rt_vec3_new(vx - d*nx, vy - d*ny, vz - d*nz);
}

void *rt_vec3_project(void *v, void *onto) {
    /* ((v·t)/(t·t))*t */
    if (!v || !onto) return rt_vec3_new(0, 0, 0);
    double vx = X(v), vy = Y(v), vz = Z(v);
    double tx = X(onto), ty = Y(onto), tz = Z(onto);
    double dot_vt = vx*tx + vy*ty + vz*tz;
    double dot_tt = tx*tx + ty*ty + tz*tz;
    if (dot_tt < 1e-12) return rt_vec3_new(0, 0, 0);
    double s = dot_vt / dot_tt;
    return rt_vec3_new(s*tx, s*ty, s*tz);
}

void *rt_vec3_clamp_len(void *v, double max_len) {
    if (!v || max_len <= 0) return rt_vec3_new(0, 0, 0);
    double vx = X(v), vy = Y(v), vz = Z(v);
    double len_sq = vx*vx + vy*vy + vz*vz;
    if (len_sq <= max_len * max_len) return rt_vec3_new(vx, vy, vz);
    double s = max_len / sqrt(len_sq);
    return rt_vec3_new(vx*s, vy*s, vz*s);
}

void *rt_vec3_move_towards(void *current, void *target, double max_delta) {
    if (!current || !target) return rt_vec3_new(0, 0, 0);
    double dx = X(target)-X(current), dy = Y(target)-Y(current), dz = Z(target)-Z(current);
    double dist = sqrt(dx*dx + dy*dy + dz*dz);
    if (dist <= max_delta || dist < 1e-12)
        return rt_vec3_new(X(target), Y(target), Z(target));
    double s = max_delta / dist;
    return rt_vec3_new(X(current)+dx*s, Y(current)+dy*s, Z(current)+dz*s);
}

double rt_vec3_angle(void *a, void *b) {
    /* acos(clamp(dot(a,b) / (|a|*|b|), -1, 1)) */
    if (!a || !b) return 0.0;
    double ax=X(a),ay=Y(a),az=Z(a), bx=X(b),by=Y(b),bz=Z(b);
    double la = sqrt(ax*ax+ay*ay+az*az), lb = sqrt(bx*bx+by*by+bz*bz);
    if (la < 1e-12 || lb < 1e-12) return 0.0;
    double cos_a = (ax*bx+ay*by+az*bz) / (la*lb);
    if (cos_a > 1.0) cos_a = 1.0;
    if (cos_a < -1.0) cos_a = -1.0;
    return acos(cos_a);
}

void *rt_vec3_min(void *a, void *b) {
    if (!a || !b) return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(fmin(X(a),X(b)), fmin(Y(a),Y(b)), fmin(Z(a),Z(b)));
}

void *rt_vec3_max(void *a, void *b) {
    if (!a || !b) return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(fmax(X(a),X(b)), fmax(Y(a),Y(b)), fmax(Z(a),Z(b)));
}
```

### Header additions: `src/runtime/graphics/rt_vec3.h`

Add declarations for all 7 new functions.

### runtime.def: 7 RT_FUNC added to existing `Viper.Math.Vec3` class

```
RT_FUNC(Vec3Reflect,     rt_vec3_reflect,      "Viper.Math.Vec3.Reflect",     "obj(obj,obj)")
RT_FUNC(Vec3Project,     rt_vec3_project,      "Viper.Math.Vec3.Project",     "obj(obj,obj)")
RT_FUNC(Vec3ClampLen,    rt_vec3_clamp_len,    "Viper.Math.Vec3.ClampLen",    "obj(obj,f64)")
RT_FUNC(Vec3MoveTowards, rt_vec3_move_towards, "Viper.Math.Vec3.MoveTowards", "obj(obj,obj,f64)")
RT_FUNC(Vec3Angle,       rt_vec3_angle,        "Viper.Math.Vec3.Angle",       "f64(obj,obj)")
RT_FUNC(Vec3Min,         rt_vec3_min,          "Viper.Math.Vec3.Min",         "obj(obj,obj)")
RT_FUNC(Vec3Max,         rt_vec3_max,          "Viper.Math.Vec3.Max",         "obj(obj,obj)")
```

Add RT_METHODs to existing Vec3 RT_CLASS_BEGIN block.

### Stubs: 7 functions (return zero Vec3 or 0.0)

---

## Files Modified/Created Summary

| Action | File | Est. LOC |
|--------|------|----------|
| MOD | `src/runtime/graphics/rt_raycast3d.c` | +150 (collision primitives) |
| MOD | `src/runtime/graphics/rt_raycast3d.h` | +15 (declarations) |
| NEW | `src/runtime/graphics/rt_physics3d.h` | ~80 |
| NEW | `src/runtime/graphics/rt_physics3d.c` | ~700 |
| MOD | `src/runtime/graphics/rt_vec3.c` | +100 |
| MOD | `src/runtime/graphics/rt_vec3.h` | +10 |
| MOD | `src/runtime/graphics/rt_graphics_stubs.c` | +50 |
| MOD | `src/il/runtime/runtime.def` | +60 entries |
| MOD | `src/il/runtime/classes/RuntimeClasses.hpp` | +4 class IDs |
| MOD | `src/il/runtime/RuntimeSignatures.cpp` | +1 include |
| MOD | `src/runtime/CMakeLists.txt` | +1 source |
| MOD | `src/tests/unit/CMakeLists.txt` | +2 tests |
| NEW | `src/tests/unit/test_rt_physics3d.cpp` | ~200 |
| NEW | `src/tests/unit/test_rt_collision3d.cpp` | ~150 |

---

## Tests

### Collision Primitive Tests (8)
- Sphere-sphere overlap: touching, separated
- Sphere-sphere penetration: correct push-out vector
- AABB-sphere overlap: inside, outside, touching
- Closest point on AABB: corner, edge, face
- Segment closest point: endpoints, midpoint
- Capsule-AABB overlap: hit, miss
- Capsule-sphere overlap: hit, miss
- Swept AABB: fraction correct, normal correct

### Physics3D Tests (15)
- Body creation (AABB, sphere, capsule): non-null, correct shape
- Gravity: body Y velocity increases over time
- Static body: doesn't move under gravity
- AABB-AABB collision: bodies pushed apart
- Sphere-sphere collision: bounce with restitution 1.0
- Sphere-sphere collision: no bounce with restitution 0.0
- Layer filtering: same layer + mask → collide
- Layer filtering: different layers → don't collide
- Trigger body: overlap detected, no physics push
- Force application: velocity increases
- Impulse application: instant velocity change
- Position set/get roundtrip
- Velocity set/get roundtrip
- World add/remove: body count changes
- Ground detection: body on floor → isGrounded=true

### Trigger Tests (5)
- Point inside trigger → contains=true
- Point outside → contains=false
- Enter detection: body moves into trigger → enter_count=1
- Exit detection: body moves out → exit_count=1
- Multiple bodies tracked independently

### Vec3 Math Tests (7)
- Reflect: v=(1,-1,0), n=(0,1,0) → (1,1,0)
- Project: v=(3,4,0) onto (1,0,0) → (3,0,0)
- ClampLen: v=(3,4,0) max=1.0 → length 1.0
- MoveTowards: current to target, clamps at max_delta
- Angle: perpendicular → π/2, parallel → 0
- Min: component-wise min of two vectors
- Max: component-wise max of two vectors

---

## Verification

1. `cmake --build build -j` — zero warnings
2. `ctest --test-dir build` — 1334+ tests pass (new add ~35)
3. `./scripts/check_runtime_completeness.sh` — passes
4. FPS demo updated to use CharacterController (replaces manual floor clamping)
5. New demo: `demo_physics3d.zia` — bouncing spheres with gravity
6. `viper build demo_physics3d.zia -o /tmp/demo_physics3d` — native compilation works
7. All existing 9 3D demos still work (no regression)

## Usage Example

```zia
var world = Viper.Game.Physics3D.World.New(0.0, -9.8, 0.0)

// Floor (static AABB)
var floor = Viper.Game.Physics3D.Body.NewAABB(10.0, 0.1, 10.0, 0.0)
Viper.Game.Physics3D.Body.SetPosition(floor, 0.0, -0.1, 0.0)
Viper.Game.Physics3D.Body.SetStatic(floor, true)
Viper.Game.Physics3D.World.Add(world, floor)

// Player (character controller)
var player = Viper.Game.Physics3D.Character.New(0.4, 1.8, 80.0)
Viper.Game.Physics3D.Character.SetPosition(player, 0.0, 1.0, 5.0)
Viper.Game.Physics3D.World.Add(world, player)

// Game loop:
var vel = new Viper.Math.Vec3(fwd * speed, vy, right * speed)
Viper.Game.Physics3D.Character.Move(player, vel, dt)
Viper.Game.Physics3D.World.Step(world, dt)
```
