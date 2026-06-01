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

#include "rt_physics3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_joints3d.h"
#include "rt_raycast3d.h"

#include "rt_physics3d_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
#include <float.h>
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);

int ph3d_i32_stack_push(int32_t **items, int32_t *count, int32_t *capacity, int32_t value);


/* The joint solver (rt_joints3d.c) reaches into a body's pose/velocity through
 * the shared rt_body3d_kinematics view declared in rt_physics3d.h. These asserts
 * pin the contract: if any shared field is reordered or retyped in rt_body3d, the
 * build fails here instead of silently corrupting joint solving at runtime. */
_Static_assert(offsetof(rt_body3d, vptr) == offsetof(rt_body3d_kinematics, vptr),
               "rt_body3d_kinematics.vptr offset drift");
_Static_assert(offsetof(rt_body3d, position) == offsetof(rt_body3d_kinematics, position),
               "rt_body3d_kinematics.position offset drift");
_Static_assert(offsetof(rt_body3d, orientation) == offsetof(rt_body3d_kinematics, orientation),
               "rt_body3d_kinematics.orientation offset drift");
_Static_assert(offsetof(rt_body3d, scale) == offsetof(rt_body3d_kinematics, scale),
               "rt_body3d_kinematics.scale offset drift");
_Static_assert(offsetof(rt_body3d, velocity) == offsetof(rt_body3d_kinematics, velocity),
               "rt_body3d_kinematics.velocity offset drift");
_Static_assert(offsetof(rt_body3d, angular_velocity) ==
                   offsetof(rt_body3d_kinematics, angular_velocity),
               "rt_body3d_kinematics.angular_velocity offset drift");
_Static_assert(offsetof(rt_body3d, force) == offsetof(rt_body3d_kinematics, force),
               "rt_body3d_kinematics.force offset drift");
_Static_assert(offsetof(rt_body3d, torque) == offsetof(rt_body3d_kinematics, torque),
               "rt_body3d_kinematics.torque offset drift");
_Static_assert(offsetof(rt_body3d, mass) == offsetof(rt_body3d_kinematics, mass),
               "rt_body3d_kinematics.mass offset drift");
_Static_assert(offsetof(rt_body3d, inv_mass) == offsetof(rt_body3d_kinematics, inv_mass),
               "rt_body3d_kinematics.inv_mass offset drift");

/// @brief Validate @p obj as a World3D handle and return its typed pointer (NULL on mismatch).
rt_world3d *world3d_checked(void *obj) {
    return (rt_world3d *)rt_g3d_checked_or_null(obj, RT_G3D_WORLD3D_CLASS_ID);
}

/// @brief Validate @p obj as a Body3D handle and return its typed pointer (NULL on mismatch).
static rt_body3d *body3d_checked(void *obj) {
    return (rt_body3d *)rt_g3d_checked_or_null(obj, RT_G3D_BODY3D_CLASS_ID);
}

/// @brief Bump a body's broadphase revision (wrapping past UINT64_MAX to 1) so cached broadphase
///        structures know to re-evaluate it.
void body3d_touch_broadphase(rt_body3d *body) {
    if (!body)
        return;
    body->broadphase_revision =
        body->broadphase_revision == UINT64_MAX ? 1u : body->broadphase_revision + 1u;
}

/// @brief Invalidate the world's cached query broadphase so the next spatial query rebuilds it.
static void world3d_invalidate_query_broadphase(rt_world3d *world) {
    if (!world)
        return;
    world->query_broadphase_valid = 0;
    world->query_broadphase_count = 0;
    world->query_broadphase_signature = 0;
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
    if (joint_type == RT_JOINT_HINGE)
        return rt_g3d_has_class(joint, RT_G3D_HINGEJOINT3D_CLASS_ID);
    if (joint_type == RT_JOINT_ROPE)
        return rt_g3d_has_class(joint, RT_G3D_ROPEJOINT3D_CLASS_ID);
    if (joint_type == RT_JOINT_SIXDOF)
        return rt_g3d_has_class(joint, RT_G3D_SIXDOFJOINT3D_CLASS_ID);
    return 0;
}

