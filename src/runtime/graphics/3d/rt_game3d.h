//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d.h
// Purpose: Runtime-backed Viper.Game3D ergonomic layer over the lower-level
//   Graphics3D, Physics3D, input, audio, and post-FX primitives.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RT_GAME3D_LAYER_WORLD = 1,
    RT_GAME3D_LAYER_DYNAMIC = 2,
    RT_GAME3D_LAYER_PLAYER = 4,
    RT_GAME3D_LAYER_TRIGGER = 8,
    RT_GAME3D_LAYER_DEBRIS = 16,
};

enum {
    RT_GAME3D_BODY_SHAPE_BOX = 0,
    RT_GAME3D_BODY_SHAPE_SPHERE = 1,
    RT_GAME3D_BODY_SHAPE_CAPSULE = 2,
};

enum {
    RT_GAME3D_SYNC_NODE_FROM_BODY = 0,
    RT_GAME3D_SYNC_BODY_FROM_NODE = 1,
    RT_GAME3D_SYNC_NODE_FROM_ANIM_ROOT_MOTION = 2,
    RT_GAME3D_SYNC_TWO_WAY_KINEMATIC = 3,
};

enum {
    RT_GAME3D_ALPHA_OPAQUE = 0,
    RT_GAME3D_ALPHA_MASK = 1,
    RT_GAME3D_ALPHA_BLEND = 2,
};

enum {
    RT_GAME3D_SHADING_PHONG = 0,
    RT_GAME3D_SHADING_TOON = 1,
    RT_GAME3D_SHADING_PBR = 2,
    RT_GAME3D_SHADING_FRESNEL = 3,
    RT_GAME3D_SHADING_EMISSIVE = 4,
    RT_GAME3D_SHADING_UNLIT = 5,
};

enum {
    RT_GAME3D_QUALITY_PERFORMANCE = 0,
    RT_GAME3D_QUALITY_BALANCED = 1,
    RT_GAME3D_QUALITY_CINEMATIC = 2,
};

enum {
    RT_GAME3D_COLLISION_ENTER = 0,
    RT_GAME3D_COLLISION_STAY = 1,
    RT_GAME3D_COLLISION_EXIT = 2,
    RT_GAME3D_COLLISION_ANY = 3,
};

int64_t rt_game3d_layers_world(void);
int64_t rt_game3d_layers_dynamic(void);
int64_t rt_game3d_layers_player(void);
int64_t rt_game3d_layers_trigger(void);
int64_t rt_game3d_layers_debris(void);

int64_t rt_game3d_body_shape_box(void);
int64_t rt_game3d_body_shape_sphere(void);
int64_t rt_game3d_body_shape_capsule(void);

int64_t rt_game3d_sync_mode_node_from_body(void);
int64_t rt_game3d_sync_mode_body_from_node(void);
int64_t rt_game3d_sync_mode_node_from_anim_root_motion(void);
int64_t rt_game3d_sync_mode_two_way_kinematic(void);

int64_t rt_game3d_alpha_mode_opaque(void);
int64_t rt_game3d_alpha_mode_mask(void);
int64_t rt_game3d_alpha_mode_blend(void);

int64_t rt_game3d_shading_model_phong(void);
int64_t rt_game3d_shading_model_toon(void);
int64_t rt_game3d_shading_model_pbr(void);
int64_t rt_game3d_shading_model_fresnel(void);
int64_t rt_game3d_shading_model_emissive(void);
int64_t rt_game3d_shading_model_unlit(void);

int64_t rt_game3d_quality_performance(void);
int64_t rt_game3d_quality_balanced(void);
int64_t rt_game3d_quality_cinematic(void);

int64_t rt_game3d_collision_enter(void);
int64_t rt_game3d_collision_stay(void);
int64_t rt_game3d_collision_exit(void);
int64_t rt_game3d_collision_any(void);

int64_t rt_game3d_key_w(void);
int64_t rt_game3d_key_a(void);
int64_t rt_game3d_key_s(void);
int64_t rt_game3d_key_d(void);
int64_t rt_game3d_key_space(void);
int64_t rt_game3d_key_escape(void);
int64_t rt_game3d_key_shift(void);
int64_t rt_game3d_key_ctrl(void);
int64_t rt_game3d_key_up(void);
int64_t rt_game3d_key_down(void);
int64_t rt_game3d_key_left(void);
int64_t rt_game3d_key_right(void);

