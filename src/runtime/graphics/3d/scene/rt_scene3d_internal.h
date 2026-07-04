//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_internal.h
// Purpose: Internal shared structs for Scene3D / SceneNode3D implementation,
//   including node-animation (glTF-style channel) types. Private to the scene
//   runtime — not part of the public Graphics3D ABI.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_scene3d.h"
#include <stdint.h>

#ifdef VIPER_ENABLE_GRAPHICS

#define RT_NODE_ANIM_PATH_TRANSLATION 0
#define RT_NODE_ANIM_PATH_ROTATION 1
#define RT_NODE_ANIM_PATH_SCALE 2
#define RT_NODE_ANIM_PATH_WEIGHTS 3

#define RT_NODE_ANIM_INTERP_LINEAR 0
#define RT_NODE_ANIM_INTERP_STEP 1
#define RT_NODE_ANIM_INTERP_CUBICSPLINE 2

#define NODE_INIT_CHILDREN 4
#define SCENE3D_ABS_MAX 1000000000000.0
#define SCENE3D_FLOAT_ABS_MAX 3.40282346638528859812e38
#define SCENE3D_PI 3.14159265358979323846

/// @brief One node-animation channel (glTF-style): the target node name, the animated
///   path (TRS/weights), interpolation mode, and the keyframe times/values plus optional
///   cubic-spline in/out tangents.
typedef struct {
    rt_string target_name;
    int32_t path;
    int32_t interpolation;
    int32_t key_count;
    int32_t value_width;
    double *times;
    float *values;
    float *in_tangents;
    float *out_tangents;
    int32_t target_node_index;
    struct rt_scene_node3d *cached_root;
    struct rt_scene_node3d *cached_target;
} rt_node_anim_channel3d;

/// @brief A node-animation clip: a named, fixed-duration set of channels plus a loop flag.
typedef struct rt_node_animation3d {
    void *vptr;
    rt_string name;
    double duration;
    rt_node_anim_channel3d *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    int8_t looping;
} rt_node_animation3d;

/// @brief A node animator: a set of clips plus playback cursor (current clip, time, speed,
///   playing flag) rooted at the scene node whose subtree it drives.
typedef struct rt_node_animator3d {
    void *vptr;
    rt_node_animation3d **animations;
    int32_t animation_count;
    int32_t animation_capacity;
    int32_t current_animation;
    double time;
    double speed;
    int8_t playing;
    struct rt_scene_node3d *root;
    struct rt_scene_node3d **cached_targets;
    int32_t cached_target_capacity;
    int32_t cached_clip_index;
    struct rt_scene_node3d *cached_root;
    float *sample_scratch;
    int32_t sample_scratch_capacity;
    struct rt_scene_node3d **traversal_stack;
    size_t traversal_stack_capacity;
} rt_node_animator3d;

struct rt_scene3d;

typedef struct {
    struct rt_scene_node3d *node;
    double world_min[3];
    double world_max[3];
    int32_t traversal_order;
    int8_t cullable;
    uint32_t world_revision;
    uint32_t geometry_revision;
} rt_scene3d_spatial_entry;

typedef struct {
    double world_min[3];
    double world_max[3];
    int32_t left;
    int32_t right;
    int32_t start;
    int32_t count;
    int32_t cullable_count;
    int8_t leaf;
} rt_scene3d_spatial_bvh_node;

typedef struct {
    rt_scene3d_spatial_entry *entries;
    int32_t count;
    int32_t capacity;
    int32_t *entry_indices;
    int32_t entry_index_capacity;
    rt_scene3d_spatial_bvh_node *nodes;
    int32_t node_count;
    int32_t node_capacity;
    int32_t root_node;
    int8_t dirty;
    int8_t topology_dirty;
    int8_t valid;
    uint32_t build_count;
    uint32_t refit_count;
    int32_t last_candidate_count;
    int32_t last_prefiltered_count;
} rt_scene3d_spatial_index;

typedef struct {
    rt_string name;
    double world_min[3];
    double world_max[3];
} rt_scene3d_visibility_zone;

