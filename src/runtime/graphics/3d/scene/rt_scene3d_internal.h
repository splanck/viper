//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene3d_internal.h
// Purpose: Internal shared structs for Scene3D / SceneNode3D implementation.
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
} rt_node_anim_channel3d;

typedef struct rt_node_animation3d {
    void *vptr;
    rt_string name;
    double duration;
    rt_node_anim_channel3d *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    int8_t looping;
} rt_node_animation3d;

typedef struct rt_node_animator3d {
    void *vptr;
    rt_node_animation3d **animations;
    int32_t animation_count;
    int32_t current_animation;
    double time;
    double speed;
    int8_t playing;
    struct rt_scene_node3d *root;
} rt_node_animator3d;

typedef struct rt_scene_node3d {
    void *vptr;

    double position[3];
    double rotation[4];
    double scale_xyz[3];

    double world_matrix[16];
    int8_t world_dirty;

    struct rt_scene_node3d *parent;
    struct rt_scene_node3d **children;
    int32_t child_count;
    int32_t child_capacity;

    void *mesh;
    void *material;
    void *light;
    void *bound_body;
    void *bound_animator;
    void *bound_node_animator;
    int32_t sync_mode;

    int8_t visible;
    rt_string name;

    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;

    struct {
        double distance;
        void *mesh;
    } *lod_levels;

    int32_t lod_count;
    int32_t lod_capacity;
} rt_scene_node3d;

typedef struct {
    void *vptr;
    rt_scene_node3d *root;
    int32_t node_count;
    int32_t last_culled_count;
} rt_scene3d;

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a named node-animation clip of the given duration (seconds).
void *rt_node_animation3d_new(rt_string name, double duration);
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
/// @brief Build a node animator that blends/sequences the given clips.
void *rt_node_animator3d_new_from_clips(void **clips, int64_t clip_count);
/// @brief Attach a node animator to a scene node so it drives its transform.
void rt_scene_node3d_bind_node_animator(void *obj, void *animator);
/// @brief Attach a Light3D to a scene node.
void rt_scene_node3d_set_light(void *obj, void *light);
/// @brief Get the Light3D attached to a scene node (NULL if none).
void *rt_scene_node3d_get_light(void *obj);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_ENABLE_GRAPHICS */
