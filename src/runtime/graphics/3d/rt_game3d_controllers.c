//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_controllers.c
// Purpose: Camera/character controllers for the Viper.Game3D layer —
//   CharacterController3D, FirstPerson, FreeFly, Orbit, and Follow controllers,
//   plus their finalizers and the entity/node sync helpers. Split out of
//   rt_game3d.c; shares private types/helpers via rt_game3d_internal.h.
// Key invariants:
//   - Camera controllers retain their active world; World3D clears that ref on
//     detach/destroy, and moving a controller detaches it from the previous world.
// Ownership/Lifetime:
//   - GC-managed handles; finalizers release retained entity/character refs.
// Links: rt_game3d_internal.h, rt_camera3d.h, rt_physics3d.h
//
//===----------------------------------------------------------------------===//

#include "rt_animcontroller3d.h"
#include "rt_asset.h"
#include "rt_audio.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_decal3d.h"
#include "rt_g3d_commit_queue.h"
#include "rt_game3d.h"
#include "rt_game3d_internal.h"
#include "rt_gltf.h"
#include "rt_graphics3d_ids.h"
#include "rt_input.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_model3d.h"
#include "rt_navmesh3d.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_particles3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_postfx3d.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_sound3d.h"
#include "rt_soundlistener3d.h"
#include "rt_soundsource3d.h"
#include "rt_string.h"
#include "rt_terrain3d.h"
#include "rt_textureasset3d.h"
#include "rt_threadpool.h"
#include "rt_trap.h"
#include "rt_vec2.h"
#include "rt_vec3.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief GC finalizer for a character controller: release its world, entity, and
///   underlying character references.
static void game3d_character_controller_finalize(void *obj) {
    rt_game3d_character_controller *controller = (rt_game3d_character_controller *)obj;
    if (!controller)
        return;
    game3d_release_ref(&controller->world);
    game3d_release_ref(&controller->entity);
    game3d_release_ref(&controller->character);
}

/// @brief Write a world-space position into a node, converting through its
///        parent's inverse world matrix so parent transforms are preserved.
static void game3d_set_node_world_position(void *node, double world_pos[3]) {
    void *parent;
    if (!node || !world_pos)
        return;
    parent = rt_scene_node3d_get_parent(node);
    if (!parent) {
        rt_scene_node3d_set_position(node, world_pos[0], world_pos[1], world_pos[2]);
        return;
    }
    {
        void *parent_world = rt_scene_node3d_get_world_matrix(parent);
        void *parent_inv = parent_world ? rt_mat4_inverse(parent_world) : NULL;
        void *world_vec = rt_vec3_new(world_pos[0], world_pos[1], world_pos[2]);
        void *local =
            (parent_inv && world_vec) ? rt_mat4_transform_point(parent_inv, world_vec) : NULL;
        if (local) {
            rt_scene_node3d_set_position(
                node, rt_vec3_x(local), rt_vec3_y(local), rt_vec3_z(local));
        } else {
            rt_scene_node3d_set_position(node, world_pos[0], world_pos[1], world_pos[2]);
        }
        game3d_release_ref(&local);
        game3d_release_ref(&world_vec);
        game3d_release_ref(&parent_inv);
        game3d_release_ref(&parent_world);
    }
}

/// @brief Push a node's current world-space position/rotation/scale into its body.
void game3d_sync_body_from_entity_node(rt_game3d_entity *entity, int8_t force) {
    if (!entity || !entity->node || !entity->body)
        return;
    if (!force && rt_scene_node3d_get_sync_mode(entity->node) != RT_GAME3D_SYNC_BODY_FROM_NODE)
        return;
    void *pos = rt_scene_node3d_get_world_position(entity->node);
    void *rot = rt_scene_node3d_get_world_rotation(entity->node);
    void *scale = rt_scene_node3d_get_world_scale(entity->node);
    if (pos)
        rt_body3d_set_position(entity->body, rt_vec3_x(pos), rt_vec3_y(pos), rt_vec3_z(pos));
    if (rot)
        rt_body3d_set_orientation(entity->body, rot);
    if (scale)
        rt_body3d_set_scale(entity->body, rt_vec3_x(scale), rt_vec3_y(scale), rt_vec3_z(scale));
    game3d_release_ref(&scale);
    game3d_release_ref(&rot);
    game3d_release_ref(&pos);
}

