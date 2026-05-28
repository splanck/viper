//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d.c
// Purpose: 3D physics world — AABB/sphere/capsule bodies, symplectic Euler
//   integration, impulse-based collision response with Baumgarte correction,
//   bidirectional layer/mask filtering, and character controller.
//
// Key invariants:
//   - Bodies in topological order not required (flat array, sweep-and-prune broad phase).
//   - Integration: forces→velocity, velocity→position (symplectic Euler).
//   - Collision response: contact-point impulse updates linear and angular velocity.
//   - Baumgarte stabilization: 40% excess penetration correction, 1% slop.
//   - Character controller: up to 3 slide iterations per move.
//   - Quaternion orientations are renormalized after every integration step.
//   - Sweep-and-prune broad-phase sorts entries by min-X for early-out.
//
// Ownership/Lifetime:
//   - World3D / Body3D / Character3D / Trigger3D are GC-managed.
//   - World3D retains its bodies and joints; finalizer releases each.
//   - Boxed query results (PhysicsHit3D / PhysicsHitList3D / CollisionEvent3D)
//     retain referenced bodies and colliders until released by the GC.
//   - Trigger3D holds tracked bodies as weak pointers — stale entries are
//     pruned during the next Update.
//
// Links: rt_physics3d.h, rt_raycast3d.h, plans/3d/20-phase-a-core-game-systems.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_physics3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_joints3d.h"
#include "rt_raycast3d.h"

#include <limits.h>
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

#define PH3D_INITIAL_BODIES 256
#define PH3D_SHAPE_AABB 0
#define PH3D_SHAPE_SPHERE 1
#define PH3D_SHAPE_CAPSULE 2
#define PH3D_MODE_DYNAMIC 0
#define PH3D_MODE_STATIC 1
#define PH3D_MODE_KINEMATIC 2
#define PH3D_SLEEP_LINEAR_THRESHOLD 0.05
#define PH3D_SLEEP_ANGULAR_THRESHOLD 0.05
#define PH3D_SLEEP_DELAY 0.5
#define PH3D_MAX_CCD_SUBSTEPS 64

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

#define PH3D_INITIAL_CONTACTS 128
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

typedef struct ph3d_broadphase_entry ph3d_broadphase_entry;

typedef struct {
    void *vptr;
    double gravity[3];
    rt_body3d **bodies;
    int32_t body_count;
    int32_t body_capacity;
    rt_contact3d *contacts;
    int32_t contact_count;
    int32_t contact_capacity;
    rt_contact3d *previous_contacts;
    int32_t previous_contact_count;
    int32_t previous_contact_capacity;
    rt_contact3d *enter_events;
    int32_t enter_event_count;
    int32_t enter_event_capacity;
    rt_contact3d *stay_events;
    int32_t stay_event_count;
    int32_t stay_event_capacity;
    rt_contact3d *exit_events;
    int32_t exit_event_count;
    int32_t exit_event_capacity;
/* Joint constraints */
#define PH3D_INITIAL_JOINTS 128
    void **joints;
    int32_t *joint_types;
    int32_t joint_count;
    int32_t joint_capacity;
    ph3d_broadphase_entry *broadphase_entries;
    int32_t broadphase_capacity;
} rt_world3d;

struct ph3d_broadphase_entry {
    rt_body3d *body;
    double min[3];
    double max[3];
};

/// @brief Validate @p obj as a World3D handle and return its typed pointer (NULL on mismatch).
static rt_world3d *world3d_checked(void *obj) {
    return (rt_world3d *)rt_g3d_checked_or_null(obj, RT_G3D_WORLD3D_CLASS_ID);
}

/// @brief Validate @p obj as a Body3D handle and return its typed pointer (NULL on mismatch).
static rt_body3d *body3d_checked(void *obj) {
    return (rt_body3d *)rt_g3d_checked_or_null(obj, RT_G3D_BODY3D_CLASS_ID);
}

/// @brief Verify that @p joint is a live joint of the kind named by @p joint_type.
/// @details Used by World3D before dispatching joint solving to confirm the table
///   entry hasn't been swapped out from under the cached type tag.
static int joint3d_matches_type(void *joint, int64_t joint_type) {
    if (!joint)
        return 0;
    if (joint_type == RT_JOINT_DISTANCE)
        return rt_g3d_has_class(joint, RT_G3D_DISTANCEJOINT3D_CLASS_ID);
    if (joint_type == RT_JOINT_SPRING)
        return rt_g3d_has_class(joint, RT_G3D_SPRINGJOINT3D_CLASS_ID);
    return 0;
}

/// @brief Return non-zero only when every component of @p v is finite.
static int ph3d_vec3_all_finite(const double v[3]) {
    return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/// @brief Compute the next power-of-two capacity that covers `needed`, starting from
///        `current` (or `initial` if current is zero).
/// @details Doubles until the result covers `needed`. Returns -1 if the next doubling
///          would overflow `INT32_MAX`, so callers can fail the reserve cleanly rather
///          than wrapping to a negative size that would pass a naive `< capacity` check
///          but produce a corrupt allocation.
/// @param current Current capacity of the array (0 = not yet allocated).
/// @param needed Minimum capacity the caller requires.
/// @param initial Floor capacity used when `current == 0`.
/// @return New capacity >= needed, or -1 on overflow.
static int32_t ph3d_next_capacity(int32_t current, int32_t needed, int32_t initial) {
    int32_t capacity = current > 0 ? current : initial;
    while (capacity < needed) {
        if (capacity > INT32_MAX / 2)
            return -1;
        capacity *= 2;
    }
    return capacity;
}

/// @brief Grow the world's body array to fit at least `needed` entries.
/// @details Shared growth contract followed by every `world3d_reserve_*` helper:
///            1. No-op and return success when the current capacity already covers it.
///            2. On realloc failure, leave the existing array intact and return 0 —
///               callers can safely continue using whatever was already there.
///            3. On successful grow, zero-initialize the freshly extended tail so
///               unused slots are always NULL-valued and can be scanned safely.
///          Only the pointer array is touched; body lifetime is owned elsewhere.
/// @return 1 on success (including no-op), 0 on allocation failure.
static int world3d_reserve_body_capacity(rt_world3d *w, int32_t needed) {
    if (!w || needed <= w->body_capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(w->body_capacity, needed, PH3D_INITIAL_BODIES);
    if (new_capacity < 0)
        return 0;
    rt_body3d **new_bodies =
        (rt_body3d **)realloc(w->bodies, (size_t)new_capacity * sizeof(*new_bodies));
    if (!new_bodies)
        return 0;
    memset(new_bodies + w->body_capacity,
           0,
           (size_t)(new_capacity - w->body_capacity) * sizeof(*new_bodies));
    w->bodies = new_bodies;
    w->body_capacity = new_capacity;
    return 1;
}

/// @brief Generic reserve helper used by all four contact-style arrays
///        (active contacts, previous contacts, enter / stay / exit event queues).
/// @details Same growth contract as `world3d_reserve_body_capacity` — realloc-in-place,
///          leave the array untouched on failure, zero-initialize the tail on grow. The
///          four thin wrappers below exist only to pick the right `(array, capacity)`
///          field pair; keeping one body avoids the five-copy-paste bug-multiplication
///          this code used to have.
static int world3d_reserve_contact_array(rt_contact3d **array,
                                         int32_t *capacity,
                                         int32_t needed) {
    if (!array || !capacity || needed <= *capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(*capacity, needed, PH3D_INITIAL_CONTACTS);
    if (new_capacity < 0)
        return 0;
    rt_contact3d *new_array =
        (rt_contact3d *)realloc(*array, (size_t)new_capacity * sizeof(*new_array));
    if (!new_array)
        return 0;
    memset(new_array + *capacity, 0, (size_t)(new_capacity - *capacity) * sizeof(*new_array));
    *array = new_array;
    *capacity = new_capacity;
    return 1;
}

/// @brief Grow w->contacts to hold at least @p needed entries.
static int world3d_reserve_contacts(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->contacts, &w->contact_capacity, needed);
}

/// @brief Grow w->previous_contacts to hold at least @p needed entries.
/// @details Stores the contact set from the previous step for enter/exit event diffing.
static int world3d_reserve_previous_contacts(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(
        &w->previous_contacts, &w->previous_contact_capacity, needed);
}

/// @brief Grow w->enter_events to hold at least @p needed entries.
/// @details Enter events are emitted when a contact pair appears this step but
///   was absent from the previous step's contact set.
static int world3d_reserve_enter_events(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->enter_events, &w->enter_event_capacity, needed);
}

/// @brief Grow w->stay_events to hold at least @p needed entries.
/// @details Stay events are emitted for contact pairs that existed both last step
///   and this step (continuous contact).
static int world3d_reserve_stay_events(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->stay_events, &w->stay_event_capacity, needed);
}

/// @brief Grow w->exit_events to hold at least @p needed entries.
/// @details Exit events are emitted when a contact pair was present last step but
///   is absent this step (separation).
static int world3d_reserve_exit_events(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->exit_events, &w->exit_event_capacity, needed);
}

/// @brief Increment `value` into `*out`, returning 0 (no write) on INT32_MAX overflow or NULL out.
static int world3d_checked_increment(int32_t value, int32_t *out) {
    if (!out || value == INT32_MAX)
        return 0;
    *out = value + 1;
    return 1;
}

