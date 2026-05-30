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
#include "rt_animcontroller3d.h"
#include "rt_asset.h"
#include "rt_blendtree3d.h"
#include "rt_audio.h"
#include "rt_canvas3d.h"
#include "rt_decal3d.h"
#include "rt_game3d.h"
#include "rt_iksolver3d.h"
#include "rt_input.h"
#include "rt_mat4.h"
#include "rt_model3d.h"
#include "rt_navmesh3d.h"
#include "rt_particles3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_postfx3d.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_sound3d.h"
#include "rt_soundlistener3d.h"
#include "rt_soundsource3d.h"
#include "rt_string.h"
#include "rt_synth.h"
#include "rt_terrain3d.h"
#include "rt_vec2.h"
#include "rt_vec3.h"

extern "C" {
#include "rt_canvas3d_internal.h"
}

#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;
static int g_tests_passed = 0;
static int g_tests_total = 0;
static int g_update_calls = 0;
static int g_overlay_calls = 0;
static double g_update_dt_sum = 0.0;
static rt_canvas3d *g_observed_canvas = nullptr;
static int32_t g_observed_input_source = -1;
static int32_t g_observed_clock_source = -1;
static int64_t g_observed_synthetic_dt_us = -1;
static rt_canvas3d *g_fixed_loop_canvas = nullptr;
static void *g_fixed_loop_world = nullptr;
static int g_fixed_loop_stop_after = 0;
static bool g_fixed_loop_destroy_at_stop = false;
static int64_t g_fixed_loop_observed_dropped = -1;
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
        std::printf("ok\n");                                                                       \
        return true;                                                                               \
    } while (0)

#define FAIL(msg)                                                                                  \
    do {                                                                                           \
        std::printf("FAIL: %s\n", msg);                                                            \
        return false;                                                                              \
    } while (0)

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        if (!(cond))                                                                               \
            FAIL(msg);                                                                             \
    } while (0)

#define EXPECT_EQ_INT(actual, expected, msg)                                                       \
    do {                                                                                           \
        const long long got_ = (long long)(actual);                                                \
        const long long want_ = (long long)(expected);                                             \
        if (got_ != want_) {                                                                       \
            std::printf("FAIL: %s (got %lld, expected %lld)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(actual, expected, eps, msg)                                                    \
    do {                                                                                           \
        const double got_ = (double)(actual);                                                      \
        const double want_ = (double)(expected);                                                   \
        if (std::fabs(got_ - want_) > (eps)) {                                                     \
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

static bool write_text_file(const char *path, const char *text) {
    FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;
    size_t len = std::strlen(text);
    bool ok = len == 0 || std::fwrite(text, 1, len, file) == len;
    return std::fclose(file) == 0 && ok;
}

template <typename Fn> static bool expect_trap_contains(Fn &&fn, const char *needle) {
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
#if RT_PLATFORM_WINDOWS
    _putenv_s("VIPER_3D_BACKEND", "software");
#else
    setenv("VIPER_3D_BACKEND", "software", 1);
#endif
}

extern "C" void game3d_test_update(double dt) {
    ++g_update_calls;
    g_update_dt_sum += dt;
}

extern "C" void game3d_test_observing_update(double dt) {
    game3d_test_update(dt);
    if (g_observed_canvas) {
        g_observed_input_source = g_observed_canvas->input_source;
        g_observed_clock_source = g_observed_canvas->clock_source;
        g_observed_synthetic_dt_us = g_observed_canvas->synthetic_dt_us;
    }
}

extern "C" void game3d_test_trapping_update(double) {
    vm_trap("runFrames callback trap");
}

extern "C" void game3d_test_fixed_update(double dt) {
    game3d_test_update(dt);
    if (g_fixed_loop_canvas && g_fixed_loop_stop_after > 0 &&
        g_update_calls >= g_fixed_loop_stop_after) {
        if (g_fixed_loop_world)
            g_fixed_loop_observed_dropped =
                rt_game3d_world_get_dropped_fixed_steps(g_fixed_loop_world);
        if (g_fixed_loop_destroy_at_stop && g_fixed_loop_world) {
            rt_game3d_world_destroy(g_fixed_loop_world);
            return;
        }
        g_fixed_loop_canvas->should_close = 1;
    }
}

extern "C" void game3d_test_overlay(void) {
    ++g_overlay_calls;
}

template <typename T> static void append_game3d_test_bytes(std::vector<uint8_t> &out, T value) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

static void game3d_test_write16(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back((uint8_t)v);
    out.push_back((uint8_t)(v >> 8));
}

static void game3d_test_write32(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back((uint8_t)v);
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 24));
}

static void game3d_test_write64(std::vector<uint8_t> &out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back((uint8_t)(v >> (i * 8)));
}

static void game3d_test_align(std::vector<uint8_t> &out, size_t alignment) {
    size_t rem = out.size() % alignment;
    if (rem != 0)
        out.insert(out.end(), alignment - rem, 0);
}

struct Game3DTestVpaEntry {
    const char *name;
    const uint8_t *data;
    size_t size;
};

static bool write_game3d_test_vpa(const char *path,
                                  const Game3DTestVpaEntry *entries,
                                  size_t entry_count) {
    std::vector<uint8_t> out;
    std::vector<uint64_t> offsets;
    out.reserve(512);
    out.insert(out.end(), {'V', 'P', 'A', '1'});
    game3d_test_write16(out, 1);
    game3d_test_write16(out, 0);
    game3d_test_write32(out, (uint32_t)entry_count);
    size_t toc_offset_pos = out.size();
    game3d_test_write64(out, 0);
    size_t toc_size_pos = out.size();
    game3d_test_write64(out, 0);
    game3d_test_write32(out, 0);

    offsets.reserve(entry_count);
    for (size_t i = 0; i < entry_count; ++i) {
        game3d_test_align(out, 8);
        offsets.push_back((uint64_t)out.size());
        out.insert(out.end(), entries[i].data, entries[i].data + entries[i].size);
    }

    game3d_test_align(out, 8);
    uint64_t toc_offset = (uint64_t)out.size();
    size_t toc_start = out.size();
    for (size_t i = 0; i < entry_count; ++i) {
        size_t name_len = std::strlen(entries[i].name);
        game3d_test_write16(out, (uint16_t)name_len);
        out.insert(out.end(), entries[i].name, entries[i].name + name_len);
        game3d_test_write64(out, offsets[i]);
        game3d_test_write64(out, (uint64_t)entries[i].size);
        game3d_test_write64(out, (uint64_t)entries[i].size);
        game3d_test_write16(out, 0);
        game3d_test_write16(out, 0);
    }
    uint64_t toc_size = (uint64_t)(out.size() - toc_start);
    for (int i = 0; i < 8; ++i) {
        out[toc_offset_pos + i] = (uint8_t)(toc_offset >> (i * 8));
        out[toc_size_pos + i] = (uint8_t)(toc_size >> (i * 8));
    }

    FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;
    bool ok = out.empty() || std::fwrite(out.data(), 1, out.size(), file) == out.size();
    return std::fclose(file) == 0 && ok;
}

static void *make_game3d_test_anim(const char *name,
                                   int64_t bone_index,
                                   double x0,
                                   double y0,
                                   double z0,
                                   double x1,
                                   double y1,
                                   double z1) {
    void *anim = rt_animation3d_new(rt_const_cstr(name), 1.0);
    void *pos0 = rt_vec3_new(x0, y0, z0);
    void *pos1 = rt_vec3_new(x1, y1, z1);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_set_looping(anim, 1);
    rt_animation3d_add_keyframe(anim, bone_index, 0.0, pos0, rot, scl);
    rt_animation3d_add_keyframe(anim, bone_index, 1.0, pos1, rot, scl);
    return anim;
}

static void *make_game3d_test_controller(double walk_distance, double event_time) {
    void *skel = rt_skeleton3d_new();
    int64_t root = rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *idle = make_game3d_test_anim("idle", root, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    void *walk = make_game3d_test_anim("walk", root, 0.0, 0.0, 0.0, walk_distance, 0.0, 0.0);
    void *controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("idle"), idle);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk);
    rt_anim_controller3d_add_event(
        controller, rt_const_cstr("walk"), event_time, rt_const_cstr("step"));
    rt_anim_controller3d_set_root_motion_bone(controller, root);
    return controller;
}

static bool test_layermasks_and_constants() {
    TEST("Game3D constants and LayerMask operations");
    EXPECT_EQ_INT(rt_game3d_layers_world(), 1, "World layer bit");
    EXPECT_EQ_INT(rt_game3d_layers_dynamic(), 2, "Dynamic layer bit");
    EXPECT_EQ_INT(rt_game3d_layers_player(), 4, "Player layer bit");
    EXPECT_EQ_INT(rt_game3d_quality_balanced(), 1, "Balanced quality value");
    EXPECT_EQ_INT(rt_game3d_collision_any(), 3, "Any collision phase value");
    EXPECT_EQ_INT(rt_game3d_shading_model_unlit(), 3, "Game3D unlit matches backend shading slot");
    EXPECT_EQ_INT(
        rt_game3d_shading_model_fresnel(), 4, "Game3D fresnel matches backend shading slot");
    EXPECT_EQ_INT(
        rt_game3d_shading_model_emissive(), 5, "Game3D emissive matches backend shading slot");

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
    EXPECT_EQ_INT(rt_game3d_layermask_get_bits(all), -1, "LayerMask.All uses every bit");
    rt_game3d_layermask_set_bits(all, -1);
    EXPECT_EQ_INT(rt_game3d_layermask_get_bits(all), -1, "negative mask bits clamp to all");

    EXPECT_TRUE(expect_trap_contains([&] { rt_game3d_layermask_of(3); }, "single positive bit"),
                "LayerMask.Of rejects multi-bit layers");
    EXPECT_TRUE(
        expect_trap_contains([&] { rt_game3d_layermask_include(mask, 0); }, "single positive bit"),
        "LayerMask.include rejects zero layer");
    PASS();
}

static bool test_world_worker_controls() {
    TEST("World3D exposes internal worker controls with Game3D naming");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Worker Controls"), 64, 48);
    EXPECT_TRUE(world != nullptr, "World3D.New returns an object");
    EXPECT_TRUE(rt_game3d_world_get_worker_count(world) >= 1, "workerCount defaults positive");
    EXPECT_EQ_INT(rt_game3d_world_get_jobs_enabled(world),
                  rt_game3d_world_get_worker_count(world) > 1 ? 1 : 0,
                  "jobsEnabled follows workerCount");

    rt_game3d_world_set_worker_count(world, 1);
    EXPECT_EQ_INT(rt_game3d_world_get_worker_count(world), 1, "setWorkerCount stores one worker");
    EXPECT_EQ_INT(rt_game3d_world_get_jobs_enabled(world), 0, "one worker disables jobs");

    rt_game3d_world_set_worker_count(world, 4);
    EXPECT_EQ_INT(rt_game3d_world_get_worker_count(world), 4, "setWorkerCount stores worker count");
    EXPECT_EQ_INT(rt_game3d_world_get_jobs_enabled(world), 1, "multiple workers enable jobs");

    rt_game3d_world_set_worker_count(world, 0);
    EXPECT_EQ_INT(rt_game3d_world_get_worker_count(world), 1, "zero worker count clamps to one");
    EXPECT_EQ_INT(rt_game3d_world_get_jobs_enabled(world), 0, "clamped single worker disables jobs");

    rt_game3d_world_set_worker_count(world, 4096);
    EXPECT_EQ_INT(rt_game3d_world_get_worker_count(world), 64, "worker count clamps to max");
    EXPECT_EQ_INT(rt_game3d_world_get_jobs_enabled(world), 1, "clamped multi-worker count enables jobs");

    rt_game3d_world_destroy(world);
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
    const double inv_sqrt2 = 0.7071067811865475;
    EXPECT_NEAR(rt_vec3_x(move), 0.0, 0.0001, "move axis x");
    EXPECT_NEAR(rt_vec3_y(move), inv_sqrt2, 0.0001, "move axis y is normalized");
    EXPECT_NEAR(rt_vec3_z(move), inv_sqrt2, 0.0001, "move axis z is normalized");

    void *look = rt_game3d_input_look_axis(input);
    EXPECT_NEAR(rt_vec2_x(look), 0.60, 0.0001, "look axis x scales mouse delta");
    EXPECT_NEAR(rt_vec2_y(look), -0.20, 0.0001, "look axis y scales mouse delta");
    rt_game3d_input_set_look_sensitivity(input, -2.0);
    EXPECT_NEAR(rt_game3d_input_get_look_sensitivity(input),
                0.01,
                0.0001,
                "negative look sensitivity falls back to default");

    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_keyboard_on_key_up(rt_game3d_key_w());
    rt_keyboard_on_key_up(rt_game3d_key_space());
    rt_mouse_button_up(rt_game3d_mouse_left());
    EXPECT_TRUE(rt_game3d_input_released(input, rt_game3d_key_w()) != 0, "W was released");
    PASS();
}

