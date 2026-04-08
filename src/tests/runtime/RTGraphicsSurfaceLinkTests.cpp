//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp
// Purpose: Link-smoke coverage for the graphics/runtime surface that must
//          remain exported in both full and graphics-disabled builds.
//
//===----------------------------------------------------------------------===//

#include "rt_audio.h"
#include "rt_animcontroller3d.h"
#include "rt_canvas3d.h"
#include "rt_collider3d.h"
#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_graphics.h"
#include "rt_joints3d.h"
#include "rt_model3d.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"
#include "rt_terrain3d.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

template <typename Fn> std::uintptr_t fn_bits(Fn fn) {
    static_assert(sizeof(fn) <= sizeof(std::uintptr_t));
    std::uintptr_t bits = 0;
    std::memcpy(&bits, &fn, sizeof(fn));
    return bits;
}

} // namespace

int main() {
    volatile std::uintptr_t surface[] = {
        fn_bits(&rt_canvas_is_available),
        fn_bits(&rt_audio_is_available),
        fn_bits(&rt_mesh3d_clear),
        fn_bits(&rt_mesh3d_from_stl),
        fn_bits(&rt_camera3d_new_ortho),
        fn_bits(&rt_camera3d_is_ortho),
        fn_bits(&rt_light3d_new_spot),
        fn_bits(&rt_scene3d_save),
        fn_bits(&rt_scene3d_sync_bindings),
        fn_bits(&rt_scene_node3d_bind_body),
        fn_bits(&rt_scene_node3d_clear_body_binding),
        fn_bits(&rt_scene_node3d_get_body),
        fn_bits(&rt_scene_node3d_set_sync_mode),
        fn_bits(&rt_scene_node3d_get_sync_mode),
        fn_bits(&rt_scene_node3d_bind_animator),
        fn_bits(&rt_scene_node3d_clear_animator_binding),
        fn_bits(&rt_scene_node3d_get_animator),
        fn_bits(&rt_fbx_get_morph_target),
        fn_bits(&rt_gltf_load),
        fn_bits(&rt_gltf_mesh_count),
        fn_bits(&rt_gltf_get_mesh),
        fn_bits(&rt_gltf_material_count),
        fn_bits(&rt_gltf_get_material),
        fn_bits(&rt_gltf_node_count),
        fn_bits(&rt_gltf_get_scene_root),
        fn_bits(&rt_anim_controller3d_new),
        fn_bits(&rt_anim_controller3d_add_state),
        fn_bits(&rt_anim_controller3d_add_transition),
        fn_bits(&rt_anim_controller3d_play),
        fn_bits(&rt_anim_controller3d_crossfade),
        fn_bits(&rt_anim_controller3d_stop),
        fn_bits(&rt_anim_controller3d_update),
        fn_bits(&rt_anim_controller3d_get_current_state),
        fn_bits(&rt_anim_controller3d_get_previous_state),
        fn_bits(&rt_anim_controller3d_get_is_transitioning),
        fn_bits(&rt_anim_controller3d_get_state_count),
        fn_bits(&rt_anim_controller3d_set_state_speed),
        fn_bits(&rt_anim_controller3d_set_state_looping),
        fn_bits(&rt_anim_controller3d_add_event),
        fn_bits(&rt_anim_controller3d_poll_event),
        fn_bits(&rt_anim_controller3d_set_root_motion_bone),
        fn_bits(&rt_anim_controller3d_get_root_motion_delta),
        fn_bits(&rt_anim_controller3d_consume_root_motion),
        fn_bits(&rt_anim_controller3d_set_layer_weight),
        fn_bits(&rt_anim_controller3d_set_layer_mask),
        fn_bits(&rt_anim_controller3d_play_layer),
        fn_bits(&rt_anim_controller3d_crossfade_layer),
        fn_bits(&rt_anim_controller3d_stop_layer),
        fn_bits(&rt_anim_controller3d_get_bone_matrix),
        fn_bits(&rt_anim_controller3d_get_final_palette_data),
        fn_bits(&rt_anim_controller3d_get_previous_palette_data),
        fn_bits(&rt_model3d_load),
        fn_bits(&rt_model3d_get_mesh_count),
        fn_bits(&rt_model3d_get_material_count),
        fn_bits(&rt_model3d_get_skeleton_count),
        fn_bits(&rt_model3d_get_animation_count),
        fn_bits(&rt_model3d_get_node_count),
        fn_bits(&rt_model3d_get_mesh),
        fn_bits(&rt_model3d_get_material),
        fn_bits(&rt_model3d_get_skeleton),
        fn_bits(&rt_model3d_get_animation),
        fn_bits(&rt_model3d_find_node),
        fn_bits(&rt_model3d_instantiate),
        fn_bits(&rt_model3d_instantiate_scene),
        fn_bits(&rt_distance_joint3d_new),
        fn_bits(&rt_distance_joint3d_get_distance),
        fn_bits(&rt_distance_joint3d_set_distance),
        fn_bits(&rt_spring_joint3d_new),
        fn_bits(&rt_spring_joint3d_get_stiffness),
        fn_bits(&rt_spring_joint3d_set_stiffness),
        fn_bits(&rt_spring_joint3d_get_damping),
        fn_bits(&rt_spring_joint3d_set_damping),
        fn_bits(&rt_spring_joint3d_get_rest_length),
        fn_bits(&rt_world3d_add_joint),
        fn_bits(&rt_world3d_remove_joint),
        fn_bits(&rt_world3d_joint_count),
        fn_bits(&rt_world3d_get_collision_count),
        fn_bits(&rt_world3d_get_collision_body_a),
        fn_bits(&rt_world3d_get_collision_body_b),
        fn_bits(&rt_world3d_get_collision_normal),
        fn_bits(&rt_world3d_get_collision_depth),
        fn_bits(&rt_world3d_get_collision_event_count),
        fn_bits(&rt_world3d_get_collision_event),
        fn_bits(&rt_world3d_get_enter_event_count),
        fn_bits(&rt_world3d_get_enter_event),
        fn_bits(&rt_world3d_get_stay_event_count),
        fn_bits(&rt_world3d_get_stay_event),
        fn_bits(&rt_world3d_get_exit_event_count),
        fn_bits(&rt_world3d_get_exit_event),
        fn_bits(&rt_world3d_raycast),
        fn_bits(&rt_world3d_raycast_all),
        fn_bits(&rt_world3d_sweep_sphere),
        fn_bits(&rt_world3d_sweep_capsule),
        fn_bits(&rt_world3d_overlap_sphere),
        fn_bits(&rt_world3d_overlap_aabb),
        fn_bits(&rt_physics_hit3d_get_body),
        fn_bits(&rt_physics_hit3d_get_collider),
        fn_bits(&rt_physics_hit3d_get_point),
        fn_bits(&rt_physics_hit3d_get_normal),
        fn_bits(&rt_physics_hit3d_get_distance),
        fn_bits(&rt_physics_hit3d_get_fraction),
        fn_bits(&rt_physics_hit3d_get_started_penetrating),
        fn_bits(&rt_physics_hit3d_get_is_trigger),
        fn_bits(&rt_physics_hit_list3d_get_count),
        fn_bits(&rt_physics_hit_list3d_get),
        fn_bits(&rt_collision_event3d_get_body_a),
        fn_bits(&rt_collision_event3d_get_body_b),
        fn_bits(&rt_collision_event3d_get_collider_a),
        fn_bits(&rt_collision_event3d_get_collider_b),
        fn_bits(&rt_collision_event3d_get_is_trigger),
        fn_bits(&rt_collision_event3d_get_contact_count),
        fn_bits(&rt_collision_event3d_get_relative_speed),
        fn_bits(&rt_collision_event3d_get_normal_impulse),
        fn_bits(&rt_collision_event3d_get_contact),
        fn_bits(&rt_collision_event3d_get_contact_point),
        fn_bits(&rt_collision_event3d_get_contact_normal),
        fn_bits(&rt_collision_event3d_get_contact_separation),
        fn_bits(&rt_contact_point3d_get_point),
        fn_bits(&rt_contact_point3d_get_normal),
        fn_bits(&rt_contact_point3d_get_separation),
        fn_bits(&rt_collider3d_new_box),
        fn_bits(&rt_collider3d_new_sphere),
        fn_bits(&rt_collider3d_new_capsule),
        fn_bits(&rt_collider3d_new_convex_hull),
        fn_bits(&rt_collider3d_new_mesh),
        fn_bits(&rt_collider3d_new_heightfield),
        fn_bits(&rt_collider3d_new_compound),
        fn_bits(&rt_collider3d_add_child),
        fn_bits(&rt_collider3d_get_type),
        fn_bits(&rt_collider3d_get_local_bounds_min),
        fn_bits(&rt_collider3d_get_local_bounds_max),
        fn_bits(&rt_body3d_new),
        fn_bits(&rt_body3d_set_collider),
        fn_bits(&rt_body3d_get_collider),
        fn_bits(&rt_body3d_set_orientation),
        fn_bits(&rt_body3d_get_orientation),
        fn_bits(&rt_body3d_set_angular_velocity),
        fn_bits(&rt_body3d_get_angular_velocity),
        fn_bits(&rt_body3d_apply_torque),
        fn_bits(&rt_body3d_apply_angular_impulse),
        fn_bits(&rt_body3d_set_linear_damping),
        fn_bits(&rt_body3d_get_linear_damping),
        fn_bits(&rt_body3d_set_angular_damping),
        fn_bits(&rt_body3d_get_angular_damping),
        fn_bits(&rt_body3d_set_kinematic),
        fn_bits(&rt_body3d_is_kinematic),
        fn_bits(&rt_body3d_set_can_sleep),
        fn_bits(&rt_body3d_can_sleep),
        fn_bits(&rt_body3d_is_sleeping),
        fn_bits(&rt_body3d_wake),
        fn_bits(&rt_body3d_sleep),
        fn_bits(&rt_body3d_set_use_ccd),
        fn_bits(&rt_body3d_get_use_ccd),
        fn_bits(&rt_terrain3d_set_splat_map),
        fn_bits(&rt_terrain3d_set_layer_texture),
        fn_bits(&rt_terrain3d_set_layer_scale),
    };

    for (std::uintptr_t bits : surface) {
        assert(bits != 0);
    }

    return 0;
}