/// @brief Copy the character's current position back onto the driven entity's node.
static void game3d_character_controller_sync_entity(rt_game3d_character_controller *controller) {
    if (!controller || !controller->entity || !controller->character)
        return;
    rt_game3d_entity *entity = (rt_game3d_entity *)controller->entity;
    void *pos = rt_character3d_get_position(controller->character);
    if (entity->node && pos) {
        double world_pos[3] = {rt_vec3_x(pos), rt_vec3_y(pos), rt_vec3_z(pos)};
        game3d_set_node_world_position(entity->node, world_pos);
    }
    game3d_release_ref(&pos);
}

/// @brief Create a capsule character controller bound to an entity, seeding the
///   character at the entity's position and wiring it into the world physics; sane
///   defaults are substituted for non-positive radius/height/mass. See header.
void *rt_game3d_character_controller_new(
    void *world_obj, void *entity_obj, double radius, double height, double mass) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.CharacterController3D.New: invalid world");
    rt_game3d_entity *entity = game3d_entity_checked(
        entity_obj, "Game3D.CharacterController3D.New: entity must be Entity3D");
    if (!world || !entity)
        return NULL;

    radius = game3d_positive_or(radius, 0.3);
    height = game3d_positive_or(height, 1.8);
    mass = game3d_nonnegative_or(mass, 70.0);

    rt_game3d_character_controller *controller = (rt_game3d_character_controller *)rt_obj_new_i64(
        RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.CharacterController3D.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_character_controller_finalize);
    game3d_assign_ref(&controller->world, world);
    controller->entity = entity;
    rt_obj_retain_maybe(entity);
    controller->character = rt_character3d_new(radius, height, mass);
    if (!controller->character) {
        if (rt_obj_release_check0(controller))
            rt_obj_free(controller);
        rt_trap("Game3D.CharacterController3D.New: Character3D allocation failed");
        return NULL;
    }
    rt_character3d_set_world(controller->character, world->physics);
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->jump_speed = RT_GAME3D_DEFAULT_JUMP_SPEED;
    controller->gravity = RT_GAME3D_DEFAULT_GRAVITY;
    controller->eye_height = height * 0.45;

    void *pos = rt_game3d_entity_position(entity);
    if (pos) {
        rt_character3d_set_position(
            controller->character, rt_vec3_x(pos), rt_vec3_y(pos), rt_vec3_z(pos));
        game3d_release_ref(&pos);
    }
    game3d_character_controller_sync_entity(controller);
    return controller;
}

/// @brief Get the underlying Character3D object (NULL if invalid).
void *rt_game3d_character_controller_get_character(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.get_character: invalid controller");
    return controller ? controller->character : NULL;
}

/// @brief Get the entity driven by this controller (NULL if invalid).
void *rt_game3d_character_controller_get_entity(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.get_entity: invalid controller");
    return controller ? controller->entity : NULL;
}

/// @brief Get the horizontal move speed in units/sec.
double rt_game3d_character_controller_get_speed(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

/// @brief Set the horizontal move speed (negatives reset to the default).
void rt_game3d_character_controller_set_speed(void *obj, double speed) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

/// @brief Get the jump launch speed in units/sec.
double rt_game3d_character_controller_get_jump_speed(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.get_jumpSpeed: invalid controller");
    return controller ? controller->jump_speed : 0.0;
}

/// @brief Set the jump launch speed (negatives reset to the default).
void rt_game3d_character_controller_set_jump_speed(void *obj, double jump_speed) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.set_jumpSpeed: invalid controller");
    if (controller)
        controller->jump_speed = game3d_nonnegative_or(jump_speed, RT_GAME3D_DEFAULT_JUMP_SPEED);
}

/// @brief Get the gravity acceleration in units/sec².
double rt_game3d_character_controller_get_gravity(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.get_gravity: invalid controller");
    return controller ? controller->gravity : 0.0;
}

/// @brief Set the gravity acceleration (non-finite resets to the default).
void rt_game3d_character_controller_set_gravity(void *obj, double gravity) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.set_gravity: invalid controller");
    if (controller)
        controller->gravity = game3d_finite_or(gravity, RT_GAME3D_DEFAULT_GRAVITY);
}

