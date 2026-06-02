//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_input.c
// Purpose: Input3D for the Viper.Game3D layer — per-frame keyboard/mouse query
//   with optional latched snapshot for deterministic replay. Split out of
//   rt_game3d.c; shares private types/helpers via rt_game3d_internal.h.
// Links: rt_game3d_internal.h, rt_input.h
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

/// @brief Allocate an Input3D handle with default look sensitivity; traps on OOM.
void *rt_game3d_input_new(void) {
    rt_game3d_input *input =
        (rt_game3d_input *)rt_obj_new_i64(RT_G3D_GAME3D_INPUT_CLASS_ID, (int64_t)sizeof(*input));
    if (!input) {
        rt_trap("Game3D.Input3D.New: allocation failed");
        return NULL;
    }
    memset(input, 0, sizeof(*input));
    input->look_sensitivity = 0.01;
    return input;
}

/// @brief Get the per-object look sensitivity (0 on invalid handle).
double rt_game3d_input_get_look_sensitivity(void *obj) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.get_LookSensitivity: invalid input");
    return input ? input->look_sensitivity : 0.0;
}

/// @brief Set the look sensitivity (negative/non-finite values reset to the default).
void rt_game3d_input_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.set_LookSensitivity: invalid input");
    if (!input)
        return;
    input->look_sensitivity =
        game3d_nonnegative_clamped_or(sensitivity, 0.01, RT_GAME3D_LOOK_SENSITIVITY_MAX);
}

/// @brief Roll input edge state forward one frame; the shared device state is polled
///   by the canvas, then copied here so each Input3D object observes a coherent
///   per-frame snapshot even if later polling mutates the process-wide state.
void rt_game3d_input_update(void *obj) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.update: invalid input");
    if (!input)
        return;
    for (int64_t key = 0; key < VIPER_KEY_MAX; key++) {
        input->key_down[key] = rt_keyboard_is_down(key) ? 1 : 0;
        input->key_pressed[key] = rt_keyboard_was_pressed(key) ? 1 : 0;
        input->key_released[key] = rt_keyboard_was_released(key) ? 1 : 0;
    }
    for (int64_t button = 0; button < VIPER_MOUSE_BUTTON_MAX; button++) {
        input->mouse_down[button] = rt_mouse_is_down(button) ? 1 : 0;
        input->mouse_pressed[button] = rt_mouse_was_pressed(button) ? 1 : 0;
        input->mouse_released[button] = rt_mouse_was_released(button) ? 1 : 0;
    }
    input->mouse_dx = rt_mouse_delta_x();
    input->mouse_dy = rt_mouse_delta_y();
    input->wheel_y = game3d_finite_or(rt_mouse_wheel_yf(), 0.0);
    input->has_snapshot = 1;
}

/// @brief True while `key` is held this frame (queries shared keyboard state).
int8_t rt_game3d_input_is_down(void *obj, int64_t key) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.isDown: invalid input");
    return game3d_input_key_down(input, key);
}

/// @brief True on the frame `key` transitions to down.
int8_t rt_game3d_input_pressed(void *obj, int64_t key) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.pressed: invalid input");
    return game3d_input_key_pressed(input, key);
}

/// @brief True on the frame `key` transitions to up.
int8_t rt_game3d_input_released(void *obj, int64_t key) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.released: invalid input");
    return game3d_input_key_released(input, key);
}

/// @brief Get this frame's raw mouse movement delta as a Vec2.
void *rt_game3d_input_mouse_delta(void *obj) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.mouseDelta: invalid input");
    return rt_vec2_new((double)game3d_input_mouse_dx(input), (double)game3d_input_mouse_dy(input));
}

/// @brief True while mouse `button` is held this frame.
int8_t rt_game3d_input_mouse_button(void *obj, int64_t button) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.mouseButton: invalid input");
    return game3d_input_mouse_down(input, button);
}

/// @brief True on the frame mouse `button` transitions to down.
int8_t rt_game3d_input_mouse_pressed(void *obj, int64_t button) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.mousePressed: invalid input");
    return game3d_input_mouse_pressed_snapshot(input, button);
}

/// @brief Get this frame's mouse wheel scroll delta along Y.
double rt_game3d_input_wheel_y(void *obj) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.wheelY: invalid input");
    return game3d_input_wheel_y_snapshot(input);
}

/// @brief Compute a normalized WASD/arrow move axis from the input state into x/z components.
/// @details Combines the held direction keys into a unit-ish 2D vector for character/camera
/// movement.
void game3d_input_move_axis_components(rt_game3d_input *input,
                                       double *out_x,
                                       double *out_y,
                                       double *out_z) {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (game3d_input_key_down(input, rt_keyboard_key_d()) ||
        game3d_input_key_down(input, rt_keyboard_key_right()))
        x += 1.0;
    if (game3d_input_key_down(input, rt_keyboard_key_a()) ||
        game3d_input_key_down(input, rt_keyboard_key_left()))
        x -= 1.0;
    if (game3d_input_key_down(input, rt_keyboard_key_w()) ||
        game3d_input_key_down(input, rt_keyboard_key_up()))
        z += 1.0;
    if (game3d_input_key_down(input, rt_keyboard_key_s()) ||
        game3d_input_key_down(input, rt_keyboard_key_down()))
        z -= 1.0;
    if (game3d_input_key_down(input, rt_keyboard_key_space()))
        y += 1.0;
    if (game3d_input_key_down(input, rt_keyboard_key_shift()) ||
        game3d_input_key_down(input, rt_keyboard_key_ctrl()))
        y -= 1.0;
    game3d_normalize_axis3(&x, &y, &z);
    if (out_x)
        *out_x = x;
    if (out_y)
        *out_y = y;
    if (out_z)
        *out_z = z;
}

/// @brief Build the WASD/arrow/space/shift movement axis as a Vec3
///   (x = strafe, y = up/down, z = forward/back); see header.
void *rt_game3d_input_move_axis(void *obj) {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.moveAxis: invalid input");
    game3d_input_move_axis_components(input, &x, &y, &z);
    return rt_vec3_new(x, y, z);
}

/// @brief Build the mouse-look axis as a Vec2 (mouse delta scaled by sensitivity).
void *rt_game3d_input_look_axis(void *obj) {
    rt_game3d_input *input = game3d_input_checked(obj, "Game3D.Input3D.lookAxis: invalid input");
    double s =
        input ? game3d_nonnegative_clamped_or(input->look_sensitivity,
                                              0.01,
                                              RT_GAME3D_LOOK_SENSITIVITY_MAX)
              : 0.01;
    double x = game3d_clamp_abs_or((double)game3d_input_mouse_dx(input) * s,
                                   0.0,
                                   RT_GAME3D_ANGLE_DEG_ABS_MAX);
    double y = game3d_clamp_abs_or((double)game3d_input_mouse_dy(input) * s,
                                   0.0,
                                   RT_GAME3D_ANGLE_DEG_ABS_MAX);
    return rt_vec2_new(x, y);
}

/// @brief Capture and hide the OS cursor for relative mouse-look.
void rt_game3d_input_capture_mouse(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.captureMouse: invalid input");
    rt_mouse_capture();
}

/// @brief Release the captured cursor back to the OS.
void rt_game3d_input_release_mouse(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.releaseMouse: invalid input");
    rt_mouse_release();
}