static bool test_input_update_snapshots_frame_state() {
    TEST("Input3D.update snapshots frame state");
    rt_keyboard_init();
    rt_mouse_init();
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();

    void *input = rt_game3d_input_new();
    rt_game3d_input_set_look_sensitivity(input, 0.1);
    rt_keyboard_on_key_down(rt_game3d_key_w());
    rt_mouse_force_delta(5, -2);
    rt_mouse_update_wheel(0.0, 0.75);
    rt_game3d_input_update(input);

    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_keyboard_on_key_up(rt_game3d_key_w());
    rt_mouse_force_delta(100, 100);
    rt_mouse_update_wheel(0.0, 9.0);

    EXPECT_TRUE(rt_game3d_input_is_down(input, rt_game3d_key_w()) != 0,
                "snapshot preserves held key state");
    EXPECT_TRUE(rt_game3d_input_pressed(input, rt_game3d_key_w()) != 0,
                "snapshot preserves pressed edge");
    void *look = rt_game3d_input_look_axis(input);
    EXPECT_NEAR(rt_vec2_x(look), 0.5, 0.0001, "snapshot preserves mouse dx");
    EXPECT_NEAR(rt_vec2_y(look), -0.2, 0.0001, "snapshot preserves mouse dy");
    EXPECT_NEAR(rt_game3d_input_wheel_y(input), 0.75, 0.0001, "snapshot preserves wheel");
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
    void *owned_stream = rt_game3d_world_get_stream(world);
    EXPECT_TRUE(owned_stream != nullptr, "World3D lazily owns a WorldStream3D");
    EXPECT_TRUE(rt_game3d_world_get_stream(world) == owned_stream,
                "World3D.stream returns a stable owned stream handle");
    EXPECT_EQ_INT(rt_game3d_world_get_entity_count(world), 0, "entityCount starts empty");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world), 0, "bodyCount starts empty");
    EXPECT_EQ_INT(rt_game3d_world_get_draw_count(world), 0, "drawCount starts empty");
    EXPECT_EQ_INT(rt_game3d_world_get_visible_node_count(world),
                  0,
                  "visibleNodeCount starts empty");
    EXPECT_EQ_INT(rt_game3d_world_get_occluded_draw_count(world),
                  0,
                  "occludedDrawCount starts empty");
    EXPECT_EQ_INT(rt_game3d_world_get_stream_resident_bytes(world),
                  0,
                  "streamResidentBytes starts empty");

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
    rt_game3d_entity_set_collision_mask(parent, rt_game3d_layermask_of(rt_game3d_layers_world()));
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
    EXPECT_EQ_INT(rt_game3d_world_get_entity_count(world),
                  3,
                  "entityCount tracks spawned entity tree");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  2,
                  "bodyCount tracks spawned entity bodies");
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
    rt_game3d_entity_set_name(parent, rt_const_cstr("Hero"));
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("Hero")) == parent,
                "findEntity index updates after rename");
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("Player")) == nullptr,
                "findEntity index drops old names after rename");
    rt_game3d_world_on_resize(world, 96, 72);
    EXPECT_EQ_INT(rt_canvas3d_get_width(rt_game3d_world_get_canvas(world)),
                  96,
                  "World3D.onResize resizes the canvas width");
    EXPECT_EQ_INT(rt_canvas3d_get_height(rt_game3d_world_get_canvas(world)),
                  72,
                  "World3D.onResize resizes the canvas height");
    EXPECT_TRUE(
        expect_trap_contains([&] { rt_game3d_world_add_light(world, 0, parent); }, "Light3D"),
        "World3D.addLight rejects non-Light3D handles");
    EXPECT_TRUE(
        expect_trap_contains([&] { rt_game3d_world_set_skybox(world, parent); }, "CubeMap3D"),
        "World3D.setSkybox rejects non-CubeMap3D handles");
    EXPECT_EQ_INT(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)),
                  4,
                  "scene contains root, parent, child, and wall");

    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_end_scene(world);
    EXPECT_EQ_INT(rt_game3d_world_get_draw_count(world),
                  rt_canvas3d_get_draw_count(rt_game3d_world_get_canvas(world)),
                  "drawCount wraps canvas telemetry");
    EXPECT_TRUE(rt_game3d_world_get_draw_count(world) >= 1, "drawCount observes scene draws");
    EXPECT_EQ_INT(rt_game3d_world_get_visible_node_count(world),
                  rt_scene3d_get_visible_node_count(rt_game3d_world_get_scene(world)),
                  "visibleNodeCount wraps scene telemetry");
    EXPECT_EQ_INT(rt_game3d_world_get_occluded_draw_count(world),
                  rt_canvas3d_get_occluded_draw_count(rt_game3d_world_get_canvas(world)),
                  "occludedDrawCount wraps canvas telemetry");

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
    EXPECT_EQ_INT(rt_game3d_world_get_entity_count(world),
                  1,
                  "entityCount drops despawned subtree");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  1,
                  "bodyCount drops despawned subtree body");
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "despawn removes entity body from physics");
    EXPECT_TRUE(rt_world3d_contains_body(rt_game3d_world_get_physics(world), body) == 0,
                "despawn removes parent body from physics membership");
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("Hero")) == nullptr,
                "findEntity ignores despawned entities after lazy name-index rebuild");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("Muzzle")) == nullptr,
                "findNode ignores despawned child nodes");
    EXPECT_EQ_INT(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)),
                  2,
                  "despawn detaches the parent subtree from the scene");

    rt_game3d_world_destroy(world);
    EXPECT_TRUE(rt_game3d_world_is_destroyed(world) != 0, "destroy marks world destroyed");
    EXPECT_TRUE(rt_game3d_entity_is_destroyed(other) != 0,
                "destroy marks still-spawned entities destroyed");
    EXPECT_TRUE(expect_trap_contains([&] { (void)rt_game3d_world_get_frame(world); },
                                     "world is destroyed"),
                "destroyed world handles trap with a clear diagnostic");
    EXPECT_TRUE(expect_trap_contains([&] { (void)rt_game3d_entity_position(other); },
                                     "entity is destroyed"),
                "destroyed entity handles trap with a clear diagnostic");
    PASS();
}

static bool test_world_navmesh_bake_hooks() {
    TEST("World3D exposes world-scoped navmesh bake hooks");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Nav Bake Unit"), 80, 60);
    EXPECT_TRUE(world != nullptr, "World3D.New returns an object");

    void *mesh = rt_mesh3d_new_plane(8.0, 8.0);
    void *material = rt_material3d_new_color(0.2, 0.6, 0.25);
    void *ground = rt_game3d_entity_of(mesh, material);
    rt_game3d_entity_set_name(ground, rt_const_cstr("NavBakeGround"));
    rt_game3d_entity_set_position(ground, 3.0, 0.0, -2.0);
    rt_game3d_world_spawn(world, ground);

    void *nav = rt_game3d_world_bake_nav_mesh(world, 0.35, 1.8, 45.0, 0.3);
    EXPECT_TRUE(nav != nullptr, "bakeNavMesh returns a NavMesh3D");
    EXPECT_EQ_INT(rt_navmesh3d_get_triangle_count(nav), 2, "bakeNavMesh uses world scene mesh");
    void *inside = rt_vec3_new(3.0, 0.0, -2.0);
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nav, inside) != 0,
                "bakeNavMesh preserves world-space transforms");

    void *tiled = rt_game3d_world_bake_tiled_nav_mesh(world, 16.0, 0.35, 1.8, 45.0, 0.3);
    EXPECT_TRUE(tiled != nullptr, "bakeTiledNavMesh returns a NavMesh3D");
    EXPECT_EQ_INT(rt_navmesh3d_get_triangle_count(tiled),
                  2,
                  "bakeTiledNavMesh uses world scene mesh");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(tiled, inside) != 0,
                "bakeTiledNavMesh preserves world-space transforms");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_entity_from_node_wraps_imported_subtree() {
    TEST("Entity3D.FromNode wraps a raw SceneNode3D subtree");
    void *root = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    rt_scene_node3d_set_name(root, rt_const_cstr("ImportedRoot"));
    rt_scene_node3d_set_name(child, rt_const_cstr("ImportedChild"));
    rt_scene_node3d_add_child(root, child);

    void *entity = rt_game3d_entity_from_node(root);
    EXPECT_TRUE(entity != nullptr, "FromNode returns an Entity3D");
    EXPECT_TRUE(rt_game3d_entity_is_group(entity) != 0, "FromNode marks the wrapper as a group");
    EXPECT_TRUE(rt_game3d_entity_get_node(entity) == root, "FromNode uses the provided root node");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_entity_get_name(entity)), "ImportedRoot") ==
                    0,
                "FromNode inherits a non-empty root node name");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D FromNode Unit"), 80, 60);
    rt_game3d_world_spawn(world, entity);
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("ImportedRoot")) == entity,
                "findEntity indexes the imported root wrapper by name");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("ImportedRoot")) == root,
                "findNode sees the imported root node");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("ImportedChild")) == child,
                "findNode sees raw children in the imported subtree");
    EXPECT_EQ_INT(rt_scene3d_get_node_count(rt_game3d_world_get_scene(world)),
                  3,
                  "scene contains world root plus imported raw subtree");

    rt_game3d_world_despawn(world, entity);
    EXPECT_TRUE(rt_game3d_world_find_entity(world, rt_const_cstr("ImportedRoot")) == nullptr,
                "despawn removes imported wrapper from the entity index");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("ImportedChild")) == nullptr,
                "despawn detaches the imported raw subtree");

    void *not_node = rt_material3d_new_color(1.0, 1.0, 1.0);
    EXPECT_TRUE(expect_trap_contains([&] { rt_game3d_entity_from_node(not_node); }, "SceneNode3D"),
                "FromNode rejects non-SceneNode3D handles");
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_entity_child_graph_reparents_and_rejects_cycles() {
    TEST("Entity3D child graph reparents and rejects cycles");
    void *parent_a = rt_game3d_entity_new();
    void *parent_b = rt_game3d_entity_new();
    void *child = rt_game3d_entity_new();

    rt_game3d_entity_add_child(parent_a, child);
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(parent_a)),
                  1,
                  "first parent receives child node");
    rt_game3d_entity_add_child(parent_a, child);
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(parent_a)),
                  1,
                  "adding same child twice is idempotent");

    rt_game3d_entity_add_child(parent_b, child);
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(parent_a)),
                  0,
                  "reparent removes child from old parent node");
    EXPECT_EQ_INT(rt_scene_node3d_child_count(rt_game3d_entity_get_node(parent_b)),
                  1,
                  "reparent adds child to new parent node");

    EXPECT_TRUE(expect_trap_contains([&] { rt_game3d_entity_add_child(child, parent_b); }, "cycle"),
                "child graph rejects cycles");
    EXPECT_TRUE(
        expect_trap_contains([&] { rt_game3d_entity_add_child(child, child); }, "own child"),
        "child graph rejects self-parenting");
    PASS();
}

static bool test_frame_loop_manual_frame_and_final_capture() {
    TEST("World3D runFrames, manual frame API, overlay, and final capture");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Unit Frames"), 64, 48);
    EXPECT_TRUE(world != nullptr, "World3D.New returns an object");
    rt_canvas3d *canvas_state = (rt_canvas3d *)rt_game3d_world_get_canvas(world);
    rt_canvas3d_set_input_source(canvas_state, 2);
    rt_canvas3d_set_clock_source(canvas_state, 0);
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_state, 0.123);

    g_update_calls = 0;
    g_update_dt_sum = 0.0;
    g_observed_canvas = canvas_state;
    g_observed_input_source = -1;
    g_observed_clock_source = -1;
    g_observed_synthetic_dt_us = -1;
    rt_game3d_world_run_frames(world, 3, 0.02, (void *)&game3d_test_observing_update);
    EXPECT_EQ_INT(g_update_calls, 3, "runFrames invokes the update callback once per frame");
    EXPECT_NEAR(g_update_dt_sum, 0.06, 0.0001, "runFrames passes fixed dt to callback");
    EXPECT_EQ_INT(g_observed_input_source, 1, "runFrames callback observes synthetic input");
    EXPECT_EQ_INT(g_observed_clock_source, 1, "runFrames callback observes synthetic clock");
    EXPECT_EQ_INT(g_observed_synthetic_dt_us, 20000, "runFrames callback observes fixed dt");
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 3, "runFrames increments frame count");
    EXPECT_NEAR(rt_game3d_world_get_dt(world), 0.02, 0.0001, "runFrames stores dt");
    EXPECT_NEAR(rt_game3d_world_get_elapsed(world), 0.06, 0.0001, "runFrames stores elapsed time");
    EXPECT_EQ_INT(canvas_state->input_source, 2, "runFrames restores input source");
    EXPECT_EQ_INT(canvas_state->clock_source, 0, "runFrames restores clock source");
    EXPECT_EQ_INT(canvas_state->synthetic_dt_us, 123000, "runFrames restores synthetic delta");

    EXPECT_TRUE(expect_trap_contains(
                    [&] {
                        rt_game3d_world_run_frames(
                            world, 1, 0.02, (void *)&game3d_test_trapping_update);
                    },
                    "runFrames callback trap"),
                "runFrames propagates callback traps");
    rt_canvas3d_set_input_source(canvas_state, 2);
    rt_canvas3d_set_clock_source(canvas_state, 0);
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_state, 0.123);

    g_overlay_calls = 0;
    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    rt_game3d_world_draw_overlay(world, (void *)&game3d_test_overlay);
    EXPECT_EQ_INT(g_overlay_calls, 1, "drawOverlay invokes overlay callback");

    void *pixels = rt_game3d_world_capture_final_frame(world);
    EXPECT_TRUE(pixels != nullptr, "captureFinalFrame returns Pixels");
    EXPECT_EQ_INT(rt_pixels_width(pixels),
                  rt_canvas3d_get_window_width(canvas_state),
                  "captured final frame width");
    EXPECT_EQ_INT(rt_pixels_height(pixels),
                  rt_canvas3d_get_window_height(canvas_state),
                  "captured final frame height");
    rt_game3d_world_present(world);
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_run_fixed_accumulator_and_spiral_guard() {
    TEST("World3D.runFixed accumulates fixed steps and caps spiral frames");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Fixed Loop Unit"), 64, 48);
    rt_canvas3d *canvas_state = (rt_canvas3d *)rt_game3d_world_get_canvas(world);
    rt_canvas3d_set_input_source(canvas_state, 1);
    rt_canvas3d_set_clock_source(canvas_state, 1);
    rt_canvas3d_set_dt_max(canvas_state, 0);
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_state, 0.25);

    g_update_calls = 0;
    g_update_dt_sum = 0.0;
    g_overlay_calls = 0;
    g_fixed_loop_canvas = canvas_state;
    g_fixed_loop_world = world;
    g_fixed_loop_stop_after = 4;
    g_fixed_loop_destroy_at_stop = true;
    g_fixed_loop_observed_dropped = -1;
    rt_game3d_world_run_fixed(world, 0.0625, (void *)&game3d_test_fixed_update);
    g_fixed_loop_canvas = nullptr;
    g_fixed_loop_world = nullptr;
    g_fixed_loop_stop_after = 0;
    g_fixed_loop_destroy_at_stop = false;

    EXPECT_EQ_INT(g_update_calls, 4, "runFixed consumes accumulated fixed steps");
    EXPECT_NEAR(g_update_dt_sum, 0.25, 0.0001, "runFixed update callback receives fixed dt");
    EXPECT_EQ_INT(g_fixed_loop_observed_dropped, 0, "normal runFixed frame does not drop steps");
    EXPECT_TRUE(rt_game3d_world_is_destroyed(world) != 0,
                "runFixed can exit when a native callback destroys the world");

    world = rt_game3d_world_new(rt_const_cstr("Game3D Fixed Loop Spiral Unit"), 64, 48);
    canvas_state = (rt_canvas3d *)rt_game3d_world_get_canvas(world);
    rt_canvas3d_set_input_source(canvas_state, 1);
    rt_canvas3d_set_clock_source(canvas_state, 1);
    rt_canvas3d_set_dt_max(canvas_state, 0);
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_state, 0.25);

    g_update_calls = 0;
    g_update_dt_sum = 0.0;
    g_fixed_loop_canvas = canvas_state;
    g_fixed_loop_world = world;
    g_fixed_loop_stop_after = 8;
    g_fixed_loop_destroy_at_stop = true;
    g_fixed_loop_observed_dropped = -1;
    rt_game3d_world_run_fixed(world, 0.015625, (void *)&game3d_test_fixed_update);
    g_fixed_loop_canvas = nullptr;
    g_fixed_loop_world = nullptr;
    g_fixed_loop_stop_after = 0;
    g_fixed_loop_destroy_at_stop = false;

    EXPECT_EQ_INT(g_update_calls, 8, "runFixed caps a large display tick to max fixed steps");
    EXPECT_NEAR(g_update_dt_sum, 0.125, 0.0001, "runFixed cap uses fixed dt for every step");
    EXPECT_EQ_INT(g_fixed_loop_observed_dropped,
                  8,
                  "runFixed records fixed steps discarded by the spiral guard");
    EXPECT_TRUE(rt_game3d_world_is_destroyed(world) != 0,
                "spiral-guard callback destroyed the world");
    PASS();
}

