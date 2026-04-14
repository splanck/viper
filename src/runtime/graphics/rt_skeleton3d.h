//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_skeleton3d.h
// Purpose: Skeleton3D, Animation3D, and AnimPlayer3D — skeletal animation
//   with bone hierarchies, keyframe interpolation (SLERP for rotation),
//   CPU skinning, and animation crossfade.
//
// Key invariants:
//   - Bones must be added in topological order (parent before child).
//   - Max 128 bones per skeleton (VGFX3D_MAX_BONES).
//   - Bone palette = global_transform * inverse_bind_pose per bone.
//   - Keyframe rotation uses SLERP; position/scale use linear interpolation.
//
// Links: plans/3d/14-skeletal-animation.md, vgfx3d_skinning.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Skeleton3D */
/// @brief Create an empty skeleton (no bones yet).
void *rt_skeleton3d_new(void);
/// @brief Append a bone with the given name, parent index (-1 = root bone), and bind-pose matrix.
/// Returns the new bone's index. Bones must be added in topological (parent-first) order.
int64_t rt_skeleton3d_add_bone(void *skel, rt_string name, int64_t parent_index, void *bind_mat4);
/// @brief Pre-compute inverse bind-pose matrices used during skinning. Call after all bones added.
void rt_skeleton3d_compute_inverse_bind(void *skel);
/// @brief Total number of bones in the skeleton.
int64_t rt_skeleton3d_get_bone_count(void *skel);
/// @brief Look up a bone index by name (-1 if not found).
int64_t rt_skeleton3d_find_bone(void *skel, rt_string name);
/// @brief Get the name of the bone at @p index (empty string if out of range).
rt_string rt_skeleton3d_get_bone_name(void *skel, int64_t index);
/// @brief Get the bind-pose matrix (Mat4) of the bone at @p index.
void *rt_skeleton3d_get_bone_bind_pose(void *skel, int64_t index);

/* Animation3D */
/// @brief Create a named animation clip with the given duration in seconds.
void *rt_animation3d_new(rt_string name, double duration);
/// @brief Add a keyframe for one bone at @p time seconds (any of position/rotation/scale may be NULL).
void rt_animation3d_add_keyframe(
    void *anim, int64_t bone_index, double time, void *position, void *rotation, void *scale);
/// @brief Set whether the animation loops automatically when it reaches its duration.
void rt_animation3d_set_looping(void *anim, int8_t loop);
/// @brief Get the looping flag.
int8_t rt_animation3d_get_looping(void *anim);
/// @brief Get the animation duration in seconds.
double rt_animation3d_get_duration(void *anim);
/// @brief Get the animation's name.
rt_string rt_animation3d_get_name(void *anim);

/* AnimPlayer3D */
/// @brief Create an animation player bound to a skeleton (drives bone palette during draw).
void *rt_anim_player3d_new(void *skeleton);
/// @brief Begin playing @p animation immediately from time 0.
void rt_anim_player3d_play(void *player, void *animation);
/// @brief Crossfade from the current animation to @p animation over @p duration seconds.
void rt_anim_player3d_crossfade(void *player, void *animation, double duration);
/// @brief Stop the player; subsequent draws use the bind pose.
void rt_anim_player3d_stop(void *player);
/// @brief Advance the player's clock by @p delta_time seconds.
void rt_anim_player3d_update(void *player, double delta_time);
/// @brief Multiply the playback speed (1.0 = normal, 2.0 = double, -1.0 = reverse).
void rt_anim_player3d_set_speed(void *player, double speed);
/// @brief Get the playback speed multiplier.
double rt_anim_player3d_get_speed(void *player);
/// @brief True if the player is actively driving an animation.
int8_t rt_anim_player3d_is_playing(void *player);
/// @brief Get the current playback time in seconds.
double rt_anim_player3d_get_time(void *player);
/// @brief Seek to a specific time in seconds (may be negative for reverse playback).
void rt_anim_player3d_set_time(void *player, double time);
/// @brief Get the world-space matrix for bone @p bone_index at the current time.
void *rt_anim_player3d_get_bone_matrix(void *player, int64_t bone_index);

/* Mesh3D extensions */
/// @brief Bind a Skeleton3D to the mesh so subsequent skinned draws use its bone palette.
void rt_mesh3d_set_skeleton(void *mesh, void *skeleton);
/// @brief Set up to 4 bone-influence pairs for one vertex (weights should sum to 1.0).
void rt_mesh3d_set_bone_weights(void *mesh,
                                int64_t vertex_index,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3);

/* Canvas3D extension */
/// @brief Draw a mesh with skinning applied via the player's current bone palette.
void rt_canvas3d_draw_mesh_skinned(
    void *canvas, void *mesh, void *transform, void *material, void *anim_player);
/// @brief Draw a mesh with multi-state blending via an AnimBlend3D handle.
void rt_canvas3d_draw_mesh_blended(
    void *canvas, void *mesh, void *transform, void *material, void *blend);

/* AnimBlend3D — multi-state animation blending */
/// @brief Create a multi-state animation blender bound to a skeleton.
void *rt_anim_blend3d_new(void *skeleton);
/// @brief Register a named animation as a blend state. Returns the new state index.
int64_t rt_anim_blend3d_add_state(void *blend, rt_string name, void *animation);
/// @brief Set the blend weight for the @p state-th state by index (typically 0..1).
void rt_anim_blend3d_set_weight(void *blend, int64_t state, double weight);
/// @brief Set the blend weight by state name.
void rt_anim_blend3d_set_weight_by_name(void *blend, rt_string name, double weight);
/// @brief Read the current blend weight of the @p state-th state.
double rt_anim_blend3d_get_weight(void *blend, int64_t state);
/// @brief Set the per-state playback speed multiplier.
void rt_anim_blend3d_set_speed(void *blend, int64_t state, double speed);
/// @brief Tick the blender forward by @p dt seconds (advances all enabled states).
void rt_anim_blend3d_update(void *blend, double dt);
/// @brief Number of registered blend states.
int64_t rt_anim_blend3d_state_count(void *blend);

#ifdef __cplusplus
}
#endif
