//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_game3d.cpp
// Purpose: Unit tests for the runtime-backed Viper.Game3D helper layer.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_game3d.h"
#include "rt_input.h"
#include "rt_model3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_postfx3d.h"
#include "rt_scene3d.h"
#include "rt_string.h"
#include "rt_vec2.h"
#include "rt_vec3.h"

#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;
static int g_tests_passed = 0;
static int g_tests_total = 0;
static int g_update_calls = 0;
static int g_overlay_calls = 0;
static double g_update_dt_sum = 0.0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_trap_jmp, 1);
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

#define TEST(name)                                                                                 \
    do {                                                                                           \
        ++g_tests_total;                                                                           \
        std::printf("  [%d] %s... ", g_tests_total, name);                                         \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        ++g_tests_passed;                                                                          \
        std::printf("ok\n");                                                                      \
        return true;                                                                               \
    } while (0)

#define FAIL(msg)                                                                                  \
    do {                                                                                           \
        std::printf("FAIL: %s\n", msg);                                                           \
        return false;                                                                              \
    } while (0)

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        if (!(cond))                                                                               \
            FAIL(msg);                                                                             \
    } while (0)

#define EXPECT_EQ_INT(actual, expected, msg)                                                        \
    do {                                                                                           \
        const long long got_ = (long long)(actual);                                                 \
        const long long want_ = (long long)(expected);                                              \
        if (got_ != want_) {                                                                       \
            std::printf("FAIL: %s (got %lld, expected %lld)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(actual, expected, eps, msg)                                                     \
    do {                                                                                           \
        const double got_ = (double)(actual);                                                       \
        const double want_ = (double)(expected);                                                    \
        if (std::fabs(got_ - want_) > (eps)) {                                                      \
            std::printf("FAIL: %s (got %.6f, expected %.6f)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static rt_string test_fixture_path(const char *relative) {
#ifdef VIPER_SOURCE_DIR
    char path[4096];
    std::snprintf(path, sizeof(path), "%s/%s", VIPER_SOURCE_DIR, relative);
    return rt_const_cstr(path);
#else
    return rt_const_cstr(relative);
#endif
}

template <typename Fn>
static bool expect_trap_contains(Fn &&fn, const char *needle) {
    g_last_trap = nullptr;
    g_expect_trap = true;
    if (setjmp(g_trap_jmp) == 0) {
        fn();
        g_expect_trap = false;
        return false;
    }
    g_expect_trap = false;
    return g_last_trap && (!needle || std::strstr(g_last_trap, needle) != nullptr);
}

static void set_software_backend_env() {
#ifdef _WIN32
    _putenv_s("VIPER_3D_BACKEND", "software");
#else
    setenv("VIPER_3D_BACKEND", "software", 1);
#endif
}

extern "C" void game3d_test_update(double dt) {
    ++g_update_calls;
    g_update_dt_sum += dt;
}

extern "C" void game3d_test_overlay(void) {
    ++g_overlay_calls;
}

static bool test_layermasks_and_constants() {
    TEST("Game3D constants and LayerMask operations");
    EXPECT_EQ_INT(rt_game3d_layers_world(), 1, "World layer bit");
    EXPECT_EQ_INT(rt_game3d_layers_dynamic(), 2, "Dynamic layer bit");
    EXPECT_EQ_INT(rt_game3d_layers_player(), 4, "Player layer bit");
    EXPECT_EQ_INT(rt_game3d_quality_balanced(), 1, "Balanced quality value");
    EXPECT_EQ_INT(rt_game3d_collision_any(), 3, "Any collision phase value");

    void *mask = rt_game3d_layermask_of(rt_game3d_layers_world());
    EXPECT_TRUE(mask != nullptr, "LayerMask.Of returns an object");
    EXPECT_EQ_INT(rt_game3d_layermask_get_bits(mask),
                  rt_game3d_layers_world(),
                  "LayerMask.Of stores the layer bit");
    EXPECT_TRUE(rt_game3d_layermask_include(mask, rt_game3d_layers_player()) == mask,
                "LayerMask.include is fluent");
    EXPECT_TRUE(rt_game3d_layermask_includes(mask, rt_game3d_layers_world()) != 0,
                "LayerMask includes initial layer");
    EXPECT_TRUE(rt_game3d_layermask_includes(mask, rt_game3d_layers_player()) != 0,
                "LayerMask includes appended layer");
    EXPECT_TRUE(rt_game3d_layermask_includes(mask, rt_game3d_layers_trigger()) == 0,
                "LayerMask excludes missing layer");

    void *all = rt_game3d_layermask_all();
    EXPECT_EQ_INT(rt_game3d_layermask_get_bits(all), INT64_MAX, "LayerMask.All uses all safe bits");
    rt_game3d_layermask_set_bits(all, -1);
    EXPECT_EQ_INT(rt_game3d_layermask_get_bits(all), INT64_MAX, "negative mask bits clamp to all");

    EXPECT_TRUE(expect_trap_contains([&] { rt_game3d_layermask_of(3); }, "single positive bit"),
                "LayerMask.Of rejects multi-bit layers");
    EXPECT_TRUE(expect_trap_contains([&] { rt_game3d_layermask_include(mask, 0); },
                                     "single positive bit"),
                "LayerMask.include rejects zero layer");
    PASS();
}

static bool test_input_axes() {
    TEST("Input3D delegates named input and exposes move/look axes");
    rt_keyboard_init();
    rt_mouse_init();
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();

    void *input = rt_game3d_input_new();
    rt_game3d_input_set_look_sensitivity(input, 0.05);

    rt_keyboard_on_key_down(rt_game3d_key_w());
    rt_keyboard_on_key_down(rt_game3d_key_space());
    rt_mouse_force_delta(12, -4);
    rt_mouse_button_down(rt_game3d_mouse_left());
    rt_mouse_update_wheel(0.0, 1.25);

    EXPECT_TRUE(rt_game3d_input_is_down(input, rt_game3d_key_w()) != 0, "W is down");
    EXPECT_TRUE(rt_game3d_input_pressed(input, rt_game3d_key_w()) != 0, "W was pressed");
    EXPECT_TRUE(rt_game3d_input_mouse_button(input, rt_game3d_mouse_left()) != 0,
                "left mouse is down");
    EXPECT_TRUE(rt_game3d_input_mouse_pressed(input, rt_game3d_mouse_left()) != 0,
                "left mouse was pressed");
    EXPECT_NEAR(rt_game3d_input_wheel_y(input), 1.25, 0.0001, "wheelY is delegated");

    void *move = rt_game3d_input_move_axis(input);
    EXPECT_NEAR(rt_vec3_x(move), 0.0, 0.0001, "move axis x");
    EXPECT_NEAR(rt_vec3_y(move), 1.0, 0.0001, "move axis y");
    EXPECT_NEAR(rt_vec3_z(move), 1.0, 0.0001, "move axis z");

    void *look = rt_game3d_input_look_axis(input);
    EXPECT_NEAR(rt_vec2_x(look), 0.60, 0.0001, "look axis x scales mouse delta");
    EXPECT_NEAR(rt_vec2_y(look), -0.20, 0.0001, "look axis y scales mouse delta");

    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_keyboard_on_key_up(rt_game3d_key_w());
    rt_keyboard_on_key_up(rt_game3d_key_space());
    rt_mouse_button_up(rt_game3d_mouse_left());
    EXPECT_TRUE(rt_game3d_input_released(input, rt_game3d_key_w()) != 0, "W was released");
    PASS();
}

static bool test_world_entity_registry_and_collision_clear() {
    TEST("World3D owns entities, bodies, names, and collision events");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Registry"), 80, 60);
    EXPECT_TRUE(world != nullptr, "World3D.New returns an object");
    EXPECT_TRUE(rt_game3d_world_get_canvas(world) != nullptr, "World3D has a canvas");
    EXPECT_TRUE(rt_game3d_world_get_camera(world) != nullptr, "World3D has a camera");
    EXPECT_TRUE(rt_game3d_world_get_scene(world) != nullptr, "World3D has a scene");
    EXPECT_TRUE(rt_game3d_world_get_physics(world) != nullptr, "World3D has physics");
    EXPECT_TRUE(rt_game3d_world_get_input(world) != nullptr, "World3D has input");
    EXPECT_TRUE(rt_game3d_world_get_audio(world) != nullptr, "World3D has audio");
    EXPECT_TRUE(rt_game3d_audio_get_listener(rt_game3d_world_get_audio(world)) != nullptr,
                "World3D audio creates a listener");
    EXPECT_TRUE(rt_game3d_world_get_effects(world) != nullptr, "World3D has effects");
    EXPECT_TRUE(rt_game3d_effects_get_postfx(rt_game3d_world_get_effects(world)) != nullptr,
                "World3D effects create post-FX");

    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *material = rt_material3d_new_color(0.8, 0.2, 0.1);
    void *parent = rt_game3d_entity_of(mesh, material);
    void *child = rt_game3d_entity_new();
    void *body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *other_body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *other = rt_game3d_entity_new();

    rt_game3d_entity_set_name(parent, rt_const_cstr("Player"));
    rt_game3d_entity_set_name(child, rt_const_cstr("Muzzle"));
    rt_game3d_entity_set_name(other, rt_const_cstr("Wall"));
    rt_game3d_entity_add_child(parent, child);
    rt_game3d_entity_set_layer(parent, rt_game3d_layers_player());
    rt_game3d_entity_set_collision_mask(
        parent, rt_game3d_layermask_of(rt_game3d_layers_world()));
    rt_game3d_entity_attach_body(parent, body);
    rt_game3d_entity_set_layer(other, rt_game3d_layers_world());
    rt_game3d_entity_attach_body(other, other_body);
    rt_game3d_entity_set_position(parent, 0.5, 0.0, 0.0);
    rt_game3d_entity_set_position(other, 0.0, 0.0, 0.0);

    EXPECT_TRUE(rt_game3d_entity_is_group(parent) != 0, "Entity3D.addChild marks group");
    rt_game3d_world_spawn(world, parent);
    rt_game3d_world_spawn(world, other);

    EXPECT_EQ_INT(rt_game3d_entity_get_id(parent), 1, "spawn assigns parent id");
    EXPECT_EQ_INT(rt_game3d_entity_get_id(child), 2, "spawn assigns child id");
    EXPECT_TRUE(rt_game3d_entity_is_spawned(parent) != 0, "parent is spawned");
    EXPECT_TRUE(rt_game3d_entity_is_spawned(child) != 0, "child is spawned");
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  2,
                  "spawn registers entity bodies with physics");
    EXPECT_EQ_INT(rt_body3d_get_collision_layer(body),
                  rt_game3d_layers_player(),
                  "entity layer propagates to body");
    EXPECT_EQ_INT(rt_body3d_get_collision_mask(body),
                  rt_game3d_layers_world(),
                  "entity collision mask propagates to body");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("Muzzle")) ==
                    rt_game3d_entity_get_node(child),
                "findNode sees child scene node");
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("Player")) == parent,
                "findEntity returns registered entity");
    EXPECT_EQ_INT(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)),
                  4,
                  "scene contains root, parent, child, and wall");

    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_collision_event_count(world, rt_game3d_collision_any()) > 0,
                "overlapping spawned bodies emit collisions");
    EXPECT_TRUE(rt_game3d_world_collision_event(world, rt_game3d_collision_any(), 0) != nullptr,
                "collisionEvent returns a boxed event");
    rt_game3d_world_clear_collision_events(world);
    EXPECT_EQ_INT(rt_game3d_world_collision_event_count(world, rt_game3d_collision_any()),
                  0,
                  "clearCollisionEvents clears live contacts");
    EXPECT_EQ_INT(rt_game3d_world_collision_event_count(world, rt_game3d_collision_enter()),
                  0,
                  "clearCollisionEvents clears enter events");

    rt_game3d_world_despawn(world, parent);
    EXPECT_TRUE(rt_game3d_entity_is_spawned(parent) == 0, "despawn clears parent spawned flag");
    EXPECT_TRUE(rt_game3d_entity_is_spawned(child) == 0, "despawn clears child spawned flag");
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "despawn removes entity body from physics");
    EXPECT_EQ_INT(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)),
                  2,
                  "despawn detaches the parent subtree from the scene");

    rt_game3d_world_destroy(world);
    EXPECT_TRUE(rt_game3d_world_is_destroyed(world) != 0, "destroy marks world destroyed");
    EXPECT_TRUE(rt_game3d_entity_is_destroyed(other) != 0,
                "destroy marks still-spawned entities destroyed");
    PASS();
}

