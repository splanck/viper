//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_skeleton3d_internal.h
// Purpose: Internal shared animation/runtime structs for skeletal animation,
//   plus the internal keyed-skinning draw fast path. Layouts are private to the
//   anim runtime — not part of the public Graphics3D ABI.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_skeleton3d.h"

#ifdef VIPER_ENABLE_GRAPHICS

#define VGFX3D_MAX_BONES 256
#define RT_ANIM_BLEND3D_MAX_STATES 8
#define RT_ANIMATION3D_MAX_CHANNELS VGFX3D_MAX_BONES
#define RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL 65536

/// @brief One bone: name, parent index (-1 = root), local bind-pose matrix, and the
///   precomputed inverse bind-pose used to build the skinning palette.
typedef struct {
    char name[64];
    int32_t parent_index;
    float bind_pose_local[16];
    float inverse_bind[16];
} vgfx3d_bone_t;

/// @brief One animation keyframe: time plus position/rotation/scale samples, each gated
///   by a presence mask so unset channels fall through to neighboring keys.
typedef struct {
    float time;
    float position[3];
    float rotation[4];
    float scale_xyz[3];
    uint8_t position_mask;
    uint8_t rotation_mask;
    uint8_t scale_mask;
} vgfx3d_keyframe_t;

/// @brief A per-bone animation channel: the target bone index and its growable,
///   time-sorted keyframe array.
typedef struct {
    int32_t bone_index;
    vgfx3d_keyframe_t *keyframes;
    int32_t keyframe_count;
    int32_t keyframe_capacity;
} vgfx3d_anim_channel_t;

/// @brief Skeleton3D payload: the bone array and a `frozen` flag set once pose buffers exist
///   (bones immutable thereafter). Runtime global-pose builders tolerate non-topological order.
/// @brief External-name alias: "a rig calling this bone `external` means my
///   bone `local`". Consulted by animation retargeting before role inference.
typedef struct {
    char external[64];
    char local[64];
} rt_skeleton3d_alias;

typedef struct rt_skeleton3d {
    void *vptr;
    vgfx3d_bone_t *bones;
    int32_t bone_count;
    int32_t bone_capacity;
    int8_t frozen;
    rt_skeleton3d_alias *aliases;
    int32_t alias_count;
    int32_t alias_capacity;
} rt_skeleton3d;

/// @brief Resolve an external bone name through a skeleton's alias table
///        (defined in rt_skeleton3d_skeleton.inc). NULL when unmapped.
const char *skeleton3d_alias_lookup(const rt_skeleton3d *s, const char *external);

/// @brief Animation3D payload: a named clip holding per-bone channels, total duration,
///   and the loop flag.
typedef struct rt_animation3d {
    void *vptr;
    char name[64];
    vgfx3d_anim_channel_t *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    float duration;
    int8_t looping;
} rt_animation3d;

/// @brief AnimPlayer3D payload: the skeleton, current and crossfade-source clips with
///   their timers/speeds, loop overrides, and the current/previous/motion bone palettes
///   plus scratch transform buffers used while evaluating a pose.
typedef struct rt_anim_player3d {
    void *vptr;
    rt_skeleton3d *skeleton;
    rt_animation3d *current;
    rt_animation3d *crossfade_from;
    float current_time;
    float crossfade_time;
    float crossfade_duration;
    float crossfade_from_time;
    float crossfade_from_speed;
    float speed;
    int8_t playing;
    int8_t loop_override_enabled;
    int8_t loop_override_value;
    int8_t crossfade_from_looping;
    float *bone_palette;
    float *prev_bone_palette;
    float *motion_palette_snapshot;
    float *local_transforms;
    float *globals_buf;
    int32_t pose_capacity;
    int64_t last_motion_frame;
    int8_t has_prev_motion_palette;
} rt_anim_player3d;

/// @brief One AnimBlend3D state: a named clip with its own weight, playback time, speed,
///   and loop flag — all states evaluated and weighted together each update.
typedef struct anim_blend_state_t {
    char name[64];
    rt_animation3d *animation;
    float weight;
    float anim_time;
    float speed;
    int8_t looping;
} anim_blend_state_t;

/// @brief AnimBlend3D payload: the skeleton, a fixed array of weighted blend states, and
///   the current/previous/motion bone palettes plus scratch transform buffers.
typedef struct rt_anim_blend3d {
    void *vptr;
    rt_skeleton3d *skeleton;
    anim_blend_state_t states[RT_ANIM_BLEND3D_MAX_STATES];
    int32_t state_count;
    float *bone_palette;
    float *prev_bone_palette;
    float *motion_palette_snapshot;
    float *local_transforms;
    float *temp_state_local;
    float *globals_buf;
    int32_t pose_capacity;
    int64_t last_motion_frame;
    int8_t has_prev_motion_palette;
} rt_anim_blend3d;