struct WorkerReplaySummary {
    int64_t frame;
    double elapsed;
    double dt;
    double entity_x;
    double entity_y;
    double entity_z;
    int64_t pixels_w;
    int64_t pixels_h;
};

static WorkerReplaySummary run_worker_replay(int64_t worker_count) {
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Worker Replay"), 64, 48);
    rt_game3d_world_set_worker_count(world, worker_count);
    rt_game3d_world_set_gravity(world, 0.0, -9.81, 0.0);

    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 0.0, 4.0, 0.0);
    rt_game3d_entity_attach_body(entity, rt_game3d_body_def_sphere(0.5, 1.0));
    rt_game3d_world_spawn(world, entity);
    rt_game3d_world_run_frames_only(world, 5, 1.0 / 60.0);

    void *pos = rt_game3d_entity_world_position(entity);
    void *pixels = rt_game3d_world_capture_final_frame(world);
    WorkerReplaySummary summary = {rt_game3d_world_get_frame(world),
                                   rt_game3d_world_get_elapsed(world),
                                   rt_game3d_world_get_dt(world),
                                   rt_vec3_x(pos),
                                   rt_vec3_y(pos),
                                   rt_vec3_z(pos),
                                   rt_pixels_width(pixels),
                                   rt_pixels_height(pixels)};
    rt_game3d_world_destroy(world);
    return summary;
}

static bool test_worker_count_runframes_replay_parity() {
    TEST("World3D.runFramesOnly is stable across workerCount settings");
    WorkerReplaySummary single = run_worker_replay(1);
    WorkerReplaySummary multi = run_worker_replay(4);

    EXPECT_EQ_INT(single.frame, multi.frame, "single/multi worker frame parity");
    EXPECT_NEAR(single.elapsed, multi.elapsed, 0.000001, "single/multi worker elapsed parity");
    EXPECT_NEAR(single.dt, multi.dt, 0.000001, "single/multi worker dt parity");
    EXPECT_NEAR(single.entity_x, multi.entity_x, 0.000001, "single/multi worker entity x parity");
    EXPECT_NEAR(single.entity_y, multi.entity_y, 0.000001, "single/multi worker entity y parity");
    EXPECT_NEAR(single.entity_z, multi.entity_z, 0.000001, "single/multi worker entity z parity");
    EXPECT_EQ_INT(single.pixels_w, multi.pixels_w, "single/multi worker final width parity");
    EXPECT_EQ_INT(single.pixels_h, multi.pixels_h, "single/multi worker final height parity");
    PASS();
}

struct WorkerAnimBatchSummary {
    double entity_x[12];
    double state_time[12];
    int64_t event_count[12];
};

static WorkerAnimBatchSummary run_worker_animation_batch(int64_t worker_count) {
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Worker Animations"), 64, 48);
    rt_game3d_world_set_worker_count(world, worker_count);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);

    void *entities[12] = {};
    void *animators[12] = {};
    for (int32_t i = 0; i < 12; ++i) {
        entities[i] = rt_game3d_entity_new();
        void *controller = make_game3d_test_controller(2.0 + (double)i * 0.25, 0.25);
        animators[i] = rt_game3d_animator_new(controller);
        rt_game3d_entity_attach_animator(entities[i], animators[i]);
        rt_scene_node3d_set_sync_mode(rt_game3d_entity_get_node(entities[i]),
                                      rt_game3d_sync_mode_node_from_anim_root_motion());
        rt_game3d_animator_play(animators[i], rt_const_cstr("walk"));
        rt_game3d_world_spawn(world, entities[i]);
    }

    rt_game3d_world_step_simulation(world, 0.25);

    WorkerAnimBatchSummary summary = {};
    for (int32_t i = 0; i < 12; ++i) {
        void *pos = rt_game3d_entity_position(entities[i]);
        summary.entity_x[i] = rt_vec3_x(pos);
        summary.state_time[i] = rt_game3d_animator_state_time(animators[i]);
        summary.event_count[i] = rt_game3d_animator_event_count(animators[i]);
    }
    rt_game3d_world_destroy(world);
    return summary;
}

static bool test_worker_count_parallel_animation_parity() {
    TEST("World3D worker jobs update animator batches deterministically");
    WorkerAnimBatchSummary single = run_worker_animation_batch(1);
    WorkerAnimBatchSummary multi = run_worker_animation_batch(4);

    for (int32_t i = 0; i < 12; ++i) {
        EXPECT_NEAR(single.entity_x[i],
                    multi.entity_x[i],
                    0.000001,
                    "single/multi animation root-motion parity");
        EXPECT_NEAR(single.state_time[i],
                    multi.state_time[i],
                    0.000001,
                    "single/multi animation state-time parity");
        EXPECT_EQ_INT(single.event_count[i], 1, "single-worker animation fires event");
        EXPECT_EQ_INT(multi.event_count[i], 1, "multi-worker animation fires event");
    }
    PASS();
}

