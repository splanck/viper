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

/// @brief Skeleton3D payload: the bone array (topological order) and a `frozen` flag set
///   once inverse-bind matrices are computed (bones immutable thereafter).
typedef struct rt_skeleton3d {
    void *vptr;
    vgfx3d_bone_t *bones;
    int32_t bone_count;
    int8_t frozen;
} rt_skeleton3d;

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
    int64_t last_motion_frame;
    int8_t has_prev_motion_palette;
} rt_anim_blend3d;

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

#endif