int64_t rt_game3d_mouse_left(void);
int64_t rt_game3d_mouse_right(void);
int64_t rt_game3d_mouse_middle(void);
int64_t rt_game3d_mouse_x1(void);
int64_t rt_game3d_mouse_x2(void);

void *rt_game3d_layermask_none(void);
void *rt_game3d_layermask_all(void);
void *rt_game3d_layermask_of(int64_t layer);
int64_t rt_game3d_layermask_get_bits(void *mask);
void rt_game3d_layermask_set_bits(void *mask, int64_t bits);
void *rt_game3d_layermask_include(void *mask, int64_t layer);
int8_t rt_game3d_layermask_includes(void *mask, int64_t layer);

void *rt_game3d_body_def_box(double half_x, double half_y, double half_z, double mass);
void *rt_game3d_body_def_sphere(double radius, double mass);
void *rt_game3d_body_def_capsule(double radius, double height, double mass);
void *rt_game3d_body_def_static_box(double half_x, double half_y, double half_z);
void *rt_game3d_body_def_static_plane(double size);
int64_t rt_game3d_body_def_get_shape(void *def);
void rt_game3d_body_def_set_shape(void *def, int64_t shape);
double rt_game3d_body_def_get_mass(void *def);
void rt_game3d_body_def_set_mass(void *def, double mass);
double rt_game3d_body_def_get_friction(void *def);
void rt_game3d_body_def_set_friction(void *def, double friction);
double rt_game3d_body_def_get_restitution(void *def);
void rt_game3d_body_def_set_restitution(void *def, double restitution);
int8_t rt_game3d_body_def_get_static(void *def);
void rt_game3d_body_def_set_static(void *def, int8_t is_static);
int8_t rt_game3d_body_def_get_kinematic(void *def);
void rt_game3d_body_def_set_kinematic(void *def, int8_t is_kinematic);
int8_t rt_game3d_body_def_get_trigger(void *def);
void rt_game3d_body_def_set_trigger(void *def, int8_t is_trigger);
int8_t rt_game3d_body_def_get_use_ccd(void *def);
void rt_game3d_body_def_set_use_ccd(void *def, int8_t use_ccd);
int64_t rt_game3d_body_def_get_layer(void *def);
void rt_game3d_body_def_set_layer_prop(void *def, int64_t layer);
void *rt_game3d_body_def_get_mask(void *def);
void rt_game3d_body_def_set_mask_prop(void *def, void *mask);
int64_t rt_game3d_body_def_get_sync_mode(void *def);
void rt_game3d_body_def_set_sync_mode_prop(void *def, int64_t sync_mode);
void *rt_game3d_body_def_with_layer(void *def, int64_t layer);
void *rt_game3d_body_def_with_mask(void *def, void *mask);
void *rt_game3d_body_def_as_trigger(void *def);
void *rt_game3d_body_def_with_sync(void *def, int64_t sync_mode);

int64_t rt_game3d_collision_event_get_phase(void *event);
void *rt_game3d_collision_event_get_a(void *event);
void *rt_game3d_collision_event_get_b(void *event);
void *rt_game3d_collision_event_get_raw(void *event);
int8_t rt_game3d_collision_event_get_is_trigger(void *event);
double rt_game3d_collision_event_get_relative_speed(void *event);
double rt_game3d_collision_event_get_normal_impulse(void *event);
void *rt_game3d_collision_event_point(void *event);
void *rt_game3d_collision_event_normal(void *event);
void *rt_game3d_collision_event_other(void *event, void *entity);

void *rt_game3d_input_new(void);
double rt_game3d_input_get_look_sensitivity(void *input);
void rt_game3d_input_set_look_sensitivity(void *input, double sensitivity);
void rt_game3d_input_update(void *input);
int8_t rt_game3d_input_is_down(void *input, int64_t key);
int8_t rt_game3d_input_pressed(void *input, int64_t key);
int8_t rt_game3d_input_released(void *input, int64_t key);
void *rt_game3d_input_mouse_delta(void *input);
int8_t rt_game3d_input_mouse_button(void *input, int64_t button);
int8_t rt_game3d_input_mouse_pressed(void *input, int64_t button);
double rt_game3d_input_wheel_y(void *input);
void *rt_game3d_input_move_axis(void *input);
void *rt_game3d_input_look_axis(void *input);
void rt_game3d_input_capture_mouse(void *input);
void rt_game3d_input_release_mouse(void *input);