static bool test_world_floating_origin_controls_and_rebase() {
    TEST("World3D floatingOrigin rebases camera, entities, and bodies");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Floating Origin"), 64, 48);
    EXPECT_EQ_INT(rt_game3d_world_get_floating_origin(world), 0, "floatingOrigin defaults off");
    void *origin = rt_game3d_world_get_world_origin(world);
    EXPECT_NEAR(rt_vec3_x(origin), 0.0, 0.000001, "worldOrigin starts at x=0");

    rt_game3d_world_set_origin_rebase_threshold(world, 10.0);
    void *camera = rt_game3d_world_get_camera(world);
    rt_camera3d_set_position(camera, rt_vec3_new(20.0, 0.0, 0.0));
    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 23.0, 0.0, 0.0);
    rt_game3d_entity_attach_body(entity, rt_game3d_body_def_sphere(0.5, 1.0));
    rt_game3d_world_spawn(world, entity);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_NEAR(rt_vec3_x(rt_camera3d_get_position(camera)), 20.0, 0.000001, "flag-off camera unchanged");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_entity_position(entity)), 23.0, 0.000001, "flag-off entity unchanged");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_world_get_world_origin(world)),
                0.0,
                0.000001,
                "flag-off worldOrigin unchanged");

    rt_game3d_world_set_floating_origin(world, 1);
    EXPECT_EQ_INT(rt_game3d_world_get_floating_origin(world), 1, "floatingOrigin setter enables");
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_NEAR(rt_vec3_x(rt_camera3d_get_position(camera)), 0.0, 0.000001, "camera rebased to origin");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_entity_position(entity)), 3.0, 0.000001, "entity shifted by rebase delta");
    EXPECT_NEAR(rt_vec3_x(rt_body3d_get_position(rt_game3d_entity_get_body(entity))),
                3.0,
                0.000001,
                "body shifted by rebase delta");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_world_get_world_origin(world)),
                20.0,
                0.000001,
                "worldOrigin accumulates rebase delta");

    void *audio = rt_game3d_world_get_audio(world);
    void *listener = rt_game3d_audio_get_listener(audio);
    EXPECT_TRUE(listener != nullptr, "floatingOrigin world has an audio listener");
    rt_game3d_audio_listener_follow_camera(audio, 0);
    rt_camera3d_set_position(camera, rt_vec3_new(50000.0, -5.0, 7000.0));
    rt_game3d_entity_set_position(entity, 50004.0, -3.0, 6998.0);
    rt_body3d_set_position(rt_game3d_entity_get_body(entity), 50004.0, -3.0, 6998.0);
    rt_game3d_audio_set_listener_pose(audio,
                                      rt_vec3_new(50002.0, -4.0, 7001.0),
                                      rt_vec3_new(0.0, 0.0, -1.0),
                                      rt_vec3_new(0.0, 1.0, 0.0));

    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    EXPECT_NEAR(rt_vec3_x(rt_camera3d_get_position(camera)), 0.0, 0.000001, "50km camera X rebased");
    EXPECT_NEAR(rt_vec3_y(rt_camera3d_get_position(camera)), 0.0, 0.000001, "50km camera Y rebased");
    EXPECT_NEAR(rt_vec3_z(rt_camera3d_get_position(camera)), 0.0, 0.000001, "50km camera Z rebased");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_entity_position(entity)), 4.0, 0.000001, "50km entity X shifted");
    EXPECT_NEAR(rt_vec3_y(rt_game3d_entity_position(entity)), 2.0, 0.000001, "50km entity Y shifted");
    EXPECT_NEAR(rt_vec3_z(rt_game3d_entity_position(entity)), -2.0, 0.000001, "50km entity Z shifted");
    void *far_body_pos = rt_body3d_get_position(rt_game3d_entity_get_body(entity));
    EXPECT_NEAR(rt_vec3_x(far_body_pos), 4.0, 0.000001, "50km body X shifted");
    EXPECT_NEAR(rt_vec3_y(far_body_pos), 2.0, 0.000001, "50km body Y shifted");
    EXPECT_NEAR(rt_vec3_z(far_body_pos), -2.0, 0.000001, "50km body Z shifted");
    void *listener_pos = rt_soundlistener3d_get_position(listener);
    EXPECT_NEAR(rt_vec3_x(listener_pos), 2.0, 0.000001, "50km listener X shifted");
    EXPECT_NEAR(rt_vec3_y(listener_pos), 1.0, 0.000001, "50km listener Y shifted");
    EXPECT_NEAR(rt_vec3_z(listener_pos), 1.0, 0.000001, "50km listener Z shifted");
    EXPECT_NEAR(rt_vec3_x(rt_game3d_world_get_world_origin(world)),
                50020.0,
                0.000001,
                "worldOrigin accumulates 50km X delta");
    EXPECT_NEAR(rt_vec3_y(rt_game3d_world_get_world_origin(world)),
                -5.0,
                0.000001,
                "worldOrigin accumulates 50km Y delta");
    EXPECT_NEAR(rt_vec3_z(rt_game3d_world_get_world_origin(world)),
                7000.0,
                0.000001,
                "worldOrigin accumulates 50km Z delta");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_step_simulation_clamps_invalid_dt() {
    TEST("World3D.stepSimulation clamps zero, negative, and non-finite dt");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Step DT Unit"), 64, 48);
    void *entity = rt_game3d_entity_new();
    rt_game3d_world_set_gravity(world, 0.0, -10.0, 0.0);
    rt_game3d_entity_set_position(entity, 0.0, 10.0, 0.0);
    rt_game3d_entity_attach_body(entity, rt_game3d_body_def_sphere(0.5, 1.0));
    rt_game3d_world_spawn(world, entity);

    rt_game3d_world_step_simulation(world, 0.0);
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 1, "zero dt clamps and advances frame");
    EXPECT_NEAR(rt_game3d_world_get_dt(world), 1.0 / 60.0, 0.0001, "zero dt clamps to default");
    rt_game3d_world_step_simulation(world, -1.0);
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 2, "negative dt clamps and advances frame");
    rt_game3d_world_step_simulation(world, NAN);
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 3, "NaN dt clamps and advances frame");
    void *pos = rt_game3d_entity_position(entity);
    EXPECT_TRUE(rt_vec3_y(pos) < 10.0, "clamped invalid dt advances physics safely");
    EXPECT_NEAR(rt_game3d_world_get_elapsed(world),
                3.0 / 60.0,
                0.0001,
                "clamped invalid dt increments elapsed time");

    rt_game3d_world_step_simulation(world, 0.1);
    pos = rt_game3d_entity_position(entity);
    EXPECT_TRUE(rt_vec3_y(pos) < 10.0, "positive dt advances physics");
    EXPECT_NEAR(rt_game3d_world_get_dt(world), 0.1, 0.0001, "positive step stores world dt");
    EXPECT_EQ_INT(rt_game3d_world_get_frame(world), 4, "direct stepSimulation increments frame");
    EXPECT_NEAR(rt_game3d_world_get_elapsed(world),
                0.15,
                0.0001,
                "direct stepSimulation increments elapsed time");

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
    EXPECT_EQ_INT(
        rt_game3d_world_get_frame(world), 1, "controller frame increments with runFramesOnly");
    rt_canvas3d_clear_synthetic_input(canvas);
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_run_frames_only_preserves_synthetic_holds() {
    TEST("World3D.runFramesOnly preserves held synthetic input across frames");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Synthetic Hold Unit"), 64, 48);
    void *canvas = rt_game3d_world_get_canvas(world);
    void *camera = rt_game3d_world_get_camera(world);
    void *controller = rt_game3d_free_fly_controller_new(world);
    rt_game3d_free_fly_controller_set_speed(controller, 10.0);
    rt_game3d_world_set_camera_controller(world, controller);

    void *before = rt_camera3d_get_position(camera);
    rt_canvas3d_push_synthetic_key(canvas, rt_game3d_key_w(), 1);
    rt_game3d_world_run_frames_only(world, 2, 0.1);
    void *after = rt_camera3d_get_position(camera);

    EXPECT_TRUE(rt_vec3_z(after) < rt_vec3_z(before) - 1.5,
                "held W remains down for both deterministic frames");
    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_character_controller_syncs_world_position_under_parent() {
    TEST("CharacterController3D writes world position through parent transforms");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Character Parent Unit"), 80, 60);
    void *parent = rt_game3d_entity_new();
    void *child = rt_game3d_entity_new();
    rt_game3d_entity_set_position(parent, 10.0, 0.0, 0.0);
    rt_game3d_entity_set_scale(parent, 2.0);
    rt_game3d_entity_set_position(child, 1.0, 0.0, 0.0);
    rt_game3d_entity_add_child(parent, child);
    rt_game3d_world_spawn(world, parent);

    void *controller = rt_game3d_character_controller_new(world, child, 0.3, 1.8, 70.0);
    rt_game3d_character_controller_teleport(controller, 14.0, 0.0, 0.0);
    void *world_pos = rt_scene_node3d_get_world_position(rt_game3d_entity_get_node(child));
    void *local_pos = rt_game3d_entity_position(child);
    EXPECT_NEAR(rt_vec3_x(world_pos), 14.0, 0.0001, "child world position follows character");
    EXPECT_NEAR(rt_vec3_x(local_pos), 2.0, 0.0001, "child local position compensates parent scale");

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
    rt_game3d_orbit_controller_set_target(orbit, entity);
    EXPECT_TRUE(rt_game3d_orbit_controller_get_target(orbit) == entity,
                "orbit target can be an Entity3D");
    rt_game3d_orbit_controller_late_update(orbit, world, 0.0);
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
    EXPECT_NEAR(
        rt_vec3_x(camera_pos), rt_vec3_x(after), 0.0001, "camera lateUpdate follows character x");
    EXPECT_NEAR(
        rt_vec3_z(camera_pos), rt_vec3_z(after), 0.0001, "camera lateUpdate follows character z");
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
    EXPECT_NEAR(rt_material3d_get_emissive_intensity(emissive), 2.5, 0.0001, "Emissive intensity");
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
    EXPECT_TRUE(rt_game3d_entity_get_material(box) == plastic,
                "Box prefab retains caller material");
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
    vgfx3d_postfx_chain_t chain = {};
    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) != 0, "PostFX.Cinematic exports a chain");
    int found_subtle_vignette = 0;
    for (int32_t i = 0; i < chain.effect_count; i++) {
        if (chain.effects[i].type == VGFX3D_POSTFX_EFFECT_VIGNETTE &&
            chain.effects[i].snapshot.vignette_radius >= 0.85f &&
            chain.effects[i].snapshot.vignette_softness >= 0.20f) {
            found_subtle_vignette = 1;
        }
    }
    vgfx3d_postfx_chain_free(&chain);
    EXPECT_TRUE(found_subtle_vignette != 0,
                "PostFX.Cinematic keeps vignette subtle enough for playable demos");
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
    {
        void *water = rt_game3d_world_find_entity(world, rt_const_cstr("Environment Water"));
        rt_mesh3d *water_mesh = (rt_mesh3d *)rt_game3d_entity_get_mesh(water);
        EXPECT_TRUE(water_mesh != nullptr && water_mesh->vertex_count > 0,
                    "Environment water owns a mesh");
        float min_x = water_mesh->vertices[0].pos[0];
        float max_x = water_mesh->vertices[0].pos[0];
        float min_z = water_mesh->vertices[0].pos[2];
        float max_z = water_mesh->vertices[0].pos[2];
        for (uint32_t i = 1; i < water_mesh->vertex_count; ++i) {
            if (water_mesh->vertices[i].pos[0] < min_x)
                min_x = water_mesh->vertices[i].pos[0];
            if (water_mesh->vertices[i].pos[0] > max_x)
                max_x = water_mesh->vertices[i].pos[0];
            if (water_mesh->vertices[i].pos[2] < min_z)
                min_z = water_mesh->vertices[i].pos[2];
            if (water_mesh->vertices[i].pos[2] > max_z)
                max_z = water_mesh->vertices[i].pos[2];
        }
        EXPECT_NEAR(max_x - min_x, 48.0, 0.001, "water width follows terrain size");
        EXPECT_NEAR(max_z - min_z, 48.0, 0.001, "water depth follows terrain size");
    }
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
    EXPECT_EQ_INT(rt_pixels_width(final_pixels),
                  rt_canvas3d_get_window_width(canvas),
                  "debug final frame width");
    EXPECT_EQ_INT(rt_pixels_height(final_pixels),
                  rt_canvas3d_get_window_height(canvas),
                  "debug final frame height");

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

    void *model_handle = rt_game3d_assets_load_model_async(path);
    EXPECT_TRUE(model_handle != nullptr, "LoadModelAsync returns an AssetHandle3D");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(model_handle) - 0.0) < 0.000001,
                "new entity AssetHandle3D starts pending");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(model_handle) != 0,
                "entity AssetHandle3D completes on first ready observation");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(model_handle) - 1.0) < 0.000001,
                "entity AssetHandle3D reports complete progress");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(model_handle)), "") ==
                    0,
                "entity AssetHandle3D has no error");
    void *async_entity = rt_game3d_asset_handle_get_entity(model_handle);
    EXPECT_TRUE(async_entity != nullptr, "entity AssetHandle3D exposes the loaded entity");
    EXPECT_TRUE(rt_game3d_entity_is_group(async_entity) != 0,
                "entity AssetHandle3D result is a group entity");
    EXPECT_TRUE(rt_game3d_asset_handle_get_template(model_handle) == nullptr,
                "entity AssetHandle3D has no template result");
    rt_game3d_asset_handle_cancel(model_handle);
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(model_handle) == async_entity,
                "cancelling a completed AssetHandle3D leaves the entity result intact");

    void *cancelled_handle = rt_game3d_assets_load_model_async(path);
    EXPECT_TRUE(cancelled_handle != nullptr, "LoadModelAsync returns a cancellable handle");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(cancelled_handle) - 0.0) <
                    0.000001,
                "cancellable AssetHandle3D remains pending before observation");
    rt_game3d_asset_handle_cancel(cancelled_handle);
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(cancelled_handle) != 0,
                "cancelled AssetHandle3D becomes terminal");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(cancelled_handle) - 1.0) <
                    0.000001,
                "cancelled AssetHandle3D reports terminal progress");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(cancelled_handle)),
                            "cancelled") == 0,
                "cancelled AssetHandle3D exposes cancellation error text");
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(cancelled_handle) == nullptr,
                "cancelled AssetHandle3D has no entity result");

    const char *missing_path = "/tmp/viper_game3d_missing_async_model.gltf";
    std::remove(missing_path);
    rt_string missing_path_s = rt_string_from_bytes(missing_path, std::strlen(missing_path));
    void *missing_entity_handle = rt_game3d_assets_load_model_async(missing_path_s);
    EXPECT_TRUE(missing_entity_handle != nullptr,
                "LoadModelAsync returns a handle for a missing filesystem path");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(missing_entity_handle) - 0.0) <
                    0.000001,
                "missing-path AssetHandle3D starts pending before observation");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(missing_entity_handle) != 0,
                "missing-path AssetHandle3D becomes terminal on observation");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(missing_entity_handle)),
                            "cannot read file") == 0,
                "missing-path AssetHandle3D exposes a load error");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(missing_entity_handle) - 1.0) <
                    0.000001,
                "missing-path AssetHandle3D reports terminal progress");
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(missing_entity_handle) == nullptr,
                "missing-path AssetHandle3D has no entity result");
    rt_string_unref(missing_path_s);

    rt_string missing_asset_s =
        rt_string_from_bytes("viper/missing/async_model_template.gltf", 39);
    void *missing_asset_handle = rt_game3d_assets_load_model_template_asset_async(missing_asset_s);
    EXPECT_TRUE(missing_asset_handle != nullptr,
                "LoadModelTemplateAssetAsync returns a handle for a missing asset path");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(missing_asset_handle) != 0,
                "missing-asset AssetHandle3D becomes terminal on observation");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(missing_asset_handle)),
                            "asset not found") == 0,
                "missing-asset AssetHandle3D exposes a load error");
    EXPECT_TRUE(rt_game3d_asset_handle_get_template(missing_asset_handle) == nullptr,
                "missing-asset AssetHandle3D has no template result");
    rt_string_unref(missing_asset_s);

    const char *corrupt_path = "/tmp/viper_game3d_corrupt_async_model.gltf";
    EXPECT_TRUE(write_text_file(corrupt_path, "not valid json"),
                "test can write corrupt async model fixture");
    rt_string corrupt_path_s = rt_string_from_bytes(corrupt_path, std::strlen(corrupt_path));
    void *corrupt_handle = rt_game3d_assets_load_model_async(corrupt_path_s);
    EXPECT_TRUE(corrupt_handle != nullptr,
                "LoadModelAsync returns a handle for a corrupt filesystem payload");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(corrupt_handle) - 0.0) <
                    0.000001,
                "corrupt AssetHandle3D starts pending before observation");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(corrupt_handle) != 0,
                "corrupt AssetHandle3D becomes terminal on observation");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(corrupt_handle)),
                            "failed to load model") == 0,
                "corrupt AssetHandle3D exposes a load error without trapping");
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(corrupt_handle) == nullptr,
                "corrupt AssetHandle3D has no entity result");
    rt_string_unref(corrupt_path_s);
    std::remove(corrupt_path);

    const char *unsupported_path = "/tmp/viper_game3d_unsupported_async_model.txt";
    EXPECT_TRUE(write_text_file(unsupported_path, "not a model"),
                "test can write unsupported async model fixture");
    rt_string unsupported_path_s =
        rt_string_from_bytes(unsupported_path, std::strlen(unsupported_path));
    void *unsupported_handle = rt_game3d_assets_load_model_template_async(unsupported_path_s);
    EXPECT_TRUE(unsupported_handle != nullptr,
                "LoadModelTemplateAsync returns a handle for an unsupported extension");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(unsupported_handle) != 0,
                "unsupported-extension AssetHandle3D becomes terminal on observation");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_asset_handle_get_error(unsupported_handle)),
                            "unsupported file extension") == 0,
                "unsupported-extension AssetHandle3D exposes a preflight load error");
    EXPECT_TRUE(rt_game3d_asset_handle_get_template(unsupported_handle) == nullptr,
                "unsupported-extension AssetHandle3D has no template result");
    rt_string_unref(unsupported_path_s);
    std::remove(unsupported_path);

    void *asset_model_handle = rt_game3d_assets_load_model_asset_async(path);
    EXPECT_TRUE(asset_model_handle != nullptr, "LoadModelAssetAsync returns an AssetHandle3D");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(asset_model_handle) != 0,
                "asset entity AssetHandle3D is ready");
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(asset_model_handle) != nullptr,
                "asset entity AssetHandle3D exposes the loaded entity");

    void *template_handle = rt_game3d_assets_load_model_template_async(path);
    EXPECT_TRUE(template_handle != nullptr, "LoadModelTemplateAsync returns an AssetHandle3D");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(template_handle) - 0.0) <
                    0.000001,
                "new template AssetHandle3D starts pending");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(template_handle) != 0,
                "template AssetHandle3D completes on first ready observation");
    EXPECT_TRUE(std::fabs(rt_game3d_asset_handle_get_progress(template_handle) - 1.0) < 0.000001,
                "template AssetHandle3D reports complete progress");
    EXPECT_TRUE(rt_game3d_asset_handle_get_template(template_handle) == tpl1,
                "template AssetHandle3D shares the cached ModelTemplate");
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(template_handle) == nullptr,
                "template AssetHandle3D has no entity result");

    void *asset_template_handle = rt_game3d_assets_load_model_template_asset_async(path);
    EXPECT_TRUE(asset_template_handle != nullptr,
                "LoadModelTemplateAssetAsync returns an AssetHandle3D");
    EXPECT_TRUE(rt_game3d_asset_handle_get_ready(asset_template_handle) != 0,
                "asset template AssetHandle3D is ready");
    EXPECT_TRUE(rt_game3d_model_template_get_is_asset(
                    rt_game3d_asset_handle_get_template(asset_template_handle)) != 0,
                "asset template AssetHandle3D exposes an asset-backed template");

    rt_game3d_assets_evict(model_handle);
    EXPECT_TRUE(rt_game3d_asset_handle_get_entity(model_handle) == async_entity,
                "evicting an entity AssetHandle3D is a stable no-op");

    rt_game3d_assets_evict(template_handle);
    EXPECT_TRUE(rt_game3d_asset_handle_get_template(template_handle) == tpl1,
                "evicting a template handle keeps the handle result alive");
    void *tpl_after_evict = rt_game3d_assets_load_model_template(path);
    EXPECT_TRUE(tpl_after_evict != nullptr && tpl_after_evict != tpl1,
                "Assets3D.Evict drops the shared cached ModelTemplate entry");

    rt_game3d_assets_set_residency_budget(0);
    void *budget_tpl1 = rt_game3d_assets_load_model_template(path);
    void *budget_tpl2 = rt_game3d_assets_load_model_template(path);
    EXPECT_TRUE(budget_tpl1 != nullptr && budget_tpl2 != nullptr && budget_tpl1 != budget_tpl2,
                "zero residency budget evicts cached templates after each load");
    rt_game3d_assets_set_residency_budget(-1);

    rt_game3d_assets_preload(path);
    rt_game3d_assets_clear_cache();
    PASS();
}