static bool test_frame_loop_manual_frame_and_final_capture() {
    TEST("World3D runFrames, manual frame API, overlay, and final capture");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Frames"), 64, 48);
    EXPECT_TRUE(world != nullptr, "World3D.New returns an object");

    g_update_calls = 0;
    g_update_dt_sum = 0.0;
    rt_game3d_world_run_frames(world, 3, 0.02, (void *)&game3d_test_update);
    EXPECT_EQ_INT(g_update_calls, 3, "runFrames invokes the update callback once per frame");
    EXPECT_NEAR(g_update_dt_sum, 0.06, 0.0001, "runFrames passes fixed dt to callback");
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 3, "runFrames increments frame count");
    EXPECT_NEAR(rt_game3d_world_get_dt(world), 0.02, 0.0001, "runFrames stores dt");
    EXPECT_NEAR(rt_game3d_world_get_elapsed(world), 0.06, 0.0001, "runFrames stores elapsed time");

    g_overlay_calls = 0;
    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    rt_game3d_world_draw_overlay(world, (void *)&game3d_test_overlay);
    EXPECT_EQ_INT(g_overlay_calls, 1, "drawOverlay invokes overlay callback");

    void *pixels = rt_game3d_world_capture_final_frame(world);
    EXPECT_TRUE(pixels != nullptr, "captureFinalFrame returns Pixels");
    EXPECT_EQ_INT(rt_pixels_width(pixels), 64, "captured final frame width");
    EXPECT_EQ_INT(rt_pixels_height(pixels), 48, "captured final frame height");
    rt_game3d_world_present(world);
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_free_fly_controller_synthetic_input() {
    TEST("FreeFlyController consumes synthetic input deterministically");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit FreeFly"), 64, 48);
    void *canvas = rt_game3d_world_get_canvas(world);
    void *camera = rt_game3d_world_get_camera(world);
    void *controller = rt_game3d_free_fly_controller_new(world);
    EXPECT_TRUE(controller != nullptr, "FreeFlyController.New returns an object");
    rt_game3d_free_fly_controller_set_speed(controller, 10.0);
    rt_game3d_free_fly_controller_set_look_sensitivity(controller, 0.10);
    rt_game3d_world_set_camera_controller(world, controller);

    void *before = rt_camera3d_get_position(camera);
    rt_canvas3d_push_synthetic_key(canvas, rt_game3d_key_w(), 1);
    rt_canvas3d_push_synthetic_mouse(canvas, 12.0, -4.0, 0, 0.0);
    rt_game3d_world_run_frames_only(world, 1, 0.1);
    void *after = rt_camera3d_get_position(camera);

    EXPECT_TRUE(rt_vec3_z(after) < rt_vec3_z(before) - 0.05,
                "W moves the free-fly camera forward before simulation");
    EXPECT_TRUE(std::fabs(rt_vec3_x(after) - rt_vec3_x(before)) > 0.001,
                "synthetic mouse look changes the free-fly heading");
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 1, "controller frame increments with runFramesOnly");
    rt_canvas3d_clear_synthetic_input(canvas);
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_orbit_and_follow_controllers() {
    TEST("OrbitController and FollowController run update/lateUpdate phases");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Orbit Follow"), 80, 60);
    void *canvas = rt_game3d_world_get_canvas(world);

    void *target = rt_vec3_new(0.0, 1.0, 0.0);
    void *orbit = rt_game3d_orbit_controller_new(world, target);
    rt_game3d_world_set_camera_controller(world, orbit);
    rt_canvas3d_push_synthetic_mouse(canvas, 20.0, -8.0, 1, 1.0);
    rt_game3d_world_run_frames_only(world, 1, 0.1);
    EXPECT_TRUE(rt_game3d_orbit_controller_get_yaw(orbit) > 0.0,
                "orbit mouse drag updates yaw during update");
    EXPECT_TRUE(rt_game3d_orbit_controller_get_distance(orbit) < 6.0,
                "orbit wheel input updates distance during update");

    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 4.0, 0.5, -2.0);
    rt_game3d_world_spawn(world, entity);
    void *offset = rt_vec3_new(0.0, 2.0, 5.0);
    void *follow = rt_game3d_follow_controller_new(world, entity, offset);
    rt_game3d_follow_controller_set_damping(follow, 0.0);
    rt_game3d_world_set_camera_controller(world, follow);
    rt_game3d_world_run_frames_only(world, 1, 0.1);
    void *pos = rt_camera3d_get_position(rt_game3d_world_get_camera(world));
    EXPECT_NEAR(rt_vec3_x(pos), 4.0, 0.0001, "follow snaps x when damping is zero");
    EXPECT_NEAR(rt_vec3_y(pos), 2.5, 0.0001, "follow applies y offset after physics");
    EXPECT_NEAR(rt_vec3_z(pos), 3.0, 0.0001, "follow applies z offset after physics");

    rt_game3d_entity_set_position(entity, 6.0, 0.5, -3.0);
    rt_game3d_world_step_simulation(world, 0.1);
    pos = rt_camera3d_get_position(rt_game3d_world_get_camera(world));
    EXPECT_NEAR(rt_vec3_x(pos), 6.0, 0.0001, "follow tracks post-physics entity x");
    EXPECT_NEAR(rt_vec3_z(pos), 2.0, 0.0001, "follow tracks post-physics entity z");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_first_person_character_controller_same_frame_motion() {
    TEST("FirstPersonController drives CharacterController3D before physics");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Character"), 80, 60);
    void *canvas = rt_game3d_world_get_canvas(world);

    void *ground = rt_game3d_entity_new();
    void *ground_body = rt_body3d_new_aabb(10.0, 0.5, 10.0, 0.0);
    rt_game3d_entity_attach_body(ground, ground_body);
    rt_game3d_entity_set_position(ground, 0.0, -0.5, 0.0);
    rt_game3d_world_spawn(world, ground);

    void *player = rt_game3d_entity_new();
    rt_game3d_entity_set_position(player, 0.0, 1.0, 4.0);
    rt_game3d_world_spawn(world, player);

    void *character = rt_game3d_character_controller_new(world, player, 0.32, 1.8, 70.0);
    void *fps = rt_game3d_first_person_controller_new(world);
    rt_game3d_first_person_controller_set_character(fps, character);
    rt_game3d_first_person_controller_set_speed(fps, 5.0);
    rt_game3d_world_set_camera_controller(world, fps);

    void *before = rt_game3d_entity_position(player);
    rt_canvas3d_push_synthetic_key(canvas, rt_game3d_key_w(), 1);
    rt_game3d_world_run_frames_only(world, 1, 0.1);
    void *after = rt_game3d_entity_position(player);
    EXPECT_TRUE(rt_vec3_z(after) < rt_vec3_z(before) - 0.05,
                "first-person W moves the character in the same run frame");

    void *camera_pos = rt_camera3d_get_position(rt_game3d_world_get_camera(world));
    EXPECT_NEAR(rt_vec3_x(camera_pos), rt_vec3_x(after), 0.0001, "camera lateUpdate follows character x");
    EXPECT_NEAR(rt_vec3_z(camera_pos), rt_vec3_z(after), 0.0001, "camera lateUpdate follows character z");
    EXPECT_TRUE(rt_vec3_y(camera_pos) > rt_vec3_y(after),
                "camera lateUpdate places the eye above the character");

    rt_canvas3d_clear_synthetic_input(canvas);
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_phase3_material_presets_and_prefabs() {
    TEST("Game3D material presets and prefab factories create usable runtime objects");
    void *plastic = rt_game3d_materials_plastic(0.8, 0.2, 0.1);
    void *metal = rt_game3d_materials_metal(0.7, 0.72, 0.75);
    void *rubber = rt_game3d_materials_rubber(0.05, 0.05, 0.05);
    void *glass = rt_game3d_materials_glass(0.2, 0.6, 0.8, 0.35);
    void *emissive = rt_game3d_materials_emissive(0.8, 0.3, 0.1, 2.5);
    void *unlit = rt_game3d_materials_unlit(0.1, 0.9, 0.4);
    void *pixels = rt_pixels_new(2, 2);
    void *textured = rt_game3d_materials_from_albedo_map(pixels);

    EXPECT_TRUE(plastic != nullptr, "Plastic returns Material3D");
    EXPECT_EQ_INT(rt_material3d_get_shading_model(plastic),
                  rt_game3d_shading_model_pbr(),
                  "Plastic uses PBR shading");
    EXPECT_NEAR(rt_material3d_get_metallic(plastic), 0.0, 0.0001, "Plastic metallic");
    EXPECT_NEAR(rt_material3d_get_metallic(metal), 1.0, 0.0001, "Metal metallic");
    EXPECT_TRUE(rt_material3d_get_roughness(rubber) > 0.80, "Rubber is rough");
    EXPECT_EQ_INT(rt_material3d_get_alpha_mode(glass),
                  rt_game3d_alpha_mode_blend(),
                  "Glass uses blend alpha");
    EXPECT_NEAR(rt_material3d_get_alpha(glass), 0.35, 0.0001, "Glass alpha");
    EXPECT_TRUE(rt_material3d_get_double_sided(glass) != 0, "Glass is double-sided");
    EXPECT_EQ_INT(rt_material3d_get_shading_model(emissive),
                  rt_game3d_shading_model_emissive(),
                  "Emissive uses emissive shading");
    EXPECT_NEAR(rt_material3d_get_emissive_intensity(emissive),
                2.5,
                0.0001,
                "Emissive intensity");
    EXPECT_TRUE(rt_material3d_get_unlit(unlit) != 0, "Unlit marks material unlit");
    EXPECT_TRUE(textured != nullptr, "FromAlbedoMap returns Material3D");

    void *box = rt_game3d_prefab_box(1.5, plastic);
    void *box_xyz = rt_game3d_prefab_box_xyz(1.0, 2.0, 3.0, metal);
    void *sphere = rt_game3d_prefab_sphere(0.75, 16, rubber);
    void *cylinder = rt_game3d_prefab_cylinder(0.5, 2.0, 16, glass);
    void *plane = rt_game3d_prefab_plane(4.0, 5.0, unlit);
    void *ground = rt_game3d_prefab_ground(10.0, plastic);

    EXPECT_TRUE(rt_game3d_entity_get_mesh(box) != nullptr, "Box prefab has mesh");
    EXPECT_EQ_INT(rt_mesh3d_get_triangle_count(rt_game3d_entity_get_mesh(box)),
                  12,
                  "Box prefab uses box mesh");
    EXPECT_TRUE(rt_game3d_entity_get_material(box) == plastic, "Box prefab retains caller material");
    EXPECT_TRUE(rt_game3d_entity_get_mesh(box_xyz) != nullptr, "BoxXYZ prefab has mesh");
    EXPECT_TRUE(rt_mesh3d_get_triangle_count(rt_game3d_entity_get_mesh(sphere)) > 0,
                "Sphere prefab has triangles");
    EXPECT_TRUE(rt_mesh3d_get_triangle_count(rt_game3d_entity_get_mesh(cylinder)) > 0,
                "Cylinder prefab has triangles");
    EXPECT_EQ_INT(rt_mesh3d_get_triangle_count(rt_game3d_entity_get_mesh(plane)),
                  2,
                  "Plane prefab is a quad");
    EXPECT_EQ_INT(rt_game3d_entity_get_layer(ground),
                  rt_game3d_layers_world(),
                  "Ground prefab uses world layer");
    PASS();
}

static bool test_phase3_world_presets_environment_and_debug() {
    TEST("Game3D lighting, quality, environment, post-FX, and debug helpers compose");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Presets"), 96, 72);
    void *canvas = rt_game3d_world_get_canvas(world);

    rt_game3d_lighting_studio(world);
    EXPECT_EQ_INT(rt_canvas3d_get_light_count(canvas), 2, "Studio lighting installs two lights");

    void *sun = rt_vec3_new(-0.2, -1.0, -0.3);
    rt_game3d_lighting_outdoor(world, sun);
    EXPECT_EQ_INT(rt_canvas3d_get_light_count(canvas), 1, "Outdoor lighting installs one sun");
    rt_game3d_lighting_night(world);
    EXPECT_EQ_INT(rt_canvas3d_get_light_count(canvas), 2, "Night lighting installs moon and fill");
    rt_game3d_lighting_interior(world);
    EXPECT_EQ_INT(rt_canvas3d_get_light_count(canvas), 2, "Interior lighting installs key and rim");
    rt_game3d_lighting_clear(world);
    EXPECT_EQ_INT(rt_canvas3d_get_light_count(canvas), 0, "Lighting.Clear removes lights");

    rt_game3d_quality_apply(world, rt_game3d_quality_cinematic());
    EXPECT_EQ_INT(rt_canvas3d_get_quality_requested(canvas),
                  rt_game3d_quality_cinematic(),
                  "Quality.Apply records requested level");
    EXPECT_TRUE(rt_canvas3d_get_quality_active(canvas) >= rt_game3d_quality_performance() &&
                    rt_canvas3d_get_quality_active(canvas) <= rt_game3d_quality_cinematic(),
                "Quality.Apply leaves a valid active level after fallback");

    rt_game3d_postfx_cinematic(world);
    void *fx = rt_game3d_effects_get_postfx(rt_game3d_world_get_effects(world));
    EXPECT_TRUE(fx != nullptr, "PostFX.Cinematic installs a chain");
    EXPECT_TRUE(rt_postfx3d_get_effect_count(fx) >= 4, "PostFX.Cinematic has visible effects");
    rt_game3d_postfx_crisp(world);
    fx = rt_game3d_effects_get_postfx(rt_game3d_world_get_effects(world));
    EXPECT_TRUE(fx != nullptr, "PostFX.Crisp installs a chain");
    EXPECT_TRUE(rt_postfx3d_get_effect_count(fx) >= 2, "PostFX.Crisp has visible effects");
    rt_game3d_postfx_none(world);
    fx = rt_game3d_effects_get_postfx(rt_game3d_world_get_effects(world));
    EXPECT_TRUE(fx != nullptr && rt_postfx3d_get_enabled(fx) == 0, "PostFX.None disables chain");

    int64_t nodes_before = rt_scene3d_get_node_count(rt_game3d_world_get_scene(world));
    void *env = rt_game3d_environment_outdoor(world);
    EXPECT_TRUE(env != nullptr, "Environment.Outdoor returns EnvHandle");
    EXPECT_TRUE(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)) > nodes_before,
                "Environment.Outdoor spawns terrain");
    EXPECT_TRUE(rt_world3d_body_count(rt_game3d_world_get_physics(world)) >= 1,
                "Environment terrain gets a static body");
    rt_game3d_env_handle_with_terrain(env, 48.0, -0.05);
    rt_game3d_env_handle_with_water(env, 0.05);
    rt_game3d_env_handle_with_fog(env, 5.0, 50.0);
    EXPECT_TRUE(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)) > nodes_before + 1,
                "EnvHandle.withWater spawns water");
    EXPECT_TRUE(rt_game3d_environment_sunset(world) != nullptr,
                "Environment.Sunset returns EnvHandle");
    EXPECT_TRUE(rt_game3d_environment_overcast(world) != nullptr,
                "Environment.Overcast returns EnvHandle");
    EXPECT_TRUE(rt_game3d_environment_night(world) != nullptr,
                "Environment.Night returns EnvHandle");

    rt_game3d_debug_show_overlay(world, 1);
    rt_game3d_debug_draw_axes(world, rt_vec3_new(0.0, 0.0, 0.0), 1.5);
    rt_game3d_debug_draw_physics(world, 1);
    rt_game3d_debug_draw_camera_info(world, 1);
    rt_game3d_debug_draw_capabilities(world, 1);

    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    void *final_pixels = rt_game3d_world_capture_final_frame(world);
    EXPECT_TRUE(final_pixels != nullptr, "debug overlay finalizes into captured Pixels");
    EXPECT_EQ_INT(rt_pixels_width(final_pixels), 96, "debug final frame width");
    EXPECT_EQ_INT(rt_pixels_height(final_pixels), 72, "debug final frame height");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_phase4_assets3d_model_templates() {
    TEST("Assets3D loads models and caches ModelTemplate objects");
    rt_string path = test_fixture_path("tests/runtime/assets/gltf/load_asset_triangle.gltf");

    rt_game3d_assets_clear_cache();

    void *model_entity = rt_game3d_assets_load_model(path);
    EXPECT_TRUE(model_entity != nullptr, "Assets3D.LoadModel returns an entity");
    EXPECT_TRUE(rt_game3d_entity_is_group(model_entity) != 0, "loaded model entity is a group");
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(model_entity)),
                  1,
                  "loaded model preserves imported root children");

    void *asset_entity = rt_game3d_assets_load_model_asset(path);
    EXPECT_TRUE(asset_entity != nullptr, "Assets3D.LoadModelAsset returns an entity");
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(asset_entity)),
                  1,
                  "asset-loaded model preserves imported root children");

    void *tpl1 = rt_game3d_assets_load_model_template(path);
    void *tpl2 = rt_game3d_assets_load_model_template(path);
    EXPECT_TRUE(tpl1 != nullptr && tpl1 == tpl2, "LoadModelTemplate reuses cached template");
    EXPECT_TRUE(rt_game3d_model_template_get_model(tpl1) != nullptr,
                "ModelTemplate exposes raw Model3D");
    EXPECT_EQ_INT(rt_model3d_get_mesh_count(rt_game3d_model_template_get_model(tpl1)),
                  1,
                  "ModelTemplate retains imported meshes");
    EXPECT_TRUE(rt_game3d_model_template_get_is_asset(tpl1) == 0,
                "filesystem template reports non-asset path");

    void *inst = rt_game3d_model_template_instantiate(tpl1);
    EXPECT_TRUE(inst != nullptr, "ModelTemplate.instantiate returns an Entity3D");
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(inst)),
                  1,
                  "ModelTemplate.instantiate clones the model root subtree");

    void *asset_tpl = rt_game3d_assets_load_model_template_asset(path);
    EXPECT_TRUE(asset_tpl != nullptr, "LoadModelTemplateAsset returns a template");
    EXPECT_TRUE(rt_game3d_model_template_get_is_asset(asset_tpl) != 0,
                "asset template records asset resolver path");
    EXPECT_EQ_INT(rt_model3d_get_mesh_count(rt_game3d_model_template_get_model(asset_tpl)),
                  1,
                  "asset template retains imported meshes");

    rt_game3d_assets_preload(path);
    rt_game3d_assets_clear_cache();
    PASS();
}