/// @brief Return non-zero only when every component of @p v is finite.
int ph3d_vec3_all_finite(const double v[3]) {
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
static int world3d_reserve_contact_array(rt_contact3d **array, int32_t *capacity, int32_t needed) {
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
int world3d_reserve_contacts(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->contacts, &w->contact_capacity, needed);
}

/// @brief Grow w->frame_contacts to hold at least @p needed entries.
/// @details Frame contacts aggregate unique pairs across all CCD substeps so
///          very brief substep contacts still participate in the frame's
///          enter/stay/exit event diff.
int world3d_reserve_frame_contacts(rt_world3d *w, int32_t needed) {
    return world3d_reserve_contact_array(&w->frame_contacts, &w->frame_contact_capacity, needed);
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
int world3d_checked_increment(int32_t value, int32_t *out) {
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
int world3d_reserve_broadphase_capacity(rt_world3d *w, int32_t needed) {
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
double ph3d_clamp_nonnegative_finite(double value, double fallback) {
    if (!isfinite(value))
        return fallback;
    return value < 0.0 ? 0.0 : value;
}

/// @brief Return @p value if it is finite, otherwise return @p fallback.
/// @details Thin guard used wherever scalar inputs (gravity components, position offsets)
///   must be well-defined doubles before being stored in body or world state.
double ph3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp user-facing solver iterations to the runtime-supported range.
static int32_t ph3d_clamp_solver_iterations(int64_t value) {
    if (value < 1)
        return 1;
    if (value > PH3D_MAX_SOLVER_ITERATIONS)
        return PH3D_MAX_SOLVER_ITERATIONS;
    return (int32_t)value;
}

/// @brief Write a sanitized 3D vector to @p dst, replacing each NaN/Inf component with 0.
/// @details Used when accepting Vec3 arguments from Zia to populate force, velocity, and
///   position arrays — ensures that a single bad component cannot contaminate the solver.
static void ph3d_vec3_set_finite(double *dst, double x, double y, double z) {
    dst[0] = ph3d_finite_or(x, 0.0);
    dst[1] = ph3d_finite_or(y, 0.0);
    dst[2] = ph3d_finite_or(z, 0.0);
}

#define PH3D_STATE_ABS_MAX 1000000000000.0

/// @brief Clamp a physics state scalar to ±PH3D_STATE_ABS_MAX, mapping NaN/inf to 0.
/// @details Keeps positions/velocities from blowing up to non-finite values that would poison the
///          whole simulation; the bound is large enough not to affect well-behaved bodies.
static double ph3d_saturate_state_value(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > PH3D_STATE_ABS_MAX)
        return PH3D_STATE_ABS_MAX;
    if (value < -PH3D_STATE_ABS_MAX)
        return -PH3D_STATE_ABS_MAX;
    return value;
}

/// @brief Saturate each component of a state vector in place (see ph3d_saturate_state_value).
void ph3d_vec3_sanitize_state(double *v) {
    if (!v)
        return;
    v[0] = ph3d_saturate_state_value(v[0]);
    v[1] = ph3d_saturate_state_value(v[1]);
    v[2] = ph3d_saturate_state_value(v[2]);
}

/// @brief Add (x, y, z) into @p dst, sanitizing the addends and saturating the result per
/// component.
static void ph3d_vec3_accumulate_state(double *dst, double x, double y, double z) {
    if (!dst)
        return;
    dst[0] = ph3d_saturate_state_value(dst[0] + ph3d_finite_or(x, 0.0));
    dst[1] = ph3d_saturate_state_value(dst[1] + ph3d_finite_or(y, 0.0));
    dst[2] = ph3d_saturate_state_value(dst[2] + ph3d_finite_or(z, 0.0));
}

/// @brief Validate @p obj as a PhysicsHit3D handle (NULL on mismatch).
static rt_physics_hit3d_obj *physics_hit3d_checked(void *obj) {
    return (rt_physics_hit3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_PHYSICSHIT3D_CLASS_ID);
}

/// @brief Validate @p obj as a PhysicsHitList3D handle (NULL on mismatch).
static rt_physics_hit_list3d_obj *physics_hit_list3d_checked(void *obj) {
    return (rt_physics_hit_list3d_obj *)rt_g3d_checked_or_null(obj,
                                                               RT_G3D_PHYSICSHITLIST3D_CLASS_ID);
}

/// @brief Validate @p obj as a CollisionEvent3D handle (NULL on mismatch).
static rt_collision_event3d_obj *collision_event3d_checked(void *obj) {
    return (rt_collision_event3d_obj *)rt_g3d_checked_or_null(obj,
                                                              RT_G3D_COLLISIONEVENT3D_CLASS_ID);
}

/// @brief Validate @p obj as a ContactPoint3D handle (NULL on mismatch).
static rt_contact_point3d_obj *contact_point3d_checked(void *obj) {
    return (rt_contact_point3d_obj *)rt_g3d_checked_or_null(obj, RT_G3D_CONTACTPOINT3D_CLASS_ID);
}

// Forward declarations for functions defined later in this translation unit.
// They are referenced from helpers above that must be inlined by the compiler
// before the full definitions appear.
/// @brief Build a rt_collider_pose from the body's position/orientation/scale fields.
void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose);
/// @brief Compose a parent pose with a child's local position/rotation/scale.
void collider_pose_compose(const rt_collider_pose *parent,
                           const double *child_position,
                           const double *child_rotation,
                           const double *child_scale,
                           rt_collider_pose *out);
/// @brief Transform a point from the collider's local space into world space.
void transform_point_from_pose(const rt_collider_pose *pose,
                               const double *local_point,
                               double *world_point);
/// @brief Transform a point from world space into the collider's local space.
void transform_point_to_local(const rt_collider_pose *pose,
                              const double *world_point,
                              double *local_point);
/// @brief Sync the body's cached primitive shape from its attached collider's geometry.
void body3d_update_shape_cache_from_collider(rt_body3d *body);
/// @brief Compute the two world-space endpoint positions of a capsule's axis segment.
void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c);
/// @brief Run narrow-phase collision detection between two bodies.
int test_collision(const rt_body3d *a,
                   const rt_body3d *b,
                   double *normal,
                   double *depth,
                   double *point,
                   void **leaf_a_out,
                   void **leaf_b_out,
                   rt_collider_pose *leaf_a_pose_out,
                   rt_collider_pose *leaf_b_pose_out);

/*==========================================================================
 * Collision detection helpers
 *=========================================================================*/

/// @brief Compute the world-space AABB of a body, writing min/max into `mn`/`mx`.
///
/// If the body has a custom collider, delegates to `rt_collider3d_*` to
/// compute the bounds in the body's pose. Otherwise falls back to the
/// cached primitive shape (AABB / sphere / capsule). This is the broad-
/// phase primitive used by every collision query.
void body_aabb(const rt_body3d *b, double *mn, double *mx) {
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
double clampd(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/// @brief 3D dot product `a · b`.
double vec3_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// @brief Squared length `|v|²` — avoids the sqrt when the magnitude isn't needed.
double vec3_len_sq(const double *v) {
    return vec3_dot(v, v);
}

/// @brief Euclidean length `|v|`.
double vec3_len(const double *v) {
    return sqrt(vec3_len_sq(v));
}

/// @brief Copy `src` xyz into `dst`.
void vec3_copy(double *dst, const double *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/// @brief Set `dst` to `(x, y, z)`.
void vec3_set(double *dst, double x, double y, double z) {
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
void vec3_sub(const double *a, const double *b, double *out) {
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
void vec3_cross(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/// @brief Negate: `dst = -src`.
void vec3_negate(const double *src, double *dst) {
    dst[0] = -src[0];
    dst[1] = -src[1];
    dst[2] = -src[2];
}

/// @brief Normalize `v` in place. Returns the original length (0 → no-op).
double vec3_normalize_in_place(double *v) {
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
void quat_identity(double *q) {
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
void quat_conjugate(const double *q, double *out) {
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
void quat_rotate_vec3(const double *q, const double *v, double *out) {
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
void collider_pose_identity(rt_collider_pose *pose) {
    if (!pose)
        return;
    vec3_set(pose->position, 0.0, 0.0, 0.0);
    quat_identity(pose->rotation);
    vec3_set(pose->scale, 1.0, 1.0, 1.0);
}

/// @brief Initialize a pose from a body's transform and collision scale.
///
/// Per-body scale is multiplied with any per-collider child scale during
/// composition (`collider_pose_compose`).
void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose) {
    collider_pose_identity(pose);
    if (!body || !pose)
        return;
    vec3_copy(pose->position, body->position);
    pose->rotation[0] = body->orientation[0];
    pose->rotation[1] = body->orientation[1];
    pose->rotation[2] = body->orientation[2];
    pose->rotation[3] = body->orientation[3];
    vec3_copy(pose->scale, body->scale);
}

/// @brief Compose a child transform with a parent pose.
///
/// Order: scale, rotate, translate (TRS). Multiplies parent and child
/// scales component-wise, then rotates the child position by the parent
/// rotation before adding it to the parent position. Child rotation is
/// pre-multiplied into the parent rotation, then re-normalized.
void collider_pose_compose(const rt_collider_pose *parent,
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
void transform_point_from_pose(const rt_collider_pose *pose,
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
void transform_point_to_local(const rt_collider_pose *pose,
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
    else
        local_point[0] = 0.0;
    if (fabs(pose->scale[1]) > 1e-12)
        local_point[1] /= pose->scale[1];
    else
        local_point[1] = 0.0;
    if (fabs(pose->scale[2]) > 1e-12)
        local_point[2] /= pose->scale[2];
    else
        local_point[2] = 0.0;
}

/// @brief Absolute scale factor sanitized for division: non-finite or near-zero → 1.0.
double pose_abs_scale_or_unit(double value) {
    value = fabs(value);
    return isfinite(value) && value > 1e-12 ? value : 1.0;
}

/// @brief Absolute scale factor with non-finite mapped to 1.0 (zero is preserved).
double pose_abs_scale_or_zero(double value) {
    value = fabs(value);
    return isfinite(value) ? value : 1.0;
}

/// @brief Transform a world-space vector into pose-local space.
/// @details Translation is ignored; inverse rotation and inverse scale are applied.
void transform_vector_to_local(const rt_collider_pose *pose,
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
void transform_normal_from_local(const rt_collider_pose *pose,
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
int capsule_axis_sample_count(double axis_len, double radius) {
    double spacing;
    int samples;
    if (!isfinite(axis_len) || axis_len <= 1e-9)
        return 1;
    spacing = (isfinite(radius) && radius > 1e-6) ? radius * 0.5 : 0.05;
    if (spacing < 1e-4)
        spacing = 1e-4;
    /* Clamp in double space before the int cast: converting an out-of-range
     * double to int is undefined behavior, and axis_len/spacing can be huge for
     * extreme scales, so the [1,256] clamp must happen before narrowing. */
    double sample_estimate = ceil(axis_len / spacing) + 1.0;
    if (!isfinite(sample_estimate) || sample_estimate >= 256.0)
        return 256;
    if (sample_estimate < 1.0)
        return 1;
    samples = (int)sample_estimate;
    return samples;
}

/// @brief Refresh the body's cached primitive-shape fields from its collider.
///
/// Bodies hold a denormalized cache of `(shape, half_extents, radius,
/// height)` so the inner collision loop doesn't have to virtual-dispatch
/// on every comparison. This needs to be re-run whenever the collider's
/// shape parameters change. Falls back to an AABB derived from the
/// collider's local bounds for any non-primitive shape (mesh, hull, etc.).
void body3d_update_shape_cache_from_collider(rt_body3d *body) {
    double local_min[3];
    double local_max[3];
    double sx;
    double sy;
    double sz;
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
    sx = fabs(body->scale[0]);
    sy = fabs(body->scale[1]);
    sz = fabs(body->scale[2]);
    if (!isfinite(sx) || sx <= 1e-12)
        sx = 1.0;
    if (!isfinite(sy) || sy <= 1e-12)
        sy = 1.0;
    if (!isfinite(sz) || sz <= 1e-12)
        sz = 1.0;

    type = rt_collider3d_get_type(body->collider);
    switch (type) {
        case RT_COLLIDER3D_TYPE_BOX:
            body->shape = PH3D_SHAPE_AABB;
            rt_collider3d_get_box_half_extents_raw(body->collider, body->half_extents);
            body->half_extents[0] *= sx;
            body->half_extents[1] *= sy;
            body->half_extents[2] *= sz;
            break;
        case RT_COLLIDER3D_TYPE_SPHERE:
            body->shape = PH3D_SHAPE_SPHERE;
            body->radius = rt_collider3d_get_radius_raw(body->collider);
            body->radius *= fmax(sx, fmax(sy, sz));
            vec3_set(body->half_extents, body->radius, body->radius, body->radius);
            break;
        case RT_COLLIDER3D_TYPE_CAPSULE:
            body->shape = PH3D_SHAPE_CAPSULE;
            {
                double raw_radius = rt_collider3d_get_radius_raw(body->collider);
                double raw_height = rt_collider3d_get_height_raw(body->collider);
                body->radius = raw_radius * fmax(sx, sz);
                body->height = fmax(raw_height - 2.0 * raw_radius, 0.0) * sy + 2.0 * body->radius;
            }
            vec3_set(body->half_extents,
                     body->radius,
                     fmax(body->height * 0.5, body->radius),
                     body->radius);
            break;
        default:
            rt_collider3d_get_local_bounds_raw(body->collider, local_min, local_max);
            body->shape = PH3D_SHAPE_AABB;
            body->half_extents[0] = fabs(local_max[0] - local_min[0]) * 0.5 * sx;
            body->half_extents[1] = fabs(local_max[1] - local_min[1]) * 0.5 * sy;
            body->half_extents[2] = fabs(local_max[2] - local_min[2]) * 0.5 * sz;
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
void body3d_wake_if_dynamic(rt_body3d *b) {
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
    ph3d_vec3_accumulate_state(b->angular_velocity, world_delta[0], world_delta[1], world_delta[2]);
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
void body3d_contact_velocity(const rt_body3d *b, const double *r, double *out) {
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
double body3d_contact_impulse_denominator(const rt_body3d *b, const double *r, const double *dir) {
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
void body3d_apply_contact_impulse(rt_body3d *b, const double *impulse, const double *r) {
    double angular_impulse[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !impulse || !r)
        return;
    if (!ph3d_vec3_all_finite(impulse))
        return;
    ph3d_vec3_accumulate_state(
        b->velocity, impulse[0] * b->inv_mass, impulse[1] * b->inv_mass, impulse[2] * b->inv_mass);
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
void *physics_hit3d_new(const rt_query_hit3d *src) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)rt_obj_new_i64(
        RT_G3D_PHYSICSHIT3D_CLASS_ID, (int64_t)sizeof(rt_physics_hit3d_obj));
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
void *physics_hit_list3d_new_ex(const rt_query_hit3d *hits,
                                int32_t count,
                                int64_t total_count,
                                int8_t truncated) {
    rt_physics_hit_list3d_obj *list;
    if (count <= 0)
        return NULL;
    list = (rt_physics_hit_list3d_obj *)rt_obj_new_i64(RT_G3D_PHYSICSHITLIST3D_CLASS_ID,
                                                       (int64_t)sizeof(*list));
    if (!list) {
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    memset(list, 0, sizeof(*list));
    rt_obj_set_finalizer(list, physics_hit_list3d_finalizer);
    list->total_count = total_count >= count ? total_count : count;
    list->truncated = truncated ? 1 : 0;
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
    point = (rt_contact_point3d_obj *)rt_obj_new_i64(RT_G3D_CONTACTPOINT3D_CLASS_ID,
                                                     (int64_t)sizeof(*point));
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
    event = (rt_collision_event3d_obj *)rt_obj_new_i64(RT_G3D_COLLISIONEVENT3D_CLASS_ID,
                                                       (int64_t)sizeof(*event));
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
    event->contact_count = contact->contact_count;
    if (event->contact_count < 1)
        event->contact_count = 1;
    if (event->contact_count > PH3D_MAX_MANIFOLD_POINTS)
        event->contact_count = PH3D_MAX_MANIFOLD_POINTS;
    memcpy(event->points, contact->points, sizeof(event->points));
    memcpy(event->normals, contact->normals, sizeof(event->normals));
    memcpy(event->separations, contact->separations, sizeof(event->separations));
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

/// @brief Release every cached collision-event object in an array and NULL each slot.
/// @details The array itself is kept (capacity unchanged) for reuse next frame; only the pooled
///          event objects are released.
static void world3d_release_event_object_array(void ***array, int32_t *capacity) {
    if (!array || !capacity || !*array)
        return;
    for (int32_t i = 0; i < *capacity; ++i) {
        void *event = (*array)[i];
        if (event && rt_obj_release_check0(event))
            rt_obj_free(event);
        (*array)[i] = NULL;
    }
}

/// @brief Release all four cached collision-event object pools (collision/enter/stay/exit).
static void world3d_release_cached_event_objects(rt_world3d *w) {
    if (!w)
        return;
    world3d_release_event_object_array(&w->collision_event_objects,
                                       &w->collision_event_object_capacity);
    world3d_release_event_object_array(&w->enter_event_objects, &w->enter_event_object_capacity);
    world3d_release_event_object_array(&w->stay_event_objects, &w->stay_event_object_capacity);
    world3d_release_event_object_array(&w->exit_event_objects, &w->exit_event_object_capacity);
}

/// @brief Ensure an event-object pool array holds at least @p needed slots (zero-filled growth).
static int world3d_reserve_event_object_array(void ***array, int32_t *capacity, int32_t needed) {
    void **grown;
    if (!array || !capacity)
        return 0;
    if (needed <= *capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(*capacity, needed, PH3D_INITIAL_CONTACTS);
    if (new_capacity < 0 || (size_t)new_capacity > SIZE_MAX / sizeof(void *))
        return 0;
    grown = (void **)realloc(*array, (size_t)new_capacity * sizeof(void *));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(new_capacity - *capacity) * sizeof(void *));
    *array = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Get (or lazily build) the pooled collision-event object for @p contact at @p index.
/// @details Reuses the slot's existing event object across frames — refreshing its fields from the
///          contact — to avoid per-frame allocation in the collision callback path.
static void *world3d_cached_event_from_contact(void ***array,
                                               int32_t *capacity,
                                               int64_t index,
                                               const rt_contact3d *contact) {
    void *event;
    if (!array || !capacity || index < 0 || index > INT32_MAX || !contact)
        return NULL;
    if (!world3d_reserve_event_object_array(array, capacity, (int32_t)index + 1))
        return collision_event3d_new_from_contact(contact);
    event = (*array)[index];
    if (event) {
        rt_obj_retain_maybe(event);
        return event;
    }
    event = collision_event3d_new_from_contact(contact);
    if (event) {
        rt_obj_retain_maybe(event);
        (*array)[index] = event;
    }
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
static int world3d_build_event_buffers(rt_world3d *w) {
    contact_pair_hash_entry *previous_table = NULL;
    contact_pair_hash_entry *current_table = NULL;
    int32_t previous_table_capacity = 0;
    int32_t current_table_capacity = 0;
    int32_t enter_count = 0;
    int32_t stay_count = 0;
    int32_t exit_count = 0;
    int32_t enter_write = 0;
    int32_t stay_write = 0;
    int32_t exit_write = 0;
    if (!w)
        return 0;

    previous_table = contact_pair_table_build(
        w->previous_contacts, w->previous_contact_count, &previous_table_capacity);
    current_table =
        contact_pair_table_build(w->contacts, w->contact_count, &current_table_capacity);

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = previous_table ? contact_pair_table_contains(
                                         previous_table, previous_table_capacity, &w->contacts[i])
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
            if (!world3d_checked_increment(stay_count, &stay_count))
                goto overflow;
        } else {
            if (!world3d_checked_increment(enter_count, &enter_count))
                goto overflow;
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
            if (!world3d_checked_increment(exit_count, &exit_count))
                goto overflow;
        }
    }

    if (!world3d_reserve_enter_events(w, enter_count) ||
        !world3d_reserve_stay_events(w, stay_count) ||
        !world3d_reserve_exit_events(w, exit_count) ||
        !world3d_reserve_previous_contacts(w, w->contact_count)) {
        rt_trap("Physics3D.World.Step: event-buffer allocation failed");
        goto fail;
    }

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = previous_table ? contact_pair_table_contains(
                                         previous_table, previous_table_capacity, &w->contacts[i])
                                   : 0;
        if (!previous_table) {
            for (int32_t j = 0; j < w->previous_contact_count; ++j) {
                if (contact_pair_equals(&w->contacts[i], &w->previous_contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (found)
            contact_snapshot_copy(&w->stay_events[stay_write++], &w->contacts[i]);
        else
            contact_snapshot_copy(&w->enter_events[enter_write++], &w->contacts[i]);
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
        if (!found)
            contact_snapshot_copy(&w->exit_events[exit_write++], &w->previous_contacts[i]);
    }

    w->enter_event_count = enter_write;
    w->stay_event_count = stay_write;
    w->exit_event_count = exit_write;
    w->previous_contact_count = w->contact_count;
    for (int32_t i = 0; i < w->contact_count; ++i)
        contact_snapshot_copy(&w->previous_contacts[i], &w->contacts[i]);
    free(previous_table);
    free(current_table);
    return 1;

overflow:
    rt_trap("Physics3D.World.Step: event count overflow");
fail:
    free(previous_table);
    free(current_table);
    return 0;
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
    w->contact_count = 0;
    w->frame_contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    world3d_release_cached_event_objects(w);
    free(w->bodies);
    free(w->contacts);
    free(w->frame_contacts);
    free(w->previous_contacts);
    free(w->enter_events);
    free(w->stay_events);
    free(w->exit_events);
    free(w->collision_event_objects);
    free(w->enter_event_objects);
    free(w->stay_event_objects);
    free(w->exit_event_objects);
    free(w->joints);
    free(w->joint_types);
    free(w->broadphase_entries);
    w->bodies = NULL;
    w->contacts = NULL;
    w->frame_contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->collision_event_objects = NULL;
    w->enter_event_objects = NULL;
    w->stay_event_objects = NULL;
    w->exit_event_objects = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_valid = 0;
    w->last_ccd_requested_substeps = 1;
    w->last_ccd_substeps = 1;
    w->ccd_substep_clamped_count = 0;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->frame_contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->collision_event_object_capacity = 0;
    w->enter_event_object_capacity = 0;
    w->stay_event_object_capacity = 0;
    w->exit_event_object_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_valid = 0;
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
    rt_world3d *w =
        (rt_world3d *)rt_obj_new_i64(RT_G3D_WORLD3D_CLASS_ID, (int64_t)sizeof(rt_world3d));
    if (!w) {
        rt_trap("Physics3D.World.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    ph3d_vec3_set_finite(w->gravity, gx, gy, gz);
    w->body_count = 0;
    w->contact_count = 0;
    w->frame_contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    w->collision_event_object_capacity = 0;
    w->enter_event_object_capacity = 0;
    w->stay_event_object_capacity = 0;
    w->exit_event_object_capacity = 0;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->frame_contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_valid = 0;
    w->bodies = NULL;
    w->contacts = NULL;
    w->frame_contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->collision_event_objects = NULL;
    w->enter_event_objects = NULL;
    w->stay_event_objects = NULL;
    w->exit_event_objects = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    w->solver_iterations = PH3D_DEFAULT_SOLVER_ITERATIONS;
    if (!world3d_reserve_body_capacity(w, PH3D_INITIAL_BODIES) ||
        !world3d_reserve_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_frame_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_previous_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_enter_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_stay_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_exit_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_joint_capacity(w, PH3D_INITIAL_JOINTS) ||
        !world3d_reserve_broadphase_capacity(w, PH3D_INITIAL_BODIES)) {
        world3d_finalizer(w);
        if (rt_obj_release_check0(w))
            rt_obj_free(w);
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
/// Clamped to `[1, PH3D_MAX_CCD_SUBSTEPS]`, with clamp diagnostics retained
/// on the world for runtime inspection.
static int world3d_compute_substeps(rt_world3d *w, double dt) {
    int substeps = 1;
    int requested = 1;
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
            double needed_f = ceil((speed * dt) / threshold);
            int needed;
            if (!isfinite(needed_f))
                continue;
            if (needed_f > (double)INT_MAX)
                needed = INT_MAX;
            else if (needed_f < 1.0)
                needed = 1;
            else
                needed = (int)needed_f;
            if (needed > substeps)
                substeps = needed;
        }
    }
    if (substeps < 1)
        substeps = 1;
    requested = substeps;
    if (substeps > PH3D_MAX_CCD_SUBSTEPS)
        substeps = PH3D_MAX_CCD_SUBSTEPS;
    w->last_ccd_requested_substeps = requested;
    w->last_ccd_substeps = substeps;
    if (requested > substeps)
        w->ccd_substep_clamped_count++;
    return substeps;
}

/// @brief Clear one-shot force and torque accumulators from every registered body.
static void world3d_clear_body_accumulators(rt_world3d *w) {
    if (!w)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->force[0] = b->force[1] = b->force[2] = 0.0;
        b->torque[0] = b->torque[1] = b->torque[2] = 0.0;
    }
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
/// After the substeps, a unique frame-wide contact set is diffed against
/// the previous frame to fire enter/stay/exit events, and per-step
/// forces/torques are zeroed for the next frame.
///
/// @param obj `World3D` handle.
/// @param dt  Step duration (seconds). No-op for `dt <= 0`.
void rt_world3d_step(void *obj, double dt) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || !isfinite(dt) || dt <= 0)
        return;
    int substeps = world3d_compute_substeps(w, dt);
    double sub_dt = dt / (double)substeps;
    world3d_invalidate_query_broadphase(w);
    world3d_release_cached_event_objects(w);
    w->frame_contact_count = 0;
    w->last_solver_island_count = 0;
    w->last_solver_active_body_count = 0;
    w->last_solver_contact_count = 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->is_grounded = 0;
        vec3_set(b->ground_normal, 0.0, 1.0, 0.0);
    }

    for (int substep = 0; substep < substeps; substep++) {
        /* Phase 1: Integration */
        for (int32_t i = 0; i < w->body_count; i++) {
            rt_body3d *b = w->bodies[i];
            double linear_scale;
            double angular_scale;
            if (!b)
                continue;

            if (b->motion_mode == PH3D_MODE_STATIC) {
                continue;
            }

            if (b->motion_mode == PH3D_MODE_DYNAMIC) {
                if (b->is_sleeping) {
                    continue;
                }

                ph3d_vec3_accumulate_state(b->velocity,
                                           (w->gravity[0] + b->force[0] * b->inv_mass) * sub_dt,
                                           (w->gravity[1] + b->force[1] * b->inv_mass) * sub_dt,
                                           (w->gravity[2] + b->force[2] * b->inv_mass) * sub_dt);
                ph3d_vec3_accumulate_state(b->angular_velocity,
                                           b->torque[0] * b->inv_inertia[0] * sub_dt,
                                           b->torque[1] * b->inv_inertia[1] * sub_dt,
                                           b->torque[2] * b->inv_inertia[2] * sub_dt);

                linear_scale = fmax(0.0, 1.0 - b->linear_damping * sub_dt);
                angular_scale = fmax(0.0, 1.0 - b->angular_damping * sub_dt);
                vec3_scale_in_place(b->velocity, linear_scale);
                vec3_scale_in_place(b->angular_velocity, angular_scale);
                ph3d_vec3_sanitize_state(b->velocity);
                ph3d_vec3_sanitize_state(b->angular_velocity);
                /* Sleep is decided post-solve (world3d_update_sleep), not here:
                 * a body resting under gravity carries -g*dt velocity at this
                 * point and only returns to ~0 once contacts are resolved. */
            }

            ph3d_vec3_accumulate_state(b->position,
                                       b->velocity[0] * sub_dt,
                                       b->velocity[1] * sub_dt,
                                       b->velocity[2] * sub_dt);
            quat_integrate(b->orientation, b->angular_velocity, sub_dt);
            body3d_touch_broadphase(b);
        }

        /* Phase 2: detect contacts once, then warm-started sequential-impulse solve.
         * Detecting once (rather than re-detecting per iteration) lets per-point
         * impulses accumulate and warm-start across frames, which is what makes
         * stacks rest stably. Joints are interleaved with the velocity iterations. */
        int32_t solver_iterations = ph3d_clamp_solver_iterations(w->solver_iterations);
        ph3d_solver_island_batch solver_batch;
        if (!world3d_detect_contacts(w))
            goto cleanup;
        if (!world3d_build_solver_island_batch(w, &solver_batch))
            goto cleanup;
        if (solver_batch.island_count > w->last_solver_island_count)
            w->last_solver_island_count = solver_batch.island_count;
        if (solver_batch.active_body_count > w->last_solver_active_body_count)
            w->last_solver_active_body_count = solver_batch.active_body_count;
        if (solver_batch.solver_contact_count > w->last_solver_contact_count)
            w->last_solver_contact_count = solver_batch.solver_contact_count;
        world3d_warm_start_solver_islands(w, &solver_batch);
        for (int32_t iter = 0; iter < solver_iterations; iter++) {
            world3d_solve_velocity_solver_islands(&solver_batch, w);
            for (int32_t j = 0; j < w->joint_count; j++) {
                if (w->joints[j])
                    rt_joint3d_solve(w->joints[j], w->joint_types[j], sub_dt);
            }
        }
        world3d_solve_position_solver_islands(&solver_batch, w);
        ph3d_solver_island_batch_free(&solver_batch);
        world3d_finalize_contacts(w);
        world3d_update_sleep(w, sub_dt);
        for (int32_t i = 0; i < w->contact_count; ++i) {
            if (!world3d_append_frame_contact_unique(w, &w->contacts[i]))
                goto cleanup;
        }
    }

    if (!world3d_publish_frame_contacts(w))
        goto cleanup;
    if (!world3d_build_event_buffers(w))
        goto cleanup;

cleanup:
    world3d_invalidate_query_broadphase(w);
    world3d_clear_body_accumulators(w);
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

/// @brief `World3D.SolverIterations` — iterative constraint-solver pass count.
int64_t rt_world3d_get_solver_iterations(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? ph3d_clamp_solver_iterations(w->solver_iterations) : 0;
}

/// @brief `World3D.SetSolverIterations(iterations)` — tune iterative solver passes.
void rt_world3d_set_solver_iterations(void *obj, int64_t iterations) {
    rt_world3d *w = world3d_checked(obj);
    if (w)
        w->solver_iterations = ph3d_clamp_solver_iterations(iterations);
}

/// @brief `World3D.LastSolverIslandCount` — max active contact islands in the last Step.
int64_t rt_world3d_get_last_solver_island_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->last_solver_island_count : 0;
}

/// @brief `World3D.LastSolverActiveBodyCount` — max awake bodies scheduled in contact islands.
int64_t rt_world3d_get_last_solver_active_body_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->last_solver_active_body_count : 0;
}

/// @brief `World3D.LastSolverContactCount` — max contacts scheduled through islands.
int64_t rt_world3d_get_last_solver_contact_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->last_solver_contact_count : 0;
}

/// @brief `World3D.Add(body)` — register a body.
///
/// Retains the body and stores it in the growable bodies array.
int8_t rt_world3d_try_add(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == b)
            return 1;
    }
    if (!world3d_reserve_body_capacity(w, w->body_count + 1)) {
        rt_trap("Physics3D: body storage allocation failed");
        return 0;
    }
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = b;
    world3d_invalidate_query_broadphase(w);
    return 1;
}

/// @brief Add a body to the world, ignoring the success flag (see rt_world3d_try_add).
void rt_world3d_add(void *obj, void *body) {
    (void)rt_world3d_try_add(obj, body);
}

/// @brief Whether a contact references @p body as either participant.
static int contact_mentions_body(const rt_contact3d *contact, const rt_body3d *body) {
    return contact && body && (contact->body_a == body || contact->body_b == body);
}

/// @brief Remove every contact mentioning @p body from a contact array, compacting it in place.
static void world3d_purge_contact_array_for_body(rt_contact3d *contacts,
                                                 int32_t *count,
                                                 const rt_body3d *body) {
    int32_t write = 0;
    if (!contacts || !count || !body)
        return;
    for (int32_t read = 0; read < *count; ++read) {
        if (contact_mentions_body(&contacts[read], body))
            continue;
        if (write != read)
            contacts[write] = contacts[read];
        write++;
    }
    for (int32_t i = write; i < *count; ++i)
        memset(&contacts[i], 0, sizeof(contacts[i]));
    *count = write;
}

/// @brief Purge all of the world's contact/event arrays of contacts involving @p body.
/// @details Called when a body leaves the world so stale contacts don't fire spurious events.
static void world3d_purge_body_contacts(rt_world3d *w, const rt_body3d *body) {
    if (!w || !body)
        return;
    world3d_release_cached_event_objects(w);
    world3d_purge_contact_array_for_body(w->contacts, &w->contact_count, body);
    world3d_purge_contact_array_for_body(w->frame_contacts, &w->frame_contact_count, body);
    world3d_purge_contact_array_for_body(w->previous_contacts, &w->previous_contact_count, body);
    world3d_purge_contact_array_for_body(w->enter_events, &w->enter_event_count, body);
    world3d_purge_contact_array_for_body(w->stay_events, &w->stay_event_count, body);
    world3d_purge_contact_array_for_body(w->exit_events, &w->exit_event_count, body);
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
            world3d_purge_body_contacts(w, b);
            w->bodies[i] = w->bodies[--w->body_count];
            w->bodies[w->body_count] = NULL;
            world3d_invalidate_query_broadphase(w);
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

/// @brief Whether @p body is currently registered in the world.
int8_t rt_world3d_contains_body(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == b)
            return 1;
    }
    return 0;
}

/// @brief `World3D.LastCCDRequestedSubsteps` — unclamped substep demand from the last Step.
int64_t rt_world3d_get_last_ccd_requested_substeps(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->last_ccd_requested_substeps : 0;
}

/// @brief `World3D.LastCCDSubsteps` — actual CCD substeps used by the last Step.
int64_t rt_world3d_get_last_ccd_substeps(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->last_ccd_substeps : 0;
}

/// @brief `World3D.CCDSubstepClampedCount` — number of frames that hit the substep cap.
int64_t rt_world3d_get_ccd_substep_clamped_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->ccd_substep_clamped_count : 0;
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

/// @brief Shift a contact's cached points by the floating-origin rebase @p delta.
/// @details When the world recenters to keep coordinates near the origin, contact points must move
///          with it; saturates the shifted values to stay finite.
static void contact3d_rebase_origin(rt_contact3d *contact, const double delta[3]) {
    if (!contact || !delta)
        return;
    contact->point[0] = ph3d_saturate_state_value(contact->point[0] - delta[0]);
    contact->point[1] = ph3d_saturate_state_value(contact->point[1] - delta[1]);
    contact->point[2] = ph3d_saturate_state_value(contact->point[2] - delta[2]);
    int32_t point_count = contact->contact_count;
    if (point_count < 0)
        point_count = 0;
    if (point_count > PH3D_MAX_MANIFOLD_POINTS)
        point_count = PH3D_MAX_MANIFOLD_POINTS;
    for (int32_t i = 0; i < point_count; ++i) {
        contact->points[i][0] = ph3d_saturate_state_value(contact->points[i][0] - delta[0]);
        contact->points[i][1] = ph3d_saturate_state_value(contact->points[i][1] - delta[1]);
        contact->points[i][2] = ph3d_saturate_state_value(contact->points[i][2] - delta[2]);
    }
}

/// @brief Apply a floating-origin rebase @p delta to every contact in an array.
static void world3d_rebase_contact_array(rt_contact3d *contacts,
                                         int32_t count,
                                         const double delta[3]) {
    if (!contacts || count <= 0 || !delta)
        return;
    for (int32_t i = 0; i < count; ++i)
        contact3d_rebase_origin(&contacts[i], delta);
}

/// @brief `World3D.RebaseOrigin(dx, dy, dz)` — shift body/contact/query state by -delta.
void rt_world3d_rebase_origin(void *obj, double dx, double dy, double dz) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    double delta[3] = {
        ph3d_finite_or(dx, 0.0),
        ph3d_finite_or(dy, 0.0),
        ph3d_finite_or(dz, 0.0),
    };
    if (delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0)
        return;

    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        if (!body)
            continue;
        body->position[0] = ph3d_saturate_state_value(body->position[0] - delta[0]);
        body->position[1] = ph3d_saturate_state_value(body->position[1] - delta[1]);
        body->position[2] = ph3d_saturate_state_value(body->position[2] - delta[2]);
        body3d_touch_broadphase(body);
    }

    world3d_rebase_contact_array(w->contacts, w->contact_count, delta);
    world3d_rebase_contact_array(w->frame_contacts, w->frame_contact_count, delta);
    world3d_rebase_contact_array(w->previous_contacts, w->previous_contact_count, delta);
    world3d_rebase_contact_array(w->enter_events, w->enter_event_count, delta);
    world3d_rebase_contact_array(w->stay_events, w->stay_event_count, delta);
    world3d_rebase_contact_array(w->exit_events, w->exit_event_count, delta);
    world3d_invalidate_query_broadphase(w);
    world3d_release_cached_event_objects(w);
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
    return world3d_cached_event_from_contact(&w->collision_event_objects,
                                             &w->collision_event_object_capacity,
                                             index,
                                             &w->contacts[index]);
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
    return world3d_cached_event_from_contact(
        &w->enter_event_objects, &w->enter_event_object_capacity, index, &w->enter_events[index]);
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
    return world3d_cached_event_from_contact(
        &w->stay_event_objects, &w->stay_event_object_capacity, index, &w->stay_events[index]);
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
    return world3d_cached_event_from_contact(
        &w->exit_event_objects, &w->exit_event_object_capacity, index, &w->exit_events[index]);
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
    world3d_release_cached_event_objects(w);
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

/// @brief `CollisionEvent3D.ContactCount` — number of points in this contact manifold.
int64_t rt_collision_event3d_get_contact_count(void *obj) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    return event ? event->contact_count : 0;
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
/// Reconstructs a transient `rt_contact3d` from the event's stored manifold
/// point and boxes it via `contact_point3d_new_from_contact`.
void *rt_collision_event3d_get_contact(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    rt_contact3d contact;
    if (!event || index < 0 || index >= event->contact_count)
        return NULL;
    memset(&contact, 0, sizeof(contact));
    contact.body_a = event->body_a;
    contact.body_b = event->body_b;
    contact.collider_a = event->collider_a;
    contact.collider_b = event->collider_b;
    vec3_copy(contact.point, event->points[index]);
    vec3_copy(contact.normal, event->normals[index]);
    contact.separation = event->separations[index];
    contact3d_init_single_point(&contact, contact.point, contact.normal, contact.separation);
    return contact_point3d_new_from_contact(&contact);
}

/// @brief `CollisionEvent3D.ContactPoint(i)` — fresh `Vec3` for the contact point.
void *rt_collision_event3d_get_contact_point(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index < 0 || index >= event->contact_count)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(event->points[index][0], event->points[index][1], event->points[index][2]);
}

/// @brief `CollisionEvent3D.ContactNormal(i)` — fresh `Vec3` for the contact normal.
void *rt_collision_event3d_get_contact_normal(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index < 0 || index >= event->contact_count)
        return rt_vec3_new(0.0, 1.0, 0.0);
    return rt_vec3_new(
        event->normals[index][0], event->normals[index][1], event->normals[index][2]);
}

/// @brief `CollisionEvent3D.ContactSeparation(i)` — signed separation distance.
///
/// Negative means penetration; zero or positive means touching/just-separated.
double rt_collision_event3d_get_contact_separation(void *obj, int64_t index) {
    rt_collision_event3d_obj *event = collision_event3d_checked(obj);
    if (!event || index < 0 || index >= event->contact_count)
        return 0.0;
    return event->separations[index];
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

/// @brief `PhysicsHitList3D.TotalCount` — full number of hits found before list truncation.
int64_t rt_physics_hit_list3d_get_total_count(void *obj) {
    rt_physics_hit_list3d_obj *list = physics_hit_list3d_checked(obj);
    return list ? list->total_count : 0;
}

/// @brief `PhysicsHitList3D.Truncated` — true when more hits matched than the list stores.
int8_t rt_physics_hit_list3d_get_truncated(void *obj) {
    rt_physics_hit_list3d_obj *list = physics_hit_list3d_checked(obj);
    return list ? list->truncated : 0;
}

/// @brief `PhysicsHitList3D[i]` — borrowed `PhysicsHit3D` reference.
void *rt_physics_hit_list3d_get(void *obj, int64_t index) {
    rt_physics_hit_list3d_obj *list = physics_hit_list3d_checked(obj);
    if (!list || index < 0 || index >= list->count)
        return NULL;
    return list->items[index];
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
    if (!body3d_motion_mode_allowed(body, collider, body->motion_mode, api_name))
        return 0;
    if (collider)
        rt_obj_retain_maybe(collider);
    if (body->collider && rt_obj_release_check0(body->collider))
        rt_obj_free(body->collider);
    body->collider = collider;
    body3d_update_shape_cache_from_collider(body);
    body3d_refresh_motion_mode(body);
    body3d_touch_broadphase(body);
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
    vec3_set(b->scale, 1.0, 1.0, 1.0);
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
        ph3d_vec3_sanitize_state(b->position);
        body3d_touch_broadphase(b);
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

/// @brief `Body3D.SetScale(x,y,z)` — set collision scale applied to the collider.
void rt_body3d_set_scale(void *o, double x, double y, double z) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return;
    b->scale[0] = ph3d_finite_or(x, 1.0);
    b->scale[1] = ph3d_finite_or(y, 1.0);
    b->scale[2] = ph3d_finite_or(z, 1.0);
    if (fabs(b->scale[0]) <= 1e-12)
        b->scale[0] = 1.0;
    if (fabs(b->scale[1]) <= 1e-12)
        b->scale[1] = 1.0;
    if (fabs(b->scale[2]) <= 1e-12)
        b->scale[2] = 1.0;
    body3d_update_shape_cache_from_collider(b);
    body3d_refresh_motion_mode(b);
    body3d_touch_broadphase(b);
    body3d_wake_if_dynamic(b);
}

/// @brief `Body3D.GetScale` — fresh `Vec3` of the collider scale.
void *rt_body3d_get_scale(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(1.0, 1.0, 1.0);
    return rt_vec3_new(b->scale[0], b->scale[1], b->scale[2]);
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
        rt_trap("Physics3DBody.SetOrientation: quat must be Quat");
        return;
    } else {
        b->orientation[0] = rt_quat_x(quat);
        b->orientation[1] = rt_quat_y(quat);
        b->orientation[2] = rt_quat_z(quat);
        b->orientation[3] = rt_quat_w(quat);
        quat_normalize(b->orientation);
    }
    body3d_touch_broadphase(b);
    body3d_wake_if_dynamic(b);
}

/// @brief `Body3D.GetOrientation` — fresh `Quat` of the body's orientation.
void *rt_body3d_get_orientation(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    return rt_quat_new(b->orientation[0], b->orientation[1], b->orientation[2], b->orientation[3]);
}

/// @brief Read a body's full pose — position, orientation quaternion, and scale — into raw arrays.
/// @details Out-params are optional (NULL-skippable). Used by the renderer/scene sync to mirror the
///          simulated body transform without boxing Vec3/Quat values.
void rt_body3d_get_pose_raw(void *o,
                            double *position_out,
                            double *rotation_out,
                            double *scale_out) {
    rt_body3d *b = body3d_checked(o);
    if (position_out)
        vec3_set(position_out, 0.0, 0.0, 0.0);
    if (rotation_out)
        quat_identity(rotation_out);
    if (scale_out)
        vec3_set(scale_out, 1.0, 1.0, 1.0);
    if (!b)
        return;
    if (position_out)
        vec3_copy(position_out, b->position);
    if (rotation_out)
        memcpy(rotation_out, b->orientation, sizeof(b->orientation));
    if (scale_out)
        vec3_copy(scale_out, b->scale);
}

/// @brief `Body3D.SetVelocity(x, y, z)` — set linear velocity (m/s).
///
/// Wakes the body if dynamic and the new velocity is non-zero.
void rt_body3d_set_velocity(void *o, double x, double y, double z) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        ph3d_vec3_set_finite(b->velocity, x, y, z);
        ph3d_vec3_sanitize_state(b->velocity);
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
        ph3d_vec3_sanitize_state(b->angular_velocity);
        if (vec3_len_sq(b->angular_velocity) > 1e-12)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief `Body3D.GetAngularVelocity` — fresh `Vec3` of angular velocity.
void *rt_body3d_get_angular_velocity(void *o) {
    rt_body3d *b = body3d_checked(o);
    if (!b)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(b->angular_velocity[0], b->angular_velocity[1], b->angular_velocity[2]);
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
        ph3d_vec3_accumulate_state(b->force, fx, fy, fz);
        if (fx != 0.0 || fy != 0.0 || fz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief Apply a continuous force at a world-space point this step, adding both
///   force and torque (r x force) — e.g. a thruster or off-center wind. Like
///   ApplyForce, it is cleared at the end of the step. Dynamic bodies only.
void rt_body3d_apply_force_at_point(
    void *o, double fx, double fy, double fz, double px, double py, double pz) {
    rt_body3d *b = body3d_checked(o);
    double force[3];
    double r[3];
    double torque[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    force[0] = ph3d_finite_or(fx, 0.0);
    force[1] = ph3d_finite_or(fy, 0.0);
    force[2] = ph3d_finite_or(fz, 0.0);
    r[0] = ph3d_finite_or(px, 0.0) - b->position[0];
    r[1] = ph3d_finite_or(py, 0.0) - b->position[1];
    r[2] = ph3d_finite_or(pz, 0.0) - b->position[2];
    vec3_cross(r, force, torque);
    ph3d_vec3_accumulate_state(b->force, force[0], force[1], force[2]);
    ph3d_vec3_accumulate_state(b->torque, torque[0], torque[1], torque[2]);
    if (force[0] != 0.0 || force[1] != 0.0 || force[2] != 0.0)
        body3d_wake_if_dynamic(b);
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
        ph3d_vec3_accumulate_state(
            b->velocity, ix * b->inv_mass, iy * b->inv_mass, iz * b->inv_mass);
        if (ix != 0.0 || iy != 0.0 || iz != 0.0)
            body3d_wake_if_dynamic(b);
    }
}

/// @brief Apply a linear impulse at a world-space point, producing both linear
///   and angular velocity via the lever arm from the center of mass — e.g. a hit
///   at a box corner makes it spin. Dynamic bodies only.
void rt_body3d_apply_impulse_at_point(
    void *o, double ix, double iy, double iz, double px, double py, double pz) {
    rt_body3d *b = body3d_checked(o);
    double impulse[3];
    double r[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    impulse[0] = ph3d_finite_or(ix, 0.0);
    impulse[1] = ph3d_finite_or(iy, 0.0);
    impulse[2] = ph3d_finite_or(iz, 0.0);
    r[0] = ph3d_finite_or(px, 0.0) - b->position[0];
    r[1] = ph3d_finite_or(py, 0.0) - b->position[1];
    r[2] = ph3d_finite_or(pz, 0.0) - b->position[2];
    body3d_apply_contact_impulse(b, impulse, r);
    if (impulse[0] != 0.0 || impulse[1] != 0.0 || impulse[2] != 0.0)
        body3d_wake_if_dynamic(b);
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
        ph3d_vec3_accumulate_state(b->torque, tx, ty, tz);
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
        ph3d_vec3_accumulate_state(b->angular_velocity,
                                   ix * b->inv_inertia[0],
                                   iy * b->inv_inertia[1],
                                   iz * b->inv_inertia[2]);
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
    if (!b)
        return;
    if (l <= 0) {
        rt_trap("Physics3DBody.SetCollisionLayer: layer must be a positive bitmask");
        return;
    }
    b->collision_layer = l;
    body3d_touch_broadphase(b);
}

/// @brief `Body3D.GetCollisionLayer` — read this body's layer bitmask.
int64_t rt_body3d_get_collision_layer(void *o) {
    rt_body3d *b = body3d_checked(o);
    return b ? b->collision_layer : 0;
}

/// @brief `Body3D.SetCollisionMask(m)` — bitmask of layers this body collides with.
void rt_body3d_set_collision_mask(void *o, int64_t m) {
    rt_body3d *b = body3d_checked(o);
    if (b) {
        b->collision_mask = m;
        body3d_touch_broadphase(b);
    }
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
        body3d_touch_broadphase(b);
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
        body3d_touch_broadphase(b);
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


#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