static bool test_assets3d_loads_packaged_gltf_hierarchy() {
    TEST("Assets3D.LoadModelAsset preserves packaged glTF hierarchy");
    const char *pack_path = "/tmp/viper_game3d_packaged_hierarchy.vpa";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f};
    for (float v : positions)
        append_game3d_test_bytes(gltf_buffer, v);

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"buffers/tri.bin\",\"byteLength\":" +
        std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"nodes\":["
        "{\"name\":\"PackRoot\",\"children\":[1]},"
        "{\"name\":\"PackChild\",\"mesh\":0,\"translation\":[1.25,0.0,-0.5]}"
        "],"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"scene\":0"
        "}";

    Game3DTestVpaEntry entries[] = {
        {"assets/models/hierarchy.gltf",
         reinterpret_cast<const uint8_t *>(gltf_json.data()),
         gltf_json.size()},
        {"assets/models/buffers/tri.bin", gltf_buffer.data(), gltf_buffer.size()},
    };
    EXPECT_TRUE(write_game3d_test_vpa(pack_path, entries, 2),
                "Game3D hierarchy asset pack can be written");
    EXPECT_TRUE(rt_asset_mount(rt_const_cstr(pack_path)) == 1,
                "Game3D hierarchy asset pack can mount");

    rt_game3d_assets_clear_cache();
    void *entity =
        rt_game3d_assets_load_model_asset(rt_const_cstr("assets/models/hierarchy.gltf"));
    EXPECT_TRUE(entity != nullptr, "LoadModelAsset loads a packaged glTF hierarchy");
    EXPECT_TRUE(rt_game3d_entity_is_group(entity) != 0, "packaged glTF imports as group entity");
    EXPECT_TRUE(rt_scene_node3d_child_count(rt_game3d_entity_get_node(entity)) > 0,
                "packaged glTF root keeps imported children");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Packaged Hierarchy"), 64, 48);
    rt_game3d_world_spawn(world, entity);
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("PackRoot")) != nullptr,
                "spawned packaged glTF exposes root node by name");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("PackChild")) != nullptr,
                "spawned packaged glTF exposes child node by name");
    rt_game3d_world_run_frames_only(world, 1, 1.0 / 60.0);
    void *pixels = rt_game3d_world_capture_final_frame(world);
    EXPECT_TRUE(pixels != nullptr, "packaged glTF hierarchy leaves a capturable frame");
    EXPECT_EQ_INT(rt_pixels_width(pixels), 64, "packaged hierarchy capture width");
    EXPECT_EQ_INT(rt_pixels_height(pixels), 48, "packaged hierarchy capture height");

    rt_game3d_world_destroy(world);
    rt_game3d_assets_clear_cache();
    rt_asset_unmount(rt_const_cstr(pack_path));
    std::remove(pack_path);
    PASS();
}

static bool test_phase5_world_stream3d_baseline() {
    TEST("WorldStream3D tracks mounted manifests and resident budgets");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D WorldStream Unit"), 80, 60);
    void *stream = rt_game3d_world_get_stream(world);
    EXPECT_TRUE(stream != nullptr, "World3D.stream returns an owned stream handle");
    EXPECT_TRUE(rt_game3d_world_get_stream(world) == stream,
                "World3D.stream is stable across repeated property reads");
    void *standalone_stream = rt_game3d_world_stream_new(world);
    EXPECT_TRUE(standalone_stream != nullptr, "WorldStream3D.New still returns a stream handle");
    EXPECT_EQ_INT(
        rt_game3d_world_stream_get_resident_cell_count(stream), 0, "initial cell count is zero");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  0,
                  "initial terrain tile count is zero");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  0,
                  "initial pending request count is zero");
    EXPECT_EQ_INT(
        rt_game3d_world_stream_get_resident_bytes(stream), 0, "initial resident bytes are zero");

    void *center = rt_vec3_new(512.0, 0.0, -256.0);
    rt_game3d_world_stream_set_center(stream, center);
    rt_game3d_world_stream_set_radii(stream, 256.0, 128.0);
    rt_game3d_world_stream_mount_cells(stream, rt_const_cstr("assets/world/cells.vscn"));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_cell_count(stream) > 0,
                "mounted cell manifest produces resident cell telemetry");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  0,
                  "terrain count stays zero until terrain manifest is mounted");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  0,
                  "baseline stream has no pending requests");
    int64_t cell_only_bytes = rt_game3d_world_stream_get_resident_bytes(stream);
    EXPECT_TRUE(cell_only_bytes > 0, "mounted cells produce resident-byte telemetry");
    EXPECT_EQ_INT(rt_game3d_world_get_stream_resident_bytes(world),
                  cell_only_bytes,
                  "World3D.streamResidentBytes wraps owned stream telemetry");

    rt_game3d_world_stream_mount_tiled_terrain(stream, rt_const_cstr("assets/world/terrain.vscn"));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile_count(stream) > 0,
                "mounted terrain manifest produces resident terrain telemetry");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_bytes(stream) > cell_only_bytes,
                "terrain residency adds to resident-byte telemetry");

    rt_game3d_world_stream_set_residency_budget(stream, 0);
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  0,
                  "zero stream residency budget evicts cells");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  0,
                  "zero stream residency budget evicts terrain tiles");
    EXPECT_EQ_INT(
        rt_game3d_world_stream_get_resident_bytes(stream), 0, "zero stream budget clears bytes");

    rt_game3d_world_stream_set_residency_budget(stream, -1);
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_cell_count(stream) > 0,
                "negative stream budget restores unlimited residency");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile_count(stream) > 0,
                "negative stream budget restores terrain residency");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_phase5_world_stream3d_terrain_manifest() {
    TEST("WorldStream3D parses tiled terrain manifests for deterministic residency");
    const char *manifest_path = "/tmp/viper_game3d_terrain_tiles_manifest.json";
    const char *near_height_path = "/tmp/viper_game3d_terrain_near.height";
    const char *far_height_path = "/tmp/viper_game3d_terrain_far.height";
    const char *near_heights =
        "viper-heightmap-v1 3 3\n"
        "0.25 0.35 0.45\n"
        "0.30 0.40 0.50\n"
        "0.35 0.45 0.55\n";
    const char *far_heights =
        "viper-heightmap-v1 3 3\n"
        "0.45 0.55 0.65\n"
        "0.50 0.60 0.70\n"
        "0.55 0.65 0.75\n";
    const char *manifest =
        "{\"tiles\":["
        "{\"name\":\"near\",\"path\":\"terrain/near.tile\",\"heightmap\":\"viper_game3d_terrain_near.height\","
        "\"center\":[0,0,0],\"radius\":32,\"bytes\":1000,"
        "\"width\":4,\"depth\":4,\"scale\":[2,3,4]},"
        "{\"name\":\"far\",\"path\":\"terrain/far.tile\",\"heightmap\":\"viper_game3d_terrain_far.height\","
        "\"center\":[512,0,0],\"radius\":32,\"bytes\":1000,"
        "\"width\":8,\"depth\":8,\"scale\":[4,3,8]}"
        "]}";
    EXPECT_TRUE(write_text_file(near_height_path, near_heights),
                "near terrain height sidecar is writable");
    EXPECT_TRUE(write_text_file(far_height_path, far_heights),
                "far terrain height sidecar is writable");
    EXPECT_TRUE(write_text_file(manifest_path, manifest), "terrain manifest fixture is writable");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Terrain Stream Unit"), 80, 60);
    void *stream = rt_game3d_world_get_stream(world);
    rt_game3d_world_stream_set_radii(stream, 96.0, 96.0);
    rt_game3d_world_stream_set_center(stream, rt_vec3_new(0.0, 0.0, 0.0));
    rt_game3d_world_stream_mount_tiled_terrain(stream, rt_const_cstr(manifest_path));
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  1,
                  "terrain manifest loads the near tile only");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  1,
                  "resident terrain tile creates a heightfield collider body");
    void *near_terrain = rt_game3d_world_stream_get_resident_terrain_tile(stream, 0);
    EXPECT_TRUE(near_terrain != nullptr, "resident terrain tile exposes a Terrain3D payload");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile(stream, 1) == nullptr,
                "out-of-range resident terrain tile query returns null");
    EXPECT_TRUE(std::strstr(rt_string_cstr(rt_game3d_world_stream_get_terrain_tile_heightmap(stream, 0)),
                            "viper_game3d_terrain_near.height") != nullptr,
                "terrain tile heightmap inspection returns the resolved sidecar path");
    EXPECT_NEAR(rt_terrain3d_get_height_at(near_terrain, 1.0, 1.0),
                0.75,
                0.001,
                "manifest terrain payload applies a relative height sidecar");
    void *near_nav = rt_game3d_world_bake_nav_mesh(world, 0.35, 1.8, 45.0, 0.3);
    EXPECT_TRUE(near_nav != nullptr, "resident terrain tile contributes a nav-bake source");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(near_nav) > 0,
                "terrain nav-bake source produces walkable triangles");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(near_nav, rt_vec3_new(0.0, 1.0, 0.0)) != 0,
                "terrain nav-bake source uses streamed tile world placement");
    rt_camera3d_look_at(rt_game3d_world_get_camera(world),
                        rt_vec3_new(0.0, 12.0, 18.0),
                        rt_vec3_new(0.0, 0.0, 0.0),
                        rt_vec3_new(0.0, 1.0, 0.0));
    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_end_scene(world);
    EXPECT_TRUE(rt_game3d_world_get_draw_count(world) > 0,
                "World3D.drawScene renders resident stream terrain tiles");
    int64_t near_bytes = rt_game3d_world_stream_get_resident_bytes(stream);
    EXPECT_TRUE(near_bytes > 1000, "terrain manifest contributes metadata and tile bytes");

    rt_game3d_world_stream_set_center(stream, rt_vec3_new(512.0, 0.0, 0.0));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  1,
                  "terrain manifest unloads distant tiles and loads the new near tile");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  1,
                  "terrain heightfield collider body swaps with tile residency");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile(stream, 0) != nullptr,
                "terrain payload remains available after tile residency changes");

    rt_game3d_world_stream_set_radii(stream, 1024.0, 1024.0);
    rt_game3d_world_stream_set_center(stream, rt_vec3_new(256.0, 0.0, 0.0));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  2,
                  "large terrain stream radius admits both manifest tiles");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  2,
                  "both resident terrain tiles own heightfield collider bodies");
    void *near_again = rt_game3d_world_stream_get_resident_terrain_tile(stream, 0);
    void *far_again = rt_game3d_world_stream_get_resident_terrain_tile(stream, 1);
    EXPECT_TRUE(near_again != nullptr && far_again != nullptr,
                "both heightmapped terrain payloads are resident");
    EXPECT_NEAR(rt_terrain3d_get_height_at(near_again, 6.0, 0.0),
                rt_terrain3d_get_height_at(far_again, 0.0, 0.0),
                0.001,
                "adjacent height sidecars can share a seam edge");

    rt_game3d_world_stream_set_residency_budget(stream, 0);
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  0,
                  "zero budget clears manifest terrain tiles");
    EXPECT_EQ_INT(rt_game3d_world_get_body_count(world),
                  0,
                  "zero budget unloads terrain heightfield collider bodies");
    EXPECT_TRUE(rt_game3d_world_bake_nav_mesh(world, 0.35, 1.8, 45.0, 0.3) == nullptr,
                "zero budget unloads terrain nav-bake sources");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile(stream, 0) == nullptr,
                "zero budget releases resident terrain payloads");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_bytes(stream),
                  0,
                  "zero budget clears manifest terrain bytes");

    rt_game3d_world_destroy(world);
    std::remove(manifest_path);
    std::remove(near_height_path);
    std::remove(far_height_path);
    PASS();
}

static bool write_stream_cell_scene(const char *path, const char *marker_name) {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    rt_scene_node3d_set_name(node, rt_const_cstr(marker_name));
    rt_scene3d_add(scene, node);
    return rt_scene3d_save(scene, rt_const_cstr(path)) == 1;
}