void *rt_game3d_entity_new(void);
void *rt_game3d_entity_of(void *mesh, void *material);
void *rt_game3d_entity_from_node(void *root);
int64_t rt_game3d_entity_get_id(void *entity);
void *rt_game3d_entity_get_node(void *entity);
void *rt_game3d_entity_get_mesh(void *entity);
void rt_game3d_entity_set_mesh_prop(void *entity, void *mesh);
void *rt_game3d_entity_get_material(void *entity);
void rt_game3d_entity_set_material_prop(void *entity, void *material);
void *rt_game3d_entity_get_body(void *entity);
void *rt_game3d_entity_get_anim(void *entity);
int64_t rt_game3d_entity_get_layer(void *entity);
void rt_game3d_entity_set_layer_prop(void *entity, int64_t layer);
void *rt_game3d_entity_get_collision_mask(void *entity);
void rt_game3d_entity_set_collision_mask_prop(void *entity, void *mask);
rt_string rt_game3d_entity_get_name(void *entity);
void rt_game3d_entity_set_name_prop(void *entity, rt_string name);
void *rt_game3d_entity_set_position(void *entity, double x, double y, double z);
void *rt_game3d_entity_set_position_v(void *entity, void *position);
void *rt_game3d_entity_set_scale(void *entity, double scale);
void *rt_game3d_entity_set_scale_xyz(void *entity, double x, double y, double z);
void *rt_game3d_entity_set_rotation_euler(void *entity, double x_deg, double y_deg, double z_deg);
void *rt_game3d_entity_set_mesh(void *entity, void *mesh);
void *rt_game3d_entity_set_material(void *entity, void *material);
void *rt_game3d_entity_add_child(void *entity, void *child);
int8_t rt_game3d_entity_is_group(void *entity);
void *rt_game3d_entity_set_name(void *entity, rt_string name);
void *rt_game3d_entity_set_layer(void *entity, int64_t layer);
void *rt_game3d_entity_set_collision_mask(void *entity, void *mask);
void *rt_game3d_entity_attach_body(void *entity, void *body_or_def);
void *rt_game3d_entity_attach_animator(void *entity, void *animator_or_controller);
void rt_game3d_entity_apply_impulse(void *entity, double x, double y, double z);
void rt_game3d_entity_set_velocity(void *entity, double x, double y, double z);
void *rt_game3d_entity_position(void *entity);
void *rt_game3d_entity_world_position(void *entity);
int8_t rt_game3d_entity_is_spawned(void *entity);
int8_t rt_game3d_entity_is_destroyed(void *entity);

void *rt_game3d_animator_new(void *controller);
void *rt_game3d_animator_get_controller(void *animator);
int8_t rt_game3d_animator_play(void *animator, rt_string name);
int8_t rt_game3d_animator_crossfade(void *animator, rt_string name, double seconds);
void rt_game3d_animator_set_speed(void *animator, rt_string name, double speed);
int8_t rt_game3d_animator_is_playing(void *animator, rt_string name);
double rt_game3d_animator_state_time(void *animator);
int64_t rt_game3d_animator_event_count(void *animator);
rt_string rt_game3d_animator_event_name(void *animator, int64_t index);
void rt_game3d_animator_update(void *animator, double dt);

void *rt_game3d_audio_get_listener(void *audio);
void *rt_game3d_effects_get_postfx(void *effects);

void rt_game3d_lighting_studio(void *world);
void rt_game3d_lighting_outdoor(void *world, void *sun_dir);
void rt_game3d_lighting_night(void *world);
void rt_game3d_lighting_interior(void *world);
void rt_game3d_lighting_clear(void *world);

void *rt_game3d_materials_plastic(double r, double g, double b);
void *rt_game3d_materials_metal(double r, double g, double b);
void *rt_game3d_materials_rubber(double r, double g, double b);
void *rt_game3d_materials_glass(double r, double g, double b, double alpha);
void *rt_game3d_materials_emissive(double r, double g, double b, double intensity);
void *rt_game3d_materials_unlit(double r, double g, double b);
void *rt_game3d_materials_from_albedo_map(void *pixels);

void rt_game3d_postfx_cinematic(void *world);
void rt_game3d_postfx_crisp(void *world);
void rt_game3d_postfx_none(void *world);

void rt_game3d_quality_apply(void *world, int64_t quality);