/// @brief Advance the character one frame from input and camera orientation.
/// @details Builds a camera-relative move vector from the WASD axis (clamped to unit
///   length), integrates jump/gravity against the grounded state, moves the Character3D
///   by `dt` (itself clamped), and syncs the result back onto the entity. Traps on
///   invalid input or a non-Camera3D camera.
void rt_game3d_character_controller_update(void *obj, void *input_obj, void *camera, double dt) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.update: invalid controller");
    (void)game3d_input_checked(input_obj, "Game3D.CharacterController3D.update: invalid input");
    if (!rt_g3d_has_class(camera, RT_G3D_CAMERA3D_CLASS_ID)) {
        rt_trap("Game3D.CharacterController3D.update: camera must be Camera3D");
        return;
    }
    if (!controller || !controller->character)
        return;

    dt = game3d_clamp_dt(dt);
    double move_x = 0.0;
    double move_y = 0.0;
    double move_z = 0.0;
    game3d_input_move_axis_components((rt_game3d_input *)input_obj, &move_x, &move_y, &move_z);
    void *forward = rt_camera3d_get_forward(camera);
    void *right = rt_camera3d_get_right(camera);

    move_x = game3d_finite_or(move_x, 0.0);
    move_y = game3d_finite_or(move_y, 0.0);
    move_z = game3d_finite_or(move_z, 0.0);
    double move_len = sqrt(move_x * move_x + move_z * move_z);
    if (isfinite(move_len) && move_len > 1.0) {
        move_x /= move_len;
        move_z /= move_len;
    }

    double fx = forward ? rt_vec3_x(forward) : 0.0;
    double fz = forward ? rt_vec3_z(forward) : -1.0;
    double rx = right ? rt_vec3_x(right) : 1.0;
    double rz = right ? rt_vec3_z(right) : 0.0;
    game3d_normalize_xz(&fx, &fz, 0.0, -1.0);
    game3d_normalize_xz(&rx, &rz, 1.0, 0.0);

    int8_t grounded = rt_character3d_is_grounded(controller->character);
    if (grounded) {
        if (controller->vertical_velocity < 0.0)
            controller->vertical_velocity = -0.5;
        if (rt_game3d_input_pressed(input_obj, rt_game3d_key_space()) || move_y > 0.5)
            controller->vertical_velocity = controller->jump_speed;
    } else {
        controller->vertical_velocity += controller->gravity * dt;
        controller->vertical_velocity = game3d_clamp(controller->vertical_velocity, -100.0, 100.0);
    }

    double speed = game3d_nonnegative_or(controller->speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
    double vx = (fx * move_z + rx * move_x) * speed;
    double vz = (fz * move_z + rz * move_x) * speed;
    void *velocity = rt_vec3_new(vx, controller->vertical_velocity, vz);
    rt_character3d_move(controller->character, velocity, dt);
    game3d_release_ref(&velocity);
    game3d_release_ref(&right);
    game3d_release_ref(&forward);
    if (rt_character3d_is_grounded(controller->character) && controller->vertical_velocity < 0.0)
        controller->vertical_velocity = -0.5;
    game3d_character_controller_sync_entity(controller);

    game3d_release_ref(&right);
    game3d_release_ref(&forward);
}

/// @brief Teleport the character to an absolute position (NaN-scrubbed), clearing
///   vertical velocity, and sync the entity. See header.
void rt_game3d_character_controller_teleport(void *obj, double x, double y, double z) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.teleport: invalid controller");
    if (!controller || !controller->character)
        return;
    controller->vertical_velocity = 0.0;
    rt_character3d_set_position(controller->character,
                                game3d_finite_or(x, 0.0),
                                game3d_finite_or(y, 0.0),
                                game3d_finite_or(z, 0.0));
    game3d_character_controller_sync_entity(controller);
}

/// @brief True if the character is currently standing on ground. See header.
int8_t rt_game3d_character_controller_grounded(void *obj) {
    rt_game3d_character_controller *controller = game3d_character_controller_checked(
        obj, "Game3D.CharacterController3D.grounded: invalid controller");
    return controller && controller->character ? rt_character3d_is_grounded(controller->character)
                                               : 0;
}

/// @brief True if `controller` is NULL or one of the four Game3D camera-controller
///   classes — the set accepted by World3D.SetCameraController.
int game3d_camera_controller_is_valid(void *controller) {
    if (!controller)
        return 1;
    int64_t cid = rt_obj_class_id(controller);
    return cid == RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID || cid == RT_G3D_GAME3D_FREEFLY_CLASS_ID ||
           cid == RT_G3D_GAME3D_ORBIT_CLASS_ID || cid == RT_G3D_GAME3D_FOLLOW_CLASS_ID;
}