static bool test_phase5_world_stream3d_manifest_cells() {
    TEST("WorldStream3D mounts and unloads VSCN scene cells from a manifest");

    const char *near_path = "/tmp/viper_game3d_stream_near.vscn";
    const char *far_path = "/tmp/viper_game3d_stream_far.vscn";
    const char *manifest_path = "/tmp/viper_game3d_stream_cells_manifest.vscn";
    EXPECT_TRUE(write_stream_cell_scene(near_path, "stream_near_marker"),
                "near stream cell fixture saves");
    EXPECT_TRUE(write_stream_cell_scene(far_path, "stream_far_marker"), "far stream cell fixture saves");

    char manifest[2048];
    std::snprintf(manifest,
                  sizeof(manifest),
                  "{"
                  "\"cells\":["
                  "{\"name\":\"near_cell\",\"path\":\"%s\",\"center\":[0,0,0],"
                  "\"radius\":8,\"bytes\":65536},"
                  "{\"name\":\"far_cell\",\"path\":\"%s\",\"center\":[1000,0,0],"
                  "\"radius\":8,\"bytes\":65536}"
                  "]"
                  "}",
                  near_path,
                  far_path);
    EXPECT_TRUE(write_text_file(manifest_path, manifest), "stream manifest fixture writes");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D WorldStream Manifest Unit"), 80, 60);
    void *stream = rt_game3d_world_get_stream(world);
    rt_game3d_world_stream_set_center(stream, rt_vec3_new(0.0, 0.0, 0.0));
    rt_game3d_world_stream_set_radii(stream, 64.0, 96.0);
    rt_game3d_world_stream_mount_cells(stream, rt_const_cstr(manifest_path));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);

    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  1,
                  "manifest stream loads only the near cell");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("stream_near_marker")) != nullptr,
                "near cell scene subtree is attached to the world scene");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("stream_far_marker")) == nullptr,
                "far cell remains unloaded outside the load radius");
    int64_t near_bytes = rt_game3d_world_stream_get_resident_bytes(stream);
    EXPECT_TRUE(near_bytes >= 65536, "manifest stream accounts loaded cell bytes");

    rt_game3d_world_stream_set_center(stream, rt_vec3_new(1000.0, 0.0, 0.0));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  1,
                  "manifest stream swaps residency after crossing cell boundary");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("stream_near_marker")) == nullptr,
                "near cell scene subtree unloads outside the unload radius");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("stream_far_marker")) != nullptr,
                "far cell scene subtree loads inside the load radius");

    rt_game3d_world_stream_set_residency_budget(stream, 0);
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  0,
                  "zero manifest stream budget unloads all cells");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("stream_far_marker")) == nullptr,
                "budget eviction detaches the resident cell subtree");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_bytes(stream),
                  0,
                  "zero manifest stream budget clears resident bytes");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_phase5_world_stream3d_hitch_budgeted_update() {
    TEST("WorldStream3D update budgets stream loads and reports pending requests");

    const char *near_path = "/tmp/viper_game3d_stream_budget_near.vscn";
    const char *far_path = "/tmp/viper_game3d_stream_budget_far.vscn";
    const char *cells_manifest_path = "/tmp/viper_game3d_stream_budget_cells.vscn";
    const char *terrain_manifest_path = "/tmp/viper_game3d_stream_budget_terrain.vscn";
    EXPECT_TRUE(write_stream_cell_scene(near_path, "budget_near_marker"),
                "near budget cell fixture saves");
    EXPECT_TRUE(write_stream_cell_scene(far_path, "budget_far_marker"),
                "far budget cell fixture saves");

    char cells_manifest[2048];
    std::snprintf(cells_manifest,
                  sizeof(cells_manifest),
                  "{"
                  "\"cells\":["
                  "{\"name\":\"budget_near\",\"path\":\"%s\",\"center\":[0,0,0],"
                  "\"radius\":16,\"bytes\":4096},"
                  "{\"name\":\"budget_far\",\"path\":\"%s\",\"center\":[1000,0,0],"
                  "\"radius\":16,\"bytes\":4096}"
                  "]"
                  "}",
                  near_path,
                  far_path);
    EXPECT_TRUE(write_text_file(cells_manifest_path, cells_manifest),
                "budget cell manifest writes");

    const char *terrain_manifest =
        "{\"tiles\":["
        "{\"name\":\"budget_terrain_near\",\"path\":\"terrain/near.tile\","
        "\"center\":[0,0,0],\"radius\":16,\"bytes\":4096,"
        "\"width\":4,\"depth\":4,\"scale\":[2,1,2]},"
        "{\"name\":\"budget_terrain_far\",\"path\":\"terrain/far.tile\","
        "\"center\":[1000,0,0],\"radius\":16,\"bytes\":4096,"
        "\"width\":4,\"depth\":4,\"scale\":[2,1,2]}"
        "]}";
    EXPECT_TRUE(write_text_file(terrain_manifest_path, terrain_manifest),
                "budget terrain manifest writes");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Stream Budget Unit"), 80, 60);
    void *stream = rt_game3d_world_get_stream(world);
    rt_game3d_world_stream_set_center(stream, rt_vec3_new(0.0, 0.0, 0.0));
    rt_game3d_world_stream_set_radii(stream, 64.0, 96.0);
    rt_game3d_world_stream_mount_cells(stream, rt_const_cstr(cells_manifest_path));
    rt_game3d_world_stream_mount_tiled_terrain(stream, rt_const_cstr(terrain_manifest_path));
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  1,
                  "initial mount admits the near cell");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  1,
                  "initial mount admits the near terrain tile");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  0,
                  "initial mount has no pending stream work");

    rt_game3d_world_stream_set_center(stream, rt_vec3_new(1000.0, 0.0, 0.0));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  1,
                  "first budgeted update loads the far cell");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  0,
                  "first budgeted update defers the far terrain tile");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  1,
                  "deferred terrain load is reported as pending");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("budget_near_marker")) == nullptr,
                "budgeted update unloads the stale near cell immediately");
    EXPECT_TRUE(rt_game3d_world_find_node(world, rt_const_cstr("budget_far_marker")) != nullptr,
                "budgeted update attaches the far cell");

    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_cell_count(stream),
                  1,
                  "second budgeted update keeps the far cell resident");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_resident_terrain_tile_count(stream),
                  1,
                  "second budgeted update loads the pending terrain tile");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  0,
                  "pending stream work drains after the second update");
    EXPECT_TRUE(rt_game3d_world_stream_get_resident_terrain_tile(stream, 0) != nullptr,
                "budgeted terrain load exposes the resident Terrain3D payload");

    rt_game3d_world_destroy(world);
    std::remove(near_path);
    std::remove(far_path);
    std::remove(cells_manifest_path);
    std::remove(terrain_manifest_path);
    PASS();
}

static bool test_phase12_world_stream3d_inspection_hooks() {
    TEST("WorldStream3D exposes editor inspection hooks for parsed entries");

    const char *near_path = "/tmp/viper_game3d_stream_inspect_near.vscn";
    const char *far_path = "/tmp/viper_game3d_stream_inspect_far.vscn";
    const char *cells_manifest_path = "/tmp/viper_game3d_stream_inspect_cells.vscn";
    const char *terrain_manifest_path = "/tmp/viper_game3d_stream_inspect_terrain.vscn";
    EXPECT_TRUE(write_stream_cell_scene(near_path, "inspect_near_marker"),
                "near inspection cell fixture saves");
    EXPECT_TRUE(write_stream_cell_scene(far_path, "inspect_far_marker"),
                "far inspection cell fixture saves");

    char cells_manifest[2048];
    std::snprintf(cells_manifest,
                  sizeof(cells_manifest),
                  "{"
                  "\"cells\":["
                  "{\"name\":\"inspect_near\",\"path\":\"%s\",\"center\":[0,0,0],"
                  "\"radius\":16,\"bytes\":1111},"
                  "{\"name\":\"inspect_far\",\"path\":\"%s\",\"center\":[512,0,0],"
                  "\"radius\":16,\"bytes\":2222}"
                  "]"
                  "}",
                  near_path,
                  far_path);
    EXPECT_TRUE(write_text_file(cells_manifest_path, cells_manifest),
                "inspection cell manifest writes");

    const char *terrain_manifest =
        "{\"tiles\":["
        "{\"name\":\"terrain_near\",\"path\":\"terrain/near.tile\",\"center\":[0,0,0],"
        "\"radius\":16,\"bytes\":3333,\"width\":8,\"depth\":10,\"scale\":[2,3,4]},"
        "{\"name\":\"terrain_far\",\"path\":\"terrain/far.tile\",\"center\":[512,0,0],"
        "\"radius\":16,\"bytes\":4444,\"width\":12,\"depth\":14,\"scale\":[4,5,6]}"
        "]}";
    EXPECT_TRUE(write_text_file(terrain_manifest_path, terrain_manifest),
                "inspection terrain manifest writes");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Stream Inspection Unit"), 80, 60);
    void *stream = rt_game3d_world_get_stream(world);
    rt_game3d_world_stream_set_radii(stream, 64.0, 64.0);
    rt_game3d_world_stream_set_center(stream, rt_vec3_new(0.0, 0.0, 0.0));
    rt_game3d_world_stream_mount_cells(stream, rt_const_cstr(cells_manifest_path));
    rt_game3d_world_stream_mount_tiled_terrain(stream, rt_const_cstr(terrain_manifest_path));

    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_count(stream),
                  2,
                  "inspection hooks expose parsed cell count");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_count(stream),
                  2,
                  "inspection hooks expose parsed terrain tile count");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_world_stream_get_cell_name(stream, 0)),
                            "inspect_near") == 0,
                "inspection hooks expose cell names");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_world_stream_get_terrain_tile_name(stream, 1)),
                            "terrain_far") == 0,
                "inspection hooks expose terrain tile names");
    void *near_cell_center = rt_game3d_world_stream_get_cell_center(stream, 0);
    void *far_tile_center = rt_game3d_world_stream_get_terrain_tile_center(stream, 1);
    EXPECT_NEAR(rt_vec3_x(near_cell_center),
                0.0,
                0.000001,
                "inspection hook returns cell center x");
    EXPECT_NEAR(rt_vec3_x(far_tile_center),
                512.0,
                0.000001,
                "inspection hook returns terrain tile center x");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_bytes(stream, 0),
                  1111,
                  "inspection hooks expose cell byte estimates");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_bytes(stream, 1),
                  4444,
                  "inspection hooks expose terrain tile byte estimates");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_resident(stream, 0),
                  1,
                  "near cell starts resident");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_resident(stream, 1),
                  0,
                  "far cell starts nonresident");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_resident(stream, 0),
                  1,
                  "near terrain tile starts resident");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_resident(stream, 1),
                  0,
                  "far terrain tile starts nonresident");

    rt_game3d_world_stream_set_center(stream, rt_vec3_new(512.0, 0.0, 0.0));
    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_resident(stream, 0),
                  0,
                  "near cell unloads after inspection-center move");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_resident(stream, 1),
                  1,
                  "far cell becomes resident after inspection-center move");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_resident(stream, 0),
                  0,
                  "near terrain tile unloads after inspection-center move");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_resident(stream, 1),
                  0,
                  "far terrain tile waits behind the per-frame stream budget");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  1,
                  "inspection-center move reports the deferred terrain request");

    rt_game3d_world_stream_update(stream, 1.0 / 60.0);
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_resident(stream, 1),
                  1,
                  "far terrain tile becomes resident after inspection-center move");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_pending_request_count(stream),
                  0,
                  "inspection-center stream work drains after a second update");

    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_world_stream_get_cell_name(stream, -1)),
                            "") == 0,
                "invalid cell name query returns empty string");
    EXPECT_TRUE(rt_game3d_world_stream_get_cell_center(stream, 99) == nullptr,
                "invalid cell center query returns null");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_cell_resident(stream, 99),
                  0,
                  "invalid cell resident query returns false");
    EXPECT_EQ_INT(rt_game3d_world_stream_get_terrain_tile_bytes(stream, 99),
                  0,
                  "invalid terrain byte query returns zero");

    rt_game3d_world_destroy(world);
    std::remove(near_path);
    std::remove(far_path);
    std::remove(cells_manifest_path);
    std::remove(terrain_manifest_path);
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
    void *body_pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(body_pos), 1.0, 0.0001, "attachBody syncs initial node X");
    EXPECT_NEAR(rt_vec3_y(body_pos), 2.0, 0.0001, "attachBody syncs initial node Y");
    EXPECT_NEAR(rt_vec3_z(body_pos), 3.0, 0.0001, "attachBody syncs initial node Z");
    rt_game3d_entity_set_position(entity, 4.0, 5.0, 6.0);
    body_pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(body_pos), 4.0, 0.0001, "setPosition syncs body X");
    EXPECT_NEAR(rt_vec3_y(body_pos), 5.0, 0.0001, "setPosition syncs body Y");
    EXPECT_NEAR(rt_vec3_z(body_pos), 6.0, 0.0001, "setPosition syncs body Z");
    rt_game3d_entity_set_rotation_euler(entity, 0.0, 90.0, 0.0);
    {
        void *body_rot = rt_body3d_get_orientation(body);
        EXPECT_TRUE(std::fabs(rt_quat_x(body_rot)) + std::fabs(rt_quat_y(body_rot)) +
                            std::fabs(rt_quat_z(body_rot)) >
                        0.5,
                    "setRotationEuler syncs body orientation");
    }
    rt_game3d_entity_set_scale_xyz(entity, 2.0, 3.0, 4.0);
    void *body_scale = rt_body3d_get_scale(body);
    EXPECT_NEAR(rt_vec3_x(body_scale), 2.0, 0.0001, "setScale syncs body scale X");
    EXPECT_NEAR(rt_vec3_y(body_scale), 3.0, 0.0001, "setScale syncs body scale Y");
    EXPECT_NEAR(rt_vec3_z(body_scale), 4.0, 0.0001, "setScale syncs body scale Z");

    rt_game3d_world_spawn(world, entity);
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "spawning BodyDef-attached entity registers the body");

    void *zero_def = rt_game3d_body_def_box(1.0, 1.0, 1.0, 1.0);
    rt_game3d_body_def_set_mass(zero_def, 0.0);
    EXPECT_TRUE(rt_game3d_body_def_get_static(zero_def) != 0,
                "BodyDef zero mass switches to static mode");
    EXPECT_TRUE(rt_game3d_body_def_get_kinematic(zero_def) == 0,
                "BodyDef zero mass clears kinematic mode");
    void *zero_entity = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(zero_entity, zero_def);
    EXPECT_TRUE(rt_body3d_is_static(rt_game3d_entity_get_body(zero_entity)) != 0,
                "zero-mass BodyDef creates a static body");

    void *kinematic_def = rt_game3d_body_def_sphere(0.5, 0.0);
    rt_game3d_body_def_set_kinematic(kinematic_def, 1);
    EXPECT_TRUE(rt_game3d_body_def_get_kinematic(kinematic_def) != 0,
                "BodyDef kinematic flag is retained");
    EXPECT_TRUE(rt_game3d_body_def_get_static(kinematic_def) == 0,
                "BodyDef kinematic clears static mode");
    EXPECT_NEAR(rt_game3d_body_def_get_mass(kinematic_def),
                1.0,
                0.0001,
                "BodyDef kinematic gets a positive mass");
    void *kinematic_entity = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(kinematic_entity, kinematic_def);
    EXPECT_TRUE(rt_body3d_is_kinematic(rt_game3d_entity_get_body(kinematic_entity)) != 0,
                "kinematic BodyDef creates a kinematic body");
    EXPECT_TRUE(rt_body3d_is_static(rt_game3d_entity_get_body(kinematic_entity)) == 0,
                "kinematic BodyDef body is not static");

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

