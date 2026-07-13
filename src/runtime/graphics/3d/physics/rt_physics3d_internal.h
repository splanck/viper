//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_internal.h
// Purpose: Internal shared types, constants, and helper declarations for the
//   Physics3D runtime, split across rt_physics3d.c (core/integration/lifecycle),
//   rt_physics3d_collision.c (narrow-phase + contact manifolds),
//   rt_physics3d_solver.c (sequential-impulse solver + broad phase),
//   rt_physics3d_query.c (raycast/overlap/sweep), and
//   rt_physics3d_character.c (character controller + trigger volumes).
//   Private to the physics runtime — not part of the public Graphics3D ABI.
//
// Key invariants:
//   - rt_body3d field layout is pinned by _Static_asserts in rt_physics3d.c
//     against the private rt_body3d_kinematics prefix view.
//   - World3D / Body3D / Character3D / Trigger3D are GC-managed; shared mutable
//     state flows exclusively through these struct pointers (no file-scope state).
//
// Ownership/Lifetime:
//   - This header declares borrowed internal pointers only; owning references
//     are held by the runtime objects that contain them.
//   - Split Physics3D translation units include this header but do not expose
//     these layouts as script-facing ABI.
//
// Links: rt_physics3d.c, rt_physics3d_world.inc, rt_physics3d_solver.c
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_body3d_kinematics_internal.h"
#include "rt_canvas3d_internal.h"
#include "rt_physics3d.h"

#include <stddef.h>
#include <stdint.h>

//===----------------------------------------------------------------------===//
// Shared runtime helpers provided by the object/vector/quaternion runtime.
//===----------------------------------------------------------------------===//
#ifdef __cplusplus
extern "C" {
#endif

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);

#ifdef __cplusplus
}
#endif

//===----------------------------------------------------------------------===//
// Shared constants.
//===----------------------------------------------------------------------===//
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
#define PH3D_DEFAULT_SOLVER_ITERATIONS 6
#define PH3D_DEFAULT_POSITION_ITERATIONS 1
#define PH3D_DEFAULT_CONTACT_BETA 0.8
#define PH3D_DEFAULT_RESTITUTION_THRESHOLD 0.5
#define PH3D_MAX_SOLVER_ITERATIONS 64
#define PH3D_MAX_CCD_SUBSTEPS 64
#define PH3D_MAX_SWEEP_STEPS 512
#define PH3D_MAX_MANIFOLD_POINTS 4
#define PH3D_INITIAL_CONTACTS 128
#define PH3D_MAX_QUERY_HITS 256
#define PH3D_QUERY_HITS_MIN 16
#define PH3D_QUERY_HITS_MAX 4096
#define PH3D_INITIAL_JOINTS 128

//===----------------------------------------------------------------------===//
// Core simulation structs.
//===----------------------------------------------------------------------===//

typedef struct rt_world3d rt_world3d;

/// @brief Rigid body payload: pose (position/orientation/scale), linear+angular
///   motion state, mass/inertia, cached primitive shape, material properties,
///   layer/mask filtering, and sleep/CCD flags.
typedef struct rt_body3d {
    void *vptr;
    double position[3];
    double orientation[4];
    double scale[3];
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
    int64_t user_data; /* opaque gameplay handle; the runtime never reads it */
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
    uint64_t broadphase_revision;
    rt_world3d *owner_world;
    int32_t owner_index;
} rt_body3d;

/// @brief A contact manifold between two bodies: representative point/normal/
///   separation plus up to PH3D_MAX_MANIFOLD_POINTS manifold points and the
///   per-point warm-start impulse accumulators persisted across frames.
typedef struct {
    rt_body3d *body_a;
    rt_body3d *body_b;
    void *collider_a;
    void *collider_b;
    double point[3];
    double normal[3];
    double separation;
    int32_t contact_count;
    double points[PH3D_MAX_MANIFOLD_POINTS][3];
    double normals[PH3D_MAX_MANIFOLD_POINTS][3];
    double separations[PH3D_MAX_MANIFOLD_POINTS];
    double relative_speed;
    double normal_impulse;
    double normal_impulse_acc[PH3D_MAX_MANIFOLD_POINTS];
    double tangent_impulse_acc[PH3D_MAX_MANIFOLD_POINTS][2];
    double restitution_bias[PH3D_MAX_MANIFOLD_POINTS];
    int8_t is_trigger;
} rt_contact3d;

