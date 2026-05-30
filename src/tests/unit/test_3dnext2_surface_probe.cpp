//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_3dnext2_surface_probe.cpp
// Purpose: Phase-0 surface probe for the 3D next-level scale plan.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt.hpp"
#include "rt_game3d.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threadpool.h"
#include "rt_vec3.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" void vm_trap(const char *msg) {
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

namespace {
static int g_tests_passed = 0;
static int g_tests_total = 0;

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
            std::printf("FAIL: %s (got %.9f, expected %.9f)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static void set_software_backend_env() {
#if RT_PLATFORM_WINDOWS
    _putenv_s("VIPER_3D_BACKEND", "software");
#else
    setenv("VIPER_3D_BACKEND", "software", 1);
#endif
}

static intptr_t surface_expected_value(intptr_t input) {
    intptr_t mixed = (input * 1103515245 + 12345) & 0x7fffffff;
    return (mixed % 1000003) + input * 17;
}

extern "C" void *surface_map_ordered(void *item) {
    intptr_t input = (intptr_t)item;
    return (void *)surface_expected_value(input);
}

static void release_object_if_unowned(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static bool test_parallel_map_ordered_surface() {
    TEST("Phase0 worker-backed ordered map surface");
    void *pool = rt_threadpool_new(4);
    EXPECT_TRUE(pool != nullptr, "custom ThreadPool should be created");

    void *input = rt_seq_new();
    EXPECT_TRUE(input != nullptr, "input sequence should be created");
    for (intptr_t i = 1; i <= 257; ++i)
        rt_seq_push(input, (void *)i);

    void *first = rt_parallel_map_pool(input, (void *)&surface_map_ordered, pool);
    void *second = rt_parallel_map_pool(input, (void *)&surface_map_ordered, pool);
    EXPECT_TRUE(first != nullptr, "first mapped sequence should be created");
    EXPECT_TRUE(second != nullptr, "second mapped sequence should be created");
    EXPECT_EQ_INT(rt_seq_len(first), 257, "first mapped sequence length");
    EXPECT_EQ_INT(rt_seq_len(second), 257, "second mapped sequence length");

    for (intptr_t i = 0; i < 257; ++i) {
        intptr_t input_value = i + 1;
        intptr_t expected = surface_expected_value(input_value);
        EXPECT_EQ_INT((intptr_t)rt_seq_get(first, i), expected, "first ordered map value");
        EXPECT_EQ_INT((intptr_t)rt_seq_get(second, i), expected, "second ordered map value");
        EXPECT_EQ_INT((intptr_t)rt_seq_get(first, i),
                      (intptr_t)rt_seq_get(second, i),
                      "ordered map replay value");
    }

    rt_threadpool_shutdown(pool);
    release_object_if_unowned(first);
    release_object_if_unowned(second);
    release_object_if_unowned(input);
    release_object_if_unowned(pool);
    PASS();
}

struct WorldReplaySummary {
    int64_t frame;
    double elapsed;
    double dt;
    double entity_x;
    double entity_y;
    double entity_z;
    int64_t pixels_w;
    int64_t pixels_h;
};

static WorldReplaySummary run_world_replay(int64_t worker_count) {
    set_software_backend_env();
    void *world = rt_game3d_world_new(rt_const_cstr("3DNext2 Surface Probe"), 64, 48);
    rt_game3d_world_set_worker_count(world, worker_count);
    rt_game3d_world_set_gravity(world, 0.0, -9.81, 0.0);

    void *entity = rt_game3d_entity_new();
    rt_game3d_entity_set_position(entity, 0.0, 4.0, 0.0);
    rt_game3d_entity_attach_body(entity, rt_game3d_body_def_sphere(0.5, 1.0));
    rt_game3d_world_spawn(world, entity);
    rt_game3d_world_run_frames_only(world, 8, 1.0 / 60.0);

    void *pos = rt_game3d_entity_world_position(entity);
    void *pixels = rt_game3d_world_capture_final_frame(world);
    WorldReplaySummary summary = {rt_game3d_world_get_frame(world),
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

static bool test_world_runframes_worker_replay_surface() {
    TEST("Phase0 World3D worker-count replay surface");
    WorldReplaySummary single = run_world_replay(1);
    WorldReplaySummary multi = run_world_replay(4);

    EXPECT_EQ_INT(single.frame, 8, "single-worker frame count");
    EXPECT_EQ_INT(multi.frame, 8, "multi-worker frame count");
    EXPECT_EQ_INT(single.frame, multi.frame, "worker-count frame parity");
    EXPECT_NEAR(single.elapsed, multi.elapsed, 0.000001, "worker-count elapsed parity");
    EXPECT_NEAR(single.dt, multi.dt, 0.000001, "worker-count dt parity");
    EXPECT_NEAR(single.entity_x, multi.entity_x, 0.000001, "worker-count entity x parity");
    EXPECT_NEAR(single.entity_y, multi.entity_y, 0.000001, "worker-count entity y parity");
    EXPECT_NEAR(single.entity_z, multi.entity_z, 0.000001, "worker-count entity z parity");
    EXPECT_EQ_INT(single.pixels_w, 64, "final capture width");
    EXPECT_EQ_INT(single.pixels_h, 48, "final capture height");
    EXPECT_EQ_INT(single.pixels_w, multi.pixels_w, "worker-count final width parity");
    EXPECT_EQ_INT(single.pixels_h, multi.pixels_h, "worker-count final height parity");
    PASS();
}
} // namespace

int main() {
    bool ok = true;
    ok = test_parallel_map_ordered_surface() && ok;
    ok = test_world_runframes_worker_replay_surface() && ok;

    if (!ok) {
        std::printf("3DNext2 surface probe: %d/%d passed\n", g_tests_passed, g_tests_total);
        return 1;
    }
    std::printf("3DNext2 surface probe: all %d tests passed\n", g_tests_total);
    return 0;
}