static bool test_phase4_body_def_attach_body() {
    TEST("BodyDef creates attached bodies with filters and sync policy");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D BodyDef Unit"), 80, 60);
    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 1.0, 2.0, 3.0);
    rt_game3d_entity_set_layer(entity, rt_game3d_layers_player());

    void *def = rt_game3d_body_def_sphere(0.5, 2.0);
    rt_game3d_body_def_set_friction(def, 0.7);
    rt_game3d_body_def_set_restitution(def, 0.15);
    rt_game3d_body_def_set_use_ccd(def, 1);
    rt_game3d_body_def_with_mask(def, rt_game3d_layermask_of(rt_game3d_layers_world()));
    rt_game3d_body_def_with_sync(def, rt_game3d_sync_mode_body_from_node());

    rt_game3d_entity_attach_body(entity, def);
    void *body = rt_game3d_entity_get_body(entity);
    EXPECT_TRUE(body != nullptr, "attachBody accepts BodyDef");
    EXPECT_EQ_INT(rt_body3d_get_collision_layer(body),
                  rt_game3d_layers_player(),
                  "BodyDef without explicit layer inherits entity layer");
    EXPECT_EQ_INT(rt_body3d_get_collision_mask(body),
                  rt_game3d_layers_world(),
                  "BodyDef mask applies to attached body");
    EXPECT_NEAR(rt_body3d_get_mass(body), 2.0, 0.0001, "BodyDef mass applies");
    EXPECT_NEAR(rt_body3d_get_friction(body), 0.7, 0.0001, "BodyDef friction applies");
    EXPECT_NEAR(rt_body3d_get_restitution(body), 0.15, 0.0001, "BodyDef restitution applies");
    EXPECT_TRUE(rt_body3d_get_use_ccd(body) != 0, "BodyDef CCD applies");
    EXPECT_EQ_INT(rt_scene_node3d_get_sync_mode(rt_game3d_entity_get_node(entity)),
                  rt_game3d_sync_mode_body_from_node(),
                  "BodyDef sync mode applies to scene node");

    rt_game3d_world_spawn(world, entity);
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "spawning BodyDef-attached entity registers the body");

    void *trigger = rt_game3d_entity_new();
    void *trigger_def = rt_game3d_body_def_static_box(1.0, 1.0, 1.0);
    rt_game3d_body_def_with_layer(trigger_def, rt_game3d_layers_trigger());
    rt_game3d_body_def_as_trigger(trigger_def);
    rt_game3d_entity_attach_body(trigger, trigger_def);
    rt_game3d_entity_set_position(trigger, 0.0, 3.0, 0.0);
    EXPECT_EQ_INT(rt_game3d_entity_get_layer(trigger),
                  rt_game3d_layers_trigger(),
                  "explicit BodyDef layer applies to entity");
    EXPECT_TRUE(rt_body3d_is_static(rt_game3d_entity_get_body(trigger)) != 0,
                "StaticBox creates a static body");
    EXPECT_TRUE(rt_body3d_is_trigger(rt_game3d_entity_get_body(trigger)) != 0,
                "asTrigger marks the body trigger-only");

    void *ground = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(ground, rt_game3d_body_def_static_plane(10.0));
    EXPECT_EQ_INT(rt_game3d_entity_get_layer(ground),
                  rt_game3d_layers_world(),
                  "StaticPlane defaults to the World layer");
    EXPECT_TRUE(rt_body3d_is_static(rt_game3d_entity_get_body(ground)) != 0,
                "StaticPlane creates a static body");

    rt_game3d_world_destroy(world);
    PASS();
}