typedef struct {
    int32_t from_zone;
    int32_t to_zone;
} rt_scene3d_visibility_portal;

/// @brief Per-camera/view LOD hysteresis state cached on a SceneNode3D.
/// @details Nodes keep a small fixed cache instead of one global LOD/impostor selection so
///   alternating cameras do not contaminate each other's hysteresis and cause mesh popping.
typedef struct rt_scene3d_lod_view_state {
    uintptr_t view_key;
    uint32_t last_used;
    int32_t lod_selected_index;
    int8_t lod_selection_valid;
    int8_t impostor_selected;
} rt_scene3d_lod_view_state;

#define RT_SCENE3D_LOD_VIEW_STATE_COUNT 4

/// @brief SceneNode3D payload: local TRS, lazily-recomputed world matrix with dirty flag,
///   parent/children links, bound mesh/material/light/body/animator(s) and sync mode,
///   visibility, name, cached subtree AABB/bounding-sphere, LOD mesh levels,
///   optional screen-error LOD selection, and generated impostor proxy assets.
typedef struct rt_scene_node3d {
    void *vptr;

    double position[3];
    double rotation[4];
    double scale_xyz[3];

    double world_matrix[16];
    int8_t world_dirty;
    uint32_t world_revision;
    uint32_t parent_world_revision_seen;

    struct rt_scene_node3d *parent;
    struct rt_scene3d *owner_scene;
    struct rt_scene_node3d **children;
    int32_t child_count;
    int32_t child_capacity;

    void *mesh;
    void *material;
    void *light;
    void *bound_body;
    void *bound_animator;
    void *bound_node_animator;
    int8_t bound_animator_scene_update;
    int32_t sync_mode;
    int32_t import_index;

    /* Bone socket: while socket_animator is set, SyncBindings drives this
     * node's world transform from parentWorld x bonePose x offset. The node
     * must be parented under the node that renders the skinned model (the
     * bone pose is model-space). Socket sync supersedes body sync. */
    void *socket_animator; /* retained AnimController3D or NULL */
    int32_t socket_bone;
    double socket_offset_pos[3];
    double socket_offset_quat[4];

    int8_t visible;
    rt_string name;

    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;
    uint32_t mesh_bounds_revision;

    struct {
        double distance;
        void *mesh;
    } *lod_levels;

    int32_t lod_count;
    int32_t lod_capacity;

    int8_t auto_lod_enabled;
    double auto_lod_screen_error_px;
    int32_t lod_selected_index;
    int8_t lod_selection_valid;
    uint32_t lod_view_state_clock;
    rt_scene3d_lod_view_state lod_view_states[RT_SCENE3D_LOD_VIEW_STATE_COUNT];

    int8_t has_impostor;
    int8_t impostor_selected;
    double impostor_distance;
    void *impostor_pixels;
    void *impostor_mesh;
    void *impostor_material;
} rt_scene_node3d;

/// @brief Scene3D payload: the implicit root node, total node count, and the
///   frustum-culled count from the most recent draw (a perf metric).
typedef struct rt_scene3d {
    void *vptr;
    rt_scene_node3d *root;
    int32_t node_count;
    int32_t last_culled_count;
    int32_t last_visible_node_count;
    int32_t last_pvs_culled_count;
    int8_t use_spatial_index;
    rt_scene3d_spatial_index spatial_index;
    rt_scene3d_visibility_zone *visibility_zones;
    int32_t visibility_zone_count;
    int32_t visibility_zone_capacity;
    rt_scene3d_visibility_portal *visibility_portals;
    int32_t visibility_portal_count;
    int32_t visibility_portal_capacity;
    rt_scene3d_spatial_entry **query_candidates;
    int32_t query_candidate_capacity;
} rt_scene3d;

/// @brief Bound a private dynamic-array count by the pointer and recorded capacity.
static inline int32_t scene3d_clamped_array_count(const void *array,
                                                  int32_t count,
                                                  int32_t capacity) {
    if (!array || count <= 0 || capacity <= 0)
        return 0;
    return count < capacity ? count : capacity;
}

