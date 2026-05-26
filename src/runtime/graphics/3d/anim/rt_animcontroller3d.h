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

/// @brief Create an animation state controller bound to a Skeleton3D.
void *rt_anim_controller3d_new(void *skeleton);

/// @brief Register a named animation state. Returns the new state index, -1 on duplicate name.
int64_t rt_anim_controller3d_add_state(void *controller, rt_string name, void *animation);
/// @brief Define a transition between two named states with the given blend duration in seconds.
int8_t rt_anim_controller3d_add_transition(
    void *controller, rt_string from_state, rt_string to_state, double blend_seconds);

/// @brief Hard-cut to the named state (no blending). Returns 1 on success, 0 if state unknown.
int8_t rt_anim_controller3d_play(void *controller, rt_string state_name);
/// @brief Smoothly crossfade into the named state over @p blend_seconds.
int8_t rt_anim_controller3d_crossfade(void *controller, rt_string state_name, double blend_seconds);
/// @brief Stop the controller (subsequent draws use the bind pose).
void rt_anim_controller3d_stop(void *controller);
/// @brief Advance the controller's clock by @p delta_time seconds (drives blends, events, root motion).
void rt_anim_controller3d_update(void *controller, double delta_time);

/// @brief Get the name of the currently-active state.
rt_string rt_anim_controller3d_get_current_state(void *controller);
/// @brief Get the name of the state we're transitioning from (empty string if not blending).
rt_string rt_anim_controller3d_get_previous_state(void *controller);
/// @brief True if the controller is currently in a crossfade.
int8_t rt_anim_controller3d_get_is_transitioning(void *controller);
/// @brief Number of registered states.
int64_t rt_anim_controller3d_get_state_count(void *controller);

/// @brief Override the per-state playback speed multiplier.
void rt_anim_controller3d_set_state_speed(void *controller, rt_string state_name, double speed);
/// @brief Override whether a state loops at the end of its clip duration.
void rt_anim_controller3d_set_state_looping(void *controller, rt_string state_name, int8_t loop);

/// @brief Schedule an event to fire when @p state_name reaches @p time_seconds during playback.
void rt_anim_controller3d_add_event(
    void *controller, rt_string state_name, double time_seconds, rt_string event_name);
/// @brief Pop the next pending event name (empty string when none queued).
rt_string rt_anim_controller3d_poll_event(void *controller);

/// @brief Designate which bone provides root motion (-1 disables root motion).
void rt_anim_controller3d_set_root_motion_bone(void *controller, int64_t bone_index);
/// @brief Peek at the accumulated root-motion translation since the last consume.
void *rt_anim_controller3d_get_root_motion_delta(void *controller);
/// @brief Consume the accumulated root-motion delta and reset the accumulator.
void *rt_anim_controller3d_consume_root_motion(void *controller);

/// @brief Set the blend weight (0..1) for an animation layer (layer 0 = base).
void rt_anim_controller3d_set_layer_weight(void *controller, int64_t layer, double weight);
/// @brief Restrict layer to bones below @p root_bone in the skeleton (additive masking).
void rt_anim_controller3d_set_layer_mask(void *controller, int64_t layer, int64_t root_bone);
/// @brief Hard-cut a specific layer to the named state.
int8_t rt_anim_controller3d_play_layer(void *controller, int64_t layer, rt_string state_name);
/// @brief Crossfade a specific layer to the named state over @p blend_seconds.
int8_t rt_anim_controller3d_crossfade_layer(
    void *controller, int64_t layer, rt_string state_name, double blend_seconds);
/// @brief Stop the named layer (its weight will fall to 0 if not currently blending).
void rt_anim_controller3d_stop_layer(void *controller, int64_t layer);

/// @brief Get the world-space matrix for bone @p bone_index after all layers + transitions are evaluated.
void *rt_anim_controller3d_get_bone_matrix(void *controller, int64_t bone_index);

/* Runtime integration helpers used by Scene3D bindings. */
/// @brief Borrow the flat float array of bone matrices for GPU upload (length = bone_count*16).
const float *rt_anim_controller3d_get_final_palette_data(void *controller, int32_t *bone_count);
/// @brief Borrow the previous frame's bone palette (used for motion vectors / TAA).
const float *rt_anim_controller3d_get_previous_palette_data(void *controller, int32_t *bone_count);
/// @brief Internal runtime helper: consume the accumulated root-motion rotation delta and reset it.
void *rt_anim_controller3d_consume_root_motion_rotation(void *controller);

#ifdef __cplusplus
}
#endif