/// @brief Return the world currently retained by a camera controller, or NULL.
void *game3d_camera_controller_get_world_ref(void *controller) {
    if (!controller)
        return NULL;
    int64_t cid = rt_obj_class_id(controller);
    if (cid == RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID)
        return ((rt_game3d_first_person_controller *)controller)->world;
    if (cid == RT_G3D_GAME3D_FREEFLY_CLASS_ID)
        return ((rt_game3d_free_fly_controller *)controller)->world;
    if (cid == RT_G3D_GAME3D_ORBIT_CLASS_ID)
        return ((rt_game3d_orbit_controller *)controller)->world;
    if (cid == RT_G3D_GAME3D_FOLLOW_CLASS_ID)
        return ((rt_game3d_follow_controller *)controller)->world;
    return NULL;
}

/// @brief Rebind a camera controller's retained world reference to @p world.
void game3d_camera_controller_bind_world_ref(void *controller, void *world) {
    if (!controller)
        return;
    int64_t cid = rt_obj_class_id(controller);
    if (cid == RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID) {
        game3d_assign_ref(&((rt_game3d_first_person_controller *)controller)->world, world);
    } else if (cid == RT_G3D_GAME3D_FREEFLY_CLASS_ID) {
        game3d_assign_ref(&((rt_game3d_free_fly_controller *)controller)->world, world);
    } else if (cid == RT_G3D_GAME3D_ORBIT_CLASS_ID) {
        game3d_assign_ref(&((rt_game3d_orbit_controller *)controller)->world, world);
    } else if (cid == RT_G3D_GAME3D_FOLLOW_CLASS_ID) {
        game3d_assign_ref(&((rt_game3d_follow_controller *)controller)->world, world);
    }
}

/// @brief Clear a camera controller's retained world reference without mutating nested controllers.
void game3d_camera_controller_clear_world_ref(void *controller) {
    game3d_camera_controller_bind_world_ref(controller, NULL);
}

/// @brief Clear a camera controller's world reference only when it still points at @p world.
void game3d_camera_controller_clear_world_ref_if(void *controller, void *world) {
    if (game3d_camera_controller_get_world_ref(controller) == world)
        game3d_camera_controller_bind_world_ref(controller, NULL);
}

/// @brief GC finalizer for the FPS controller: release its character controller.
static void game3d_first_person_controller_finalize(void *obj) {
    rt_game3d_first_person_controller *controller = (rt_game3d_first_person_controller *)obj;
    if (controller) {
        game3d_release_ref(&controller->world);
        game3d_release_ref(&controller->character_controller);
    }
}

/// @brief Create a first-person controller bound to the world's camera. See header.
void *rt_game3d_first_person_controller_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.New: invalid world");
    if (!world)
        return NULL;
    rt_game3d_first_person_controller *controller =
        (rt_game3d_first_person_controller *)rt_obj_new_i64(RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID,
                                                            (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FirstPersonController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_first_person_controller_finalize);
    game3d_assign_ref(&controller->world, world);
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->look_sensitivity = RT_GAME3D_DEFAULT_LOOK_SENSITIVITY;
    controller->capture_mouse = 1;
    if (world->camera)
        rt_camera3d_fps_init(world->camera);
    return controller;
}

/// @brief Get the character controller driving movement (NULL if none/invalid).
void *rt_game3d_first_person_controller_get_character(void *obj) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.get_character: invalid controller");
    return controller ? controller->character_controller : NULL;
}

/// @brief Set the character controller driving movement; traps on a non-CharacterController3D.
void rt_game3d_first_person_controller_set_character(void *obj, void *character_controller) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.set_character: invalid controller");
    if (character_controller &&
        !rt_g3d_has_class(character_controller, RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID)) {
        rt_trap("Game3D.FirstPersonController.set_character: value must be CharacterController3D");
        return;
    }
    if (controller)
        game3d_assign_ref(&controller->character_controller, character_controller);
}

/// @brief Get the move speed in units/sec.
double rt_game3d_first_person_controller_get_speed(void *obj) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

/// @brief Set the move speed (negatives reset to the default).
void rt_game3d_first_person_controller_set_speed(void *obj, double speed) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

/// @brief Get the mouse-look sensitivity.
double rt_game3d_first_person_controller_get_look_sensitivity(void *obj) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.get_lookSensitivity: invalid controller");
    return controller ? controller->look_sensitivity : 0.0;
}