typedef struct ph3d_broadphase_entry ph3d_broadphase_entry;

/// @brief Physics world: body/contact/event arrays, joint list, sweep-and-prune
///   broad-phase cache, fixed-step state, solver tuning, and per-frame diagnostics.
struct rt_world3d {
    void *vptr;
    double gravity[3];
    rt_body3d **bodies;
    int32_t body_count;
    int32_t body_capacity;
    rt_contact3d *contacts;
    int32_t contact_count;
    int32_t contact_capacity;
    rt_contact3d *frame_contacts;
    int32_t frame_contact_count;
    int32_t frame_contact_capacity;
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
    void **collision_event_objects;
    int32_t collision_event_object_capacity;
    void **enter_event_objects;
    int32_t enter_event_object_capacity;
    void **stay_event_objects;
    int32_t stay_event_object_capacity;
    void **exit_event_objects;
    int32_t exit_event_object_capacity;
    void **joints;
    int32_t *joint_types;
    int32_t joint_count;
    int32_t joint_capacity;
    ph3d_broadphase_entry *broadphase_entries;
    ph3d_broadphase_entry *broadphase_sort_scratch;
    int32_t broadphase_capacity;
    int32_t broadphase_sort_scratch_capacity;
    uint64_t broadphase_world_revision;
    int32_t query_broadphase_count;
    uint64_t query_broadphase_signature;
    uint64_t query_broadphase_mesh_epoch;
    uint64_t query_broadphase_collider_epoch;
    int8_t query_broadphase_valid;
    void *query_sphere_collider;
    void *query_box_collider;
    /* Configurable query result capacity (RaycastAll/Overlap*): lists still
     * report TotalCount/Truncated; this bounds how many hits are returned.
     * The scratch buffer holds max_query_hits rt_query_hit3d entries (typed
     * void* here because rt_query_hit3d is defined below this struct). */
    void *query_hits_scratch;
    int32_t max_query_hits;
    int64_t broadphase_fallback_count;
    int32_t last_ccd_requested_substeps;
    int32_t last_ccd_substeps;
    int64_t ccd_substep_clamped_count;
    int32_t last_ccd_clamped_body_count;
    int64_t ccd_substep_clamped_body_count;
    int64_t ccd_toi_count; /* total swept time-of-impact clips applied */
    int32_t solver_iterations;
    int32_t position_iterations;
    double contact_beta;
    double restitution_threshold;
    double fixed_step_accumulator;
    double fixed_step_alpha;
    int64_t dropped_fixed_steps;
    int32_t last_solver_island_count;
    int32_t last_solver_active_body_count;
    int32_t last_solver_contact_count;
};

struct ph3d_broadphase_entry {
    rt_body3d *body;
    double min[3];
    double max[3];
};

//===----------------------------------------------------------------------===//
// Posing and query-result structs.
//===----------------------------------------------------------------------===//

/// @brief A world-space placement (position/rotation/scale) used to pose colliders.
typedef struct {
    double position[3];
    double rotation[4];
    double scale[3];
} rt_collider_pose;

/// @brief A raw raycast/sweep hit record (no boxing), filled by query helpers.
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

/// @brief Boxed PhysicsHit3D handle returned to managed code.
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

/// @brief Boxed PhysicsHitList3D handle (sorted, optionally truncated).
typedef struct {
    void *vptr;
    void **items;
    rt_query_hit3d *raw_hits;
    int64_t count;
    int64_t capacity;
    int64_t allocated_count;
    int64_t total_count;
    int8_t truncated;
} rt_physics_hit_list3d_obj;

/// @brief Boxed CollisionEvent3D handle carrying a snapshot of a contact manifold.
typedef struct {
    void *vptr;
    rt_body3d *body_a;
    rt_body3d *body_b;
    void *collider_a;
    void *collider_b;
    double point[3];
    double normal[3];
    double separation;
    int32_t contact_count;
    double points[PH3D_MAX_MANIFOLD_POINTS][3];
    double normals[PH3D_MAX_MANIFOLD_POINTS][3];
    double separations[PH3D_MAX_MANIFOLD_POINTS];
    double relative_speed;
    double normal_impulse;
    int8_t is_trigger;
} rt_collision_event3d_obj;

/// @brief Boxed ContactPoint3D handle (single manifold point).
typedef struct {
    void *vptr;
    double point[3];
    double normal[3];
    double separation;
} rt_contact_point3d_obj;

