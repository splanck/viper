//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_canvas3d_production.cpp
// Purpose: Headless production-readiness checks for Canvas3D runtime behavior.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_string.h"

#include <cmath>
#include <cstdio>

extern "C" {
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

#define EXPECT_EQ_I64(a, b, msg)                                                                   \
    do {                                                                                           \
        tests_run++;                                                                               \
        if ((int64_t)(a) != (int64_t)(b))                                                          \
            std::fprintf(stderr,                                                                   \
                         "FAIL: %s (expected %lld, got %lld)\n",                                   \
                         msg,                                                                      \
                         (long long)(b),                                                           \
                         (long long)(a));                                                          \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (std::fabs((double)(a) - (double)(b)) > (eps))                                          \
            std::fprintf(                                                                          \
                stderr, "FAIL: %s (expected %f, got %f)\n", msg, (double)(b), (double)(a));        \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static float g_clear_r = -1.0f;
static float g_clear_g = -1.0f;
static float g_clear_b = -1.0f;

static void fake_clear(void *, vgfx_window_t, float r, float g, float b) {
    g_clear_r = r;
    g_clear_g = g;
    g_clear_b = b;
}

static void fake_set_render_target(void *, vgfx3d_rendertarget_t *) {}

static void fake_shadow_begin(void *, int32_t, float *, int32_t, int32_t, const float *) {}

static void fake_shadow_draw(void *, const vgfx3d_draw_cmd_t *) {}

static void fake_shadow_end(void *, int32_t, float) {}

static void fake_draw_skybox(void *, const void *) {}

static void fake_submit_draw_instanced(void *,
                                       vgfx_window_t,
                                       const vgfx3d_draw_cmd_t *,
                                       const float *,
                                       int32_t,
                                       const vgfx3d_light_params_t *,
                                       int32_t,
                                       const float *,
                                       int8_t,
                                       int8_t) {}

static int fake_readback_rgba(void *, uint8_t *, int32_t, int32_t, int32_t) {
    return 0;
}

static void fake_present_postfx(void *, const vgfx3d_postfx_chain_t *) {}

static vgfx3d_backend_t make_fake_gpu_backend() {
    vgfx3d_backend_t backend = {};
    backend.name = "testgpu";
    backend.clear = fake_clear;
    backend.set_render_target = fake_set_render_target;
    backend.shadow_begin = fake_shadow_begin;
    backend.shadow_draw = fake_shadow_draw;
    backend.shadow_end = fake_shadow_end;
    backend.draw_skybox = fake_draw_skybox;
    backend.submit_draw_instanced = fake_submit_draw_instanced;
    backend.readback_rgba = fake_readback_rgba;
    backend.present_postfx = fake_present_postfx;
    return backend;
}

static int backend_supports(rt_canvas3d *canvas, const char *name) {
    rt_string s = rt_const_cstr(name);
    int supported = rt_canvas3d_backend_supports(canvas, s);
    rt_string_unref(s);
    return supported;
}

static void test_null_canvas_has_no_capabilities() {
    EXPECT_EQ_I64(rt_canvas3d_get_backend_capabilities(nullptr),
                  0,
                  "null canvas reports no backend capabilities");
    EXPECT_TRUE(!backend_supports(nullptr, "shadows"),
                "null canvas does not support named capabilities");
}

static void test_software_backend_reports_canvas_fallback_features() {
    rt_canvas3d canvas = {};
    canvas.backend = &vgfx3d_software_backend;

    int64_t caps = rt_canvas3d_get_backend_capabilities(&canvas);
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE) != 0,
                "software backend advertises software mode");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU) == 0,
                "software backend does not advertise GPU mode");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_RENDER_TARGET) != 0,
                "software backend supports render targets");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK) != 0,
                "software backend supports window readback");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SHADOWS) != 0,
                "software backend supports shadow maps");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SKYBOX) != 0,
                "software backend supports Canvas3D skybox fallback");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_POSTFX) != 0,
                "software backend supports CPU post effects");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX) == 0,
                "software backend does not advertise GPU post effects");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY) != 0,
                "software backend advertises final overlays after CPU post effects");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT) != 0,
                "software backend advertises final-frame screenshots");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY) == 0,
                "software backend does not advertise GPU postfx overlay composition");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_HARDWARE_INSTANCING) == 0,
                "software backend does not advertise hardware instancing");

    EXPECT_TRUE(backend_supports(&canvas, "postfx-overlay"),
                "BackendSupports accepts postfx-overlay alias");
    EXPECT_TRUE(backend_supports(&canvas, "final_screenshot"),
                "BackendSupports accepts final_screenshot alias");
}