/// @brief Grow the joint table — the one reserve helper that has to allocate two
///        parallel arrays atomically (joint pointers + joint type tags).
/// @details The two new arrays are allocated before the world is mutated. On failure,
///          the old arrays and `joint_capacity` remain unchanged. Both tails are
///          zero-initialized on success so unused slots always read as NULL / 0 joint_type.
static int world3d_reserve_joint_capacity(rt_world3d *w, int32_t needed) {
    if (!w || needed <= w->joint_capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(w->joint_capacity, needed, PH3D_INITIAL_JOINTS);
    if (new_capacity < 0)
        return 0;
    void **new_joints = (void **)calloc((size_t)new_capacity, sizeof(*new_joints));
    int32_t *new_types = (int32_t *)calloc((size_t)new_capacity, sizeof(*new_types));
    if (!new_joints || !new_types) {
        free(new_joints);
        free(new_types);
        return 0;
    }
    if (w->joint_count > 0) {
        memcpy(new_joints, w->joints, (size_t)w->joint_count * sizeof(*new_joints));
        memcpy(new_types, w->joint_types, (size_t)w->joint_count * sizeof(*new_types));
    }
    free(w->joints);
    free(w->joint_types);
    w->joints = new_joints;
    w->joint_types = new_types;
    w->joint_capacity = new_capacity;
    return 1;
}

/// @brief Grow the sweep-and-prune broadphase scratch table. Capacity is sized per-body
///        so the initial floor tracks `PH3D_INITIAL_BODIES`. Unlike the body-pointer
///        array, entries are treated as pure scratch — the tail is NOT zero-initialized
///        because the broadphase rebuild always writes every entry before reading it.
static int world3d_reserve_broadphase_capacity(rt_world3d *w, int32_t needed) {
    if (!w || needed <= w->broadphase_capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(w->broadphase_capacity, needed, PH3D_INITIAL_BODIES);
    if (new_capacity < 0)
        return 0;
    ph3d_broadphase_entry *new_entries = (ph3d_broadphase_entry *)realloc(
        w->broadphase_entries, (size_t)new_capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    w->broadphase_entries = new_entries;
    w->broadphase_capacity = new_capacity;
    return 1;
}

/// @brief Clamp @p value to [0, +∞), substituting @p fallback for non-finite inputs.
/// @details Used to sanitize body properties such as mass, restitution, and damping
///   on creation so internal state never contains NaN or negative physical quantities.
static double ph3d_clamp_nonnegative_finite(double value, double fallback) {
    if (!isfinite(value))
        return fallback;
    return value < 0.0 ? 0.0 : value;
}

/// @brief Return @p value if it is finite, otherwise return @p fallback.
/// @details Thin guard used wherever scalar inputs (gravity components, position offsets)
///   must be well-defined doubles before being stored in body or world state.
static double ph3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Write a sanitized 3D vector to @p dst, replacing each NaN/Inf component with 0.
/// @details Used when accepting Vec3 arguments from Zia to populate force, velocity, and
///   position arrays — ensures that a single bad component cannot contaminate the solver.
static void ph3d_vec3_set_finite(double *dst, double x, double y, double z) {
    dst[0] = ph3d_finite_or(x, 0.0);
    dst[1] = ph3d_finite_or(y, 0.0);
    dst[2] = ph3d_finite_or(z, 0.0);
}

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

/// @brief Validate @p obj as a PhysicsHit3D handle (NULL on mismatch).
static rt_physics_hit3d_obj *physics_hit3d_checked(void *obj) {
    return (rt_physics_hit3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_PHYSICSHIT3D_CLASS_ID);
}

/// @brief Validate @p obj as a PhysicsHitList3D handle (NULL on mismatch).
static rt_physics_hit_list3d_obj *physics_hit_list3d_checked(void *obj) {
    return (rt_physics_hit_list3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_PHYSICSHITLIST3D_CLASS_ID);
}

/// @brief Validate @p obj as a CollisionEvent3D handle (NULL on mismatch).
static rt_collision_event3d_obj *collision_event3d_checked(void *obj) {
    return (rt_collision_event3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_COLLISIONEVENT3D_CLASS_ID);
}

/// @brief Validate @p obj as a ContactPoint3D handle (NULL on mismatch).
static rt_contact_point3d_obj *contact_point3d_checked(void *obj) {
    return (rt_contact_point3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_CONTACTPOINT3D_CLASS_ID);
}

/// @brief Validate @p obj as a Character3D handle (NULL on mismatch).
static rt_character3d *character3d_checked(void *obj) {
    return (rt_character3d *)rt_g3d_checked_or_null(obj, RT_G3D_CHARACTER3D_CLASS_ID);
}

// Forward declarations for functions defined later in this translation unit.
// They are referenced from helpers above that must be inlined by the compiler
// before the full definitions appear.
/// @brief Build a rt_collider_pose from the body's position/orientation/scale fields.
static void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose);
/// @brief Compose a parent pose with a child's local position/rotation/scale.
static void collider_pose_compose(const rt_collider_pose *parent,
                                  const double *child_position,
                                  const double *child_rotation,
                                  const double *child_scale,
                                  rt_collider_pose *out);
/// @brief Transform a point from the collider's local space into world space.
static void transform_point_from_pose(const rt_collider_pose *pose,
                                      const double *local_point,
                                      double *world_point);
/// @brief Transform a point from world space into the collider's local space.
static void transform_point_to_local(const rt_collider_pose *pose,
                                     const double *world_point,
                                     double *local_point);
/// @brief Sync the body's cached primitive shape from its attached collider's geometry.
static void body3d_update_shape_cache_from_collider(rt_body3d *body);
/// @brief Compute the two world-space endpoint positions of a capsule's axis segment.
static void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c);
/// @brief Run narrow-phase collision detection between two bodies.
static int test_collision(const rt_body3d *a,
                          const rt_body3d *b,
                          double *normal,
                          double *depth,
                          double *point,
                          void **leaf_a_out,
                          void **leaf_b_out);
static int test_simple_collision(const rt_body3d *a,
                                 const rt_body3d *b,
                                 double *normal,
                                 double *depth);

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
        double a[3], c[3];
        capsule_axis_endpoints(b, a, c);
        mn[0] = (a[0] < c[0] ? a[0] : c[0]) - b->radius;
        mn[1] = (a[1] < c[1] ? a[1] : c[1]) - b->radius;
        mn[2] = (a[2] < c[2] ? a[2] : c[2]) - b->radius;
        mx[0] = (a[0] > c[0] ? a[0] : c[0]) + b->radius;
        mx[1] = (a[1] > c[1] ? a[1] : c[1]) + b->radius;
        mx[2] = (a[2] > c[2] ? a[2] : c[2]) + b->radius;
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

/// @brief Component add: `out = a + b`.
static void vec3_add(const double *a, const double *b, double *out) {
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

/// @brief Cross product: `out = a x b`.
static void vec3_cross(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
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
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        quat_identity(q);
        return;
    }
    double len_sq = quat_len_sq(q);
    if (!isfinite(len_sq) || len_sq < 1e-24) {
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
    if (!isfinite(axis_len) || !isfinite(angle) || axis_len < 1e-12 || fabs(angle) < 1e-12) {
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
    if (!isfinite(speed) || !isfinite(dt) || speed < 1e-12 || dt <= 0.0)
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
    double rotated[4];
    quat_conjugate(q, q_conj);
    quat_mul(q, qv, tmp);
    quat_mul(tmp, q_conj, rotated);
    out[0] = rotated[0];
    out[1] = rotated[1];
    out[2] = rotated[2];
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

/// @brief Absolute scale factor sanitized for division: non-finite or near-zero → 1.0.
static double pose_abs_scale_or_unit(double value) {
    value = fabs(value);
    return isfinite(value) && value > 1e-12 ? value : 1.0;
}

/// @brief Absolute scale factor with non-finite mapped to 1.0 (zero is preserved).
static double pose_abs_scale_or_zero(double value) {
    value = fabs(value);
    return isfinite(value) ? value : 1.0;
}

/// @brief Transform a world-space vector into pose-local space.
/// @details Translation is ignored; inverse rotation and inverse scale are applied.
static void transform_vector_to_local(const rt_collider_pose *pose,
                                      const double *world_vec,
                                      double *local_vec) {
    double inv_rotation[4];
    if (!pose || !world_vec || !local_vec)
        return;
    quat_conjugate(pose->rotation, inv_rotation);
    quat_rotate_vec3(inv_rotation, world_vec, local_vec);
    if (fabs(pose->scale[0]) > 1e-12)
        local_vec[0] /= pose->scale[0];
    else
        local_vec[0] = 0.0;
    if (fabs(pose->scale[1]) > 1e-12)
        local_vec[1] /= pose->scale[1];
    else
        local_vec[1] = 0.0;
    if (fabs(pose->scale[2]) > 1e-12)
        local_vec[2] /= pose->scale[2];
    else
        local_vec[2] = 0.0;
}

/// @brief Transform a local-space normal into world space via inverse-transpose scale.
static void transform_normal_from_local(const rt_collider_pose *pose,
                                        const double *local_normal,
                                        double *world_normal) {
    double scaled[3];
    if (!pose || !local_normal || !world_normal)
        return;
    scaled[0] = local_normal[0] / pose_abs_scale_or_unit(pose->scale[0]);
    scaled[1] = local_normal[1] / pose_abs_scale_or_unit(pose->scale[1]);
    scaled[2] = local_normal[2] / pose_abs_scale_or_unit(pose->scale[2]);
    quat_rotate_vec3(pose->rotation, scaled, world_normal);
    if (vec3_normalize_in_place(world_normal) <= 1e-12)
        vec3_set(world_normal, 0.0, 1.0, 0.0);
}

/// @brief Choose how many points to sample along a capsule's central axis when
///   approximating it for collision: spacing ≈ radius/2 (min 1e-4), clamped to [1, 256].
static int capsule_axis_sample_count(double axis_len, double radius) {
    double spacing;
    int samples;
    if (!isfinite(axis_len) || axis_len <= 1e-9)
        return 1;
    spacing = (isfinite(radius) && radius > 1e-6) ? radius * 0.5 : 0.05;
    if (spacing < 1e-4)
        spacing = 1e-4;
    samples = (int)ceil(axis_len / spacing) + 1;
    if (samples < 1)
        samples = 1;
    if (samples > 256)
        samples = 256;
    return samples;
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
        vec3_set(body->half_extents, body->radius, fmax(body->height * 0.5, body->radius), body->radius);
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
        double cylinder_height = fmax(b->height - 2.0 * b->radius, 0.0);
        double h2 = cylinder_height * cylinder_height;
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

/// @brief Apply a world-space angular impulse through the body's local diagonal inertia.
static void body3d_apply_world_angular_impulse(rt_body3d *b, const double *angular_impulse) {
    double inv_rotation[4];
    double local_impulse[3];
    double local_delta[3];
    double world_delta[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !angular_impulse)
        return;
    if (!ph3d_vec3_all_finite(angular_impulse))
        return;
    quat_conjugate(b->orientation, inv_rotation);
    quat_rotate_vec3(inv_rotation, angular_impulse, local_impulse);
    local_delta[0] = local_impulse[0] * b->inv_inertia[0];
    local_delta[1] = local_impulse[1] * b->inv_inertia[1];
    local_delta[2] = local_impulse[2] * b->inv_inertia[2];
    quat_rotate_vec3(b->orientation, local_delta, world_delta);
    b->angular_velocity[0] += world_delta[0];
    b->angular_velocity[1] += world_delta[1];
    b->angular_velocity[2] += world_delta[2];
}

/// @brief Return `I^-1 * v` in world space using the body's local diagonal inertia.
static void body3d_world_inv_inertia_mul(const rt_body3d *b, const double *v, double *out) {
    double inv_rotation[4];
    double local_v[3];
    double local_out[3];
    if (!out)
        return;
    vec3_set(out, 0.0, 0.0, 0.0);
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !v || !ph3d_vec3_all_finite(v))
        return;
    quat_conjugate(b->orientation, inv_rotation);
    quat_rotate_vec3(inv_rotation, v, local_v);
    local_out[0] = local_v[0] * b->inv_inertia[0];
    local_out[1] = local_v[1] * b->inv_inertia[1];
    local_out[2] = local_v[2] * b->inv_inertia[2];
    quat_rotate_vec3(b->orientation, local_out, out);
}

/// @brief Velocity at a world-space contact point, including angular motion.
static void body3d_contact_velocity(const rt_body3d *b, const double *r, double *out) {
    double angular_component[3];
    if (!out)
        return;
    if (!b) {
        vec3_set(out, 0.0, 0.0, 0.0);
        return;
    }
    vec3_cross(b->angular_velocity, r, angular_component);
    vec3_add(b->velocity, angular_component, out);
}

/// @brief Effective inverse mass contribution for an impulse direction at offset `r`.
static double body3d_contact_impulse_denominator(const rt_body3d *b,
                                                 const double *r,
                                                 const double *dir) {
    double r_cross_dir[3];
    double inv_i_term[3];
    double angular_cross[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !r || !dir)
        return 0.0;
    vec3_cross(r, dir, r_cross_dir);
    body3d_world_inv_inertia_mul(b, r_cross_dir, inv_i_term);
    vec3_cross(inv_i_term, r, angular_cross);
    return b->inv_mass + vec3_dot(angular_cross, dir);
}

/// @brief Apply a world-space linear impulse at a contact offset from the center of mass.
static void body3d_apply_contact_impulse(rt_body3d *b, const double *impulse, const double *r) {
    double angular_impulse[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !impulse || !r)
        return;
    if (!ph3d_vec3_all_finite(impulse))
        return;
    b->velocity[0] += impulse[0] * b->inv_mass;
    b->velocity[1] += impulse[1] * b->inv_mass;
    b->velocity[2] += impulse[2] * b->inv_mass;
    vec3_cross(r, impulse, angular_impulse);
    body3d_apply_world_angular_impulse(b, angular_impulse);
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

/// @brief Return the two world-space endpoints of a capsule's oriented axis segment.
///
/// Capsules are authored along local Y with the body position at the center.
/// The body orientation rotates that local axis before collision tests use it.
static void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c) {
    double half_axis = fmax(b->height * 0.5 - b->radius, 0.0);
    double local_axis[3] = {0.0, half_axis, 0.0};
    double axis[3];
    quat_rotate_vec3(b->orientation, local_axis, axis);
    vec3_set(a, b->position[0] - axis[0], b->position[1] - axis[1], b->position[2] - axis[2]);
    vec3_set(c, b->position[0] + axis[0], b->position[1] + axis[1], b->position[2] + axis[2]);
}

/// @brief Find the point on segment [a, b] closest to @p point.
/// @details Projects @p point onto the line through a and b, clamps the
///   parameter t to [0, 1] to stay within the segment, then evaluates
///   the position.  The degenerate case (a == b, denom ≈ 0) returns a.
/// @param a        Segment start endpoint (world space).
/// @param b        Segment end endpoint (world space).
/// @param point    Query point (world space).
/// @param closest  Output: closest point on the segment to @p point.
static void closest_point_on_segment(const double *a,
                                     const double *b,
                                     const double *point,
                                     double *closest) {
    double ab[3], ap[3];
    vec3_sub(b, a, ab);
    vec3_sub(point, a, ap);
    double denom = vec3_dot(ab, ab);
    double t = denom > 1e-18 ? vec3_dot(ap, ab) / denom : 0.0;
    t = clampd(t, 0.0, 1.0);
    closest[0] = a[0] + ab[0] * t;
    closest[1] = a[1] + ab[1] * t;
    closest[2] = a[2] + ab[2] * t;
}

/// @brief Find the pair of closest points between two line segments in 3D.
/// @details Implements the GDC/Ericson Real-Time Collision Detection segment–segment
///   closest-point algorithm.  Handles degenerate cases where either or both segments
///   collapse to a point (length ≤ eps).  The result is the world-space pair (c1, c2)
///   such that |c1 - c2| is minimized over the two segments.  Used in capsule–capsule
///   and capsule–box narrow-phase collision to compute the axis-to-axis gap.
/// @param p1  Start of segment 1.
/// @param q1  End of segment 1.
/// @param p2  Start of segment 2.
/// @param q2  End of segment 2.
/// @param c1  Output: closest point on segment 1.
/// @param c2  Output: closest point on segment 2.
static void closest_points_on_segments(const double *p1,
                                       const double *q1,
                                       const double *p2,
                                       const double *q2,
                                       double *c1,
                                       double *c2) {
    const double eps = 1e-18;
    double d1[3], d2[3], r[3];
    vec3_sub(q1, p1, d1);
    vec3_sub(q2, p2, d2);
    vec3_sub(p1, p2, r);
    double a = vec3_dot(d1, d1);
    double e = vec3_dot(d2, d2);
    double f = vec3_dot(d2, r);
    double s, t;

    if (a <= eps && e <= eps) {
        vec3_copy(c1, p1);
        vec3_copy(c2, p2);
        return;
    }
    if (a <= eps) {
        s = 0.0;
        t = clampd(f / e, 0.0, 1.0);
    } else {
        double c = vec3_dot(d1, r);
        if (e <= eps) {
            t = 0.0;
            s = clampd(-c / a, 0.0, 1.0);
        } else {
            double b = vec3_dot(d1, d2);
            double denom = a * e - b * b;
            if (denom != 0.0)
                s = clampd((b * f - c * e) / denom, 0.0, 1.0);
            else
                s = 0.0;
            t = (b * s + f) / e;
            if (t < 0.0) {
                t = 0.0;
                s = clampd(-c / a, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = clampd((b - c) / a, 0.0, 1.0);
            }
        }
    }
    c1[0] = p1[0] + d1[0] * s;
    c1[1] = p1[1] + d1[1] * s;
    c1[2] = p1[2] + d1[2] * s;
    c2[0] = p2[0] + d2[0] * t;
    c2[1] = p2[1] + d2[1] * t;
    c2[2] = p2[2] + d2[2] * t;
}

/// @brief Compute the squared distance from @p point to the AABB defined by [mn, mx].
/// @details Projects the point onto the closest face/edge/corner of the box using
///   per-axis clamping, then returns the squared length of the gap vector.  Returns
///   0 for points inside the AABB.  Used by the segment-to-AABB search to evaluate
///   candidate t parameters quickly without a sqrt.
/// @param point  Query point (world space, double[3]).
/// @param mn     AABB minimum corner (world space, double[3]).
/// @param mx     AABB maximum corner (world space, double[3]).
/// @return       Squared Euclidean distance from @p point to the nearest AABB surface.
static double point_aabb_distance_sq(const double *point, const double *mn, const double *mx) {
    double q[3] = {clampd(point[0], mn[0], mx[0]),
                   clampd(point[1], mn[1], mx[1]),
                   clampd(point[2], mn[2], mx[2])};
    double delta[3];
    vec3_sub(point, q, delta);
    return vec3_len_sq(delta);
}

/// @brief Find the point on segment [a, c] that is closest to the AABB [mn, mx].
/// @details Uses a multi-probe sampling strategy: evaluates point_aabb_distance_sq
///   at t = 0, 1, and at each of the six per-axis candidate t values derived from
///   the AABB face planes (where the segment crosses each slab boundary).  The
///   sample with the smallest squared distance wins.  This avoids a full analytic
///   segment–AABB solver while remaining robust for the capsule–box narrow phase.
/// @param a           Segment start endpoint (world space, double[3]).
/// @param c           Segment end endpoint (world space, double[3]).
/// @param mn          AABB minimum corner (world space, double[3]).
/// @param mx          AABB maximum corner (world space, double[3]).
/// @param closest_axis  Output: best-candidate point on the segment (world space, double[3]).
static void closest_point_segment_to_aabb(const double *a,
                                          const double *c,
                                          const double *mn,
                                          const double *mx,
                                          double *closest_axis) {
    double d[3];
    double best_t = 0.0;
    double best_dist = 1e300;
    vec3_sub(c, a, d);

    #define PH3D_EVAL_SEG_AABB_T(t_expr) do { \
        double eval_t = clampd((t_expr), 0.0, 1.0); \
        double p_eval[3] = {a[0] + d[0] * eval_t, a[1] + d[1] * eval_t, a[2] + d[2] * eval_t}; \
        double dist_eval = point_aabb_distance_sq(p_eval, mn, mx); \
        if (dist_eval < best_dist) { \
            best_dist = dist_eval; \
            best_t = eval_t; \
        } \
    } while (0)

    PH3D_EVAL_SEG_AABB_T(0.0);
    PH3D_EVAL_SEG_AABB_T(1.0);
    {
        double center[3] = {
            (mn[0] + mx[0]) * 0.5,
            (mn[1] + mx[1]) * 0.5,
            (mn[2] + mx[2]) * 0.5,
        };
        double ac[3];
        vec3_sub(center, a, ac);
        double len_sq = vec3_len_sq(d);
        if (len_sq > 1e-18)
            PH3D_EVAL_SEG_AABB_T(vec3_dot(ac, d) / len_sq);
    }
    for (int axis = 0; axis < 3; axis++) {
        if (fabs(d[axis]) > 1e-18) {
            PH3D_EVAL_SEG_AABB_T((mn[axis] - a[axis]) / d[axis]);
            PH3D_EVAL_SEG_AABB_T((mx[axis] - a[axis]) / d[axis]);
        }
    }
    {
        double lo = 0.0;
        double hi = 1.0;
        for (int iter = 0; iter < 24; iter++) {
            double m1 = lo + (hi - lo) / 3.0;
            double m2 = hi - (hi - lo) / 3.0;
            double p1[3] = {a[0] + d[0] * m1, a[1] + d[1] * m1, a[2] + d[2] * m1};
            double p2[3] = {a[0] + d[0] * m2, a[1] + d[1] * m2, a[2] + d[2] * m2};
            if (point_aabb_distance_sq(p1, mn, mx) < point_aabb_distance_sq(p2, mn, mx))
                hi = m2;
            else
                lo = m1;
        }
        PH3D_EVAL_SEG_AABB_T((lo + hi) * 0.5);
    }

    #undef PH3D_EVAL_SEG_AABB_T

    closest_axis[0] = a[0] + d[0] * best_t;
    closest_axis[1] = a[1] + d[1] * best_t;
    closest_axis[2] = a[2] + d[2] * best_t;
}

/// @brief Project a point onto the capsule's axis segment.
///
/// Used as the first step in capsule-vs-anything distance computations.
static void closest_point_capsule_axis_to_point(const rt_body3d *cap,
                                                const double *point,
                                                double *closest) {
    double a[3], c[3];
    capsule_axis_endpoints(cap, a, c);
    closest_point_on_segment(a, c, point, closest);
}

/// @brief Find the closest point pair on two capsule axes.
///
/// Handles arbitrary capsule orientations by solving segment-vs-segment.
static void closest_points_capsule_axes(const rt_body3d *a,
                                        const rt_body3d *b,
                                        double *closest_a,
                                        double *closest_b) {
    double aa[3], ac[3], ba[3], bc[3];
    capsule_axis_endpoints(a, aa, ac);
    capsule_axis_endpoints(b, ba, bc);
    closest_points_on_segments(aa, ac, ba, bc, closest_a, closest_b);
}

/// @brief Find the point on a capsule's axis closest to a box.
///
/// Used as the first step of capsule-vs-AABB collision.
static void closest_point_capsule_axis_to_aabb(const rt_body3d *cap,
                                               const rt_body3d *box,
                                               double *closest_axis) {
    double mn[3], mx[3], a[3], c[3];
    body_aabb(box, mn, mx);
    capsule_axis_endpoints(cap, a, c);
    closest_point_segment_to_aabb(a, c, mn, mx, closest_axis);
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

typedef struct contact_pair_hash_entry {
    const rt_contact3d *contact;
    uintptr_t hash;
} contact_pair_hash_entry;

/// @brief Fold `value` into running hash `key` (golden-ratio mix, à la Boost hash_combine);
///   never returns 0 so the result can serve as a non-empty-slot marker.
static uintptr_t contact_pair_hash_mix(uintptr_t key, uintptr_t value) {
    key ^= value + (uintptr_t)0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key ? key : (uintptr_t)1u;
}

/// @brief Hash a contact's identity (both body and both collider pointers) into one key.
static uintptr_t contact_pair_hash_value(const rt_contact3d *contact) {
    uintptr_t key = (uintptr_t)contact->body_a;
    key = contact_pair_hash_mix(key, (uintptr_t)contact->body_b);
    key = contact_pair_hash_mix(key, (uintptr_t)contact->collider_a);
    key = contact_pair_hash_mix(key, (uintptr_t)contact->collider_b);
    return key ? key : (uintptr_t)1u;
}

/// @brief Choose a power-of-two table capacity above 2×`count` (kept sparse to bound
///   probe lengths); returns 0 on overflow or non-positive count.
static int contact_pair_table_capacity(int32_t count, int32_t *out_capacity) {
    int32_t cap = 16;
    if (!out_capacity || count <= 0)
        return 0;
    if (count > INT32_MAX / 2)
        return 0;
    while (cap < count * 2) {
        if (cap > INT32_MAX / 2)
            return 0;
        cap *= 2;
    }
    if ((size_t)cap > SIZE_MAX / sizeof(contact_pair_hash_entry))
        return 0;
    *out_capacity = cap;
    return 1;
}

/// @brief Build an open-addressing (linear-probe) hash set over `count` contacts so
///   membership can be tested in O(1); returns the table (caller frees) and its
///   capacity via @p out_capacity, or NULL on allocation failure.
static contact_pair_hash_entry *contact_pair_table_build(const rt_contact3d *contacts,
                                                         int32_t count,
                                                         int32_t *out_capacity) {
    int32_t capacity = 0;
    contact_pair_hash_entry *table;
    if (!contacts || count <= 0 || !out_capacity)
        return NULL;
    if (!contact_pair_table_capacity(count, &capacity))
        return NULL;
    table = (contact_pair_hash_entry *)calloc((size_t)capacity, sizeof(*table));
    if (!table)
        return NULL;
    for (int32_t i = 0; i < count; ++i) {
        uintptr_t hash = contact_pair_hash_value(&contacts[i]);
        int32_t slot = (int32_t)(hash & (uintptr_t)(capacity - 1));
        for (int32_t probe = 0; probe < capacity; ++probe) {
            int32_t idx = (slot + probe) & (capacity - 1);
            if (!table[idx].contact) {
                table[idx].contact = &contacts[i];
                table[idx].hash = hash;
                break;
            }
        }
    }
    *out_capacity = capacity;
    return table;
}

/// @brief True if `needle`'s contact pair is present in the linear-probe table built by
///   contact_pair_table_build; stops at the first empty slot (open-addressing absence).
static int contact_pair_table_contains(const contact_pair_hash_entry *table,
                                       int32_t capacity,
                                       const rt_contact3d *needle) {
    uintptr_t hash;
    int32_t slot;
    if (!table || capacity <= 0 || !needle)
        return 0;
    hash = contact_pair_hash_value(needle);
    slot = (int32_t)(hash & (uintptr_t)(capacity - 1));
    for (int32_t probe = 0; probe < capacity; ++probe) {
        int32_t idx = (slot + probe) & (capacity - 1);
        if (!table[idx].contact)
            return 0;
        if (table[idx].hash == hash && contact_pair_equals(table[idx].contact, needle))
            return 1;
    }
    return 0;
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
        double sx = fabs(pose->scale[0]);
        double sy = fabs(pose->scale[1]);
        double sz = fabs(pose->scale[2]);
        double max_radial_scale = sx > sz ? sx : sz;
        double axis_dir[3];
        double local_y[3] = {0.0, 1.0, 0.0};
        quat_rotate_vec3(pose->rotation, local_y, axis_dir);
        if (vec3_normalize_in_place(axis_dir) <= 1e-12)
            vec3_set(axis_dir, 0.0, 1.0, 0.0);
        double side = vec3_dot(dir, axis_dir) >= 0.0 ? 1.0 : -1.0;
        out_point[0] =
            pose->position[0] + axis_dir[0] * half_axis * sy * side +
            dir[0] * radius * max_radial_scale;
        out_point[1] =
            pose->position[1] + axis_dir[1] * half_axis * sy * side +
            dir[1] * radius * max_radial_scale;
        out_point[2] =
            pose->position[2] + axis_dir[2] * half_axis * sy * side +
            dir[2] * radius * max_radial_scale;
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
/// Uses the overlapping region of the two world AABBs to pick a point on
/// the active contact face. This is deliberately conservative: for large
/// static boxes (floors, walls) a support point along an axis-aligned normal
/// can land on a far corner when the other normal components are zero, which
/// creates bogus torque in the contact solver.
static void compute_contact_point_from_leafs(void *a_leaf,
                                             const rt_collider_pose *a_pose,
                                             void *b_leaf,
                                             const rt_collider_pose *b_pose,
                                             const double *normal,
                                             double *point_out) {
    double point_a[3];
    double point_b[3];
    double inv_normal[3];
    double amn[3];
    double amx[3];
    double bmn[3];
    double bmx[3];
    double abs_n[3];
    int axis = 0;
    if (!point_out) {
        return;
    }
    vec3_set(point_out, 0.0, 0.0, 0.0);
    if (!a_leaf || !a_pose || !b_leaf || !b_pose || !normal)
        return;

    rt_collider3d_compute_world_aabb_raw(
        a_leaf, a_pose->position, a_pose->rotation, a_pose->scale, amn, amx);
    rt_collider3d_compute_world_aabb_raw(
        b_leaf, b_pose->position, b_pose->rotation, b_pose->scale, bmn, bmx);

    abs_n[0] = fabs(normal[0]);
    abs_n[1] = fabs(normal[1]);
    abs_n[2] = fabs(normal[2]);
    if (abs_n[1] > abs_n[axis])
        axis = 1;
    if (abs_n[2] > abs_n[axis])
        axis = 2;

    if (isfinite(amn[0]) && isfinite(amn[1]) && isfinite(amn[2]) &&
        isfinite(amx[0]) && isfinite(amx[1]) && isfinite(amx[2]) &&
        isfinite(bmn[0]) && isfinite(bmn[1]) && isfinite(bmn[2]) &&
        isfinite(bmx[0]) && isfinite(bmx[1]) && isfinite(bmx[2])) {
        for (int i = 0; i < 3; ++i) {
            double lo = fmax(amn[i], bmn[i]);
            double hi = fmin(amx[i], bmx[i]);
            if (i == axis && abs_n[i] > 1e-8) {
                double a_face = normal[i] >= 0.0 ? amx[i] : amn[i];
                double b_face = normal[i] >= 0.0 ? bmn[i] : bmx[i];
                point_out[i] = (a_face + b_face) * 0.5;
            } else if (lo <= hi) {
                point_out[i] = (lo + hi) * 0.5;
            } else {
                double ac = (amn[i] + amx[i]) * 0.5;
                double bc = (bmn[i] + bmx[i]) * 0.5;
                point_out[i] = (ac + bc) * 0.5;
            }
        }
        return;
    }

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
        {
            double raw_radius = rt_collider3d_get_radius_raw(collider);
            double raw_height = rt_collider3d_get_height_raw(collider);
            double scaled_radius = raw_radius * (sx > sz ? sx : sz);
            double scaled_cylinder = fmax(raw_height - 2.0 * raw_radius, 0.0) * sy;
            out->radius = scaled_radius;
            out->height = scaled_cylinder + 2.0 * scaled_radius;
        }
        return 1;
    default:
        return 0;
    }
}

/// @brief Compute a box collider's world half-extents by scaling its raw local
///   half-extents by the pose's absolute per-axis scale.
static void box_scaled_half_extents(void *collider,
                                    const rt_collider_pose *pose,
                                    double *half_extents) {
    double raw[3];
    rt_collider3d_get_box_half_extents_raw(collider, raw);
    half_extents[0] = raw[0] * pose_abs_scale_or_zero(pose ? pose->scale[0] : 1.0);
    half_extents[1] = raw[1] * pose_abs_scale_or_zero(pose ? pose->scale[1] : 1.0);
    half_extents[2] = raw[2] * pose_abs_scale_or_zero(pose ? pose->scale[2] : 1.0);
}

/// @brief Derive the pose's three orthonormal world axes (rows of its rotation) by
///   rotating the local basis vectors; degenerate axes fall back to the identity basis.
static void pose_rotation_axes(const rt_collider_pose *pose, double axes[3][3]) {
    const double local_x[3] = {1.0, 0.0, 0.0};
    const double local_y[3] = {0.0, 1.0, 0.0};
    const double local_z[3] = {0.0, 0.0, 1.0};
    quat_rotate_vec3(pose->rotation, local_x, axes[0]);
    quat_rotate_vec3(pose->rotation, local_y, axes[1]);
    quat_rotate_vec3(pose->rotation, local_z, axes[2]);
    for (int i = 0; i < 3; i++) {
        if (vec3_normalize_in_place(axes[i]) <= 1e-12) {
            axes[i][0] = axes[i][1] = axes[i][2] = 0.0;
            axes[i][i] = 1.0;
        }
    }
}

/// @brief SAT helper: if @p penetration is the smallest seen so far, record it and the
///   signed separating axis as the current best (minimum-penetration) result.
static void obb_record_axis(const double *axis,
                            double sign,
                            double penetration,
                            double *best_penetration,
                            double *best_axis) {
    if (penetration >= *best_penetration)
        return;
    *best_penetration = penetration;
    best_axis[0] = axis[0] * sign;
    best_axis[1] = axis[1] * sign;
    best_axis[2] = axis[2] * sign;
}

/// @brief Exact SAT test for two oriented box colliders.
static int test_box_box_obb(void *a_collider,
                            const rt_collider_pose *a_pose,
                            void *b_collider,
                            const rt_collider_pose *b_pose,
                            double *normal,
                            double *depth) {
    double a_he[3], b_he[3];
    double A[3][3], B[3][3];
    double R[3][3], AbsR[3][3];
    double t_world[3], t[3];
    double best_depth = DBL_MAX;
    double best_axis[3] = {1.0, 0.0, 0.0};
    const double eps = 1e-9;

    if (!a_collider || !a_pose || !b_collider || !b_pose || !normal || !depth)
        return 0;

    box_scaled_half_extents(a_collider, a_pose, a_he);
    box_scaled_half_extents(b_collider, b_pose, b_he);
    pose_rotation_axes(a_pose, A);
    pose_rotation_axes(b_pose, B);

    vec3_sub(b_pose->position, a_pose->position, t_world);
    for (int i = 0; i < 3; i++) {
        t[i] = vec3_dot(t_world, A[i]);
        for (int j = 0; j < 3; j++) {
            R[i][j] = vec3_dot(A[i], B[j]);
            AbsR[i][j] = fabs(R[i][j]) + eps;
        }
    }

    for (int i = 0; i < 3; i++) {
        double ra = a_he[i];
        double rb = b_he[0] * AbsR[i][0] + b_he[1] * AbsR[i][1] + b_he[2] * AbsR[i][2];
        double dist = fabs(t[i]);
        double penetration = ra + rb - dist;
        if (penetration < 0.0)
            return 0;
        obb_record_axis(A[i], t[i] >= 0.0 ? 1.0 : -1.0, penetration, &best_depth, best_axis);
    }

    for (int j = 0; j < 3; j++) {
        double ra = a_he[0] * AbsR[0][j] + a_he[1] * AbsR[1][j] + a_he[2] * AbsR[2][j];
        double rb = b_he[j];
        double dist = fabs(t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j]);
        double penetration = ra + rb - dist;
        if (penetration < 0.0)
            return 0;
        {
            double sign = vec3_dot(t_world, B[j]) >= 0.0 ? 1.0 : -1.0;
            obb_record_axis(B[j], sign, penetration, &best_depth, best_axis);
        }
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double axis[3];
            double ra;
            double rb;
            double dist;
            double penetration;
            vec3_cross(A[i], B[j], axis);
            if (vec3_normalize_in_place(axis) <= 1e-8)
                continue;
            ra = a_he[(i + 1) % 3] * AbsR[(i + 2) % 3][j] +
                 a_he[(i + 2) % 3] * AbsR[(i + 1) % 3][j];
            rb = b_he[(j + 1) % 3] * AbsR[i][(j + 2) % 3] +
                 b_he[(j + 2) % 3] * AbsR[i][(j + 1) % 3];
            dist = fabs(t[(i + 2) % 3] * R[(i + 1) % 3][j] -
                        t[(i + 1) % 3] * R[(i + 2) % 3][j]);
            penetration = ra + rb - dist;
            if (penetration < 0.0)
                return 0;
            obb_record_axis(axis,
                            vec3_dot(t_world, axis) >= 0.0 ? 1.0 : -1.0,
                            penetration,
                            &best_depth,
                            best_axis);
        }
    }

    vec3_copy(normal, best_axis);
    if (vec3_normalize_in_place(normal) <= 1e-12)
        vec3_set(normal, 0.0, 1.0, 0.0);
    *depth = best_depth == DBL_MAX ? 0.0 : best_depth;
    return *depth >= 0.0;
}

/// @brief Box-vs-sphere using the box's full oriented pose.
static int test_box_sphere_pose(void *box_collider,
                                const rt_collider_pose *box_pose,
                                const rt_body3d *sphere,
                                double *normal,
                                double *depth) {
    double raw_he[3];
    double local_center[3];
    double local_closest[3];
    double world_closest[3];
    double delta[3];
    double dist_sq;
    if (!box_collider || !box_pose || !sphere || !normal || !depth)
        return 0;
    rt_collider3d_get_box_half_extents_raw(box_collider, raw_he);
    transform_point_to_local(box_pose, sphere->position, local_center);
    for (int i = 0; i < 3; i++)
        local_closest[i] = clampd(local_center[i], -raw_he[i], raw_he[i]);
    transform_point_from_pose(box_pose, local_closest, world_closest);
    vec3_sub(sphere->position, world_closest, delta);
    dist_sq = vec3_len_sq(delta);
    if (dist_sq > sphere->radius * sphere->radius)
        return 0;
    if (dist_sq > 1e-18) {
        double dist = sqrt(dist_sq);
        normal[0] = delta[0] / dist;
        normal[1] = delta[1] / dist;
        normal[2] = delta[2] / dist;
        *depth = sphere->radius - dist;
        return 1;
    }

    {
        int axis = 0;
        double face_distance[3];
        double local_normal[3] = {0.0, 0.0, 0.0};
        for (int i = 0; i < 3; i++) {
            face_distance[i] =
                (raw_he[i] - fabs(local_center[i])) * pose_abs_scale_or_unit(box_pose->scale[i]);
            if (face_distance[i] < face_distance[axis])
                axis = i;
        }
        local_normal[axis] = local_center[axis] >= 0.0 ? 1.0 : -1.0;
        transform_normal_from_local(box_pose, local_normal, normal);
        *depth = sphere->radius + fmax(face_distance[axis], 0.0);
        return 1;
    }
}

/// @brief Box-vs-capsule via adaptive sphere samples along the capsule axis.
static int test_box_capsule_pose(void *box_collider,
                                 const rt_collider_pose *box_pose,
                                 const rt_body3d *capsule,
                                 double *normal,
                                 double *depth) {
    double a[3], b[3], axis[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    int samples;
    if (!box_collider || !box_pose || !capsule)
        return 0;
    capsule_axis_endpoints(capsule, a, b);
    vec3_sub(b, a, axis);
    samples = capsule_axis_sample_count(vec3_len(axis), capsule->radius);
    for (int i = 0; i < samples; i++) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        double cur_normal[3];
        double cur_depth;
        make_temp_sphere(&sphere,
                         (double[3]){a[0] + axis[0] * t,
                                     a[1] + axis[1] * t,
                                     a[2] + axis[2] * t},
                         capsule->radius);
        if (test_box_sphere_pose(box_collider, box_pose, &sphere, cur_normal, &cur_depth) &&
            (!hit || cur_depth > best_depth)) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
            hit = 1;
        }
    }
    if (!hit)
        return 0;
    vec3_copy(normal, best_normal);
    *depth = best_depth;
    return 1;
}

/// @brief Narrow-phase dispatch for two posed primitive colliders (box/sphere/capsule):
///   routes to the matching box-box/box-sphere/box-capsule (etc.) test, flipping the
///   returned normal so it always points from A toward B. Returns 1 on overlap with
///   @p normal/@p depth filled, else 0.
static int test_simple_collider_pose_collision(void *a_collider,
                                               const rt_collider_pose *a_pose,
                                               void *b_collider,
                                               const rt_collider_pose *b_pose,
                                               double *normal,
                                               double *depth) {
    int64_t a_type = rt_collider3d_get_type(a_collider);
    int64_t b_type = rt_collider3d_get_type(b_collider);
    rt_body3d proxy_a;
    rt_body3d proxy_b;

    if (a_type == RT_COLLIDER3D_TYPE_BOX && b_type == RT_COLLIDER3D_TYPE_BOX)
        return test_box_box_obb(a_collider, a_pose, b_collider, b_pose, normal, depth);

    if (a_type == RT_COLLIDER3D_TYPE_BOX) {
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b))
            return 0;
        if (proxy_b.shape == PH3D_SHAPE_SPHERE)
            return test_box_sphere_pose(a_collider, a_pose, &proxy_b, normal, depth);
        if (proxy_b.shape == PH3D_SHAPE_CAPSULE)
            return test_box_capsule_pose(a_collider, a_pose, &proxy_b, normal, depth);
    }

    if (b_type == RT_COLLIDER3D_TYPE_BOX) {
        int hit;
        if (!build_simple_proxy(a_pose, a_collider, &proxy_a))
            return 0;
        if (proxy_a.shape == PH3D_SHAPE_SPHERE)
            hit = test_box_sphere_pose(b_collider, b_pose, &proxy_a, normal, depth);
        else if (proxy_a.shape == PH3D_SHAPE_CAPSULE)
            hit = test_box_capsule_pose(b_collider, b_pose, &proxy_a, normal, depth);
        else
            hit = 0;
        if (hit)
            vec3_negate(normal, normal);
        return hit;
    }

    if (!build_simple_proxy(a_pose, a_collider, &proxy_a) ||
        !build_simple_proxy(b_pose, b_collider, &proxy_b))
        return 0;
    return test_simple_collision(&proxy_a, &proxy_b, normal, depth);
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

/// @brief Capsule-vs-mesh narrow phase via adaptive sphere samples along the capsule axis.
///
/// Samples at radius-relative spacing instead of a fixed small count, which
/// avoids missing side contacts on long capsules.
static int test_meshlike_capsule(rt_mesh3d *mesh,
                                 const rt_collider_pose *mesh_pose,
                                 const rt_body3d *capsule,
                                 double *normal,
                                 double *depth) {
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    double axis_a[3], axis_b[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int samples;
    capsule_axis_endpoints(capsule, axis_a, axis_b);
    {
        double axis_delta[3];
        vec3_sub(axis_b, axis_a, axis_delta);
        half_axis = vec3_len(axis_delta);
    }
    samples = capsule_axis_sample_count(half_axis, capsule->radius);
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        memset(&sphere, 0, sizeof(sphere));
        sphere.shape = PH3D_SHAPE_SPHERE;
        sphere.position[0] = axis_a[0] + (axis_b[0] - axis_a[0]) * t;
        sphere.position[1] = axis_a[1] + (axis_b[1] - axis_a[1]) * t;
        sphere.position[2] = axis_a[2] + (axis_b[2] - axis_a[2]) * t;
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

/// @brief Initialize a collider pose (identity scale) from a body's position and
///   orientation — used to bring a body proxy into the SAT routines.
static void body_pose_from_proxy(const rt_body3d *body, rt_collider_pose *pose) {
    collider_pose_identity(pose);
    if (!body)
        return;
    vec3_copy(pose->position, body->position);
    pose->rotation[0] = body->orientation[0];
    pose->rotation[1] = body->orientation[1];
    pose->rotation[2] = body->orientation[2];
    pose->rotation[3] = body->orientation[3];
}

/// @brief One SAT axis test for a triangle vs a box (in the box's local frame): project
///   both onto the normalized @p axis_in; returns 0 if they are separated, else 1 and
///   updates the running minimum-overlap axis/sign. Zero-length axes are skipped (return 1).
static int test_triangle_box_local_axis(const double tri[3][3],
                                        const double he[3],
                                        const double axis_in[3],
                                        double *best_depth,
                                        double *best_axis,
                                        double *best_sign) {
    double axis[3] = {axis_in[0], axis_in[1], axis_in[2]};
    double len = vec3_normalize_in_place(axis);
    double min_p;
    double max_p;
    double radius;
    double overlap;
    if (len <= 1e-10)
        return 1;
    min_p = max_p = vec3_dot(tri[0], axis);
    for (int i = 1; i < 3; i++) {
        double p = vec3_dot(tri[i], axis);
        if (p < min_p)
            min_p = p;
        if (p > max_p)
            max_p = p;
    }
    radius = he[0] * fabs(axis[0]) + he[1] * fabs(axis[1]) + he[2] * fabs(axis[2]);
    if (max_p < -radius || min_p > radius)
        return 0;
    overlap = fmin(radius - min_p, max_p + radius);
    if (overlap < *best_depth) {
        double centroid[3] = {(tri[0][0] + tri[1][0] + tri[2][0]) / 3.0,
                              (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0,
                              (tri[0][2] + tri[1][2] + tri[2][2]) / 3.0};
        *best_depth = overlap;
        vec3_copy(best_axis, axis);
        *best_sign = vec3_dot(centroid, axis) <= 0.0 ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Full triangle-vs-oriented-box overlap test via the separating-axis theorem:
///   transforms the triangle into the box's local space and tests the 3 box axes, the
///   triangle normal, and the 9 edge cross-product axes. Returns 1 on overlap with the
///   world-space @p normal (pointing out of the box) and penetration @p depth.
static int test_triangle_box_obb(const double *world_a,
                                 const double *world_b,
                                 const double *world_c,
                                 const rt_body3d *box,
                                 double *normal,
                                 double *depth) {
    rt_collider_pose box_pose;
    double tri[3][3];
    double edge0[3], edge1[3], edge2[3];
    double tri_normal[3];
    double best_depth = DBL_MAX;
    double best_axis[3] = {0.0, 1.0, 0.0};
    double best_sign = 1.0;
    const double axes[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    if (!world_a || !world_b || !world_c || !box || !normal || !depth)
        return 0;
    body_pose_from_proxy(box, &box_pose);
    transform_point_to_local(&box_pose, world_a, tri[0]);
    transform_point_to_local(&box_pose, world_b, tri[1]);
    transform_point_to_local(&box_pose, world_c, tri[2]);

    vec3_sub(tri[1], tri[0], edge0);
    vec3_sub(tri[2], tri[1], edge1);
    vec3_sub(tri[0], tri[2], edge2);
    vec3_cross(edge0, edge1, tri_normal);

    for (int i = 0; i < 3; i++) {
        if (!test_triangle_box_local_axis(
                tri, box->half_extents, axes[i], &best_depth, best_axis, &best_sign))
            return 0;
    }
    if (!test_triangle_box_local_axis(
            tri, box->half_extents, tri_normal, &best_depth, best_axis, &best_sign))
        return 0;
    for (int e = 0; e < 3; e++) {
        const double *edge = e == 0 ? edge0 : (e == 1 ? edge1 : edge2);
        for (int a = 0; a < 3; a++) {
            double axis[3];
            vec3_cross(edge, axes[a], axis);
            if (!test_triangle_box_local_axis(
                    tri, box->half_extents, axis, &best_depth, best_axis, &best_sign))
                return 0;
        }
    }

    best_axis[0] *= best_sign;
    best_axis[1] *= best_sign;
    best_axis[2] *= best_sign;
    quat_rotate_vec3(box_pose.rotation, best_axis, normal);
    if (vec3_normalize_in_place(normal) <= 1e-12)
        vec3_set(normal, 0.0, 1.0, 0.0);
    *depth = best_depth == DBL_MAX ? 0.0 : best_depth;
    return 1;
}

/// @brief Test a posed triangle mesh against a box body by running the triangle-vs-OBB
///   SAT test over every mesh triangle and keeping the deepest contact. Returns 1 on any
///   overlap with @p normal/@p depth from the deepest triangle, else 0.
static int test_meshlike_box(rt_mesh3d *mesh,
                             const rt_collider_pose *mesh_pose,
                             const rt_body3d *box,
                             double *normal,
                             double *depth) {
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    if (!mesh || !mesh_pose || !box)
        return 0;
    rt_mesh3d_refresh_bounds(mesh);
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], local[3], cur_normal[3], cur_depth;
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
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
        if (test_triangle_box_obb(a, b, c, box, cur_normal, &cur_depth) &&
            (!hit || cur_depth > best_depth)) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
            hit = 1;
        }
    }
    if (!hit)
        return 0;
    vec3_copy(normal, best_normal);
    *depth = best_depth;
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
    double scale_y;
    double local_radius_y;
    double penetration;
    transform_point_to_local(field_pose, sphere->position, local_center);
    if (!rt_collider3d_sample_heightfield_raw(
            heightfield, local_center[0], local_center[2], &surface_height, local_normal))
        return 0;
    scale_y = pose_abs_scale_or_unit(field_pose->scale[1]);
    local_radius_y = sphere->radius / scale_y;
    penetration = surface_height - (local_center[1] - local_radius_y);
    if (penetration <= 0.0)
        return 0;
    transform_normal_from_local(field_pose, local_normal, normal);
    *depth = penetration * scale_y;
    return 1;
}

/// @brief Capsule-vs-heightfield narrow phase.
///
/// Samples along the oriented capsule axis and keeps the deepest hit.
static int test_heightfield_capsule(void *heightfield,
                                    const rt_collider_pose *field_pose,
                                    const rt_body3d *capsule,
                                    double *normal,
                                    double *depth) {
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    double axis_a[3], axis_b[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int samples = half_axis > 1e-9 ? 5 : 1;
    capsule_axis_endpoints(capsule, axis_a, axis_b);
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        double cur_normal[3];
        double cur_depth;
        memset(&sphere, 0, sizeof(sphere));
        sphere.shape = PH3D_SHAPE_SPHERE;
        sphere.position[0] = axis_a[0] + (axis_b[0] - axis_a[0]) * t;
        sphere.position[1] = axis_a[1] + (axis_b[1] - axis_a[1]) * t;
        sphere.position[2] = axis_a[2] + (axis_b[2] - axis_a[2]) * t;
        sphere.radius = capsule->radius;
        if (test_heightfield_sphere(heightfield, field_pose, &sphere, cur_normal, &cur_depth) &&
            cur_depth > best_depth) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
        }
    }
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Box-vs-heightfield narrow phase via 5-sample bottom probe.
///
/// Samples the heightfield at the oriented box's four local bottom corners
/// and its bottom center; the deepest penetration wins. Approximate but
/// cheap; matches the resolution of the heightfield grid in practice.
static int test_heightfield_box(void *heightfield,
                                const rt_collider_pose *field_pose,
                                const rt_body3d *box,
                                double *normal,
                                double *depth) {
    rt_collider_pose box_pose;
    double samples[5][3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    double local_samples[5][3];
    if (!box)
        return 0;
    body_pose_from_proxy(box, &box_pose);
    vec3_set(local_samples[0], -box->half_extents[0], -box->half_extents[1], -box->half_extents[2]);
    vec3_set(local_samples[1], -box->half_extents[0], -box->half_extents[1], box->half_extents[2]);
    vec3_set(local_samples[2], box->half_extents[0], -box->half_extents[1], -box->half_extents[2]);
    vec3_set(local_samples[3], box->half_extents[0], -box->half_extents[1], box->half_extents[2]);
    vec3_set(local_samples[4], 0.0, -box->half_extents[1], 0.0);
    for (int i = 0; i < 5; i++)
        transform_point_from_pose(&box_pose, local_samples[i], samples[i]);
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
                transform_normal_from_local(field_pose, local_normal, best_normal);
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
    *depth = best_depth * pose_abs_scale_or_unit(field_pose->scale[1]);
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
        if (!test_simple_collider_pose_collision(
                a_collider, a_pose, b_collider, b_pose, normal, depth))
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
            (proxy_b.shape == PH3D_SHAPE_AABB &&
             !test_meshlike_box(mesh, a_pose, &proxy_b, normal, depth)) ||
            ((proxy_b.shape != PH3D_SHAPE_SPHERE && proxy_b.shape != PH3D_SHAPE_CAPSULE &&
              proxy_b.shape != PH3D_SHAPE_AABB) &&
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
                                const double *point,
                                double depth,
                                double *relative_speed_out) {
    double r_a[3];
    double r_b[3];
    double vel_a[3];
    double vel_b[3];
    double rv_vec[3];
    double rv;
    double denom;
    double e;
    double j;
    double impulse[3];
    int woke = 0;

    if (point) {
        vec3_sub(point, a->position, r_a);
        vec3_sub(point, b->position, r_b);
    } else {
        vec3_set(r_a, 0.0, 0.0, 0.0);
        vec3_set(r_b, 0.0, 0.0, 0.0);
    }

    body3d_contact_velocity(a, r_a, vel_a);
    body3d_contact_velocity(b, r_b, vel_b);
    vec3_sub(vel_b, vel_a, rv_vec);
    rv = vec3_dot(rv_vec, n);
    if (relative_speed_out)
        *relative_speed_out = fabs(rv);
    if (rv > 0) {
        double inv_sum = a->inv_mass + b->inv_mass;
        if (inv_sum > 1e-12) {
            double slop = 0.01;
            double correction = fmax(depth - slop, 0.0) * 0.4 / inv_sum;
            a->position[0] -= correction * a->inv_mass * n[0];
            a->position[1] -= correction * a->inv_mass * n[1];
            a->position[2] -= correction * a->inv_mass * n[2];
            b->position[0] += correction * b->inv_mass * n[0];
            b->position[1] += correction * b->inv_mass * n[1];
            b->position[2] += correction * b->inv_mass * n[2];
        }
        return 0.0; /* separating */
    }

    e = (a->restitution < b->restitution) ? a->restitution : b->restitution;
    denom = body3d_contact_impulse_denominator(a, r_a, n) +
            body3d_contact_impulse_denominator(b, r_b, n);
    if (denom < 1e-12)
        return 0.0;

    j = -(1.0 + e) * rv / denom;
    impulse[0] = j * n[0];
    impulse[1] = j * n[1];
    impulse[2] = j * n[2];
    {
        double neg_impulse[3] = {-impulse[0], -impulse[1], -impulse[2]};
        body3d_apply_contact_impulse(a, neg_impulse, r_a);
        body3d_apply_contact_impulse(b, impulse, r_b);
    }

    /* Coulomb friction — tangential impulse capped by mu * normal impulse */
    {
        double fa = ph3d_clamp_nonnegative_finite(a->friction, 0.0);
        double fb = ph3d_clamp_nonnegative_finite(b->friction, 0.0);
        double mu = sqrt(fa * fb);
        double rv_n;
        double tx;
        double ty;
        double tz;
        double tlen;
        body3d_contact_velocity(a, r_a, vel_a);
        body3d_contact_velocity(b, r_b, vel_b);
        vec3_sub(vel_b, vel_a, rv_vec);
        rv_n = vec3_dot(rv_vec, n);
        tx = rv_vec[0] - rv_n * n[0];
        ty = rv_vec[1] - rv_n * n[1];
        tz = rv_vec[2] - rv_n * n[2];
        tlen = sqrt(tx * tx + ty * ty + tz * tz);
        if (tlen > 1e-8) {
            double tangent[3];
            double tangent_denom;
            double jt;
            double friction_impulse[3];
            tx /= tlen;
            ty /= tlen;
            tz /= tlen;
            vec3_set(tangent, tx, ty, tz);
            tangent_denom = body3d_contact_impulse_denominator(a, r_a, tangent) +
                            body3d_contact_impulse_denominator(b, r_b, tangent);
            if (tangent_denom < 1e-12)
                tangent_denom = denom;
            jt = -vec3_dot(rv_vec, tangent) / tangent_denom;
            if (fabs(jt) > mu * j)
                jt = (jt > 0 ? 1.0 : -1.0) * mu * j;
            friction_impulse[0] = jt * tx;
            friction_impulse[1] = jt * ty;
            friction_impulse[2] = jt * tz;
            {
                double neg_friction_impulse[3] = {
                    -friction_impulse[0], -friction_impulse[1], -friction_impulse[2]};
                body3d_apply_contact_impulse(a, neg_friction_impulse, r_a);
                body3d_apply_contact_impulse(b, friction_impulse, r_b);
            }
            if (fabs(jt) > 1e-8) {
                woke = 1;
            }
        }
    }

    /* Baumgarte positional correction (40%, 1% slop) */
    double slop = 0.01;
    double inv_sum = a->inv_mass + b->inv_mass;
    double correction = inv_sum > 1e-12 ? fmax(depth - slop, 0.0) * 0.4 / inv_sum : 0.0;
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

/// @brief qsort comparator for sweep-and-prune broad-phase — sorts entries by min-X.
/// @details After sorting, the inner collision loop can break early as soon as
///   entry[j].min[0] > entry[i].max[0], reducing the O(n²) pair count in practice.
static int ph3d_broadphase_compare_min_x(const void *lhs, const void *rhs) {
    const ph3d_broadphase_entry *a = (const ph3d_broadphase_entry *)lhs;
    const ph3d_broadphase_entry *b = (const ph3d_broadphase_entry *)rhs;
    if (a->min[0] < b->min[0])
        return -1;
    if (a->min[0] > b->min[0])
        return 1;
    return 0;
}

/// @brief Return 1 if two 3D AABBs overlap, 0 if separated on any axis.
/// @details Separating-axis test on all three axes simultaneously.  Used in the
///   broad phase after the X-axis early-out to confirm overlap on Y and Z.
static int ph3d_bounds_overlap(const double *a_min, const double *a_max, const double *b_min, const double *b_max) {
    return a_min[0] <= b_max[0] && a_max[0] >= b_min[0] && a_min[1] <= b_max[1] &&
           a_max[1] >= b_min[1] && a_min[2] <= b_max[2] && a_max[2] >= b_min[2];
}

/// @brief Run narrow-phase collision detection and impulse response for one body pair.
/// @details Skips pairs that are both non-dynamic or that fail the bidirectional
///   layer/mask filter.  On a detected overlap, appends a new rt_contact3d to the
///   world's contact array (reallocating if needed), then either calls resolve_collision
///   for non-trigger pairs (applying impulse + Baumgarte correction) or just records
///   the relative speed for trigger pairs.  Updates is_grounded on whichever body has
///   a contact normal pointing more than ~45° upward (|normal.y| > 0.7).
/// @param w  World containing the bodies.
/// @param a  First body of the candidate pair.
/// @param b  Second body of the candidate pair.
/// @return   1 on success (including skipped pairs), 0 if the contact array
///           could not be reallocated.
static int world3d_process_collision_pair(rt_world3d *w, rt_body3d *a, rt_body3d *b) {
    double normal[3], depth, point[3];
    double relative_speed = 0.0;
    double normal_impulse = 0.0;
    void *leaf_a = NULL;
    void *leaf_b = NULL;

    if (!w || !a || !b || a == b)
        return 1;
    if (a->motion_mode != PH3D_MODE_DYNAMIC && b->motion_mode != PH3D_MODE_DYNAMIC)
        return 1;
    if (!(a->collision_layer & b->collision_mask))
        return 1;
    if (!(b->collision_layer & a->collision_mask))
        return 1;
    if (!test_collision(a, b, normal, &depth, point, &leaf_a, &leaf_b))
        return 1;

    int32_t next_contact_count;
    if (!world3d_checked_increment(w->contact_count, &next_contact_count) ||
        !world3d_reserve_contacts(w, next_contact_count)) {
        rt_trap("Physics3D.World.Step: contact allocation failed");
        return 0;
    }

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
        normal_impulse = resolve_collision(a, b, normal, point, depth, &relative_speed);
    }
    c->relative_speed = relative_speed;
    c->normal_impulse = normal_impulse;

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
    return 1;
}

/// @brief Cheap broad-phase rejection test for a body pair before narrow-phase: rejects
///   self-pairs, static-vs-static pairs, and pairs failing the bidirectional layer/mask
///   filter. Returns 1 only if the pair could plausibly collide.
static int world3d_pair_can_collide_cheap(const rt_body3d *a, const rt_body3d *b) {
    if (!a || !b || a == b)
        return 0;
    if (a->motion_mode != PH3D_MODE_DYNAMIC && b->motion_mode != PH3D_MODE_DYNAMIC)
        return 0;
    if (!(a->collision_layer & b->collision_mask))
        return 0;
    if (!(b->collision_layer & a->collision_mask))
        return 0;
    return 1;
}

/// @brief Run the full broad-phase + narrow-phase collision pass for one world step.
/// @details Clears the contact list from the previous step, then:
///   1. Attempts to allocate a broadphase_entries scratch array for sweep-and-prune.
///   2. If allocation fails, falls back to brute-force O(n²) pair testing.
///   3. On success: computes each body's AABB, sorts entries by min-X (SAP), and
///      only tests pairs whose X intervals overlap, short-circuiting the inner loop
///      when the X-axis gap guarantees no overlap.
///   Each surviving pair is processed by world3d_process_collision_pair, which
///   handles narrow-phase detection, impulse resolution, and contact recording.
/// @param w  World to process.
/// @return   1 on success, 0 if any contact could not be allocated.
static int world3d_detect_and_resolve_contacts(rt_world3d *w) {
    if (!w)
        return 0;
    w->contact_count = 0;
    if (w->body_count <= 1)
        return 1;

    if (!world3d_reserve_broadphase_capacity(w, w->body_count)) {
        for (int32_t i = 0; i < w->body_count; i++) {
            for (int32_t j = i + 1; j < w->body_count; j++) {
                if (!world3d_pair_can_collide_cheap(w->bodies[i], w->bodies[j]))
                    continue;
                if (!world3d_process_collision_pair(w, w->bodies[i], w->bodies[j]))
                    return 0;
            }
        }
        return 1;
    }

    ph3d_broadphase_entry *entries = w->broadphase_entries;
    int32_t entry_count = 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *body = w->bodies[i];
        if (!body || !body->collider)
            continue;
        entries[entry_count].body = body;
        body_aabb(body, entries[entry_count].min, entries[entry_count].max);
        entry_count++;
    }
    qsort(entries, (size_t)entry_count, sizeof(*entries), ph3d_broadphase_compare_min_x);

    for (int32_t i = 0; i < entry_count; i++) {
        for (int32_t j = i + 1; j < entry_count; j++) {
            if (entries[j].min[0] > entries[i].max[0])
                break;
            if (!ph3d_bounds_overlap(entries[i].min,
                                     entries[i].max,
                                     entries[j].min,
                                     entries[j].max))
                continue;
            if (!world3d_pair_can_collide_cheap(entries[i].body, entries[j].body))
                continue;
            if (!world3d_process_collision_pair(w, entries[i].body, entries[j].body)) {
                return 0;
            }
        }
    }
    return 1;
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
        (rt_physics_hit3d_obj *)rt_obj_new_i64(RT_G3D_PHYSICSHIT3D_CLASS_ID, (int64_t)sizeof(rt_physics_hit3d_obj));
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
    list = (rt_physics_hit_list3d_obj *)rt_obj_new_i64(RT_G3D_PHYSICSHITLIST3D_CLASS_ID, (int64_t)sizeof(*list));
    if (!list) {
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    memset(list, 0, sizeof(*list));
    rt_obj_set_finalizer(list, physics_hit_list3d_finalizer);
    list->items = (void **)calloc((size_t)count, sizeof(void *));
    if (!list->items) {
        if (rt_obj_release_check0(list))
            rt_obj_free(list);
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    list->count = count;
    for (int32_t i = 0; i < count; ++i) {
        list->items[i] = physics_hit3d_new(&hits[i]);
        if (!list->items[i]) {
            if (rt_obj_release_check0(list))
                rt_obj_free(list);
            rt_trap("PhysicsHitList3D: allocation failed");
            return NULL;
        }
    }
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
    point = (rt_contact_point3d_obj *)rt_obj_new_i64(RT_G3D_CONTACTPOINT3D_CLASS_ID, (int64_t)sizeof(*point));
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
    event = (rt_collision_event3d_obj *)rt_obj_new_i64(RT_G3D_COLLISIONEVENT3D_CLASS_ID, (int64_t)sizeof(*event));
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
/// Event arrays grow with the live contact list so large production scenes
/// do not silently drop contacts after the initial capacity.
static void world3d_build_event_buffers(rt_world3d *w) {
    contact_pair_hash_entry *previous_table = NULL;
    contact_pair_hash_entry *current_table = NULL;
    int32_t previous_table_capacity = 0;
    int32_t current_table_capacity = 0;
    if (!w)
        return;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;

    previous_table = contact_pair_table_build(
        w->previous_contacts, w->previous_contact_count, &previous_table_capacity);
    current_table =
        contact_pair_table_build(w->contacts, w->contact_count, &current_table_capacity);

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = previous_table ? contact_pair_table_contains(previous_table,
                                                                 previous_table_capacity,
                                                                 &w->contacts[i])
                                   : 0;
        if (!previous_table) {
            for (int32_t j = 0; j < w->previous_contact_count; ++j) {
                if (contact_pair_equals(&w->contacts[i], &w->previous_contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (found) {
            int32_t next_stay_count;
            if (!world3d_checked_increment(w->stay_event_count, &next_stay_count) ||
                !world3d_reserve_stay_events(w, next_stay_count)) {
                rt_trap("Physics3D.World.Step: stay-event allocation failed");
                free(previous_table);
                free(current_table);
                return;
            }
            contact_snapshot_copy(&w->stay_events[w->stay_event_count++], &w->contacts[i]);
        } else {
            int32_t next_enter_count;
            if (!world3d_checked_increment(w->enter_event_count, &next_enter_count) ||
                !world3d_reserve_enter_events(w, next_enter_count)) {
                rt_trap("Physics3D.World.Step: enter-event allocation failed");
                free(previous_table);
                free(current_table);
                return;
            }
            contact_snapshot_copy(&w->enter_events[w->enter_event_count++], &w->contacts[i]);
        }
    }

    for (int32_t i = 0; i < w->previous_contact_count; ++i) {
        int found = current_table ? contact_pair_table_contains(current_table,
                                                               current_table_capacity,
                                                               &w->previous_contacts[i])
                                  : 0;
        if (!current_table) {
            for (int32_t j = 0; j < w->contact_count; ++j) {
                if (contact_pair_equals(&w->previous_contacts[i], &w->contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            int32_t next_exit_count;
            if (!world3d_checked_increment(w->exit_event_count, &next_exit_count) ||
                !world3d_reserve_exit_events(w, next_exit_count)) {
                rt_trap("Physics3D.World.Step: exit-event allocation failed");
                free(previous_table);
                free(current_table);
                return;
            }
            contact_snapshot_copy(&w->exit_events[w->exit_event_count++], &w->previous_contacts[i]);
        }
    }

    if (!world3d_reserve_previous_contacts(w, w->contact_count)) {
        rt_trap("Physics3D.World.Step: previous-contact allocation failed");
        free(previous_table);
        free(current_table);
        return;
    }
    w->previous_contact_count = w->contact_count;
    for (int32_t i = 0; i < w->contact_count; ++i)
        contact_snapshot_copy(&w->previous_contacts[i], &w->contacts[i]);
    free(previous_table);
    free(current_table);
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
    free(w->bodies);
    free(w->contacts);
    free(w->previous_contacts);
    free(w->enter_events);
    free(w->stay_events);
    free(w->exit_events);
    free(w->joints);
    free(w->joint_types);
    free(w->broadphase_entries);
    w->bodies = NULL;
    w->contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
}

/// @brief `Physics3D.World.New(gx, gy, gz)` — construct an empty world with gravity.
///
/// Allocates an `rt_world3d`, initializes the gravity vector, and zeros
/// every contact / event / joint table. Body, contact, and joint arrays
/// start at production-sized defaults and grow on demand. Hooks the GC
/// finalizer so dropping the world frees the simulation.
///
/// @param gx,gy,gz Initial world gravity acceleration (m/s²).
/// @return Owned `World3D` handle.
void *rt_world3d_new(double gx, double gy, double gz) {
    rt_world3d *w = (rt_world3d *)rt_obj_new_i64(RT_G3D_WORLD3D_CLASS_ID, (int64_t)sizeof(rt_world3d));
    if (!w) {
        rt_trap("Physics3D.World.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    ph3d_vec3_set_finite(w->gravity, gx, gy, gz);
    w->body_count = 0;
    w->contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
    w->bodies = NULL;
    w->contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    if (!world3d_reserve_body_capacity(w, PH3D_INITIAL_BODIES) ||
        !world3d_reserve_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_previous_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_enter_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_stay_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_exit_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_joint_capacity(w, PH3D_INITIAL_JOINTS) ||
        !world3d_reserve_broadphase_capacity(w, PH3D_INITIAL_BODIES)) {
        world3d_finalizer(w);
        rt_trap("Physics3D.World.New: storage allocation failed");
        return NULL;
    }
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
    if (!w || !isfinite(dt) || dt <= 0.0)
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
        if (!isfinite(speed))
            continue;
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
///   2. **Collision detection**: sweep-and-prune broad phase, narrow phase
///      via `test_collision`, append contacts, run `resolve_collision` for
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
    rt_world3d *w = world3d_checked(obj);
    if (!w || !isfinite(dt) || dt <= 0)
        return;
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
        if (!world3d_detect_and_resolve_contacts(w))
            return;

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
/// dispatch correctly. Storage grows on demand.
void rt_world3d_add_joint(void *obj, void *joint, int64_t joint_type) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || !joint)
        return;
    if (!joint3d_matches_type(joint, joint_type)) {
        rt_trap("Physics3D.World.AddJoint: joint object does not match joint type");
        return;
    }
    for (int32_t i = 0; i < w->joint_count; i++) {
        if (w->joints[i] == joint)
            return;
    }
    if (!world3d_reserve_joint_capacity(w, w->joint_count + 1)) {
        rt_trap("Physics3D: joint storage allocation failed");
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
    rt_world3d *w = world3d_checked(obj);
    if (!w || !joint)
        return;
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
    rt_world3d *w = world3d_checked(obj);
    return w ? w->joint_count : 0;
}

/// @brief `World3D.Add(body)` — register a body.
///
/// Retains the body and stores it in the growable bodies array.
void rt_world3d_add(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == b)
            return;
    }
    if (!world3d_reserve_body_capacity(w, w->body_count + 1)) {
        rt_trap("Physics3D: body storage allocation failed");
        return;
    }
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = b;
}

/// @brief `World3D.Remove(body)` — unregister a body.
///
/// Same swap-with-last compaction as `RemoveJoint`. Releases the world's
/// reference, which may free the body if no other holder remains.
void rt_world3d_remove(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == b) {
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
    rt_world3d *w = world3d_checked(obj);
    return w ? w->body_count : 0;
}

/// @brief `World3D.SetGravity(gx, gy, gz)` — change the world gravity vector.
///
/// Takes effect on the next `Step`. Existing in-flight velocities are
/// not modified.
void rt_world3d_set_gravity(void *obj, double gx, double gy, double gz) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    ph3d_vec3_set_finite(w->gravity, gx, gy, gz);
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
    rt_world3d *w = world3d_checked(obj);
    return w ? w->contact_count : 0;
}

/// @brief `World3D.CollisionBodyA(i)` — borrowed reference to body A in contact `i`.
void *rt_world3d_get_collision_body_a(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return NULL;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_a;
}

/// @brief `World3D.CollisionBodyB(i)` — borrowed reference to body B in contact `i`.
void *rt_world3d_get_collision_body_b(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return NULL;
    if (index < 0 || index >= w->contact_count)
        return NULL;
    return w->contacts[index].body_b;
}

/// @brief `World3D.CollisionNormal(i)` — fresh `Vec3` of the contact normal.
void *rt_world3d_get_collision_normal(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return rt_vec3_new(0, 0, 0);
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
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return 0;
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
    rt_world3d *w = world3d_checked(obj);
    return w ? w->contact_count : 0;
}

/// @brief `World3D.CollisionEvent(i)` — boxed `CollisionEvent3D` for contact `i`.
///
/// Returns NULL on out-of-range. The returned event is owned by the
/// caller (unlike the borrowed-reference body accessors).
void *rt_world3d_get_collision_event(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
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
    rt_world3d *w = world3d_checked(obj);
    return w ? w->enter_event_count : 0;
}

/// @brief `World3D.EnterEvent(i)` — boxed event for the i-th new contact.
void *rt_world3d_get_enter_event(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || index < 0 || index >= w->enter_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->enter_events[index]);
}

/// @brief `World3D.StayEventCount` — contacts that persisted from the previous frame.
int64_t rt_world3d_get_stay_event_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->stay_event_count : 0;
}

/// @brief `World3D.StayEvent(i)` — boxed event for the i-th continuing contact.
void *rt_world3d_get_stay_event(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || index < 0 || index >= w->stay_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->stay_events[index]);
}

/// @brief `World3D.ExitEventCount` — contacts that ended this frame.
int64_t rt_world3d_get_exit_event_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->exit_event_count : 0;
}

/// @brief `World3D.ExitEvent(i)` — boxed event for the i-th newly-ended contact.
void *rt_world3d_get_exit_event(void *obj, int64_t index) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || index < 0 || index >= w->exit_event_count)
        return NULL;
    return collision_event3d_new_from_contact(&w->exit_events[index]);
}

/// @brief Clear all collision buffers currently visible to event/query APIs.
///
/// This is intentionally stronger than only zeroing this frame's contacts:
/// callers use it after handling events, so the next `Step` should not report
/// stale `stay` or `exit` transitions from the cleared frame.
void rt_world3d_clear_collision_events(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    w->contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
}

// Boxed `CollisionEvent3D` field accessors — exposed to Zia as
// properties on the event object. Borrowed references for body /
// collider (the event already holds a retained ref).

/// @brief `CollisionEvent3D.BodyA` — first body in the collision pair.
void *rt_collision_event3d_get_body_a(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->body_a : NULL;
}

/// @brief `CollisionEvent3D.BodyB` — second body in the collision pair.
void *rt_collision_event3d_get_body_b(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->body_b : NULL;
}

/// @brief `CollisionEvent3D.ColliderA` — leaf collider on body A (for compounds).
void *rt_collision_event3d_get_collider_a(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->collider_a : NULL;
}

/// @brief `CollisionEvent3D.ColliderB` — leaf collider on body B (for compounds).
void *rt_collision_event3d_get_collider_b(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->collider_b : NULL;
}

/// @brief `CollisionEvent3D.IsTrigger` — whether either party is a trigger.
int8_t rt_collision_event3d_get_is_trigger(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->is_trigger : 0;
}

/// @brief `CollisionEvent3D.ContactCount` — currently 1 per event (single-point contacts).
///
/// Reserved for future multi-point contact manifolds; today every event
/// carries exactly one representative contact point.
int64_t rt_collision_event3d_get_contact_count(void *obj) {
    return collision_event3d_checked(obj) ? 1 : 0;
}

/// @brief `CollisionEvent3D.RelativeSpeed` — closing speed along the contact normal.
double rt_collision_event3d_get_relative_speed(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->relative_speed : 0.0;
}

/// @brief `CollisionEvent3D.NormalImpulse` — magnitude of the resolved normal impulse.
///
/// Useful for damage models or audio: louder/heavier sound on bigger
/// impulses. Always 0 for trigger events (no impulse is applied).
double rt_collision_event3d_get_normal_impulse(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->normal_impulse : 0.0;
}

/// @brief `CollisionEvent3D.Contact(i)` — boxed contact-point sub-object.
///
/// Currently only `i == 0` is valid (single-point contacts). Reconstructs
/// a transient `rt_contact3d` from the event's stored fields and boxes
/// it via `contact_point3d_new_from_contact`.
void *rt_collision_event3d_get_contact(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
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
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index != 0)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(event->point[0], event->point[1], event->point[2]);
}

/// @brief `CollisionEvent3D.ContactNormal(i)` — fresh `Vec3` for the contact normal.
void *rt_collision_event3d_get_contact_normal(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index != 0)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(event->normal[0], event->normal[1], event->normal[2]);
}

/// @brief `CollisionEvent3D.ContactSeparation(i)` — signed separation distance.
///
/// Negative means penetration; zero or positive means touching/just-separated.
double rt_collision_event3d_get_contact_separation(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index != 0)
        return 0.0;
    return event->separation;
}

/// @brief `ContactPoint3D.Point` — world-space contact location.
void *rt_contact_point3d_get_point(void *obj) {
    rt_contact_point3d_obj *contact = contact_point3d_checked(obj);
    if (!contact)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(contact->point[0], contact->point[1], contact->point[2]);
}

/// @brief `ContactPoint3D.Normal` — world-space contact normal (A→B).
void *rt_contact_point3d_get_normal(void *obj) {
    rt_contact_point3d_obj *contact = contact_point3d_checked(obj);
    if (!contact)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(contact->normal[0], contact->normal[1], contact->normal[2]);
}

/// @brief `ContactPoint3D.Separation` — signed gap (negative = penetrating).
double rt_contact_point3d_get_separation(void *obj) {
    rt_contact_point3d_obj *contact = contact_point3d_checked(obj);
    return contact ? contact->separation : 0.0;
}

// PhysicsHit3D field accessors — exposed as Zia properties on the
// boxed query result. All borrowed references / safe defaults on NULL.

/// @brief `PhysicsHit3D.Distance` — world-space distance from query origin.
double rt_physics_hit3d_get_distance(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->distance : 0.0;
}

/// @brief `PhysicsHit3D.Body` — the body that was hit.
void *rt_physics_hit3d_get_body(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->body : NULL;
}

/// @brief `PhysicsHit3D.Collider` — the leaf collider that was hit.
void *rt_physics_hit3d_get_collider(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->collider : NULL;
}

/// @brief `PhysicsHit3D.Point` — world-space hit location.
void *rt_physics_hit3d_get_point(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    if (!hit)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(hit->point[0], hit->point[1], hit->point[2]);
}

/// @brief `PhysicsHit3D.Normal` — world-space surface normal at the hit.
void *rt_physics_hit3d_get_normal(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    if (!hit)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(hit->normal[0], hit->normal[1], hit->normal[2]);
}

/// @brief `PhysicsHit3D.Fraction` — t along the swept path (0..1).
double rt_physics_hit3d_get_fraction(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->fraction : 0.0;
}

/// @brief `PhysicsHit3D.StartedPenetrating` — true if the query started inside the collider.
int8_t rt_physics_hit3d_get_started_penetrating(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->started_penetrating : 0;
}

/// @brief `PhysicsHit3D.IsTrigger` — true if the hit collider belongs to a trigger body.
int8_t rt_physics_hit3d_get_is_trigger(void *obj) {
    rt_physics_hit3d_obj *hit = physics_hit3d_checked(obj);
    return hit ? hit->is_trigger : 0;
}

/// @brief `PhysicsHitList3D.Count` — number of hits in the list.
int64_t rt_physics_hit_list3d_get_count(void *obj) {
    rt_physics_hit_list3d_obj *list = physics_hit_list3d_checked(obj);
    return list ? list->count : 0;
}

/// @brief `PhysicsHitList3D[i]` — borrowed `PhysicsHit3D` reference.
void *rt_physics_hit_list3d_get(void *obj, int64_t index) {
    rt_physics_hit_list3d_obj *list = physics_hit_list3d_checked(obj);
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
/// in radius/body-feature-relative increments looking for the first overlap,
/// and refines the impact `t` via bisection. There is deliberately no fixed
/// world-unit minimum step: tiny spheres must still detect thin geometry.
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
        step_dist = radius > 1e-6 ? radius * 0.25 : body_extent * 0.05;
        if (!isfinite(step_dist) || step_dist <= 0.0)
            step_dist = 1e-4;
        if (step_dist < 1e-5)
            step_dist = 1e-5;
        if (step_dist > 0.25)
            step_dist = 0.25;
    }

    steps = (int)ceil(delta_len / step_dist);
    if (steps < 1)
        steps = 1;
    if (steps > 8192)
        steps = 8192;

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
/// Approximates the capsule sweep as adaptive sphere-sweeps sampled
/// along the axis. Picks the closest hit.
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
    rt_query_hit3d best = {0};
    void *sphere_collider;
    if (!a || !b || !delta || !other || !other->collider)
        return 0;
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return 0;
    vec3_sub(b, a, axis);
    axis_len = vec3_len(axis);
    samples = capsule_axis_sample_count(axis_len, radius);
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
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    double center[3];
    rt_body3d query_body;
    void *sphere_collider;
    if (!w || !rt_g3d_is_vec3(center_obj) || !isfinite(radius) || radius < 0.0)
        return NULL;
    center[0] = rt_vec3_x(center_obj);
    center[1] = rt_vec3_y(center_obj);
    center[2] = rt_vec3_z(center_obj);
    if (!ph3d_vec3_all_finite(center))
        return NULL;
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
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    double mn[3], mx[3], center[3], half[3];
    rt_body3d query_body;
    void *box_collider;
    if (!w || !rt_g3d_is_vec3(min_obj) || !rt_g3d_is_vec3(max_obj))
        return NULL;
    mn[0] = rt_vec3_x(min_obj);
    mn[1] = rt_vec3_y(min_obj);
    mn[2] = rt_vec3_z(min_obj);
    mx[0] = rt_vec3_x(max_obj);
    mx[1] = rt_vec3_y(max_obj);
    mx[2] = rt_vec3_z(max_obj);
    if (!ph3d_vec3_all_finite(mn) || !ph3d_vec3_all_finite(mx))
        return NULL;
    center[0] = (mn[0] + mx[0]) * 0.5;
    center[1] = (mn[1] + mx[1]) * 0.5;
    center[2] = (mn[2] + mx[2]) * 0.5;
    half[0] = fabs(mx[0] - mn[0]) * 0.5;
    half[1] = fabs(mx[1] - mn[1]) * 0.5;
    half[2] = fabs(mx[2] - mn[2]) * 0.5;
    if (!ph3d_vec3_all_finite(center) || !ph3d_vec3_all_finite(half))
        return NULL;
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
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double center[3], delta[3];
    double max_distance;
    void *sphere_collider;
    if (!w || !rt_g3d_is_vec3(center_obj) || !rt_g3d_is_vec3(delta_obj) || !isfinite(radius) ||
        radius < 0.0)
        return NULL;
    center[0] = rt_vec3_x(center_obj);
    center[1] = rt_vec3_y(center_obj);
    center[2] = rt_vec3_z(center_obj);
    delta[0] = rt_vec3_x(delta_obj);
    delta[1] = rt_vec3_y(delta_obj);
    delta[2] = rt_vec3_z(delta_obj);
    if (!ph3d_vec3_all_finite(center) || !ph3d_vec3_all_finite(delta))
        return NULL;
    max_distance = vec3_len(delta);
    if (!isfinite(max_distance))
        return NULL;
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
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double a[3], b[3], delta[3];
    double max_distance;
    if (!w || !rt_g3d_is_vec3(a_obj) || !rt_g3d_is_vec3(b_obj) || !rt_g3d_is_vec3(delta_obj) ||
        !isfinite(radius) || radius < 0.0)
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
    if (!ph3d_vec3_all_finite(a) || !ph3d_vec3_all_finite(b) ||
        !ph3d_vec3_all_finite(delta))
        return NULL;
    max_distance = vec3_len(delta);
    if (!isfinite(max_distance))
        return NULL;
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

/// @brief Populate a ray-hit result record (body, distance, world point, and
///        normal) from a confirmed intersection at @p distance along the ray.
static void ray_fill_hit(rt_body3d *body,
                         double distance,
                         double max_distance,
                         const double *origin,
                         const double *dir,
                         const double *normal,
                         int started,
                         rt_query_hit3d *out_hit) {
    if (!out_hit)
        return;
    memset(out_hit, 0, sizeof(*out_hit));
    out_hit->body = body;
    out_hit->collider = body ? body->collider : NULL;
    out_hit->distance = distance;
    out_hit->fraction = max_distance > 1e-12 ? distance / max_distance : 0.0;
    out_hit->started_penetrating = (int8_t)(started ? 1 : 0);
    out_hit->is_trigger = body ? body->is_trigger : 0;
    out_hit->point[0] = origin[0] + dir[0] * distance;
    out_hit->point[1] = origin[1] + dir[1] * distance;
    out_hit->point[2] = origin[2] + dir[2] * distance;
    if (normal)
        vec3_copy(out_hit->normal, normal);
    else
        vec3_negate(dir, out_hit->normal);
}

/// @brief Ray vs sphere intersection (analytic quadratic solve).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_sphere_raw(const double *origin,
                              const double *dir,
                              const double *center,
                              double radius,
                              double max_distance,
                              double *out_t,
                              double *out_normal,
                              int *out_started) {
    double m[3];
    vec3_sub(origin, center, m);
    double c = vec3_dot(m, m) - radius * radius;
    if (c <= 0.0) {
        if (out_t)
            *out_t = 0.0;
        if (out_started)
            *out_started = 1;
        if (out_normal)
            vec3_negate(dir, out_normal);
        return 1;
    }
    double b = vec3_dot(m, dir);
    double disc = b * b - c;
    if (disc < 0.0)
        return 0;
    double t = -b - sqrt(disc);
    if (t < 0.0 || t > max_distance)
        return 0;
    if (out_t)
        *out_t = t;
    if (out_started)
        *out_started = 0;
    if (out_normal) {
        out_normal[0] = origin[0] + dir[0] * t - center[0];
        out_normal[1] = origin[1] + dir[1] * t - center[1];
        out_normal[2] = origin[2] + dir[2] * t - center[2];
        if (vec3_normalize_in_place(out_normal) <= 1e-12)
            vec3_negate(dir, out_normal);
    }
    return 1;
}

/// @brief Ray vs axis-aligned box intersection (slab method).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_aabb_raw(const double *origin,
                            const double *dir,
                            const double *mn,
                            const double *mx,
                            double max_distance,
                            double *out_t,
                            double *out_normal,
                            int *out_started) {
    double tmin = 0.0;
    double tmax = max_distance;
    double n[3] = {0.0, 0.0, 0.0};
    int inside = 1;
    for (int axis = 0; axis < 3; axis++) {
        if (origin[axis] < mn[axis] || origin[axis] > mx[axis])
            inside = 0;
        if (fabs(dir[axis]) < 1e-12) {
            if (origin[axis] < mn[axis] || origin[axis] > mx[axis])
                return 0;
            continue;
        }
        double inv = 1.0 / dir[axis];
        double t1 = (mn[axis] - origin[axis]) * inv;
        double t2 = (mx[axis] - origin[axis]) * inv;
        double sign = -1.0;
        if (t1 > t2) {
            double tmp = t1;
            t1 = t2;
            t2 = tmp;
            sign = 1.0;
        }
        if (t1 > tmin) {
            tmin = t1;
            vec3_set(n, 0.0, 0.0, 0.0);
            n[axis] = sign;
        }
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return 0;
    }
    if (inside) {
        if (out_t)
            *out_t = 0.0;
        if (out_started)
            *out_started = 1;
        if (out_normal)
            vec3_negate(dir, out_normal);
        return 1;
    }
    if (tmin < 0.0 || tmin > max_distance)
        return 0;
    if (out_t)
        *out_t = tmin;
    if (out_started)
        *out_started = 0;
    if (out_normal)
        vec3_copy(out_normal, n);
    return 1;
}

/// @brief Ray vs capsule intersection (cylinder body plus hemisphere caps).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_capsule_raw(const double *origin,
                               const double *dir,
                               const double *a,
                               const double *b,
                               double radius,
                               double max_distance,
                               double *out_t,
                               double *out_normal,
                               int *out_started) {
    double best_t = max_distance + 1.0;
    double best_n[3] = {0.0, 0.0, 0.0};
    int best_started = 0;
    int found = 0;
    double t, n[3];
    int started;
    if (raycast_sphere_raw(origin, dir, a, radius, max_distance, &t, n, &started)) {
        best_t = t;
        vec3_copy(best_n, n);
        best_started = started;
        found = 1;
    }
    if (raycast_sphere_raw(origin, dir, b, radius, max_distance, &t, n, &started) &&
        (!found || t < best_t)) {
        best_t = t;
        vec3_copy(best_n, n);
        best_started = started;
        found = 1;
    }
    double axis[3];
    vec3_sub(b, a, axis);
    double h = vec3_normalize_in_place(axis);
    if (h > 1e-12) {
        double m[3];
        vec3_sub(origin, a, m);
        double md = vec3_dot(m, axis);
        double nd = vec3_dot(dir, axis);
        double mp[3] = {m[0] - axis[0] * md, m[1] - axis[1] * md, m[2] - axis[2] * md};
        double dp[3] = {dir[0] - axis[0] * nd, dir[1] - axis[1] * nd, dir[2] - axis[2] * nd};
        double qa = vec3_dot(dp, dp);
        double qb = 2.0 * vec3_dot(mp, dp);
        double qc = vec3_dot(mp, mp) - radius * radius;
        if (qc <= 0.0 && md >= 0.0 && md <= h) {
            best_t = 0.0;
            vec3_negate(dir, best_n);
            best_started = 1;
            found = 1;
        } else if (qa > 1e-12) {
            double disc = qb * qb - 4.0 * qa * qc;
            if (disc >= 0.0) {
                double root = sqrt(disc);
                double roots[2] = {(-qb - root) / (2.0 * qa), (-qb + root) / (2.0 * qa)};
                for (int i = 0; i < 2; i++) {
                    double ct = roots[i];
                    double y = md + ct * nd;
                    if (ct < 0.0 || ct > max_distance || y < 0.0 || y > h)
                        continue;
                    if (!found || ct < best_t) {
                        double p[3] = {origin[0] + dir[0] * ct,
                                       origin[1] + dir[1] * ct,
                                       origin[2] + dir[2] * ct};
                        double c[3] = {a[0] + axis[0] * y, a[1] + axis[1] * y, a[2] + axis[2] * y};
                        best_t = ct;
                        best_n[0] = p[0] - c[0];
                        best_n[1] = p[1] - c[1];
                        best_n[2] = p[2] - c[2];
                        if (vec3_normalize_in_place(best_n) <= 1e-12)
                            vec3_negate(dir, best_n);
                        best_started = 0;
                        found = 1;
                    }
                }
            }
        }
    }
    if (!found)
        return 0;
    if (out_t)
        *out_t = best_t;
    if (out_normal)
        vec3_copy(out_normal, best_n);
    if (out_started)
        *out_started = best_started;
    return 1;
}

/// @brief Raycast against a posed box (optionally Minkowski-expanded by @p expand_radius
///   for swept-sphere tests): transform the ray into box-local space and slab-test the
///   AABB. Returns 1 on hit with distance @p out_t, world normal @p out_normal, and
///   @p out_started set when the ray began already inside the box.
static int raycast_box_pose_raw(void *box_collider,
                                const rt_collider_pose *pose,
                                const double *origin,
                                const double *dir,
                                double max_distance,
                                double expand_radius,
                                double *out_t,
                                double *out_normal,
                                int *out_started) {
    double he[3];
    double local_origin[3];
    double local_dir[3];
    double mn[3];
    double mx[3];
    double local_normal[3] = {0.0, 0.0, 0.0};
    double t = 0.0;
    int started = 0;
    if (!box_collider || !pose)
        return 0;
    rt_collider3d_get_box_half_extents_raw(box_collider, he);
    for (int i = 0; i < 3; i++) {
        double expansion = expand_radius / pose_abs_scale_or_unit(pose->scale[i]);
        mn[i] = -he[i] - expansion;
        mx[i] = he[i] + expansion;
    }
    transform_point_to_local(pose, origin, local_origin);
    transform_vector_to_local(pose, dir, local_dir);
    if (!raycast_aabb_raw(local_origin, local_dir, mn, mx, max_distance, &t, local_normal, &started))
        return 0;
    if (out_t)
        *out_t = t;
    if (out_started)
        *out_started = started;
    if (out_normal) {
        if (started)
            vec3_negate(dir, out_normal);
        else
            transform_normal_from_local(pose, local_normal, out_normal);
    }
    return 1;
}

/// @brief Möller-Trumbore ray/triangle intersection in world space. Returns 1 on a hit
///   within @p max_distance with distance @p out_t and the geometric face normal
///   @p out_normal, else 0 (including parallel rays and barycentric misses).
static int raycast_triangle_world(const double *origin,
                                  const double *dir,
                                  const double *a,
                                  const double *b,
                                  const double *c,
                                  double max_distance,
                                  double *out_t,
                                  double *out_normal) {
    double e1[3], e2[3], pvec[3], tvec[3], qvec[3];
    double det, inv_det, u, v, t;
    vec3_sub(b, a, e1);
    vec3_sub(c, a, e2);
    vec3_cross(dir, e2, pvec);
    det = vec3_dot(e1, pvec);
    if (fabs(det) < 1e-12)
        return 0;
    inv_det = 1.0 / det;
    vec3_sub(origin, a, tvec);
    u = vec3_dot(tvec, pvec) * inv_det;
    if (u < 0.0 || u > 1.0)
        return 0;
    vec3_cross(tvec, e1, qvec);
    v = vec3_dot(dir, qvec) * inv_det;
    if (v < 0.0 || u + v > 1.0)
        return 0;
    t = vec3_dot(e2, qvec) * inv_det;
    if (t < 0.0 || t > max_distance)
        return 0;
    if (out_t)
        *out_t = t;
    if (out_normal) {
        triangle_normal(a, b, c, out_normal);
        if (vec3_dot(out_normal, dir) > 0.0)
            vec3_negate(out_normal, out_normal);
    }
    return 1;
}

/// @brief Raycast a posed triangle mesh by transforming each triangle to world space and
///   keeping the nearest Möller-Trumbore hit within @p max_distance. Returns 1 on hit
///   with @p out_t/@p out_normal; meshes are surfaces so @p out_started is always 0.
static int raycast_meshlike_pose_raw(rt_mesh3d *mesh,
                                     const rt_collider_pose *pose,
                                     const double *origin,
                                     const double *dir,
                                     double max_distance,
                                     double *out_t,
                                     double *out_normal,
                                     int *out_started) {
    double best_t = max_distance + 1.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int found = 0;
    if (!mesh || !pose || mesh->index_count < 3)
        return 0;
    rt_mesh3d_refresh_bounds(mesh);
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], local[3], t, n[3];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        local[0] = mesh->vertices[i0].pos[0];
        local[1] = mesh->vertices[i0].pos[1];
        local[2] = mesh->vertices[i0].pos[2];
        transform_point_from_pose(pose, local, a);
        local[0] = mesh->vertices[i1].pos[0];
        local[1] = mesh->vertices[i1].pos[1];
        local[2] = mesh->vertices[i1].pos[2];
        transform_point_from_pose(pose, local, b);
        local[0] = mesh->vertices[i2].pos[0];
        local[1] = mesh->vertices[i2].pos[1];
        local[2] = mesh->vertices[i2].pos[2];
        transform_point_from_pose(pose, local, c);
        if (raycast_triangle_world(origin, dir, a, b, c, max_distance, &t, n) && t < best_t) {
            best_t = t;
            vec3_copy(best_normal, n);
            found = 1;
        }
    }
    if (!found)
        return 0;
    if (out_t)
        *out_t = best_t;
    if (out_normal)
        vec3_copy(out_normal, best_normal);
    if (out_started)
        *out_started = 0;
    return 1;
}

