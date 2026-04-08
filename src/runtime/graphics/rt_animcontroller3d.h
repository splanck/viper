//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_animcontroller3d.h
// Purpose: High-level 3D animation state controller built on skeletal clips.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_anim_controller3d_new(void *skeleton);

int64_t rt_anim_controller3d_add_state(void *controller, rt_string name, void *animation);
int8_t rt_anim_controller3d_add_transition(
    void *controller, rt_string from_state, rt_string to_state, double blend_seconds);

int8_t rt_anim_controller3d_play(void *controller, rt_string state_name);
int8_t rt_anim_controller3d_crossfade(void *controller, rt_string state_name, double blend_seconds);
void rt_anim_controller3d_stop(void *controller);
void rt_anim_controller3d_update(void *controller, double delta_time);

rt_string rt_anim_controller3d_get_current_state(void *controller);
rt_string rt_anim_controller3d_get_previous_state(void *controller);
int8_t rt_anim_controller3d_get_is_transitioning(void *controller);
int64_t rt_anim_controller3d_get_state_count(void *controller);

void rt_anim_controller3d_set_state_speed(void *controller, rt_string state_name, double speed);
void rt_anim_controller3d_set_state_looping(void *controller, rt_string state_name, int8_t loop);

void rt_anim_controller3d_add_event(
    void *controller, rt_string state_name, double time_seconds, rt_string event_name);
rt_string rt_anim_controller3d_poll_event(void *controller);

void rt_anim_controller3d_set_root_motion_bone(void *controller, int64_t bone_index);
void *rt_anim_controller3d_get_root_motion_delta(void *controller);
void *rt_anim_controller3d_consume_root_motion(void *controller);

void rt_anim_controller3d_set_layer_weight(void *controller, int64_t layer, double weight);
void rt_anim_controller3d_set_layer_mask(void *controller, int64_t layer, int64_t root_bone);
int8_t rt_anim_controller3d_play_layer(void *controller, int64_t layer, rt_string state_name);
int8_t rt_anim_controller3d_crossfade_layer(
    void *controller, int64_t layer, rt_string state_name, double blend_seconds);
void rt_anim_controller3d_stop_layer(void *controller, int64_t layer);

void *rt_anim_controller3d_get_bone_matrix(void *controller, int64_t bone_index);

/* Runtime integration helpers used by Scene3D bindings. */
const float *rt_anim_controller3d_get_final_palette_data(void *controller, int32_t *bone_count);
const float *rt_anim_controller3d_get_previous_palette_data(void *controller, int32_t *bone_count);

#ifdef __cplusplus
}
#endif
