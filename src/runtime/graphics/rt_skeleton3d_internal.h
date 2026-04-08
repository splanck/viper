//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_skeleton3d_internal.h
// Purpose: Internal shared animation/runtime structs for skeletal animation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_skeleton3d.h"

#ifdef VIPER_ENABLE_GRAPHICS

#define VGFX3D_MAX_BONES 128
#define RT_ANIM_BLEND3D_MAX_STATES 8

typedef struct {
    char name[64];
    int32_t parent_index;
    float bind_pose_local[16];
    float inverse_bind[16];
} vgfx3d_bone_t;

typedef struct {
    float time;
    float position[3];
    float rotation[4];
    float scale_xyz[3];
} vgfx3d_keyframe_t;

typedef struct {
    int32_t bone_index;
    vgfx3d_keyframe_t *keyframes;
    int32_t keyframe_count;
    int32_t keyframe_capacity;
} vgfx3d_anim_channel_t;

typedef struct rt_skeleton3d {
    void *vptr;
    vgfx3d_bone_t *bones;
    int32_t bone_count;
} rt_skeleton3d;

typedef struct rt_animation3d {
    void *vptr;
    char name[64];
    vgfx3d_anim_channel_t *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    float duration;
    int8_t looping;
} rt_animation3d;

typedef struct rt_anim_player3d {
    void *vptr;
    rt_skeleton3d *skeleton;
    rt_animation3d *current;
    rt_animation3d *crossfade_from;
    float current_time;
    float crossfade_time;
    float crossfade_duration;
    float crossfade_from_time;
    float speed;
    int8_t playing;
    int8_t loop_override_enabled;
    int8_t loop_override_value;
    float *bone_palette;
    float *prev_bone_palette;
    float *motion_palette_snapshot;
    float *local_transforms;
    float *globals_buf;
    int64_t last_motion_frame;
    int8_t has_prev_motion_palette;
} rt_anim_player3d;

typedef struct anim_blend_state_t {
    char name[64];
    rt_animation3d *animation;
    float weight;
    float anim_time;
    float speed;
    int8_t looping;
} anim_blend_state_t;

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

#endif