/// @brief Raycast a posed heightfield collider: clip the ray to the field AABB, then
///   march in local space sampling terrain height until the ray dips below the surface.
///   Returns 1 on hit with @p out_t/@p out_normal (sampled surface normal), else 0.
static int raycast_heightfield_pose_raw(void *heightfield,
                                        const rt_collider_pose *pose,
                                        const double *origin,
                                        const double *dir,
                                        double max_distance,
                                        double *out_t,
                                        double *out_normal,
                                        int *out_started) {
    double mn[3], mx[3], entry_t, aabb_normal[3];
    int started = 0;
    double local_origin[3], local_dir[3];
    double start_t;
    double step;
    double prev_t;
    double prev_clearance = DBL_MAX;
    int has_prev = 0;
    rt_collider3d_compute_world_aabb_raw(heightfield, pose->position, pose->rotation, pose->scale, mn, mx);
    if (!raycast_aabb_raw(origin, dir, mn, mx, max_distance, &entry_t, aabb_normal, &started))
        return 0;
    transform_point_to_local(pose, origin, local_origin);
    transform_vector_to_local(pose, dir, local_dir);
    start_t = started ? 0.0 : entry_t;
    step = max_distance / 512.0;
    if (!isfinite(step) || step < 1e-4)
        step = 1e-4;
    if (step > 0.25)
        step = 0.25;
    prev_t = start_t;
    for (double t = start_t; t <= max_distance + 1e-9; t += step) {
        double local_point[3] = {local_origin[0] + local_dir[0] * t,
                                 local_origin[1] + local_dir[1] * t,
                                 local_origin[2] + local_dir[2] * t};
        double surface = 0.0;
        double local_normal[3] = {0.0, 1.0, 0.0};
        double clearance;
        if (!rt_collider3d_sample_heightfield_raw(
                heightfield, local_point[0], local_point[2], &surface, local_normal)) {
            prev_t = t;
            has_prev = 0;
            continue;
        }
        clearance = local_point[1] - surface;
        if (clearance <= 0.0) {
            double hit_t = t;
            if (has_prev && prev_clearance > 0.0) {
                double lo = prev_t;
                double hi = t;
                for (int iter = 0; iter < 16; iter++) {
                    double mid = (lo + hi) * 0.5;
                    double mid_point[3] = {local_origin[0] + local_dir[0] * mid,
                                           local_origin[1] + local_dir[1] * mid,
                                           local_origin[2] + local_dir[2] * mid};
                    double mid_surface = 0.0;
                    double mid_normal[3] = {0.0, 1.0, 0.0};
                    if (rt_collider3d_sample_heightfield_raw(
                            heightfield, mid_point[0], mid_point[2], &mid_surface, mid_normal) &&
                        mid_point[1] - mid_surface <= 0.0) {
                        hi = mid;
                        vec3_copy(local_normal, mid_normal);
                    } else {
                        lo = mid;
                    }
                }
                hit_t = hi;
            }
            if (out_t)
                *out_t = hit_t;
            if (out_started)
                *out_started = (t == start_t && clearance <= 0.0) ? 1 : 0;
            if (out_normal)
                transform_normal_from_local(pose, local_normal, out_normal);
            return 1;
        }
        prev_clearance = clearance;
        prev_t = t;
        has_prev = 1;
    }
    return 0;
}

