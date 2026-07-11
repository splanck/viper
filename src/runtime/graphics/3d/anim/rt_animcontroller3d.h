//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_animcontroller3d.h
// Purpose: Public API for AnimController3D — a high-level skeletal animation
//   state machine layered over AnimPlayer3D. Exposes named states with
//   per-state speed/looping, blended transitions, time-scheduled events,
//   root-motion extraction, and masked overlay layers.
//
// Key invariants:
//   - All entry points take opaque GC-managed controller handles (void *).
//   - Layer 0 is the base layer; higher layers are masked replace or additive overlays.
//   - Times are in seconds, blend weights in 0..1; bone_index -1 disables
//     root motion.
//
// Links: rt_animcontroller3d.c (implementation),
//   rt_skeleton3d.h (underlying AnimPlayer3D), docs/viperlib/graphics/animation.md
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
int8_t rt_anim_controller3d_add_transition(void *controller,
                                           rt_string from_state,
                                           rt_string to_state,
                                           double blend_seconds);

/// @brief Hard-cut to the named state (no blending). Returns 1 on success, 0 if state unknown.
int8_t rt_anim_controller3d_play(void *controller, rt_string state_name);
/// @brief Smoothly crossfade into the named state over @p blend_seconds.
int8_t rt_anim_controller3d_crossfade(void *controller, rt_string state_name, double blend_seconds);
/// @brief Stop the controller (subsequent draws use the bind pose).
void rt_anim_controller3d_stop(void *controller);
/// @brief Advance the controller's clock by @p delta_time seconds (drives blends, events, root
/// motion).
void rt_anim_controller3d_update(void *controller, double delta_time);

/// @brief Get the name of the currently-active state.
rt_string rt_anim_controller3d_get_current_state(void *controller);
/// @brief Get the name of the state we're transitioning from (empty string if not blending).
rt_string rt_anim_controller3d_get_previous_state(void *controller);
/// @brief True if the controller is currently in a crossfade.
int8_t rt_anim_controller3d_get_is_transitioning(void *controller);
/// @brief Number of registered states.
int64_t rt_anim_controller3d_get_state_count(void *controller);
/// @brief Current base-layer playback time in seconds.
double rt_anim_controller3d_get_state_time(void *controller);
/// @brief True if the named state is the active, playing base-layer state.
int8_t rt_anim_controller3d_is_state_playing(void *controller, rt_string state_name);

/// @brief Override the per-state playback speed multiplier.
void rt_anim_controller3d_set_state_speed(void *controller, rt_string state_name, double speed);
/// @brief Override whether a state loops at the end of its clip duration.
void rt_anim_controller3d_set_state_looping(void *controller, rt_string state_name, int8_t loop);
/// @brief Configure deterministic update-rate LOD for distant / low-priority controllers.
void rt_anim_controller3d_set_animation_lod(void *controller, double distance, double rate_hz);
/// @brief Configure bone-count LOD: freeze bones at/after `max_bones` to bind-local (<=0 disables).
void rt_anim_controller3d_set_bone_lod(void *controller, int64_t max_bones);
/// @brief Use a BlendTree3D as the base pose source; pass NULL to clear it.
int8_t rt_anim_controller3d_set_blend_tree(void *controller, void *blend_tree);
/// @brief Apply an IKSolver3D after controller layers and before skinning; pass NULL to clear it.
int8_t rt_anim_controller3d_set_ik_solver(void *controller, void *ik_solver);

/// @brief Schedule an event to fire when @p state_name reaches @p time_seconds during playback.
/// @details Event names are stored in a fixed queue and truncated to 63 bytes plus NUL.
void rt_anim_controller3d_add_event(void *controller,
                                    rt_string state_name,
                                    double time_seconds,
                                    rt_string event_name);
/// @brief Pop the next pending event name (empty string when none queued).
/// @details Pending event names are bounded to 63 bytes; oldest queued events drop on overflow.
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
/// @brief Hard-cut a specific overlay layer to a true additive bind-pose delta state.
int8_t rt_anim_controller3d_play_layer_additive(void *controller,
                                                int64_t layer,
                                                rt_string state_name);
/// @brief Crossfade a specific layer to the named state over @p blend_seconds.
int8_t rt_anim_controller3d_crossfade_layer(void *controller,
                                            int64_t layer,
                                            rt_string state_name,
                                            double blend_seconds);
/// @brief Crossfade a specific overlay layer to a true additive bind-pose delta state.
int8_t rt_anim_controller3d_crossfade_layer_additive(void *controller,
                                                     int64_t layer,
                                                     rt_string state_name,
                                                     double blend_seconds);
/// @brief Stop the named layer (its weight will fall to 0 if not currently blending).
void rt_anim_controller3d_stop_layer(void *controller, int64_t layer);

/// @brief Get the model-space (skeleton-root-relative) global matrix for bone
/// @p bone_index after all layers + transitions are evaluated. Compose with the
/// owning node's world transform to reach world space.
void *rt_anim_controller3d_get_bone_matrix(void *controller, int64_t bone_index);

/// @brief Extract a bone's model-space position + rotation quaternion from the
/// final pose. Returns 1 on success, 0 on missing pose/invalid bone.
int rt_anim_controller3d_get_bone_pose(void *controller,
                                       int64_t bone_index,
                                       double *out_pos,
                                       double *out_quat);

/* Runtime integration helpers used by Scene3D bindings. */
/// @brief Borrow the Skeleton3D bound to this controller.
void *rt_anim_controller3d_get_skeleton(void *controller);
/// @brief Internal (ragdoll): overwrite masked bones' model-space globals after
///        evaluation, propagate deltas to unmasked descendants, refresh palette.
void rt_anim_controller3d_apply_pose_override(void *controller,
                                              const int8_t *mask,
                                              const float *globals);
/// @brief Borrow the flat float array of bone matrices for GPU upload (length = bone_count*16).
const float *rt_anim_controller3d_get_final_palette_data(void *controller, int32_t *bone_count);
/// @brief Borrow the previous frame's bone palette (used for motion vectors / TAA).
const float *rt_anim_controller3d_get_previous_palette_data(void *controller, int32_t *bone_count);
/// @brief Internal runtime helper: consume the accumulated root-motion rotation delta and reset it.
void *rt_anim_controller3d_consume_root_motion_rotation(void *controller);

#ifdef __cplusplus
}
#endif