static bool test_shared_body_attachment_rejected_without_state_leak() {
    TEST("Game3D rejects sharing one Physics3DBody across spawned entities");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Shared Body Unit"), 80, 60);
    void *owner = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(owner, rt_game3d_body_def_sphere(0.5, 1.0));
    void *shared = rt_game3d_entity_get_body(owner);
    EXPECT_TRUE(shared != nullptr, "owner has a body to share");
    rt_game3d_world_spawn(world, owner);
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "owner body is registered once");

    void *pending = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(pending, shared);
    EXPECT_TRUE(expect_trap_contains(
                    [&]() { rt_game3d_world_spawn(world, pending); }, "already attached"),
                "spawn rejects a body already owned by another entity");
    EXPECT_TRUE(rt_game3d_entity_is_spawned(pending) == 0,
                "failed shared-body spawn rolls back spawned flag");
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "failed shared-body spawn does not duplicate physics body");

    void *spawned = rt_game3d_entity_new();
    rt_game3d_world_spawn(world, spawned);
    EXPECT_TRUE(expect_trap_contains(
                    [&]() { rt_game3d_entity_attach_body(spawned, shared); }, "already attached"),
                "attachBody rejects a body already owned by another spawned entity");
    EXPECT_TRUE(rt_game3d_entity_get_body(spawned) == nullptr,
                "failed attachBody leaves the spawned entity bodyless");
    EXPECT_EQ_INT(rt_world3d_body_count(rt_game3d_world_get_physics(world)),
                  1,
                  "failed attachBody preserves physics body count");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_entity_transform_sanitizes_and_respects_sync_mode() {
    TEST("Entity3D transform setters sanitize values and respect physics sync mode");
    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Transform Sync Unit"), 80, 60);
    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 1.0, 2.0, 3.0);
    rt_game3d_entity_attach_body(entity, rt_game3d_body_def_sphere(0.5, 1.0));
    void *body = rt_game3d_entity_get_body(entity);
    void *node = rt_game3d_entity_get_node(entity);
    EXPECT_TRUE(body != nullptr, "BodyDef attach creates a body");
    EXPECT_EQ_INT(rt_scene_node3d_get_sync_mode(node),
                  rt_game3d_sync_mode_node_from_body(),
                  "BodyDef default sync mode is node-from-body");

    rt_game3d_world_spawn(world, entity);
    void *body_pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(body_pos), 1.0, 0.0001, "spawn force-syncs initial body X");
    EXPECT_NEAR(rt_vec3_y(body_pos), 2.0, 0.0001, "spawn force-syncs initial body Y");
    EXPECT_NEAR(rt_vec3_z(body_pos), 3.0, 0.0001, "spawn force-syncs initial body Z");

    rt_game3d_entity_set_position(entity, NAN, 4.0, INFINITY);
    void *node_pos = rt_scene_node3d_get_position(node);
    EXPECT_NEAR(rt_vec3_x(node_pos), 0.0, 0.0001, "non-finite position X falls back to zero");
    EXPECT_NEAR(rt_vec3_y(node_pos), 4.0, 0.0001, "finite position Y is retained");
    EXPECT_NEAR(rt_vec3_z(node_pos), 0.0, 0.0001, "non-finite position Z falls back to zero");
    body_pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(body_pos), 1.0, 0.0001, "node-from-body mode does not push X");
    EXPECT_NEAR(rt_vec3_y(body_pos), 2.0, 0.0001, "node-from-body mode does not push Y");
    EXPECT_NEAR(rt_vec3_z(body_pos), 3.0, 0.0001, "node-from-body mode does not push Z");

    rt_game3d_entity_set_scale_xyz(entity, 0.0, NAN, INFINITY);
    void *node_scale = rt_scene_node3d_get_scale(node);
    EXPECT_NEAR(rt_vec3_x(node_scale), 1.0, 0.0001, "zero scale X falls back to one");
    EXPECT_NEAR(rt_vec3_y(node_scale), 1.0, 0.0001, "non-finite scale Y falls back to one");
    EXPECT_NEAR(rt_vec3_z(node_scale), 1.0, 0.0001, "non-finite scale Z falls back to one");

    rt_game3d_entity_set_rotation_euler(entity, NAN, 90.0, INFINITY);
    void *node_rot = rt_scene_node3d_get_rotation(node);
    EXPECT_TRUE(std::isfinite(rt_quat_x(node_rot)) && std::isfinite(rt_quat_y(node_rot)) &&
                    std::isfinite(rt_quat_z(node_rot)) && std::isfinite(rt_quat_w(node_rot)),
                "non-finite rotation components produce a finite quaternion");
    void *body_rot = rt_body3d_get_orientation(body);
    EXPECT_NEAR(rt_quat_x(body_rot), 0.0, 0.0001, "node-from-body mode does not push rotation X");
    EXPECT_NEAR(rt_quat_y(body_rot), 0.0, 0.0001, "node-from-body mode does not push rotation Y");
    EXPECT_NEAR(rt_quat_z(body_rot), 0.0, 0.0001, "node-from-body mode does not push rotation Z");
    EXPECT_NEAR(rt_quat_w(body_rot), 1.0, 0.0001, "node-from-body mode does not push rotation W");

    rt_scene_node3d_set_sync_mode(node, rt_game3d_sync_mode_body_from_node());
    rt_game3d_entity_set_position(entity, 7.0, 8.0, 9.0);
    body_pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(body_pos), 7.0, 0.0001, "body-from-node mode pushes X");
    EXPECT_NEAR(rt_vec3_y(body_pos), 8.0, 0.0001, "body-from-node mode pushes Y");
    EXPECT_NEAR(rt_vec3_z(body_pos), 9.0, 0.0001, "body-from-node mode pushes Z");

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
    rt_game3d_body_def_with_sync(ball_def, rt_game3d_sync_mode_body_from_node());
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
    EXPECT_EQ_INT(rt_game3d_collision_event_get_contact_count(enter),
                  rt_collision_event3d_get_contact_count(rt_game3d_collision_event_get_raw(enter)),
                  "Collision3DEvent.contactCount mirrors the wrapped raw event");
    EXPECT_TRUE(rt_game3d_collision_event_get_contact_count(enter) >= 1,
                "Collision3DEvent exposes at least one contact for an overlap");
    void *first_point = rt_game3d_collision_event_point(enter);
    void *indexed_point = rt_game3d_collision_event_contact_point(enter, 0);
    EXPECT_NEAR(rt_vec3_x(indexed_point), rt_vec3_x(first_point), 0.0001,
                "contactPoint(0) matches point() X");
    EXPECT_NEAR(rt_vec3_y(indexed_point), rt_vec3_y(first_point), 0.0001,
                "contactPoint(0) matches point() Y");
    EXPECT_NEAR(rt_vec3_z(indexed_point), rt_vec3_z(first_point), 0.0001,
                "contactPoint(0) matches point() Z");
    void *first_normal = rt_game3d_collision_event_normal(enter);
    void *indexed_normal = rt_game3d_collision_event_contact_normal(enter, 0);
    EXPECT_NEAR(rt_vec3_x(indexed_normal), rt_vec3_x(first_normal), 0.0001,
                "contactNormal(0) matches normal() X");
    EXPECT_NEAR(rt_vec3_y(indexed_normal), rt_vec3_y(first_normal), 0.0001,
                "contactNormal(0) matches normal() Y");
    EXPECT_NEAR(rt_vec3_z(indexed_normal), rt_vec3_z(first_normal), 0.0001,
                "contactNormal(0) matches normal() Z");
    EXPECT_TRUE(rt_game3d_collision_event_contact_separation(enter, 0) <= 0.0,
                "contactSeparation(0) reports penetration for overlap");
    void *missing_point = rt_game3d_collision_event_contact_point(enter, 99);
    void *missing_normal = rt_game3d_collision_event_contact_normal(enter, 99);
    EXPECT_NEAR(rt_vec3_x(missing_point), 0.0, 0.0001, "missing contact point X fallback");
    EXPECT_NEAR(rt_vec3_y(missing_point), 0.0, 0.0001, "missing contact point Y fallback");
    EXPECT_NEAR(rt_vec3_z(missing_point), 0.0, 0.0001, "missing contact point Z fallback");
    EXPECT_NEAR(rt_vec3_x(missing_normal), 0.0, 0.0001, "missing contact normal X fallback");
    EXPECT_NEAR(rt_vec3_y(missing_normal), 1.0, 0.0001, "missing contact normal Y fallback");
    EXPECT_NEAR(rt_vec3_z(missing_normal), 0.0, 0.0001, "missing contact normal Z fallback");
    EXPECT_NEAR(rt_game3d_collision_event_contact_separation(enter, 99), 0.0, 0.0001,
                "missing contact separation fallback");
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

static bool test_phase5_animator3d_events_and_root_motion() {
    TEST("Animator3D wraps controllers, reports events, and drives world root motion");

    void *controller = make_game3d_test_controller(10.0, 0.5);
    void *animator = rt_game3d_animator_new(controller);
    EXPECT_TRUE(animator != nullptr, "Animator3D.New returns an object");
    EXPECT_TRUE(rt_game3d_animator_get_controller(animator) == controller,
                "Animator3D exposes the wrapped controller");
    EXPECT_TRUE(rt_game3d_animator_play(animator, rt_const_cstr("walk")) != 0,
                "Animator3D.play starts a named state");
    EXPECT_TRUE(rt_game3d_animator_is_playing(animator, rt_const_cstr("walk")) != 0,
                "Animator3D.isPlaying checks the active state");
    rt_game3d_animator_set_speed(animator, rt_const_cstr("walk"), 1.0);
    rt_game3d_animator_update(animator, 0.5);
    EXPECT_NEAR(rt_game3d_animator_state_time(animator),
                0.5,
                0.0001,
                "Animator3D.stateTime reports base layer time");
    EXPECT_EQ_INT(rt_game3d_animator_event_count(animator), 1, "Animator3D captures events");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_animator_event_name(animator, 0)), "step") ==
                    0,
                "Animator3D.eventName returns the captured event");
    EXPECT_TRUE(rt_game3d_animator_crossfade(animator, rt_const_cstr("idle"), 0.1) != 0,
                "Animator3D.crossfade switches to another state");
    EXPECT_TRUE(rt_game3d_animator_is_playing(animator, rt_const_cstr("idle")) != 0,
                "Animator3D.isPlaying reflects crossfades");

    void *layer_skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(layer_skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    int64_t arm_bone =
        rt_skeleton3d_add_bone(layer_skel, rt_const_cstr("arm"), 0, rt_mat4_translate(0.0, 1.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(layer_skel);
    void *base = make_game3d_test_anim("base", arm_bone, 0.0, 2.0, 0.0, 0.0, 2.0, 0.0);
    void *raise = make_game3d_test_anim("raise", arm_bone, 0.0, 3.0, 0.0, 0.0, 3.0, 0.0);
    void *reach = make_game3d_test_anim("reach", arm_bone, 0.0, 5.0, 0.0, 0.0, 5.0, 0.0);
    void *layer_controller = rt_anim_controller3d_new(layer_skel);
    rt_anim_controller3d_add_state(layer_controller, rt_const_cstr("base"), base);
    rt_anim_controller3d_add_state(layer_controller, rt_const_cstr("raise"), raise);
    rt_anim_controller3d_add_state(layer_controller, rt_const_cstr("reach"), reach);
    rt_anim_controller3d_add_event(
        layer_controller, rt_const_cstr("raise"), 0.0, rt_const_cstr("raiseEnter"));
    rt_anim_controller3d_add_event(
        layer_controller, rt_const_cstr("reach"), 0.0, rt_const_cstr("reachEnter"));
    void *layer_animator = rt_game3d_animator_new(layer_controller);
    rt_game3d_animator_play(layer_animator, rt_const_cstr("base"));
    rt_anim_controller3d_set_layer_mask(layer_controller, 1, arm_bone);
    rt_anim_controller3d_set_layer_weight(layer_controller, 1, 1.0);
    EXPECT_TRUE(rt_game3d_animator_play_layer_additive(
                    layer_animator, 0, rt_const_cstr("raise")) == 0,
                "Animator3D.playLayerAdditive rejects the base layer");
    EXPECT_TRUE(rt_game3d_animator_play_layer_additive(
                    layer_animator, 1, rt_const_cstr("raise")) != 0,
                "Animator3D.playLayerAdditive forwards to the wrapped controller");
    EXPECT_EQ_INT(rt_game3d_animator_event_count(layer_animator),
                  1,
                  "Animator3D.playLayerAdditive captures layer entry events");
    EXPECT_TRUE(std::strcmp(
                    rt_string_cstr(rt_game3d_animator_event_name(layer_animator, 0)),
                    "raiseEnter") == 0,
                "Animator3D.playLayerAdditive preserves layer event names");
    rt_game3d_animator_update(layer_animator, 0.0);
    void *arm_mat = rt_anim_controller3d_get_bone_matrix(layer_controller, arm_bone);
    EXPECT_NEAR(rt_mat4_get(arm_mat, 1, 3),
                4.0,
                0.1,
                "Animator3D.playLayerAdditive applies bind-pose delta overlays");
    EXPECT_TRUE(rt_game3d_animator_crossfade_layer_additive(
                    layer_animator, 0, rt_const_cstr("reach"), 1.0) == 0,
                "Animator3D.crossfadeLayerAdditive rejects the base layer");
    EXPECT_TRUE(rt_game3d_animator_crossfade_layer_additive(
                    layer_animator, 1, rt_const_cstr("reach"), 1.0) != 0,
                "Animator3D.crossfadeLayerAdditive forwards to the wrapped controller");
    EXPECT_EQ_INT(rt_game3d_animator_event_count(layer_animator),
                  1,
                  "Animator3D.crossfadeLayerAdditive captures layer entry events");
    EXPECT_TRUE(std::strcmp(
                    rt_string_cstr(rt_game3d_animator_event_name(layer_animator, 0)),
                    "reachEnter") == 0,
                "Animator3D.crossfadeLayerAdditive preserves layer event names");
    rt_game3d_animator_update(layer_animator, 0.5);
    arm_mat = rt_anim_controller3d_get_bone_matrix(layer_controller, arm_bone);
    EXPECT_NEAR(rt_mat4_get(arm_mat, 1, 3),
                5.0,
                0.15,
                "Animator3D.crossfadeLayerAdditive blends additive overlay deltas");

    void *bt_idle = make_game3d_test_anim("btIdle", 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    void *bt_lean = make_game3d_test_anim("btLean", 0, 8.0, 0.0, 0.0, 8.0, 0.0, 0.0);
    void *blend_tree = rt_blend_tree3d_new_1d(layer_skel);
    rt_blend_tree3d_add_sample(blend_tree, bt_idle, 0.0, 0.0);
    rt_blend_tree3d_add_sample(blend_tree, bt_lean, 1.0, 0.0);
    rt_blend_tree3d_set_param(blend_tree, 0.25, 0.0);
    EXPECT_TRUE(rt_game3d_animator_set_blend_tree(layer_animator, blend_tree) != 0,
                "Animator3D.setBlendTree forwards to the wrapped controller");
    rt_game3d_animator_update(layer_animator, 0.0);
    void *root_mat = rt_anim_controller3d_get_bone_matrix(layer_controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                2.0,
                0.1,
                "Animator3D.setBlendTree drives controller base pose");
    EXPECT_TRUE(rt_game3d_animator_set_blend_tree(layer_animator, layer_skel) == 0,
                "Animator3D.setBlendTree rejects non-tree handles");

    void *ik_skel = rt_skeleton3d_new();
    int64_t hip = rt_skeleton3d_add_bone(ik_skel, rt_const_cstr("hip"), -1, rt_mat4_identity());
    int64_t knee = rt_skeleton3d_add_bone(ik_skel, rt_const_cstr("knee"), hip, rt_mat4_translate(1.0, 0.0, 0.0));
    int64_t foot = rt_skeleton3d_add_bone(ik_skel, rt_const_cstr("foot"), knee, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(ik_skel);
    void *ik_controller = rt_anim_controller3d_new(ik_skel);
    void *ik_animator = rt_game3d_animator_new(ik_controller);
    void *ik_solver = rt_ik_solver3d_two_bone(ik_skel, hip, knee, foot);
    rt_ik_solver3d_set_target(ik_solver, rt_vec3_new(1.0, 1.0, 0.0));
    EXPECT_TRUE(rt_game3d_animator_set_ik_solver(ik_animator, ik_solver) != 0,
                "Animator3D.setIKSolver forwards to the wrapped controller");
    void *foot_mat = rt_anim_controller3d_get_bone_matrix(ik_controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 3), 1.0, 0.03, "Animator3D.setIKSolver reaches target x");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 3), 1.0, 0.03, "Animator3D.setIKSolver reaches target y");
    EXPECT_TRUE(rt_game3d_animator_set_ik_solver(ik_animator, ik_skel) == 0,
                "Animator3D.setIKSolver rejects non-solver handles");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Animator Unit"), 80, 60);
    void *entity = rt_game3d_entity_new();
    void *controller2 = make_game3d_test_controller(10.0, 0.25);
    void *animator2 = rt_game3d_animator_new(controller2);
    rt_game3d_entity_attach_animator(entity, animator2);
    EXPECT_TRUE(rt_game3d_entity_get_anim(entity) == animator2,
                "Entity3D.attachAnimator stores the Game3D animator");
    EXPECT_TRUE(rt_scene_node3d_get_animator(rt_game3d_entity_get_node(entity)) == controller2,
                "Entity3D.attachAnimator binds the raw controller to the node");
    rt_scene_node3d_set_sync_mode(rt_game3d_entity_get_node(entity),
                                  rt_game3d_sync_mode_node_from_anim_root_motion());
    rt_game3d_animator_play(animator2, rt_const_cstr("walk"));
    rt_game3d_world_spawn(world, entity);
    rt_game3d_world_step_simulation(world, 0.25);

    void *pos = rt_game3d_entity_position(entity);
    EXPECT_NEAR(rt_vec3_x(pos), 2.5, 0.1, "World3D advances animators before scene sync");
    EXPECT_EQ_INT(rt_game3d_animator_event_count(animator2),
                  1,
                  "World3D animation update exposes frame events");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_animator_event_name(animator2, 0)), "step") ==
                    0,
                "World3D animation update preserves event names");

    void *entity2 = rt_game3d_entity_new();
    void *controller3 = make_game3d_test_controller(4.0, 0.25);
    rt_game3d_entity_attach_animator(entity2, controller3);
    EXPECT_TRUE(rt_game3d_entity_get_anim(entity2) != nullptr,
                "Entity3D.attachAnimator accepts raw AnimController3D");
    EXPECT_TRUE(rt_game3d_animator_get_controller(rt_game3d_entity_get_anim(entity2)) ==
                    controller3,
                "raw controller attach creates an Animator3D wrapper");

    rt_game3d_world_destroy(world);
    PASS();
}