/// @brief Top-level raycast dispatch for any posed collider: routes by collider type to
///   the box/sphere/capsule/mesh/heightfield/compound raycast (recursing into compound
///   children and returning the hit leaf via @p out_leaf). Returns 1 on the nearest hit
///   within @p max_distance with @p out_t/@p out_normal/@p out_started filled.
static int raycast_collider_pose(void *collider,
                                 const rt_collider_pose *pose,
                                 const double *origin,
                                 const double *dir,
                                 double max_distance,
                                 double *out_t,
                                 double *out_normal,
                                 int *out_started,
                                 void **out_leaf) {
    int64_t type;
    double t = 0.0;
    double normal[3] = {0.0, 1.0, 0.0};
    int started = 0;
    if (!collider || !pose)
        return 0;
    type = rt_collider3d_get_type(collider);
    if (type == RT_COLLIDER3D_TYPE_BOX) {
        if (!raycast_box_pose_raw(collider, pose, origin, dir, max_distance, 0.0, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_SPHERE) {
        double radius = rt_collider3d_get_radius_raw(collider);
        double sx = pose_abs_scale_or_unit(pose->scale[0]);
        double sy = pose_abs_scale_or_unit(pose->scale[1]);
        double sz = pose_abs_scale_or_unit(pose->scale[2]);
        double max_scale = sx > sy ? sx : sy;
        if (sz > max_scale)
            max_scale = sz;
        if (!raycast_sphere_raw(origin, dir, pose->position, radius * max_scale, max_distance, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_CAPSULE) {
        rt_body3d proxy;
        double a[3], b[3];
        if (!build_simple_proxy(pose, collider, &proxy))
            return 0;
        capsule_axis_endpoints(&proxy, a, b);
        if (!raycast_capsule_raw(origin, dir, a, b, proxy.radius, max_distance, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_COMPOUND) {
        int64_t child_count = rt_collider3d_get_child_count_raw(collider);
        int found = 0;
        double best_t = max_distance + 1.0;
        double best_normal[3] = {0.0, 1.0, 0.0};
        int best_started = 0;
        void *best_leaf = NULL;
        for (int64_t i = 0; i < child_count; i++) {
            void *child = rt_collider3d_get_child_raw(collider, i);
            double child_pos[3], child_rot[4], child_scale[3];
            rt_collider_pose child_pose;
            double cur_t, cur_normal[3];
            int cur_started;
            void *cur_leaf = NULL;
            rt_collider3d_get_child_transform_raw(collider, i, child_pos, child_rot, child_scale);
            collider_pose_compose(pose, child_pos, child_rot, child_scale, &child_pose);
            if (raycast_collider_pose(child,
                                      &child_pose,
                                      origin,
                                      dir,
                                      max_distance,
                                      &cur_t,
                                      cur_normal,
                                      &cur_started,
                                      &cur_leaf) &&
                cur_t < best_t) {
                best_t = cur_t;
                vec3_copy(best_normal, cur_normal);
                best_started = cur_started;
                best_leaf = cur_leaf ? cur_leaf : child;
                found = 1;
            }
        }
        if (!found)
            return 0;
        t = best_t;
        vec3_copy(normal, best_normal);
        started = best_started;
        collider = best_leaf ? best_leaf : collider;
    } else if (type == RT_COLLIDER3D_TYPE_CONVEX_HULL || type == RT_COLLIDER3D_TYPE_MESH) {
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(collider);
        if (!raycast_meshlike_pose_raw(mesh, pose, origin, dir, max_distance, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_HEIGHTFIELD) {
        if (!raycast_heightfield_pose_raw(collider, pose, origin, dir, max_distance, &t, normal, &started))
            return 0;
    } else {
        double mn[3], mx[3];
        rt_collider3d_compute_world_aabb_raw(collider, pose->position, pose->rotation, pose->scale, mn, mx);
        if (!raycast_aabb_raw(origin, dir, mn, mx, max_distance, &t, normal, &started))
            return 0;
    }
    if (out_t)
        *out_t = t;
    if (out_normal)
        vec3_copy(out_normal, normal);
    if (out_started)
        *out_started = started;
    if (out_leaf)
        *out_leaf = collider;
    return 1;
}

/// @brief Ray vs a physics body: dispatches to the actual attached collider
///        shape, recursing through compound children and testing mesh triangles.
/// @return Non-zero on a hit (nearest distance written), 0 otherwise.
static int raycast_body(rt_body3d *body,
                        const double *origin,
                        const double *dir,
                        double max_distance,
                        rt_query_hit3d *out_hit) {
    double t = 0.0;
    double normal[3] = {0.0, 0.0, 0.0};
    int started = 0;
    void *leaf = NULL;
    if (!body || !body->collider)
        return 0;
    {
        rt_collider_pose pose;
        collider_pose_from_body(body, &pose);
        if (!raycast_collider_pose(
                body->collider, &pose, origin, dir, max_distance, &t, normal, &started, &leaf))
            return 0;
    }
    ray_fill_hit(body, t, max_distance, origin, dir, normal, started, out_hit);
    if (out_hit && leaf)
        out_hit->collider = leaf;
    return 1;
}

/// @brief `World3D.Raycast(origin, direction, maxDistance, mask)` — first hit along a ray.
///
/// Uses collider-specific tests for boxes, spheres, capsules, meshes, hulls,
/// compounds, and heightfields. Returns NULL when the direction is zero or
/// `maxDistance <= 0`.
void *rt_world3d_raycast(void *obj, void *origin_obj, void *direction_obj, double max_distance, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double origin[3];
    double dir[3];
    if (!w || !rt_g3d_is_vec3(origin_obj) || !rt_g3d_is_vec3(direction_obj) || !isfinite(max_distance) ||
        max_distance <= 0.0)
        return NULL;
    origin[0] = rt_vec3_x(origin_obj);
    origin[1] = rt_vec3_y(origin_obj);
    origin[2] = rt_vec3_z(origin_obj);
    if (!ph3d_vec3_all_finite(origin))
        return NULL;
    dir[0] = rt_vec3_x(direction_obj);
    dir[1] = rt_vec3_y(direction_obj);
    dir[2] = rt_vec3_z(direction_obj);
    if (!isfinite(dir[0]) || !isfinite(dir[1]) || !isfinite(dir[2]))
        return NULL;
    if (vec3_normalize_in_place(dir) <= 1e-12)
        return NULL;
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (!raycast_body(body, origin, dir, max_distance, &hit))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    return found ? physics_hit3d_new(&best_hit) : NULL;
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
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    double origin[3], dir[3];
    int32_t hit_count = 0;
    if (!w || !rt_g3d_is_vec3(origin_obj) || !rt_g3d_is_vec3(direction_obj) || !isfinite(max_distance) ||
        max_distance <= 0.0)
        return NULL;
    origin[0] = rt_vec3_x(origin_obj);
    origin[1] = rt_vec3_y(origin_obj);
    origin[2] = rt_vec3_z(origin_obj);
    if (!ph3d_vec3_all_finite(origin))
        return NULL;
    dir[0] = rt_vec3_x(direction_obj);
    dir[1] = rt_vec3_y(direction_obj);
    dir[2] = rt_vec3_z(direction_obj);
    if (!isfinite(dir[0]) || !isfinite(dir[1]) || !isfinite(dir[2]))
        return NULL;
    if (vec3_normalize_in_place(dir) <= 1e-12)
        return NULL;
    for (int32_t i = 0; i < w->body_count && hit_count < PH3D_MAX_QUERY_HITS; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (raycast_body(body, origin, dir, max_distance, &hit))
            hit_count = query_hit_insert_sorted(hits, hit_count, &hit);
    }
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
    if (collider && !rt_g3d_has_class(collider, RT_G3D_COLLIDER3D_CLASS_ID)) {
        rt_trap("Physics3DBody.SetCollider: collider must be a Collider3D");
        return 0;
    }
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
    rt_body3d *b = (rt_body3d *)rt_obj_new_i64(RT_G3D_BODY3D_CLASS_ID, (int64_t)sizeof(rt_body3d));
    if (!b) {
        rt_trap("Physics3D.Body.New: allocation failed");
        return NULL;
    }
    memset(b, 0, sizeof(rt_body3d));
    b->mass = ph3d_clamp_nonnegative_finite(mass, 0.0);
    b->restitution = 0.3;
    b->friction = 0.5;
    b->collision_layer = 1;
    b->collision_mask = ~(int64_t)0;
    b->motion_mode = (b->mass <= 1e-12) ? PH3D_MODE_STATIC : PH3D_MODE_DYNAMIC;
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
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return;
    body3d_assign_collider(
        b, collider, "Physics3DBody.SetCollider: collider requires a static body");
}

/// @brief `Body3D.GetCollider` — borrowed reference to the body's collider.
void *rt_body3d_get_collider(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->collider : NULL;
}

/// @brief `Body3D.SetPosition(x, y, z)` — teleport (also wakes if dynamic).
///
/// Bypasses collision response, so use sparingly during simulation
/// (per-step adjustments are usually better via velocity).
void rt_body3d_set_position(void *o, double x, double y, double z) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        ph3d_vec3_set_finite(b->position, x, y, z);
        body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetPosition` — fresh `Vec3` of the body's world position.
void *rt_body3d_get_position(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(b->position[0], b->position[1], b->position[2]);
}

/// @brief `Body3D.SetOrientation(quat)` — set the body's orientation quaternion.
///
/// NULL `quat` resets to identity. Always normalizes to defend against
/// caller-passed unnormalized quaternions.
void rt_body3d_set_orientation(void *o, void *quat) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return;
    if (!quat) {
        quat_identity(b->orientation);
    } else if (!rt_g3d_is_quat(quat)) {
        return;
    } else {
        b->orientation[0] = rt_quat_x(quat);
        b->orientation[1] = rt_quat_y(quat);
        b->orientation[2] = rt_quat_z(quat);
        b->orientation[3] = rt_quat_w(quat);
        quat_normalize(b->orientation);
    }
    body3d_wake_if_dynamic(b);
}

/// @brief `Body3D.GetOrientation` — fresh `Quat` of the body's orientation.
void *rt_body3d_get_orientation(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    return rt_quat_new(
        b->orientation[0], b->orientation[1], b->orientation[2], b->orientation[3]);
}

/// @brief `Body3D.SetVelocity(x, y, z)` — set linear velocity (m/s).
///
/// Wakes the body if dynamic and the new velocity is non-zero.
void rt_body3d_set_velocity(void *o, double x, double y, double z) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        ph3d_vec3_set_finite(b->velocity, x, y, z);
        if (vec3_len_sq(b->velocity) > 1e-12)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetVelocity` — fresh `Vec3` of linear velocity.
void *rt_body3d_get_velocity(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(b->velocity[0], b->velocity[1], b->velocity[2]);
}

// Body3D physical-state accessors — set/get pairs for velocity,
// angular velocity, force, and torque. Setters wake the body if
// dynamic; getters return safe defaults on NULL.

/// @brief `Body3D.SetAngularVelocity(x, y, z)` — set angular velocity (radians/sec).
void rt_body3d_set_angular_velocity(void *o, double x, double y, double z) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        ph3d_vec3_set_finite(b->angular_velocity, x, y, z);
        if (vec3_len_sq(b->angular_velocity) > 1e-12)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetAngularVelocity` — fresh `Vec3` of angular velocity.
void *rt_body3d_get_angular_velocity(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(
        b->angular_velocity[0], b->angular_velocity[1], b->angular_velocity[2]);
}

/// @brief `Body3D.ApplyForce(fx, fy, fz)` — accumulate a continuous force (N).
///
/// The force is consumed by the next `World3D.Step` and zeroed at end
/// of step. Apply once per frame for sustained force, every frame for
/// sustained acceleration. No-op for non-dynamic bodies.
void rt_body3d_apply_force(void *o, double fx, double fy, double fz) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        fx = ph3d_finite_or(fx, 0.0);
        fy = ph3d_finite_or(fy, 0.0);
        fz = ph3d_finite_or(fz, 0.0);
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
    rt_body3d *b = body3d_checked(o);
    if (b) {
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        ix = ph3d_finite_or(ix, 0.0);
        iy = ph3d_finite_or(iy, 0.0);
        iz = ph3d_finite_or(iz, 0.0);
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
    rt_body3d *b = body3d_checked(o);
    if (b) {
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        tx = ph3d_finite_or(tx, 0.0);
        ty = ph3d_finite_or(ty, 0.0);
        tz = ph3d_finite_or(tz, 0.0);
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
    rt_body3d *b = body3d_checked(o);
    if (b) {
        if (b->motion_mode != PH3D_MODE_DYNAMIC)
            return;
        ix = ph3d_finite_or(ix, 0.0);
        iy = ph3d_finite_or(iy, 0.0);
        iz = ph3d_finite_or(iz, 0.0);
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
    rt_body3d *b = body3d_checked(o);
    if (b) {
        double value = ph3d_clamp_nonnegative_finite(r, 0.0);
        b->restitution = value > 1.0 ? 1.0 : value;
    }
}

/// @brief `Body3D.GetRestitution` — read bounciness.
double rt_body3d_get_restitution(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->restitution : 0;
}

/// @brief `Body3D.SetFriction(f)` — set friction coefficient.
///
/// Pair friction is `sqrt(a.friction * b.friction)` — geometric mean
/// matches typical material-interaction tables.
void rt_body3d_set_friction(void *o, double f) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->friction = ph3d_clamp_nonnegative_finite(f, 0.0);
}

/// @brief `Body3D.GetFriction` — read friction coefficient.
double rt_body3d_get_friction(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->friction : 0;
}

/// @brief `Body3D.SetLinearDamping(d)` — per-second linear velocity decay.
///
/// Each step scales velocity by `max(0, 1 - d*dt)`. Useful as a soft
/// air-resistance proxy. Negative values clamp to 0.
void rt_body3d_set_linear_damping(void *o, double d) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->linear_damping = ph3d_clamp_nonnegative_finite(d, 0.0);
}

/// @brief `Body3D.GetLinearDamping` — read linear damping coefficient.
double rt_body3d_get_linear_damping(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->linear_damping : 0.0;
}

/// @brief `Body3D.SetAngularDamping(d)` — per-second angular velocity decay.
void rt_body3d_set_angular_damping(void *o, double d) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->angular_damping = ph3d_clamp_nonnegative_finite(d, 0.0);
}

/// @brief `Body3D.GetAngularDamping` — read angular damping coefficient.
double rt_body3d_get_angular_damping(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->angular_damping : 0.0;
}

/// @brief `Body3D.SetCollisionLayer(l)` — bitmask labeling this body's category.
void rt_body3d_set_collision_layer(void *o, int64_t l) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->collision_layer = l;
}

/// @brief `Body3D.GetCollisionLayer` — read this body's layer bitmask.
int64_t rt_body3d_get_collision_layer(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->collision_layer : 0;
}

/// @brief `Body3D.SetCollisionMask(m)` — bitmask of layers this body collides with.
void rt_body3d_set_collision_mask(void *o, int64_t m) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->collision_mask = m;
}

/// @brief `Body3D.GetCollisionMask` — read this body's collision mask.
int64_t rt_body3d_get_collision_mask(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->collision_mask : 0;
}

/// @brief `Body3D.SetStatic(s)` — toggle static motion mode.
///
/// Traps if the body's collider is incompatible with the requested
/// mode (e.g., switching a triangle-mesh-collider body to dynamic).
/// Refreshes derived state (inv_mass, inv_inertia) on success.
void rt_body3d_set_static(void *o, int8_t s) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
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
    rt_body3d *b = body3d_checked(o);
    return b ? b->is_static : 0;
}

/// @brief `Body3D.SetKinematic(k)` — toggle kinematic motion mode.
///
/// Kinematic bodies move under script control (position/velocity set
/// directly) but are treated as infinitely massive by the impulse
/// solver — they push dynamic bodies but aren't pushed in return.
void rt_body3d_set_kinematic(void *o, int8_t k) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
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
    rt_body3d *b = body3d_checked(o);
    return b ? b->is_kinematic : 0;
}

/// @brief `Body3D.SetTrigger(t)` — toggle "report contacts but don't physically respond".
///
/// Trigger bodies fire collision events but don't apply impulses or
/// positional correction — useful for damage volumes, pickup zones, etc.
void rt_body3d_set_trigger(void *o, int8_t t) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->is_trigger = t;
}

/// @brief `Body3D.IsTrigger` — true if the body is in trigger-only mode.
int8_t rt_body3d_is_trigger(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->is_trigger : 0;
}

/// @brief `Body3D.SetCanSleep(canSleep)` — toggle automatic sleep eligibility.
///
/// When false, the body never enters sleep mode (always integrated).
/// Disabling sleep on a quiet body wakes it immediately so the next
/// step processes it normally.
void rt_body3d_set_can_sleep(void *o, int8_t can_sleep) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        b->can_sleep = can_sleep ? 1 : 0;
        if (!b->can_sleep) {
            b->is_sleeping = 0;
            b->sleep_time = 0.0;
        }
    }
}

/// @brief `Body3D.CanSleep` — read sleep eligibility flag.
int8_t rt_body3d_can_sleep(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->can_sleep : 0;
}

/// @brief `Body3D.IsSleeping` — true when the body is currently quiescent.
int8_t rt_body3d_is_sleeping(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->is_sleeping : 0;
}

/// @brief `Body3D.Wake` — force the body awake (no-op for non-dynamic).
void rt_body3d_wake(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        body3d_wake_if_dynamic(b);
}

/// @brief `Body3D.Sleep` — manually put the body to sleep.
///
/// Zeros velocity / angular velocity / accumulated forces. No-op
/// when the body can't sleep or isn't dynamic. Bypasses the normal
/// sleep-delay countdown.
void rt_body3d_sleep(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return;
    if (b->motion_mode != PH3D_MODE_DYNAMIC || !b->can_sleep)
        return;
    b->is_sleeping = 1;
    b->sleep_time = PH3D_SLEEP_DELAY;
    vec3_set(b->velocity, 0.0, 0.0, 0.0);
    vec3_set(b->angular_velocity, 0.0, 0.0, 0.0);
    vec3_set(b->force, 0.0, 0.0, 0.0);
    vec3_set(b->torque, 0.0, 0.0, 0.0);
}

/// @brief `Body3D.SetUseCcd(useCcd)` — opt this body into CCD substeps.
///
/// CCD subdivides high-speed steps to prevent tunneling through thin
/// geometry. Enable for fast-moving bodies (bullets, balls); leave off
/// otherwise to save CPU.
void rt_body3d_set_use_ccd(void *o, int8_t use_ccd) {
    rt_body3d *b = body3d_checked(o);
    if (b)
        b->use_ccd = use_ccd ? 1 : 0;
}

/// @brief `Body3D.GetUseCcd` — read CCD opt-in flag.
int8_t rt_body3d_get_use_ccd(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->use_ccd : 0;
}

/// @brief `Body3D.IsGrounded` — true when the body has an upward contact this frame.
///
/// "Upward" = contact normal Y > 0.7 (~45° max slope). Set/cleared by
/// `World3D.Step`; queryable from frame to frame.
int8_t rt_body3d_is_grounded(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->is_grounded : 0;
}

/// @brief `Body3D.GroundNormal` — fresh `Vec3` of the latest ground normal.
///
/// Defaults to +Y when not grounded.
void *rt_body3d_get_ground_normal(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(0, 1, 0);
    return rt_vec3_new(b->ground_normal[0], b->ground_normal[1], b->ground_normal[2]);
}

/// @brief `Body3D.Mass` — read the body's mass (zero for static).
double rt_body3d_get_mass(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->mass : 0;
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
    rt_character3d *c = (rt_character3d *)rt_obj_new_i64(RT_G3D_CHARACTER3D_CLASS_ID, (int64_t)sizeof(rt_character3d));
    if (!c) {
        rt_trap("Physics3D.Character.New: allocation failed");
        return NULL;
    }
    c->vptr = NULL;
    c->body = (rt_body3d *)rt_body3d_new_capsule(radius, height, mass);
    if (!c->body) {
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        return NULL;
    }
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
    rt_character3d *ctrl = character3d_checked(obj);
    if (!ctrl || !rt_g3d_is_vec3(velocity_vec) || !isfinite(dt) || dt <= 0)
        return;
    rt_body3d *body = ctrl->body;
    if (!body)
        return;

    double vx = ph3d_finite_or(rt_vec3_x(velocity_vec), 0.0);
    double vy = ph3d_finite_or(rt_vec3_y(velocity_vec), 0.0);
    double vz = ph3d_finite_or(rt_vec3_z(velocity_vec), 0.0);

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
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->step_height = ph3d_clamp_nonnegative_finite(h, 0.0);
}

/// @brief `Character3D.GetStepHeight` — read the configured step height.
double rt_character3d_get_step_height(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->step_height : 0.3;
}

/// @brief `Character3D.SetSlopeLimit(degrees)` — max walkable slope angle.
///
/// Stored as `cos(angle)` to make the per-step "is this surface walkable"
/// test a single comparison (no trig in the hot path).
void rt_character3d_set_slope_limit(void *o, double degrees) {
    rt_character3d *c = character3d_checked(o);
    if (c) {
        degrees = ph3d_finite_or(degrees, 45.0);
        degrees = clampd(degrees, 0.0, 89.9);
        c->slope_limit_cos = cos(degrees * 3.14159265358979323846 / 180.0);
    }
}

/// @brief `Character3D.SetWorld(world)` — bind the character to a physics world.
///
/// Required before `Move` will collide against anything. Releases any
/// previous world reference and retains the new one. NULL detaches.
void rt_character3d_set_world(void *o, void *world) {
    rt_character3d *ctrl = character3d_checked(o);
    rt_world3d *w = world3d_checked(world);
    if (!ctrl)
        return;
    if (world && !w)
        return;
    if (ctrl->world == w)
        return;
    if (w)
        rt_obj_retain_maybe(w);
    if (ctrl->world && rt_obj_release_check0(ctrl->world))
        rt_obj_free(ctrl->world);
    ctrl->world = w;
}

/// @brief `Character3D.GetWorld` — borrowed reference to the bound world.
void *rt_character3d_get_world(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->world : NULL;
}

/// @brief `Character3D.IsGrounded` — true when standing on a walkable surface.
int8_t rt_character3d_is_grounded(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->is_grounded : 0;
}

/// @brief `Character3D.JustLanded` — edge-detect: true on the first frame after landing.
///
/// Compares this frame's grounded state to the previous frame's. Useful
/// for landing animations, fall-damage triggers, dust puffs, etc.
int8_t rt_character3d_just_landed(void *o) {
    rt_character3d *c = character3d_checked(o);
    if (!c)
        return 0;
    return c->is_grounded && !c->was_grounded;
}

/// @brief `Character3D.GetPosition` — fresh `Vec3` of the body's position.
void *rt_character3d_get_position(void *o) {
    rt_character3d *c = character3d_checked(o);
    if (!c)
        return rt_vec3_new(0, 0, 0);
    return rt_body3d_get_position(c->body);
}

/// @brief `Character3D.SetPosition(x, y, z)` — teleport the controller.
///
/// Direct delegation to the underlying body. Caller is responsible for
/// avoiding teleports into geometry.
void rt_character3d_set_position(void *o, double x, double y, double z) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        rt_body3d_set_position(c->body, x, y, z);
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

/// @brief Validate @p obj as a Trigger3D handle and return its typed pointer (NULL on mismatch).
static rt_trigger3d *trigger3d_checked(void *obj) {
    return (rt_trigger3d *)rt_g3d_checked_or_null(obj, RT_G3D_TRIGGER3D_CLASS_ID);
}

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
    rt_trigger3d *t = (rt_trigger3d *)rt_obj_new_i64(RT_G3D_TRIGGER3D_CLASS_ID, (int64_t)sizeof(rt_trigger3d));
    if (!t) {
        rt_trap("Trigger3D.New: allocation failed");
        return NULL;
    }
    memset(t, 0, sizeof(rt_trigger3d));
    x0 = ph3d_finite_or(x0, 0.0);
    y0 = ph3d_finite_or(y0, 0.0);
    z0 = ph3d_finite_or(z0, 0.0);
    x1 = ph3d_finite_or(x1, 0.0);
    y1 = ph3d_finite_or(y1, 0.0);
    z1 = ph3d_finite_or(z1, 0.0);
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
    rt_trigger3d *t = trigger3d_checked(obj);
    if (!t || !rt_g3d_is_vec3(point))
        return 0;
    double px = rt_vec3_x(point), py = rt_vec3_y(point), pz = rt_vec3_z(point);
    if (!isfinite(px) || !isfinite(py) || !isfinite(pz))
        return 0;
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
    rt_trigger3d *t = trigger3d_checked(obj);
    rt_world3d *w = world3d_checked(world_obj);
    int8_t seen[TRG3D_MAX_TRACKED];
    if (!t || !w)
        return;
    memset(seen, 0, sizeof(seen));

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

        seen[idx] = 1;
        t->is_inside[idx] = inside;
        if (inside && !t->was_inside[idx])
            t->enter_count++;
        if (!inside && t->was_inside[idx])
            t->exit_count++;
    }

    /* Weakly tracked bodies absent from the world left the trigger/world.
     * Emit exit once for bodies that were previously inside, then forget them. */
    for (int32_t i = 0; i < t->tracked_count;) {
        if (seen[i]) {
            i++;
            continue;
        }
        if (t->was_inside[i])
            t->exit_count++;
        for (int32_t j = i; j < t->tracked_count - 1; j++) {
            t->tracked_bodies[j] = t->tracked_bodies[j + 1];
            t->was_inside[j] = t->was_inside[j + 1];
            t->is_inside[j] = t->is_inside[j + 1];
            seen[j] = seen[j + 1];
        }
        t->tracked_count--;
        t->tracked_bodies[t->tracked_count] = NULL;
        t->was_inside[t->tracked_count] = 0;
        t->is_inside[t->tracked_count] = 0;
        seen[t->tracked_count] = 0;
    }
}

/// @brief `Trigger3D.EnterCount` — bodies that entered this trigger this frame.
int64_t rt_trigger3d_get_enter_count(void *obj) {
    rt_trigger3d *t = trigger3d_checked(obj);
    return t ? t->enter_count : 0;
}

/// @brief `Trigger3D.ExitCount` — bodies that left this trigger this frame.
int64_t rt_trigger3d_get_exit_count(void *obj) {
    rt_trigger3d *t = trigger3d_checked(obj);
    return t ? t->exit_count : 0;
}

/// @brief `Trigger3D.SetBounds(x0..z1)` — replace the trigger's AABB.
///
/// Auto-orders the corners. Tracked-body state is preserved across the
/// resize, so a body that was inside the old box and is also inside
/// the new box remains "in" without firing an enter event.
void rt_trigger3d_set_bounds(
    void *obj, double x0, double y0, double z0, double x1, double y1, double z1) {
    rt_trigger3d *t = trigger3d_checked(obj);
    if (!t)
        return;
    x0 = ph3d_finite_or(x0, 0.0);
    y0 = ph3d_finite_or(y0, 0.0);
    z0 = ph3d_finite_or(z0, 0.0);
    x1 = ph3d_finite_or(x1, 0.0);
    y1 = ph3d_finite_or(y1, 0.0);
    z1 = ph3d_finite_or(z1, 0.0);
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