/// @brief Set the mouse-look sensitivity (negatives reset to the default).
void rt_game3d_first_person_controller_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.set_lookSensitivity: invalid controller");
    if (controller)
        controller->look_sensitivity =
            game3d_nonnegative_or(sensitivity, RT_GAME3D_DEFAULT_LOOK_SENSITIVITY);
}

/// @brief Capture and hide the cursor and remember the captured state.
void rt_game3d_first_person_controller_capture_mouse(void *obj) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.captureMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 1;
        rt_mouse_capture();
    }
}

/// @brief Release the cursor and clear the captured state.
void rt_game3d_first_person_controller_release_mouse(void *obj) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.releaseMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 0;
        rt_mouse_release();
    }
}

/// @brief Update FPS look + movement for the frame.
/// @details Applies mouse-look to the camera; if a character controller is attached it
///   drives ground movement through it, otherwise it free-walks the camera directly.
///   Re-captures the cursor each frame when capture is enabled.
void rt_game3d_first_person_controller_update(void *obj, void *world_obj, double dt) {
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.update: invalid world");
    if (!controller || !world || !world->camera || !world->input)
        return;
    dt = game3d_clamp_dt(dt);
    if (controller->capture_mouse)
        rt_mouse_capture();
    double yaw = (double)game3d_input_mouse_dx((rt_game3d_input *)world->input) *
                 controller->look_sensitivity;
    double pitch = 0.0 - (double)game3d_input_mouse_dy((rt_game3d_input *)world->input) *
                             controller->look_sensitivity;
    if (controller->character_controller) {
        rt_camera3d_fps_update(world->camera, yaw, pitch, 0.0, 0.0, 0.0, 0.0, dt);
        rt_game3d_character_controller_set_speed(controller->character_controller,
                                                 controller->speed);
        rt_game3d_character_controller_update(
            controller->character_controller, world->input, world->camera, dt);
    } else {
        double move_x = 0.0;
        double move_y = 0.0;
        double move_z = 0.0;
        game3d_input_move_axis_components(
            (rt_game3d_input *)world->input, &move_x, &move_y, &move_z);
        double speed = game3d_nonnegative_or(controller->speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
        rt_camera3d_fps_update(
            world->camera, yaw, pitch, move_z, move_x, move_y, speed, dt);
    }
}

/// @brief After physics, snap the camera to the character's eye height (only when a
///   character controller is attached). See header.
void rt_game3d_first_person_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_first_person_controller *controller = game3d_first_person_controller_checked(
        obj, "Game3D.FirstPersonController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.lateUpdate: invalid world");
    if (!controller || !world || !world->camera || !controller->character_controller)
        return;
    rt_game3d_character_controller *character = game3d_character_controller_checked(
        controller->character_controller,
        "Game3D.FirstPersonController.lateUpdate: invalid character");
    if (!character || !character->character)
        return;
    void *pos = rt_character3d_get_position(character->character);
    if (pos) {
        void *eye =
            rt_vec3_new(rt_vec3_x(pos), rt_vec3_y(pos) + character->eye_height, rt_vec3_z(pos));
        rt_camera3d_set_position(world->camera, eye);
        game3d_release_ref(&eye);
    }
    game3d_release_ref(&pos);
}

/// @brief GC finalizer for a FreeFlyController: release its retained world/camera references.
static void game3d_free_fly_controller_finalize(void *obj) {
    rt_game3d_free_fly_controller *controller = (rt_game3d_free_fly_controller *)obj;
    if (controller)
        game3d_release_ref(&controller->world);
}

/// @brief Create a free-fly spectator controller for the world's camera. See header.
void *rt_game3d_free_fly_controller_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FreeFlyController.New: invalid world");
    if (!world)
        return NULL;
    rt_game3d_free_fly_controller *controller = (rt_game3d_free_fly_controller *)rt_obj_new_i64(
        RT_G3D_GAME3D_FREEFLY_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FreeFlyController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_free_fly_controller_finalize);
    game3d_assign_ref(&controller->world, world);
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->look_sensitivity = RT_GAME3D_DEFAULT_LOOK_SENSITIVITY;
    controller->capture_mouse = 1;
    if (world->camera)
        rt_camera3d_fps_init(world->camera);
    return controller;
}

/// @brief Get the fly speed in units/sec.
double rt_game3d_free_fly_controller_get_speed(void *obj) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

/// @brief Set the fly speed (negatives reset to the default).
void rt_game3d_free_fly_controller_set_speed(void *obj, double speed) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