//===----------------------------------------------------------------------===//
// Cluster-shared support structs.
//===----------------------------------------------------------------------===//

/// @brief One slot in the contact-pair de-dup hash table (open addressing).
typedef struct contact_pair_hash_entry {
    const rt_contact3d *contact;
    uintptr_t hash;
} contact_pair_hash_entry;

/// @brief Scratch arrays for one solver step's union-find islands and the
///   per-island contact-index partition (owned/freed by the solver).
typedef struct {
    int32_t *parent;
    int32_t *active_body;
    int32_t *root_to_island;
    int32_t *body_island;
    int32_t *island_contact_counts;
    int32_t *island_write_offsets;
    int32_t *island_offsets;
    int32_t *contact_indices;
    int32_t island_count;
    int32_t active_body_count;
    int32_t solver_contact_count;
} ph3d_solver_island_batch;

/// @brief One node of the physics mesh BVH (shared by narrow-phase + raycast).
typedef struct rt_physics_mesh_bvh_node {
    float min[3];
    float max[3];
    int32_t left;
    int32_t right;
    int32_t start;
    int32_t count;
} rt_physics_mesh_bvh_node;

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Shared internal helpers exposed for the split rt_physics3d_*.c sibling TUs.
//===----------------------------------------------------------------------===//

// --- Vector / quaternion / pose math (defined in rt_physics3d.c) ---
void vec3_copy(double *dst, const double *src);
void vec3_set(double *dst, double x, double y, double z);
void vec3_sub(const double *a, const double *b, double *out);
void vec3_cross(const double *a, const double *b, double *out);
void vec3_negate(const double *src, double *dst);
double vec3_dot(const double *a, const double *b);
double vec3_len(const double *v);
double vec3_len_sq(const double *v);
double vec3_normalize_in_place(double *v);
double clampd(double v, double lo, double hi);
void quat_identity(double *q);
void quat_conjugate(const double *q, double *out);
void quat_rotate_vec3(const double *q, const double *v, double *out);
void collider_pose_identity(rt_collider_pose *pose);
void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose);
void collider_pose_compose(const rt_collider_pose *parent,
                           const double *child_position,
                           const double *child_rotation,
                           const double *child_scale,
                           rt_collider_pose *out);
void transform_point_from_pose(const rt_collider_pose *pose,
                               const double *local_point,
                               double *world_point);
void transform_point_to_local(const rt_collider_pose *pose,
                              const double *world_point,
                              double *local_point);
void transform_vector_to_local(const rt_collider_pose *pose,
                               const double *world_vec,
                               double *local_vec);
void transform_normal_from_local(const rt_collider_pose *pose,
                                 const double *local_normal,
                                 double *world_normal);
double pose_abs_scale_or_unit(double value);
double pose_abs_scale_or_zero(double value);
int capsule_axis_sample_count(double axis_len, double radius);
void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c);
void body_aabb(const rt_body3d *b, double *mn, double *mx);
int body3d_has_collision_geometry(const rt_body3d *body);
int ph3d_vec3_all_finite(const double v[3]);
double ph3d_clamp_nonnegative_finite(double value, double fallback);
double ph3d_finite_or(double value, double fallback);
void ph3d_vec3_sanitize_state(double *v);

// --- Body dynamics (defined in rt_physics3d.c) ---
void body3d_touch_broadphase(rt_body3d *body);
void body3d_wake_if_dynamic(rt_body3d *b);
void body3d_update_shape_cache_from_collider(rt_body3d *body);
void body3d_contact_velocity(const rt_body3d *b, const double *r, double *out);
double body3d_contact_impulse_denominator(const rt_body3d *b, const double *r, const double *dir);
void body3d_apply_contact_impulse(rt_body3d *b, const double *impulse, const double *r);

// --- World book-keeping / validation (defined in rt_physics3d.c) ---
rt_world3d *world3d_checked(void *obj);
int world3d_checked_increment(int32_t value, int32_t *out);
int world3d_reserve_contacts(rt_world3d *w, int32_t needed);
int world3d_reserve_frame_contacts(rt_world3d *w, int32_t needed);
int world3d_reserve_broadphase_capacity(rt_world3d *w, int32_t needed);
void rt_world3d_test_set_broadphase_alloc_failure(int8_t enabled);
void *physics_hit3d_new(const rt_query_hit3d *src);
void *physics_hit_list3d_new_ex(const rt_query_hit3d *hits,
                                int32_t count,
                                int64_t total_count,
                                int8_t truncated);