static bool test_phase6_sound3d_and_effects3d_helpers() {
    TEST("Sound3D helpers and Effects3D presets integrate with World3D");

    void *world = rt_game3d_world_new(rt_const_cstr("Game3D Audio VFX Unit"), 96, 72);
    void *camera = rt_game3d_world_get_camera(world);
    void *audio = rt_game3d_world_get_audio(world);
    void *listener = rt_game3d_audio_get_listener(audio);
    void *effects = rt_game3d_world_get_effects(world);
    EXPECT_TRUE(audio != nullptr, "World3D creates Sound3D");
    EXPECT_TRUE(listener != nullptr, "Sound3D creates a listener");
    EXPECT_TRUE(effects != nullptr, "World3D creates EffectRegistry3D");
    EXPECT_TRUE(rt_game3d_audio_get_listener_follows_camera(audio) != 0,
                "Sound3D listener follows camera by default");

    rt_camera3d_look_at(
        camera, rt_vec3_new(3.0, 2.0, 6.0), rt_vec3_new(3.0, 2.0, 5.0), rt_vec3_new(0.0, 1.0, 0.0));
    rt_game3d_world_step_simulation(world, 0.1);
    void *listener_pos = rt_soundlistener3d_get_position(listener);
    EXPECT_NEAR(rt_vec3_x(listener_pos), 3.0, 0.001, "listener syncs bound camera position");

    rt_game3d_audio_listener_follow_camera(audio, 0);
    rt_camera3d_look_at(
        camera, rt_vec3_new(7.0, 2.0, 6.0), rt_vec3_new(7.0, 2.0, 5.0), rt_vec3_new(0.0, 1.0, 0.0));
    rt_game3d_world_step_simulation(world, 0.1);
    listener_pos = rt_soundlistener3d_get_position(listener);
    EXPECT_NEAR(
        rt_vec3_x(listener_pos), 3.0, 0.001, "listenerFollowCamera(false) freezes listener pose");

    void *manual_pos = rt_vec3_new(9.0, 1.0, 2.0);
    void *manual_forward = rt_vec3_new(0.0, 0.0, -1.0);
    void *manual_up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_game3d_audio_set_listener_pose(audio, manual_pos, manual_forward, manual_up);
    listener_pos = rt_soundlistener3d_get_position(listener);
    EXPECT_NEAR(rt_vec3_x(listener_pos), 9.0, 0.001, "setListenerPose writes listener position");
    EXPECT_TRUE(rt_game3d_audio_get_listener_follows_camera(audio) == 0,
                "setListenerPose disables camera follow");
    rt_game3d_audio_set_attenuation(audio, 2.0, 12.0);
    rt_game3d_audio_set_volume(audio, 70);
    EXPECT_NEAR(
        rt_game3d_audio_get_ref_distance(audio), 2.0, 0.0001, "Sound3D stores ref distance");
    EXPECT_NEAR(
        rt_game3d_audio_get_max_distance(audio), 12.0, 0.0001, "Sound3D stores max distance");
    EXPECT_EQ_INT(rt_game3d_audio_get_volume(audio), 70, "Sound3D clamps/stores volume");

    rt_sound3d_listener_state state;
    double pos[3] = {0.0, 0.0, 0.0};
    double fwd[3] = {0.0, 0.0, -1.0};
    double src[3] = {5.0, 0.0, 0.0};
    int64_t spatial_volume = 0;
    int64_t spatial_pan = 0;
    rt_sound3d_listener_state_set(&state, pos, fwd, nullptr);
    rt_sound3d_compute_voice_params(&state, src, 10.0, 100, &spatial_volume, &spatial_pan);
    EXPECT_EQ_INT(spatial_volume, 50, "Sound3D attenuation halves volume at half max distance");
    EXPECT_TRUE(spatial_pan > 90, "Sound3D pans right-side sources to the right");

    if (rt_audio_is_available() && rt_audio_init()) {
        void *clip = rt_synth_tone(440, 80, RT_WAVE_SINE);
        if (clip) {
            void *entity = rt_game3d_entity_new();
            rt_game3d_entity_set_position(entity, 1.0, 0.0, 0.0);
            rt_game3d_world_spawn(world, entity);
            void *attached = rt_game3d_audio_play_attached(audio, clip, entity);
            EXPECT_TRUE(attached != nullptr, "playAttached returns an SoundSource3D");
            EXPECT_NEAR(rt_soundsource3d_get_max_distance(attached),
                        12.0,
                        0.001,
                        "playAttached applies Sound3D attenuation");
            rt_game3d_entity_set_position(entity, 4.0, 0.0, 0.0);
            rt_game3d_world_step_simulation(world, 0.1);
            void *source_pos = rt_soundsource3d_get_position(attached);
            EXPECT_NEAR(rt_vec3_x(source_pos), 4.0, 0.001, "attached audio follows entity node");

            void *one_shot = rt_game3d_audio_play_at(audio, clip, rt_vec3_new(2.0, 0.0, 0.0));
            EXPECT_TRUE(one_shot != nullptr, "playAt returns an SoundSource3D");
            (void)rt_game3d_audio_play2d(audio, clip);
            EXPECT_TRUE(rt_game3d_audio_get_source_count(audio) >= 2,
                        "Sound3D tracks sources it creates");
            rt_game3d_audio_clear_sources(audio);
            EXPECT_EQ_INT(
                rt_game3d_audio_get_source_count(audio), 0, "clearSources drops source refs");
        }
    }

    void *origin = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    void *side = rt_vec3_new(1.0, 0.1, 0.0);
    void *explosion = rt_game3d_effects3d_explosion(world, origin);
    void *sparks = rt_game3d_effects3d_sparks(world, origin, side);
    void *dust = rt_game3d_effects3d_dust(world, origin);
    void *smoke = rt_game3d_effects3d_smoke(world, origin);
    void *decal = rt_game3d_effects3d_impact_decal(world, origin, up);
    EXPECT_TRUE(explosion != nullptr && sparks != nullptr && dust != nullptr && smoke != nullptr,
                "Effects3D particle presets return emitters");
    EXPECT_TRUE(decal != nullptr, "Effects3D.ImpactDecal returns a decal");
    EXPECT_EQ_INT(
        rt_game3d_effects_get_count(effects), 5, "Effects3D presets register five effects");
    EXPECT_EQ_INT(
        rt_game3d_effects_get_particles_count(effects), 4, "Effects3D registers particles");
    EXPECT_EQ_INT(rt_game3d_effects_get_decal_count(effects), 1, "Effects3D registers decals");
    EXPECT_TRUE(rt_particles3d_get_count(explosion) > 0, "Explosion spawns an immediate burst");

    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    EXPECT_TRUE(rt_game3d_world_capture_final_frame(world) != nullptr,
                "drawEffects renders without failing");

    rt_game3d_effects_update(effects, 3.0);
    EXPECT_EQ_INT(rt_game3d_effects_get_count(effects), 0, "Effects3D presets auto-expire");

    void *manual_particles = rt_particles3d_new(8);
    rt_particles3d_burst(manual_particles, 4);
    rt_game3d_effects_add_particles(effects, manual_particles, 0.1);
    void *manual_decal = rt_decal3d_new(origin, up, 0.25, nullptr);
    rt_decal3d_set_lifetime(manual_decal, 0.1);
    rt_game3d_effects_add_decal(effects, manual_decal);
    EXPECT_EQ_INT(
        rt_game3d_effects_get_count(effects), 2, "EffectRegistry3D accepts manual effects");
    rt_game3d_effects_update(effects, 0.2);
    EXPECT_EQ_INT(
        rt_game3d_effects_get_count(effects), 0, "EffectRegistry3D expires manual effects");

    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    void *ground = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(ground, rt_game3d_body_def_static_box(1.0, 1.0, 1.0));
    void *ball = rt_game3d_entity_new();
    rt_game3d_entity_attach_body(ball, rt_game3d_body_def_sphere(0.5, 1.0));
    rt_game3d_world_spawn(world, ground);
    rt_game3d_world_spawn(world, ball);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    void *event = rt_game3d_world_collision_event(world, rt_game3d_collision_enter(), 0);
    EXPECT_TRUE(event != nullptr, "collision event exists for audio/VFX trigger point");
    if (event) {
        void *point = rt_game3d_collision_event_point(event);
        EXPECT_TRUE(rt_game3d_effects3d_dust(world, point) != nullptr,
                    "collision point can spawn a positional effect");
    }

    rt_game3d_world_destroy(world);
    PASS();
}

int main() {
    set_software_backend_env();
    bool ok = true;
    ok = test_layermasks_and_constants() && ok;
    ok = test_world_worker_controls() && ok;
    ok = test_input_axes() && ok;
    ok = test_input_update_snapshots_frame_state() && ok;
    ok = test_world_entity_registry_and_collision_clear() && ok;
    ok = test_world_navmesh_bake_hooks() && ok;
    ok = test_entity_from_node_wraps_imported_subtree() && ok;
    ok = test_entity_child_graph_reparents_and_rejects_cycles() && ok;
    ok = test_frame_loop_manual_frame_and_final_capture() && ok;
    ok = test_run_fixed_accumulator_and_spiral_guard() && ok;
    ok = test_worker_count_runframes_replay_parity() && ok;
    ok = test_worker_count_parallel_animation_parity() && ok;
    ok = test_world_floating_origin_controls_and_rebase() && ok;
    ok = test_step_simulation_clamps_invalid_dt() && ok;
    ok = test_free_fly_controller_synthetic_input() && ok;
    ok = test_run_frames_only_preserves_synthetic_holds() && ok;
    ok = test_character_controller_syncs_world_position_under_parent() && ok;
    ok = test_orbit_and_follow_controllers() && ok;
    ok = test_first_person_character_controller_same_frame_motion() && ok;
    ok = test_phase3_material_presets_and_prefabs() && ok;
    ok = test_phase3_world_presets_environment_and_debug() && ok;
    ok = test_phase4_assets3d_model_templates() && ok;
    ok = test_assets3d_loads_packaged_gltf_hierarchy() && ok;
    ok = test_phase5_world_stream3d_baseline() && ok;
    ok = test_phase5_world_stream3d_terrain_manifest() && ok;
    ok = test_phase5_world_stream3d_manifest_cells() && ok;
    ok = test_phase5_world_stream3d_hitch_budgeted_update() && ok;
    ok = test_phase12_world_stream3d_inspection_hooks() && ok;
    ok = test_phase4_body_def_attach_body() && ok;
    ok = test_shared_body_attachment_rejected_without_state_leak() && ok;
    ok = test_entity_transform_sanitizes_and_respects_sync_mode() && ok;
    ok = test_phase4_collision_events_wrapped_with_entities() && ok;
    ok = test_phase5_animator3d_events_and_root_motion() && ok;
    ok = test_phase6_sound3d_and_effects3d_helpers() && ok;

    std::printf("\nGame3D runtime tests: %d/%d passed\n", g_tests_passed, g_tests_total);
    return ok && g_tests_passed == g_tests_total ? 0 : 1;
}