/// @brief Get the mouse-look sensitivity.
double rt_game3d_free_fly_controller_get_look_sensitivity(void *obj) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.get_lookSensitivity: invalid controller");
    return controller ? controller->look_sensitivity : 0.0;
}

/// @brief Set the mouse-look sensitivity (negatives reset to the default).
void rt_game3d_free_fly_controller_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.set_lookSensitivity: invalid controller");
    if (controller)
        controller->look_sensitivity =
            game3d_nonnegative_or(sensitivity, RT_GAME3D_DEFAULT_LOOK_SENSITIVITY);
}

/// @brief Capture and hide the cursor and remember the captured state.
void rt_game3d_free_fly_controller_capture_mouse(void *obj) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.captureMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 1;
        rt_mouse_capture();
    }
}

/// @brief Release the cursor and clear the captured state.
void rt_game3d_free_fly_controller_release_mouse(void *obj) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.releaseMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 0;
        rt_mouse_release();
    }
}

/// @brief Update free-fly look and 6-DOF movement (including vertical) from input for
///   the frame; re-captures the cursor when capture is enabled.
void rt_game3d_free_fly_controller_update(void *obj, void *world_obj, double dt) {
    rt_game3d_free_fly_controller *controller = game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FreeFlyController.update: invalid world");
    if (!controller || !world || !world->camera || !world->input)
        return;
    dt = game3d_clamp_dt(dt);
    if (controller->capture_mouse)
        rt_mouse_capture();
    double move_x = 0.0;
    double move_y = 0.0;
    double move_z = 0.0;
    game3d_input_move_axis_components((rt_game3d_input *)world->input, &move_x, &move_y, &move_z);
    double yaw = (double)game3d_input_mouse_dx((rt_game3d_input *)world->input) *
                 controller->look_sensitivity;
    double pitch = 0.0 - (double)game3d_input_mouse_dy((rt_game3d_input *)world->input) *
                             controller->look_sensitivity;
    rt_camera3d_fps_update(
        world->camera, yaw, pitch, move_z, move_x, move_y, controller->speed, dt);
}

/// @brief No-op late update (free-fly needs no post-physics pass); validates handles. See header.
void rt_game3d_free_fly_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    (void)game3d_free_fly_controller_checked(
        obj, "Game3D.FreeFlyController.lateUpdate: invalid controller");
    (void)game3d_world_checked(world_obj, "Game3D.FreeFlyController.lateUpdate: invalid world");
}

/// @brief GC finalizer for the orbit controller: release its target reference.
static void game3d_orbit_controller_finalize(void *obj) {
    rt_game3d_orbit_controller *controller = (rt_game3d_orbit_controller *)obj;
    if (controller) {
        game3d_release_ref(&controller->world);
        game3d_release_ref(&controller->target);
    }
}

/// @brief Create an orbit controller circling the given Vec3 target; traps on a
///   non-Vec3 target. See header.
void *rt_game3d_orbit_controller_new(void *world_obj, void *target) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.New: invalid world");
    if (!rt_g3d_is_vec3(target) && !rt_g3d_has_class(target, RT_G3D_GAME3D_ENTITY_CLASS_ID)) {
        rt_trap("Game3D.OrbitController.New: target must be Vec3 or Entity3D");
        return NULL;
    }
    if (!world)
        return NULL;
    rt_game3d_orbit_controller *controller = (rt_game3d_orbit_controller *)rt_obj_new_i64(
        RT_G3D_GAME3D_ORBIT_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.OrbitController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_orbit_controller_finalize);
    game3d_assign_ref(&controller->world, world);
    game3d_assign_ref(&controller->target, target);
    controller->distance = 6.0;
    controller->min_distance = 1.0;
    controller->max_distance = 100.0;
    controller->pitch = 20.0;
    controller->orbit_sensitivity = 0.25;
    controller->zoom_sensitivity = 1.0;
    return controller;
}

/// @brief Get the orbit target Vec3 (NULL if invalid).
void *rt_game3d_orbit_controller_get_target(void *obj) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.get_target: invalid controller");
    return controller ? controller->target : NULL;
}

/// @brief Set the orbit target; traps on a non-Vec3/non-Entity3D.
void rt_game3d_orbit_controller_set_target(void *obj, void *target) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.set_target: invalid controller");
    if (!rt_g3d_is_vec3(target) && !rt_g3d_has_class(target, RT_G3D_GAME3D_ENTITY_CLASS_ID)) {
        rt_trap("Game3D.OrbitController.set_target: target must be Vec3 or Entity3D");
        return;
    }
    if (controller)
        game3d_assign_ref(&controller->target, target);
}