// --- Narrow-phase + contact manifolds (defined in rt_physics3d_collision.c) ---
int bodies_can_collide(const rt_body3d *a, const rt_body3d *b);
int query_mask_matches_body(const rt_body3d *body, int64_t mask);
int aabb_overlap_raw(const double *amn, const double *amx, const double *bmn, const double *bmx);
void swept_aabb_from_points(const double *start_min,
                            const double *start_max,
                            const double *delta,
                            double *swept_min,
                            double *swept_max);
void triangle_normal(const double *a, const double *b, const double *c, double *normal);
int build_simple_proxy(const rt_collider_pose *pose, void *collider, rt_body3d *out);
void contact_snapshot_copy(rt_contact3d *dst, const rt_contact3d *src);
void contact3d_init_single_point(rt_contact3d *contact,
                                 const double *point,
                                 const double *normal,
                                 double separation);
void contact3d_expand_aabb_manifold(rt_contact3d *contact, const rt_body3d *a, const rt_body3d *b);
void contact3d_expand_capsule_manifold(rt_contact3d *contact,
                                       const rt_body3d *a,
                                       const rt_body3d *b);
void contact3d_expand_compound_manifold(rt_contact3d *contact,
                                        const rt_body3d *a,
                                        const rt_body3d *b);
int contact3d_expand_obb_manifold(rt_contact3d *contact,
                                  void *a_leaf,
                                  const rt_collider_pose *a_pose,
                                  void *b_leaf,
                                  const rt_collider_pose *b_pose);
int contact_pair_equals(const rt_contact3d *a, const rt_contact3d *b);
contact_pair_hash_entry *contact_pair_table_build(const rt_contact3d *contacts,
                                                  int32_t count,
                                                  int32_t *out_capacity);
int contact_pair_table_contains(const contact_pair_hash_entry *table,
                                int32_t capacity,
                                const rt_contact3d *needle);
int world3d_append_frame_contact_unique(rt_world3d *w, const rt_contact3d *contact);
int world3d_publish_frame_contacts(rt_world3d *w);
int test_collision(const rt_body3d *a,
                   const rt_body3d *b,
                   double *normal,
                   double *depth,
                   double *point,
                   void **leaf_a_out,
                   void **leaf_b_out,
                   rt_collider_pose *leaf_a_pose_out,
                   rt_collider_pose *leaf_b_pose_out);

// --- Solver + broad phase (defined in rt_physics3d_solver.c) ---
int ph3d_broadphase_compare_min_x(const void *lhs, const void *rhs);
int ph3d_broadphase_compare_entries_axis(const ph3d_broadphase_entry *a,
                                         const ph3d_broadphase_entry *b,
                                         int primary);
int world3d_reserve_broadphase_sort_scratch(rt_world3d *w, int32_t needed);
void ph3d_broadphase_sort_entries(ph3d_broadphase_entry *entries,
                                  ph3d_broadphase_entry *scratch,
                                  int32_t count,
                                  int axis);
int ph3d_i32_stack_push(int32_t **items, int32_t *count, int32_t *capacity, int32_t value);
void ph3d_solver_island_batch_free(ph3d_solver_island_batch *batch);
int world3d_build_solver_island_batch(rt_world3d *w, ph3d_solver_island_batch *batch);
void world3d_warm_start_solver_islands(rt_world3d *w, const ph3d_solver_island_batch *batch);
void world3d_solve_velocity_solver_islands(const ph3d_solver_island_batch *batch, rt_world3d *w);
void world3d_solve_position_solver_islands(const ph3d_solver_island_batch *batch, rt_world3d *w);
int world3d_detect_contacts(rt_world3d *w);
void world3d_finalize_contacts(rt_world3d *w);
void world3d_update_sleep(rt_world3d *w, double sub_dt);

// --- Raycast / sweep / overlap (defined in rt_physics3d_query.c) ---
int mesh_physics_bvh_rebuild(rt_mesh3d *mesh);
void transform_local_aabb_to_world(const rt_collider_pose *pose,
                                   const float *mn,
                                   const float *mx,
                                   double *out_min,
                                   double *out_max);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_ENABLE_GRAPHICS */
