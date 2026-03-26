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
extern "C"
{
#endif

    /* Skeleton3D */
    void *rt_skeleton3d_new(void);
    int64_t rt_skeleton3d_add_bone(void *skel,
                                   rt_string name,
                                   int64_t parent_index,
                                   void *bind_mat4);
    void rt_skeleton3d_compute_inverse_bind(void *skel);
    int64_t rt_skeleton3d_get_bone_count(void *skel);
    int64_t rt_skeleton3d_find_bone(void *skel, rt_string name);
    rt_string rt_skeleton3d_get_bone_name(void *skel, int64_t index);
    void *rt_skeleton3d_get_bone_bind_pose(void *skel, int64_t index);

    /* Animation3D */
    void *rt_animation3d_new(rt_string name, double duration);
    void rt_animation3d_add_keyframe(
        void *anim, int64_t bone_index, double time, void *position, void *rotation, void *scale);
    void rt_animation3d_set_looping(void *anim, int8_t loop);
    int8_t rt_animation3d_get_looping(void *anim);
    double rt_animation3d_get_duration(void *anim);
    rt_string rt_animation3d_get_name(void *anim);

    /* AnimPlayer3D */
    void *rt_anim_player3d_new(void *skeleton);
    void rt_anim_player3d_play(void *player, void *animation);
    void rt_anim_player3d_crossfade(void *player, void *animation, double duration);
    void rt_anim_player3d_stop(void *player);
    void rt_anim_player3d_update(void *player, double delta_time);
    void rt_anim_player3d_set_speed(void *player, double speed);
    double rt_anim_player3d_get_speed(void *player);
    int8_t rt_anim_player3d_is_playing(void *player);
    double rt_anim_player3d_get_time(void *player);
    void rt_anim_player3d_set_time(void *player, double time);
    void *rt_anim_player3d_get_bone_matrix(void *player, int64_t bone_index);

    /* Mesh3D extensions */
    void rt_mesh3d_set_skeleton(void *mesh, void *skeleton);
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
    void rt_canvas3d_draw_mesh_skinned(
        void *canvas, void *mesh, void *transform, void *material, void *anim_player);
    void rt_canvas3d_draw_mesh_blended(
        void *canvas, void *mesh, void *transform, void *material, void *blend);

    /* AnimBlend3D — multi-state animation blending */
    void *rt_anim_blend3d_new(void *skeleton);
    int64_t rt_anim_blend3d_add_state(void *blend, rt_string name, void *animation);
    void rt_anim_blend3d_set_weight(void *blend, int64_t state, double weight);
    void rt_anim_blend3d_set_weight_by_name(void *blend, rt_string name, double weight);
    double rt_anim_blend3d_get_weight(void *blend, int64_t state);
    void rt_anim_blend3d_set_speed(void *blend, int64_t state, double speed);
    void rt_anim_blend3d_update(void *blend, double dt);
    int64_t rt_anim_blend3d_state_count(void *blend);

#ifdef __cplusplus
}
#endif