static int event_mentions(void *event, void *a, void *b) {
    return (rt_game3d_collision_event_get_a(event) == a &&
            rt_game3d_collision_event_get_b(event) == b) ||
           (rt_game3d_collision_event_get_a(event) == b &&
            rt_game3d_collision_event_get_b(event) == a);
}

static bool test_phase4_collision_events_wrapped_with_entities() {
    TEST("Collision3DEvent wraps raw contacts with phases and owning entities");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Collision Unit"), 80, 60);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);

    void *ground = rt_game3d_entity_new();
    rt_game3d_entity_set_name(ground, rt_const_cstr("Ground"));
    rt_game3d_entity_attach_body(ground, rt_game3d_body_def_static_plane(8.0));
    rt_game3d_entity_set_position(ground, 0.0, 0.0, 0.0);

    void *ball = rt_game3d_entity_new();
    rt_game3d_entity_set_name(ball, rt_const_cstr("Ball"));
    void *ball_def = rt_game3d_body_def_sphere(0.5, 1.0);
    rt_game3d_body_def_with_mask(ball_def, rt_game3d_layermask_of(rt_game3d_layers_world()));
    rt_game3d_entity_attach_body(ball, ball_def);
    rt_game3d_entity_set_position(ball, 0.0, 0.45, 0.0);

    rt_game3d_world_spawn(world, ground);
    rt_game3d_world_spawn(world, ball);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);

    EXPECT_TRUE(rt_game3d_world_collision_event_count(world, rt_game3d_collision_enter()) > 0,
                "first overlap emits enter event");
    void *enter = rt_game3d_world_collision_event(world, rt_game3d_collision_enter(), 0);
    EXPECT_TRUE(enter != nullptr, "collisionEvent returns Game3D event wrapper");
    EXPECT_EQ_INT(rt_game3d_collision_event_get_phase(enter),
                  rt_game3d_collision_enter(),
                  "enter event reports Enter phase");
    EXPECT_TRUE(event_mentions(enter, ground, ball), "event maps bodies back to owning entities");
    EXPECT_TRUE(rt_game3d_collision_event_other(enter, ground) == ball,
                "Collision3DEvent.other returns the opposite entity");
    EXPECT_TRUE(rt_game3d_collision_event_get_raw(enter) != nullptr,
                "Collision3DEvent exposes raw Graphics3D event");
    EXPECT_TRUE(rt_game3d_collision_event_point(enter) != nullptr,
                "Collision3DEvent.point returns a Vec3");
    EXPECT_TRUE(rt_game3d_collision_event_normal(enter) != nullptr,
                "Collision3DEvent.normal returns a Vec3");
    EXPECT_TRUE(rt_game3d_world_collision_event_count(world, rt_game3d_collision_any()) >=
                    rt_game3d_world_collision_event_count(world, rt_game3d_collision_enter()),
                "Any phase includes transition buffers");

    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_collision_event_count(world, rt_game3d_collision_stay()) > 0,
                "second overlap emits stay event");

    rt_game3d_entity_set_position(ball, 0.0, 5.0, 0.0);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_collision_event_count(world, rt_game3d_collision_exit()) > 0,
                "separating bodies emits exit event");
    void *exit = rt_game3d_world_collision_event(world, rt_game3d_collision_exit(), 0);
    EXPECT_EQ_INT(rt_game3d_collision_event_get_phase(exit),
                  rt_game3d_collision_exit(),
                  "exit event reports Exit phase");

    rt_game3d_world_destroy(world);

    world = rt_game3d_world_new(rt_const_cstr("Game3D Trigger Collision Unit"), 80, 60);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    void *trigger = rt_game3d_entity_new();
    void *trigger_def = rt_game3d_body_def_static_box(1.0, 1.0, 1.0);
    rt_game3d_body_def_with_layer(trigger_def, rt_game3d_layers_trigger());
    rt_game3d_body_def_as_trigger(trigger_def);
    rt_game3d_entity_attach_body(trigger, trigger_def);
    rt_game3d_entity_set_position(trigger, 0.0, 0.0, 0.0);
    ball = rt_game3d_entity_new();
    ball_def = rt_game3d_body_def_sphere(0.5, 1.0);
    void *trigger_mask = rt_game3d_layermask_of(rt_game3d_layers_world());
    rt_game3d_layermask_include(trigger_mask, rt_game3d_layers_trigger());
    rt_game3d_body_def_with_mask(ball_def, trigger_mask);
    rt_game3d_entity_attach_body(ball, ball_def);
    rt_game3d_entity_set_position(ball, 0.0, 0.0, 0.0);
    rt_game3d_world_spawn(world, trigger);
    rt_game3d_world_spawn(world, ball);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    void *trigger_event = rt_game3d_world_collision_event(world, rt_game3d_collision_enter(), 0);
    EXPECT_TRUE(trigger_event != nullptr, "trigger overlap emits an event");
    EXPECT_TRUE(rt_game3d_collision_event_get_is_trigger(trigger_event) != 0,
                "trigger event reports trigger status");

    rt_game3d_world_destroy(world);
    PASS();
}

int main() {
    set_software_backend_env();
    bool ok = true;
    ok = test_layermasks_and_constants() && ok;
    ok = test_input_axes() && ok;
    ok = test_world_entity_registry_and_collision_clear() && ok;
    ok = test_frame_loop_manual_frame_and_final_capture() && ok;
    ok = test_free_fly_controller_synthetic_input() && ok;
    ok = test_orbit_and_follow_controllers() && ok;
    ok = test_first_person_character_controller_same_frame_motion() && ok;
    ok = test_phase3_material_presets_and_prefabs() && ok;
    ok = test_phase3_world_presets_environment_and_debug() && ok;
    ok = test_phase4_assets3d_model_templates() && ok;
    ok = test_phase4_body_def_attach_body() && ok;
    ok = test_phase4_collision_events_wrapped_with_entities() && ok;

    std::printf("\nGame3D runtime tests: %d/%d passed\n", g_tests_passed, g_tests_total);
    return ok && g_tests_passed == g_tests_total ? 0 : 1;
}