/// @brief Get the orbit distance (radius) in world units.
double rt_game3d_orbit_controller_get_distance(void *obj) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.get_distance: invalid controller");
    return controller ? controller->distance : 0.0;
}

/// @brief Set the orbit distance, clamped to the controller's min/max range.
void rt_game3d_orbit_controller_set_distance(void *obj, double distance) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.set_distance: invalid controller");
    if (controller)
        controller->distance =
            game3d_clamp(distance, controller->min_distance, controller->max_distance);
}

/// @brief Get the horizontal orbit angle (yaw) in degrees.
double rt_game3d_orbit_controller_get_yaw(void *obj) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.get_yaw: invalid controller");
    return controller ? controller->yaw : 0.0;
}

/// @brief Set the yaw angle in degrees (non-finite resets to 0).
void rt_game3d_orbit_controller_set_yaw(void *obj, double yaw) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.set_yaw: invalid controller");
    if (controller)
        controller->yaw = game3d_finite_or(yaw, 0.0);
}

/// @brief Get the vertical orbit angle (pitch) in degrees.
double rt_game3d_orbit_controller_get_pitch(void *obj) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.get_pitch: invalid controller");
    return controller ? controller->pitch : 0.0;
}

/// @brief Set the pitch angle in degrees, clamped to [-85, 85] to avoid gimbal flip.
void rt_game3d_orbit_controller_set_pitch(void *obj, double pitch) {
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.set_pitch: invalid controller");
    if (controller)
        controller->pitch = game3d_clamp(pitch, -85.0, 85.0);
}

/// @brief Update orbit yaw/pitch from left-drag and distance from the wheel; clamps
///   pitch and distance to their ranges. See header.
void rt_game3d_orbit_controller_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.update: invalid world");
    if (!controller || !world || !world->input)
        return;
    if (rt_game3d_input_mouse_button(world->input, rt_game3d_mouse_left())) {
        controller->yaw += (double)game3d_input_mouse_dx((rt_game3d_input *)world->input) *
                           controller->orbit_sensitivity;
        controller->pitch -= (double)game3d_input_mouse_dy((rt_game3d_input *)world->input) *
                             controller->orbit_sensitivity;
        controller->pitch = game3d_clamp(controller->pitch, -85.0, 85.0);
    }
    controller->distance -= game3d_input_wheel_y_snapshot((rt_game3d_input *)world->input) *
                            controller->zoom_sensitivity;
    controller->distance =
        game3d_clamp(controller->distance, controller->min_distance, controller->max_distance);
}

/// @brief After physics, reposition the camera on the orbit sphere around the target. See header.
void rt_game3d_orbit_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_orbit_controller *controller = game3d_orbit_controller_checked(
        obj, "Game3D.OrbitController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.lateUpdate: invalid world");
    if (controller && world && world->camera && controller->target) {
        double target_pos[3];
        if (rt_g3d_has_class(controller->target, RT_G3D_GAME3D_ENTITY_CLASS_ID)) {
            if (!game3d_entity_world_position_components((rt_game3d_entity *)controller->target,
                                                         target_pos))
                return;
        } else if (rt_g3d_is_vec3(controller->target)) {
            target_pos[0] = rt_vec3_x(controller->target);
            target_pos[1] = rt_vec3_y(controller->target);
            target_pos[2] = rt_vec3_z(controller->target);
        } else {
            return;
        }
        rt_camera3d_orbit_components(world->camera,
                                     target_pos[0],
                                     target_pos[1],
                                     target_pos[2],
                                     controller->distance,
                                     controller->yaw,
                                     controller->pitch);
    }
}

/// @brief GC finalizer for the follow controller: release its target and offset.
static void game3d_follow_controller_finalize(void *obj) {
    rt_game3d_follow_controller *controller = (rt_game3d_follow_controller *)obj;
    if (!controller)
        return;
    game3d_release_ref(&controller->world);
    game3d_release_ref(&controller->target_entity);
    game3d_release_ref(&controller->offset);
}