/// @brief Bound a private dynamic-array count by its backing pointer and capacity.
static inline int32_t skeleton3d_clamped_array_count(const void *array,
                                                     int32_t count,
                                                     int32_t capacity) {
    if (!array || count <= 0 || capacity <= 0)
        return 0;
    return count < capacity ? count : capacity;
}

/// @brief Safe number of bones that may be read directly from a Skeleton3D.
static inline int32_t skeleton3d_safe_bone_count(const rt_skeleton3d *skeleton) {
    int32_t count = skeleton ? skeleton3d_clamped_array_count(skeleton->bones,
                                                              skeleton->bone_count,
                                                              skeleton->bone_capacity)
                             : 0;
    return count < VGFX3D_MAX_BONES ? count : VGFX3D_MAX_BONES;
}

/// @brief Return a valid parent index for @p bone, or -1 when the stored parent is unusable.
static inline int32_t skeleton3d_valid_parent_index(const rt_skeleton3d *skeleton,
                                                    int32_t bone,
                                                    int32_t bone_count) {
    int32_t parent;
    if (!skeleton || !skeleton->bones || bone < 0 || bone >= bone_count)
        return -1;
    parent = skeleton->bones[bone].parent_index;
    if (parent < 0 || parent >= bone_count || parent == bone)
        return -1;
    return parent;
}

/// @brief Safe number of channels that may be read directly from an Animation3D.
static inline int32_t animation3d_safe_channel_count(const rt_animation3d *animation) {
    int32_t count = animation ? skeleton3d_clamped_array_count(animation->channels,
                                                               animation->channel_count,
                                                               animation->channel_capacity)
                              : 0;
    return count < RT_ANIMATION3D_MAX_CHANNELS ? count : RT_ANIMATION3D_MAX_CHANNELS;
}

/// @brief Safe number of keyframes that may be read directly from one animation channel.
static inline int32_t animation3d_safe_keyframe_count(const vgfx3d_anim_channel_t *channel) {
    int32_t count = channel ? skeleton3d_clamped_array_count(channel->keyframes,
                                                             channel->keyframe_count,
                                                             channel->keyframe_capacity)
                            : 0;
    return count < RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL
               ? count
               : RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL;
}

/// @brief Whether every channel in @p animation can address a bone in @p skeleton.
static inline int8_t animation3d_channels_fit_skeleton(const rt_animation3d *animation,
                                                       const rt_skeleton3d *skeleton) {
    if (!animation || !skeleton)
        return 0;
    int32_t bone_count = skeleton3d_safe_bone_count(skeleton);
    int32_t channel_count = animation3d_safe_channel_count(animation);
    for (int32_t i = 0; i < channel_count; i++) {
        int32_t bone = animation->channels[i].bone_index;
        if (bone < 0 || bone >= bone_count)
            return 0;
    }
    return 1;
}

/// @brief Safe number of fixed-capacity blend states that may be read from AnimBlend3D.
static inline int32_t animblend3d_safe_state_count(const rt_anim_blend3d *blend) {
    int32_t limit;
    int32_t count = 0;
    if (!blend || blend->state_count <= 0)
        return 0;
    limit = blend->state_count < RT_ANIM_BLEND3D_MAX_STATES ? blend->state_count
                                                            : RT_ANIM_BLEND3D_MAX_STATES;
    while (count < limit && blend->states[count].animation)
        count++;
    return count;
}

/// @brief Draw a skinned mesh using an explicit model matrix and a caller-
///        supplied keyed bone palette (internal fast path used by the
///        animation player to avoid recomputing the pose per draw).
void rt_canvas3d_draw_mesh_matrix_skinned_keyed(void *canvas,
                                                void *mesh,
                                                const double *model_matrix,
                                                void *material,
                                                const void *motion_key,
                                                const float *bone_palette,
                                                const float *prev_bone_palette,
                                                int32_t bone_count);
/// @brief Bounds-aware variant of the keyed skinned draw path.
/// @details Used by Scene3D after it has expanded local bounds for skeletal motion. The explicit
///   bounds are forwarded to Canvas3D for frustum/shadow fitting while CPU skinning and GPU
///   skinning selection remain unchanged.
void rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(void *canvas,
                                                       void *mesh,
                                                       const double *model_matrix,
                                                       void *material,
                                                       const void *motion_key,
                                                       const float *bone_palette,
                                                       const float *prev_bone_palette,
                                                       int32_t bone_count,
                                                       const float *local_bounds_min,
                                                       const float *local_bounds_max,
                                                       int8_t conservative_bounds,
                                                       int8_t disable_occlusion);

/// @brief Build global bone matrices from contiguous local matrices, tolerating non-topological
///        parent order and breaking parent cycles by treating the cycle edge as a root.
void skeleton3d_compute_globals_from_locals(const rt_skeleton3d *skeleton,
                                            const float *locals,
                                            float *globals,
                                            int32_t bone_count);

#endif