void *rt_game3d_prefab_box(double size, void *material);
void *rt_game3d_prefab_box_xyz(double width, double height, double depth, void *material);
void *rt_game3d_prefab_sphere(double radius, int64_t segments, void *material);
void *rt_game3d_prefab_cylinder(double radius, double height, int64_t segments, void *material);
void *rt_game3d_prefab_plane(double width, double depth, void *material);
void *rt_game3d_prefab_ground(double size, void *material);

void *rt_game3d_assets_load_model(rt_string path);
void *rt_game3d_assets_load_model_asset(rt_string path);
void *rt_game3d_assets_load_model_template(rt_string path);
void *rt_game3d_assets_load_model_template_asset(rt_string path);
void rt_game3d_assets_preload(rt_string path);
void rt_game3d_assets_clear_cache(void);

void *rt_game3d_model_template_get_model(void *model_template);
rt_string rt_game3d_model_template_get_path(void *model_template);
int8_t rt_game3d_model_template_get_is_asset(void *model_template);
void *rt_game3d_model_template_instantiate(void *model_template);

void *rt_game3d_environment_outdoor(void *world);
void *rt_game3d_environment_sunset(void *world);
void *rt_game3d_environment_overcast(void *world);
void *rt_game3d_environment_night(void *world);
void *rt_game3d_env_handle_with_terrain(void *env, double size, double height);
void *rt_game3d_env_handle_with_water(void *env, double level);
void *rt_game3d_env_handle_with_fog(void *env, double near_plane, double far_plane);

void rt_game3d_debug_show_overlay(void *world, int8_t enabled);
void rt_game3d_debug_draw_axes(void *world, void *origin, double size);
void rt_game3d_debug_draw_physics(void *world, int8_t enabled);
void rt_game3d_debug_draw_camera_info(void *world, int8_t enabled);
void rt_game3d_debug_draw_capabilities(void *world, int8_t enabled);

void *rt_game3d_character_controller_new(
    void *world, void *entity, double radius, double height, double mass);
void *rt_game3d_character_controller_get_character(void *controller);
void *rt_game3d_character_controller_get_entity(void *controller);
double rt_game3d_character_controller_get_speed(void *controller);
void rt_game3d_character_controller_set_speed(void *controller, double speed);
double rt_game3d_character_controller_get_jump_speed(void *controller);
void rt_game3d_character_controller_set_jump_speed(void *controller, double jump_speed);
double rt_game3d_character_controller_get_gravity(void *controller);
void rt_game3d_character_controller_set_gravity(void *controller, double gravity);
void rt_game3d_character_controller_update(void *controller, void *input, void *camera, double dt);
void rt_game3d_character_controller_teleport(void *controller, double x, double y, double z);
int8_t rt_game3d_character_controller_grounded(void *controller);

void *rt_game3d_first_person_controller_new(void *world);
void *rt_game3d_first_person_controller_get_character(void *controller);
void rt_game3d_first_person_controller_set_character(void *controller, void *character_controller);
double rt_game3d_first_person_controller_get_speed(void *controller);
void rt_game3d_first_person_controller_set_speed(void *controller, double speed);
double rt_game3d_first_person_controller_get_look_sensitivity(void *controller);
void rt_game3d_first_person_controller_set_look_sensitivity(void *controller, double sensitivity);
void rt_game3d_first_person_controller_capture_mouse(void *controller);
void rt_game3d_first_person_controller_release_mouse(void *controller);
void rt_game3d_first_person_controller_update(void *controller, void *world, double dt);
void rt_game3d_first_person_controller_late_update(void *controller, void *world, double dt);

void *rt_game3d_free_fly_controller_new(void *world);
double rt_game3d_free_fly_controller_get_speed(void *controller);
void rt_game3d_free_fly_controller_set_speed(void *controller, double speed);
double rt_game3d_free_fly_controller_get_look_sensitivity(void *controller);
void rt_game3d_free_fly_controller_set_look_sensitivity(void *controller, double sensitivity);
void rt_game3d_free_fly_controller_capture_mouse(void *controller);
void rt_game3d_free_fly_controller_release_mouse(void *controller);
void rt_game3d_free_fly_controller_update(void *controller, void *world, double dt);
void rt_game3d_free_fly_controller_late_update(void *controller, void *world, double dt);