/// @brief Create a follow controller chasing `target_entity` at a Vec3 `offset`; traps
///   on a bad entity or non-Vec3 offset. See header.
void *rt_game3d_follow_controller_new(void *world_obj, void *target_entity, void *offset) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FollowController.New: invalid world");
    if (!game3d_entity_checked(target_entity,
                               "Game3D.FollowController.New: target must be Entity3D"))
        return NULL;
    if (!rt_g3d_is_vec3(offset)) {
        rt_trap("Game3D.FollowController.New: offset must be Vec3");
        return NULL;
    }
    if (!world)
        return NULL;
    rt_game3d_follow_controller *controller = (rt_game3d_follow_controller *)rt_obj_new_i64(
        RT_G3D_GAME3D_FOLLOW_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FollowController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_follow_controller_finalize);
    game3d_assign_ref(&controller->world, world);
    game3d_assign_ref(&controller->target_entity, target_entity);
    game3d_assign_ref(&controller->offset, offset);
    controller->damping = RT_GAME3D_DEFAULT_FOLLOW_DAMPING;
    return controller;
}

/// @brief Get the followed entity (NULL if none/invalid).
void *rt_game3d_follow_controller_get_target(void *obj) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.get_target: invalid controller");
    return controller ? controller->target_entity : NULL;
}

/// @brief Set the followed entity (validated when non-NULL).
void rt_game3d_follow_controller_set_target(void *obj, void *target_entity) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.set_target: invalid controller");
    if (target_entity &&
        !game3d_entity_checked(target_entity,
                               "Game3D.FollowController.set_target: target must be Entity3D"))
        return;
    if (controller)
        game3d_assign_ref(&controller->target_entity, target_entity);
}

/// @brief Get the follow offset Vec3 (NULL if invalid).
void *rt_game3d_follow_controller_get_offset(void *obj) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.get_offset: invalid controller");
    return controller ? controller->offset : NULL;
}

/// @brief Set the follow offset; traps on a non-Vec3.
void rt_game3d_follow_controller_set_offset(void *obj, void *offset) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.set_offset: invalid controller");
    if (!rt_g3d_is_vec3(offset)) {
        rt_trap("Game3D.FollowController.set_offset: offset must be Vec3");
        return;
    }
    if (controller)
        game3d_assign_ref(&controller->offset, offset);
}

/// @brief Get the position-smoothing damping factor.
double rt_game3d_follow_controller_get_damping(void *obj) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.get_damping: invalid controller");
    return controller ? controller->damping : 0.0;
}

/// @brief Set the damping factor (negatives reset to the default).
void rt_game3d_follow_controller_set_damping(void *obj, double damping) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.set_damping: invalid controller");
    if (controller)
        controller->damping = game3d_nonnegative_or(damping, RT_GAME3D_DEFAULT_FOLLOW_DAMPING);
}

/// @brief No-op pre-physics update (follow runs in late update); validates handles. See header.
void rt_game3d_follow_controller_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    (void)game3d_follow_controller_checked(obj,
                                           "Game3D.FollowController.update: invalid controller");
    (void)game3d_world_checked(world_obj, "Game3D.FollowController.update: invalid world");
}

/// @brief After physics, exponentially damp the camera toward target+offset and look at
///   the target. The damping uses a frame-rate-independent `1 - exp(-damping·dt)` blend. See
///   header.
void rt_game3d_follow_controller_late_update(void *obj, void *world_obj, double dt) {
    rt_game3d_follow_controller *controller = game3d_follow_controller_checked(
        obj, "Game3D.FollowController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FollowController.lateUpdate: invalid world");
    if (!controller || !world || !world->camera || !controller->target_entity ||
        !controller->offset)
        return;

    dt = game3d_clamp_dt(dt);
    double target_pos[3];
    double current[3];
    if (!game3d_entity_world_position_components((rt_game3d_entity *)controller->target_entity,
                                                 target_pos))
        return;
    if (!rt_camera3d_get_position_components(world->camera, &current[0], &current[1], &current[2]))
        return;
    double target_x = target_pos[0] + rt_vec3_x(controller->offset);
    double target_y = target_pos[1] + rt_vec3_y(controller->offset);
    double target_z = target_pos[2] + rt_vec3_z(controller->offset);
    double alpha = controller->damping <= 0.0 ? 1.0 : 1.0 - exp(0.0 - controller->damping * dt);
    alpha = game3d_clamp(alpha, 0.0, 1.0);
    double x = current[0] + (target_x - current[0]) * alpha;
    double y = current[1] + (target_y - current[1]) * alpha;
    double z = current[2] + (target_z - current[2]) * alpha;
    rt_camera3d_look_at_components(
        world->camera, x, y, z, target_pos[0], target_pos[1], target_pos[2], 0.0, 1.0, 0.0);
}