/// @brief Safe number of children that may be read directly from a SceneNode3D.
static inline int32_t scene3d_node_child_count(const rt_scene_node3d *node) {
    return node ? scene3d_clamped_array_count(node->children,
                                              node->child_count,
                                              node->child_capacity)
                : 0;
}

/// @brief Safe number of LOD entries that may be read directly from a SceneNode3D.
static inline int32_t scene3d_node_lod_count(const rt_scene_node3d *node) {
    return node ? scene3d_clamped_array_count(node->lod_levels, node->lod_count, node->lod_capacity)
                : 0;
}

/// @brief Safe number of animation channels that may be read directly from a node clip.
static inline int32_t scene3d_node_animation_channel_count(const rt_node_animation3d *animation) {
    return animation ? scene3d_clamped_array_count(animation->channels,
                                                   animation->channel_count,
                                                   animation->channel_capacity)
                     : 0;
}

/// @brief Safe number of clips that may be read directly from a node animator.
static inline int32_t scene3d_node_animator_clip_count(const rt_node_animator3d *animator) {
    return animator ? scene3d_clamped_array_count(animator->animations,
                                                  animator->animation_count,
                                                  animator->animation_capacity)
                    : 0;
}

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Add a keyframe channel targeting @p target_name's @p path property.
/// @details @p interpolation selects step/linear; keys are @p key_count tuples
///          of @p value_width floats sampled at @p times. @return channel index.
int64_t rt_node_animation3d_add_channel(void *obj,
                                        rt_string target_name,
                                        int64_t path,
                                        int64_t interpolation,
                                        int64_t key_count,
                                        int64_t value_width,
                                        const double *times,
                                        const float *values);
/// @brief Add a cubic-spline keyframe channel (with in/out tangents) to a clip.
/// @return The new channel's index within the animation.
int64_t rt_node_animation3d_add_cubic_channel(void *obj,
                                              rt_string target_name,
                                              int64_t path,
                                              int64_t key_count,
                                              int64_t value_width,
                                              const double *times,
                                              const float *values,
                                              const float *in_tangents,
                                              const float *out_tangents);
/// @brief Internal importer hook: bind a channel to a stable source node index.
void rt_node_animation3d_set_channel_target_node_index(void *obj,
                                                       int64_t channel_index,
                                                       int64_t node_index);
/// @brief Build a node animator that blends/sequences the given clips.
void *rt_node_animator3d_new_from_clips(void **clips, int64_t clip_count);
/// @brief Enable/disable Scene3D.SyncBindings auto-update for the skeletal controller binding.
void rt_scene_node3d_set_animator_scene_update(void *obj, int8_t enabled);
/// @brief Attach a Light3D to a scene node.
void rt_scene_node3d_set_light(void *obj, void *light);
/// @brief Get the Light3D attached to a scene node (NULL if none).
void *rt_scene_node3d_get_light(void *obj);

// Shared internal helpers exposed for the split rt_scene3d_*.c sibling TUs.
rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target);
void mark_dirty(rt_scene_node3d *node);
void mat4d_identity(double *out);
void node_animator_update(rt_node_animator3d *animator, double dt);
int node_contains(const rt_scene_node3d *root, const rt_scene_node3d *target);
void scene3d_quat_normalize_local(double *q);
void recompute_world_matrix(rt_scene_node3d *node);
void scene_bounds_reset(float out_min[3], float out_max[3]);
void scene_mesh_bounds(rt_mesh3d *mesh, float out_min[3], float out_max[3], float *out_radius);
/// @brief Combine the geometry revisions of a node's drawable meshes into one dirty signature.
uint32_t scene_node_geometry_revision_signature(rt_scene_node3d *node);
/// @brief Refresh a node's cached local mesh bounds if its primary mesh geometry changed.
void scene_node_refresh_mesh_bounds(rt_scene_node3d *node);
void scene_node_assign_owner_recursive(rt_scene_node3d *node, rt_scene3d *owner);
void scene_node_clear_owner_recursive(rt_scene_node3d *node, rt_scene3d *owner);
int scene_node_collect_subtree_bounds(rt_scene_node3d *node,
                                      const double *node_to_root,
                                      float out_min[3],
                                      float out_max[3]);