void *rt_game3d_orbit_controller_new(void *world, void *target);
void *rt_game3d_orbit_controller_get_target(void *controller);
void rt_game3d_orbit_controller_set_target(void *controller, void *target);
double rt_game3d_orbit_controller_get_distance(void *controller);
void rt_game3d_orbit_controller_set_distance(void *controller, double distance);
double rt_game3d_orbit_controller_get_yaw(void *controller);
void rt_game3d_orbit_controller_set_yaw(void *controller, double yaw);
double rt_game3d_orbit_controller_get_pitch(void *controller);
void rt_game3d_orbit_controller_set_pitch(void *controller, double pitch);
void rt_game3d_orbit_controller_update(void *controller, void *world, double dt);
void rt_game3d_orbit_controller_late_update(void *controller, void *world, double dt);

void *rt_game3d_follow_controller_new(void *world, void *target_entity, void *offset);
void *rt_game3d_follow_controller_get_target(void *controller);
void rt_game3d_follow_controller_set_target(void *controller, void *target_entity);
void *rt_game3d_follow_controller_get_offset(void *controller);
void rt_game3d_follow_controller_set_offset(void *controller, void *offset);
double rt_game3d_follow_controller_get_damping(void *controller);
void rt_game3d_follow_controller_set_damping(void *controller, double damping);
void rt_game3d_follow_controller_update(void *controller, void *world, double dt);
void rt_game3d_follow_controller_late_update(void *controller, void *world, double dt);

void *rt_game3d_world_new(rt_string title, int64_t width, int64_t height);
void *rt_game3d_world_new_with_camera(
    rt_string title, int64_t width, int64_t height, double fov_deg, double near_plane, double far_plane);
void rt_game3d_world_destroy(void *world);
int8_t rt_game3d_world_is_destroyed(void *world);
void *rt_game3d_world_get_canvas(void *world);
void *rt_game3d_world_get_camera(void *world);
void *rt_game3d_world_get_scene(void *world);
void *rt_game3d_world_get_physics(void *world);
void *rt_game3d_world_get_input(void *world);
void *rt_game3d_world_get_audio(void *world);
void *rt_game3d_world_get_effects(void *world);
double rt_game3d_world_get_dt(void *world);
double rt_game3d_world_get_elapsed(void *world);
int64_t rt_game3d_world_get_frame(void *world);
void *rt_game3d_world_spawn(void *world, void *entity);
void rt_game3d_world_despawn(void *world, void *entity);
void *rt_game3d_world_find_node(void *world, rt_string name);
void *rt_game3d_world_find_entity(void *world, rt_string name);
void rt_game3d_world_set_camera_controller(void *world, void *controller);
void rt_game3d_world_look_at(void *world, void *target);
void rt_game3d_world_on_resize(void *world, int64_t width, int64_t height);
void rt_game3d_world_set_ambient(void *world, double r, double g, double b);
void rt_game3d_world_add_light(void *world, int64_t slot, void *light);
void rt_game3d_world_clear_lights(void *world);
void rt_game3d_world_set_skybox(void *world, void *cubemap);
void rt_game3d_world_set_fog(
    void *world, double r, double g, double b, double near_plane, double far_plane);
void rt_game3d_world_set_quality(void *world, int64_t quality);
int64_t rt_game3d_world_collision_event_count(void *world, int64_t phase);
void *rt_game3d_world_collision_event(void *world, int64_t phase, int64_t index);
void rt_game3d_world_clear_collision_events(void *world);
void rt_game3d_world_set_gravity(void *world, double x, double y, double z);
void rt_game3d_world_run(void *world, void *update);
void rt_game3d_world_run_with_overlay(void *world, void *update, void *overlay);
void rt_game3d_world_run_fixed(void *world, double step_sec, void *update);
void rt_game3d_world_run_fixed_with_overlay(void *world, double step_sec, void *update, void *overlay);
void rt_game3d_world_run_frames(void *world, int64_t frame_count, double step_sec, void *update);
void rt_game3d_world_run_frames_only(void *world, int64_t frame_count, double step_sec);
int8_t rt_game3d_world_tick(void *world);
void rt_game3d_world_step_simulation(void *world, double step_sec);
void rt_game3d_world_begin_frame(void *world);
void rt_game3d_world_draw_scene(void *world);
void rt_game3d_world_draw_effects(void *world);
void rt_game3d_world_end_scene(void *world);
void rt_game3d_world_draw_overlay(void *world, void *overlay);
void *rt_game3d_world_capture_final_frame(void *world);
void rt_game3d_world_present(void *world);

#ifdef __cplusplus
}
#endif