static void test_gpu_backend_capability_bits_and_names() {
    vgfx3d_backend_t fake_gpu_backend = make_fake_gpu_backend();
    rt_canvas3d canvas = {};
    canvas.backend = &fake_gpu_backend;

    int64_t caps = rt_canvas3d_get_backend_capabilities(&canvas);
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU) != 0, "GPU backend advertises GPU mode");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE) == 0,
                "GPU backend does not advertise software mode");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_RENDER_TARGET) != 0,
                "GPU backend advertises render targets");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK) != 0,
                "GPU backend advertises window readback when hook exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SHADOWS) != 0,
                "GPU backend advertises shadows only when all hooks exist");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_SKYBOX) != 0,
                "GPU backend advertises skybox when draw hook exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_HARDWARE_INSTANCING) != 0,
                "GPU backend advertises hardware instancing when hook exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_POSTFX) != 0,
                "GPU backend advertises post effects when present_postfx exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX) != 0,
                "GPU backend advertises GPU post effects when present_postfx exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY) == 0,
                "GPU backend does not advertise final overlay after GPU postfx by default");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT) != 0,
                "GPU backend advertises final screenshots when readback hook exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY) == 0,
                "GPU backend does not advertise GPU postfx overlay composition until implemented");

    EXPECT_TRUE(backend_supports(&canvas, "hardware_instancing"),
                "BackendSupports accepts hardware_instancing");
    EXPECT_TRUE(backend_supports(&canvas, "instancing"),
                "BackendSupports accepts instancing alias");
    EXPECT_TRUE(backend_supports(&canvas, "readback"), "BackendSupports accepts readback alias");
    EXPECT_TRUE(backend_supports(&canvas, "gpu_post_fx"),
                "BackendSupports accepts gpu_post_fx alias");
    EXPECT_TRUE(backend_supports(&canvas, "final-screenshot"),
                "BackendSupports accepts final-screenshot alias");
    EXPECT_TRUE(!backend_supports(&canvas, "gpu-postfx-overlay"),
                "BackendSupports reports GPU postfx overlay unsupported until implemented");
    EXPECT_TRUE(!backend_supports(&canvas, "missing_feature"),
                "BackendSupports rejects unknown capability names");
}

static void test_frustum_culling_aliases_share_state() {
    rt_canvas3d canvas = {};

    rt_canvas3d_set_frustum_culling(&canvas, 1);
    EXPECT_EQ_I64(canvas.occlusion_culling, 1, "SetFrustumCulling enables culling state");

    rt_canvas3d_set_occlusion_culling(&canvas, 0);
    EXPECT_EQ_I64(canvas.occlusion_culling, 0, "SetOcclusionCulling alias disables same state");

    rt_canvas3d_set_occlusion_culling(&canvas, -1);
    EXPECT_EQ_I64(canvas.occlusion_culling, 1, "culling setters normalize nonzero values");

    rt_canvas3d_set_frustum_culling(nullptr, 1);
    rt_canvas3d_set_occlusion_culling(nullptr, 1);
    EXPECT_TRUE(true, "culling setters tolerate null canvas handles");
}

static void test_canvas_render_state_sanitizes_inputs() {
    vgfx3d_backend_t fake_gpu_backend = make_fake_gpu_backend();
    vgfx3d_rendertarget_t dummy_target = {};
    rt_canvas3d canvas = {};
    canvas.backend = &fake_gpu_backend;
    canvas.gfx_win = (vgfx_window_t)1;
    canvas.render_target = &dummy_target;

    rt_canvas3d_clear(&canvas, NAN, -1.0, 2.0);
    EXPECT_NEAR(g_clear_r, 0.0, 0.001, "Clear clamps NaN red to zero");
    EXPECT_NEAR(g_clear_g, 0.0, 0.001, "Clear clamps negative green to zero");
    EXPECT_NEAR(g_clear_b, 1.0, 0.001, "Clear clamps blue to one");

    rt_canvas3d_set_ambient(&canvas, NAN, -2.0, 3.0);
    EXPECT_NEAR(canvas.ambient[0], 0.0, 0.001, "Ambient clamps NaN to zero");
    EXPECT_NEAR(canvas.ambient[1], 0.0, 0.001, "Ambient clamps negative values");
    EXPECT_NEAR(canvas.ambient[2], 1.0, 0.001, "Ambient clamps high values");

    canvas.delta_time_ms = 1000;
    canvas.dt_max_ms = 16;
    EXPECT_EQ_I64(rt_canvas3d_get_delta_time(&canvas), 16, "Delta time getter applies max clamp");
}

int main() {
    test_null_canvas_has_no_capabilities();
    test_software_backend_reports_canvas_fallback_features();
    test_gpu_backend_capability_bits_and_names();
    test_frustum_culling_aliases_share_state();
    test_canvas_render_state_sanitizes_inputs();

    if (tests_passed != tests_run) {
        std::fprintf(
            stderr, "test_rt_canvas3d_production: %d/%d checks passed\n", tests_passed, tests_run);
        return 1;
    }

    std::printf("test_rt_canvas3d_production: %d/%d checks passed\n", tests_passed, tests_run);
    return 0;
}