void scene_node_get_world_position(rt_scene_node3d *node, double *x, double *y, double *z);
void scene_node_get_world_rotation(rt_scene_node3d *node, double *out_quat);
rt_scene_node3d *scene_node3d_checked(void *obj);
double scene3d_clamp_abs_or(double value, double fallback);
void scene3d_canonicalize_aabb_d(double mn[3], double mx[3]);
double scene3d_distance_or_zero(double value);
void scene3d_mark_spatial_dirty(rt_scene3d *scene);
int scene3d_normalize_vec3d(double v[3]);
void scene3d_release_ref(void **slot);
double scene3d_scale_or_unit(double value);
int scene3d_grow_stack_storage(void **buffer, size_t *capacity, size_t elem_size);
int scene3d_grow_array_i32(void **buffer,
                           int32_t *capacity,
                           int32_t needed,
                           int32_t min_capacity,
                           size_t elem_size,
                           int zero_new);
int scene_node_stack_push(rt_scene_node3d ***stack,
                          size_t *count,
                          size_t *capacity,
                          rt_scene_node3d *node);

typedef struct {
    rt_scene_node3d *node;
    void *inherited_animator;
} scene_index_build_stack_item_t;

typedef struct {
    rt_scene3d_spatial_entry **items;
    int32_t count;
    int32_t capacity;
} scene3d_spatial_candidate_list_t;

void scene_bounds_include_point_d(double bounds_min[3],
                                  double bounds_max[3],
                                  const double point[3]);
void scene_bounds_reset_d(double out_min[3], double out_max[3]);
int scene_index_build_stack_push(scene_index_build_stack_item_t **stack,
                                 size_t *count,
                                 size_t *capacity,
                                 rt_scene_node3d *node,
                                 void *inherited_animator);
rt_scene3d *scene3d_checked(void *obj);
int scene3d_read_vec3d(void *obj, double out[3], const char *trap_message);
int scene3d_node_world_mesh_aabb(rt_scene_node3d *node,
                                 double world_min[3],
                                 double world_max[3]);
int scene3d_aabb_intersects_aabb(const double a_min[3],
                                 const double a_max[3],
                                 const double b_min[3],
                                 const double b_max[3]);
int scene3d_aabb_intersects_sphere(const double aabb_min[3],
                                   const double aabb_max[3],
                                   const double center[3],
                                   double radius);
int scene3d_ray_intersects_aabb(const double origin[3],
                                const double direction[3],
                                const double aabb_min[3],
                                const double aabb_max[3],
                                double max_distance,
                                double *out_t);
int scene3d_ray_sweep_bounds(const double origin[3],
                             const double direction[3],
                             double max_distance,
                             double out_min[3],
                             double out_max[3]);
float scene3d_float_or_zero(double value);
int scene3d_transform_aabb_d(const float obj_min[3],
                             const float obj_max[3],
                             const double world_matrix[16],
                             double out_min[3],
                             double out_max[3]);
int scene3d_spatial_collect_aabb(rt_scene3d *scene,
                                 const double query_min[3],
                                 const double query_max[3],
                                 scene3d_spatial_candidate_list_t *out,
                                 int count_cullable_prefilter);
int scene3d_spatial_collect_all(rt_scene3d *scene, scene3d_spatial_candidate_list_t *out);
int scene3d_node_world_draw_union_aabb(rt_scene_node3d *node,
                                       void *effective_animator,
                                       double world_min[3],
                                       double world_max[3],
                                       double *out_radius);
int scene3d_mesh_has_dynamic_deformation(rt_mesh3d *mesh, void *effective_animator);
double scene3d_mesh_dynamic_bound_pad(rt_mesh3d *mesh,
                                      void *effective_animator,
                                      double base_radius);

typedef struct {
    int32_t *items;
    int32_t count;
    int32_t capacity;
} scene3d_spatial_node_stack_t;

void *scene3d_effective_animator(rt_scene_node3d *node);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_ENABLE_GRAPHICS */
