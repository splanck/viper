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
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_vec3.h"
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

#define EXPECT_EQ_U64(a, b, msg)                                                                   \
    do {                                                                                           \
        tests_run++;                                                                               \
        if ((uint64_t)(a) != (uint64_t)(b))                                                        \
            std::fprintf(stderr,                                                                   \
                         "FAIL: %s (expected 0x%016llx, got 0x%016llx)\n",                         \
                         msg,                                                                      \
                         (unsigned long long)(b),                                                  \
                         (unsigned long long)(a));                                                 \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void set_identity_matrix(double *m) {
    std::memset(m, 0, sizeof(double) * 16u);
    m[0] = 1.0;
    m[5] = 1.0;
    m[10] = 1.0;
    m[15] = 1.0;
}

static void set_translation_matrix(double *m, double x, double y, double z) {
    set_identity_matrix(m);
    m[3] = x;
    m[7] = y;
    m[11] = z;
}

static int project_world_to_pixel(const rt_canvas3d *canvas,
                                  float wx,
                                  float wy,
                                  float wz,
                                  int32_t width,
                                  int32_t height,
                                  int32_t *out_x,
                                  int32_t *out_y) {
    const float *m = canvas ? canvas->cached_vp : nullptr;
    float cx;
    float cy;
    float cw;
    float ndc_x;
    float ndc_y;

    if (!m || !out_x || !out_y || width <= 0 || height <= 0)
        return 0;
    cx = wx * m[0] + wy * m[1] + wz * m[2] + m[3];
    cy = wx * m[4] + wy * m[5] + wz * m[6] + m[7];
    cw = wx * m[12] + wy * m[13] + wz * m[14] + m[15];
    if (!std::isfinite(cw) || cw <= 1e-6f)
        return 0;
    ndc_x = cx / cw;
    ndc_y = cy / cw;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
        return 0;
    *out_x = (int32_t)((ndc_x + 1.0f) * 0.5f * (float)width);
    *out_y = (int32_t)((1.0f - ndc_y) * 0.5f * (float)height);
    return *out_x >= 0 && *out_x < width && *out_y >= 0 && *out_y < height;
}

static float sample_luminance_box(const vgfx3d_rendertarget_t *target,
                                  int32_t cx,
                                  int32_t cy,
                                  int32_t radius) {
    float sum = 0.0f;
    int32_t count = 0;

    if (!target || !target->color_buf || target->width <= 0 || target->height <= 0 ||
        target->stride < target->width * 4)
        return 0.0f;
    for (int32_t y = cy - radius; y <= cy + radius; y++) {
        if (y < 0 || y >= target->height)
            continue;
        for (int32_t x = cx - radius; x <= cx + radius; x++) {
            const uint8_t *px;
            float r;
            float g;
            float b;

            if (x < 0 || x >= target->width)
                continue;
            px = &target->color_buf[y * target->stride + x * 4];
            r = (float)px[0] / 255.0f;
            g = (float)px[1] / 255.0f;
            b = (float)px[2] / 255.0f;
            sum += r * 0.2126f + g * 0.7152f + b * 0.0722f;
            count++;
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

static uint64_t hash_render_target_rgba(const vgfx3d_rendertarget_t *target) {
    uint64_t hash = 1469598103934665603ull;

    if (!target || !target->color_buf || target->width <= 0 || target->height <= 0 ||
        target->stride < target->width * 4)
        return 0;
    for (int32_t y = 0; y < target->height; y++) {
        const uint8_t *row = &target->color_buf[y * target->stride];
        for (int32_t x = 0; x < target->width * 4; x++) {
            hash ^= row[x];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

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

static void fake_present(void *) {}

static void fake_present_postfx(void *, const vgfx3d_postfx_chain_t *) {}

static void fake_apply_postfx(void *, const vgfx3d_postfx_chain_t *) {}

static int64_t fake_native_texture_caps(void *) {
    return RT_CANVAS3D_BACKEND_CAP_BC7 | RT_CANVAS3D_BACKEND_CAP_ASTC |
           RT_CANVAS3D_BACKEND_CAP_ETC2;
}

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
    backend.present = fake_present;
    backend.present_postfx = fake_present_postfx;
    backend.apply_postfx = fake_apply_postfx;
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
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_HLOD) != 0,
                "software backend advertises runtime HLOD/impostor proxies");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_OCCLUSION) != 0,
                "software backend advertises CPU occlusion culling");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_BC7) == 0,
                "software backend does not advertise BC7 compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ASTC) == 0,
                "software backend does not advertise ASTC compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ETC2) == 0,
                "software backend does not advertise ETC2 compressed upload");

    EXPECT_TRUE(backend_supports(&canvas, "postfx-overlay"),
                "BackendSupports accepts postfx-overlay alias");
    EXPECT_TRUE(backend_supports(&canvas, "final_screenshot"),
                "BackendSupports accepts final_screenshot alias");
    EXPECT_TRUE(backend_supports(&canvas, "hlod"), "BackendSupports accepts hlod capability alias");
    EXPECT_TRUE(backend_supports(&canvas, "occlusion"),
                "BackendSupports accepts occlusion capability alias");
    EXPECT_TRUE(!backend_supports(&canvas, "bc7"),
                "BackendSupports reports bc7 unsupported until backend upload exists");
    EXPECT_TRUE(!backend_supports(&canvas, "astc"),
                "BackendSupports reports astc unsupported until backend upload exists");
    EXPECT_TRUE(!backend_supports(&canvas, "etc2"),
                "BackendSupports reports etc2 unsupported until backend upload exists");
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
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY) != 0,
                "GPU backend advertises final overlay after split GPU postfx");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT) != 0,
                "GPU backend advertises final screenshots when readback hook exists");
    EXPECT_TRUE(
        (caps & RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY) != 0,
        "GPU backend advertises GPU postfx overlay composition when split apply/present exists");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_HLOD) != 0,
                "GPU backend advertises runtime HLOD/impostor proxies");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_BC7) == 0,
                "generic GPU backend does not imply BC7 compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ASTC) == 0,
                "generic GPU backend does not imply ASTC compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ETC2) == 0,
                "generic GPU backend does not imply ETC2 compressed upload");

    EXPECT_TRUE(backend_supports(&canvas, "hardware_instancing"),
                "BackendSupports accepts hardware_instancing");
    EXPECT_TRUE(backend_supports(&canvas, "instancing"),
                "BackendSupports accepts instancing alias");
    EXPECT_TRUE(backend_supports(&canvas, "readback"), "BackendSupports accepts readback alias");
    EXPECT_TRUE(backend_supports(&canvas, "gpu_post_fx"),
                "BackendSupports accepts gpu_post_fx alias");
    EXPECT_TRUE(backend_supports(&canvas, "final-screenshot"),
                "BackendSupports accepts final-screenshot alias");
    EXPECT_TRUE(
        backend_supports(&canvas, "gpu-postfx-overlay"),
        "BackendSupports reports GPU postfx overlay support when split apply/present exists");
    EXPECT_TRUE(backend_supports(&canvas, "impostor"),
                "BackendSupports accepts impostor alias for HLOD proxies");
    EXPECT_TRUE(!backend_supports(&canvas, "bc7"),
                "BackendSupports accepts bc7 as a false capability name");
    EXPECT_TRUE(!backend_supports(&canvas, "astc"),
                "BackendSupports accepts astc as a false capability name");
    EXPECT_TRUE(!backend_supports(&canvas, "etc2"),
                "BackendSupports accepts etc2 as a false capability name");
    EXPECT_TRUE(!backend_supports(&canvas, "missing_feature"),
                "BackendSupports rejects unknown capability names");
}

static void test_gpu_backend_native_texture_capability_hook() {
    vgfx3d_backend_t fake_gpu_backend = make_fake_gpu_backend();
    rt_canvas3d canvas = {};
    fake_gpu_backend.get_native_texture_caps = fake_native_texture_caps;
    canvas.backend = &fake_gpu_backend;

    int64_t caps = rt_canvas3d_get_backend_capabilities(&canvas);
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_BC7) != 0,
                "native texture hook advertises BC7 compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ASTC) != 0,
                "native texture hook advertises ASTC compressed upload");
    EXPECT_TRUE((caps & RT_CANVAS3D_BACKEND_CAP_ETC2) != 0,
                "native texture hook advertises ETC2 compressed upload");
    EXPECT_TRUE(backend_supports(&canvas, "bc7"), "BackendSupports reports hooked BC7 support");
    EXPECT_TRUE(backend_supports(&canvas, "astc"), "BackendSupports reports hooked ASTC support");
    EXPECT_TRUE(backend_supports(&canvas, "etc2"), "BackendSupports reports hooked ETC2 support");
}

static void test_frustum_culling_aliases_share_state() {
    rt_canvas3d canvas = {};

    rt_canvas3d_set_frustum_culling(&canvas, 1);
    EXPECT_EQ_I64(canvas.frustum_culling, 1, "SetFrustumCulling enables frustum culling state");
    EXPECT_EQ_I64(canvas.occlusion_culling, 0, "SetFrustumCulling does not enable occlusion");

    rt_canvas3d_set_occlusion_culling(&canvas, 0);
    EXPECT_EQ_I64(canvas.frustum_culling, 0, "SetOcclusionCulling(false) disables frustum culling");
    EXPECT_EQ_I64(canvas.occlusion_culling, 0, "SetOcclusionCulling(false) disables occlusion");

    rt_canvas3d_set_occlusion_culling(&canvas, -1);
    EXPECT_EQ_I64(canvas.frustum_culling, 1, "SetOcclusionCulling enables frustum rejection");
    EXPECT_EQ_I64(canvas.occlusion_culling, 1, "SetOcclusionCulling enables occlusion");

    rt_canvas3d_set_frustum_culling(&canvas, 0);
    EXPECT_EQ_I64(canvas.frustum_culling, 0, "SetFrustumCulling(false) disables frustum state");
    EXPECT_EQ_I64(
        canvas.occlusion_culling, 0, "SetFrustumCulling(false) disables dependent occlusion");

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

static void test_software_spot_light_shadow_render_is_stable() {
    const int32_t width = 96;
    const int32_t height = 96;
    const uint64_t expected_hash = 0x728c7560b310b408ull;
    rt_canvas3d canvas = {};
    void *target_obj = nullptr;
    void *camera = nullptr;
    void *eye = nullptr;
    void *look = nullptr;
    void *up = nullptr;
    void *light_pos = nullptr;
    void *light_dir = nullptr;
    void *spot = nullptr;
    void *plane = nullptr;
    void *sphere = nullptr;
    void *plane_mat = nullptr;
    void *sphere_mat = nullptr;
    double plane_model[16];
    double sphere_model[16];
    int32_t shadow_x = 0;
    int32_t shadow_y = 0;
    int32_t lit_left_x = 0;
    int32_t lit_left_y = 0;
    int32_t lit_right_x = 0;
    int32_t lit_right_y = 0;
    float shadow_luma;
    float lit_left_luma;
    float lit_right_luma;
    uint64_t hash;
    rt_rendertarget3d *target_owner;
    vgfx3d_rendertarget_t *target;

    canvas.backend = &vgfx3d_software_backend;
    canvas.backend_ctx = vgfx3d_software_backend.create_ctx(nullptr, width, height);
    canvas.gfx_win = (vgfx_window_t)1;
    canvas.width = width;
    canvas.height = height;
    canvas.framebuffer_width = width;
    canvas.framebuffer_height = height;
    canvas.shadow_cascade_count = 1;
    EXPECT_TRUE(canvas.backend_ctx != nullptr, "Software backend context is available");
    if (!canvas.backend_ctx)
        return;

    target_obj = rt_rendertarget3d_new(width, height);
    camera = rt_camera3d_new(55.0, 1.0, 0.1, 20.0);
    eye = rt_vec3_new(0.0, 2.4, 5.0);
    look = rt_vec3_new(0.0, 0.25, 0.0);
    up = rt_vec3_new(0.0, 1.0, 0.0);
    light_pos = rt_vec3_new(-3.0, 5.0, 4.0);
    light_dir = rt_vec3_new(3.0, -5.0, -4.0);
    spot = rt_light3d_new_spot(light_pos, light_dir, 1.0, 0.96, 0.88, 1.0 / 64.0, 18.0, 34.0);
    plane = rt_mesh3d_new_plane(6.0, 6.0);
    sphere = rt_mesh3d_new_sphere(0.55, 24);
    plane_mat = rt_material3d_new_color(0.76, 0.76, 0.72);
    sphere_mat = rt_material3d_new_color(0.82, 0.82, 0.86);

    EXPECT_TRUE(target_obj && camera && eye && look && up && light_pos && light_dir && spot &&
                    plane && sphere && plane_mat && sphere_mat,
                "Spot-shadow render test allocates scene resources");
    if (!target_obj || !camera || !eye || !look || !up || !light_pos || !light_dir || !spot ||
        !plane || !sphere || !plane_mat || !sphere_mat)
        goto cleanup;

    rt_camera3d_look_at(camera, eye, look, up);
    rt_light3d_set_intensity(spot, 5.5);
    rt_canvas3d_set_render_target(&canvas, target_obj);
    rt_canvas3d_enable_shadows(&canvas, 128);
    rt_canvas3d_set_shadow_bias(&canvas, 0.0012);
    rt_canvas3d_set_ambient(&canvas, 0.035, 0.035, 0.04);
    rt_canvas3d_set_light(&canvas, 0, spot);

    set_identity_matrix(plane_model);
    set_translation_matrix(sphere_model, 0.0, 0.65, 0.0);

    rt_canvas3d_clear(&canvas, 0.018, 0.020, 0.024);
    rt_canvas3d_begin(&canvas, camera);
    EXPECT_TRUE(
        project_world_to_pixel(&canvas, 0.45f, 0.0f, -0.55f, width, height, &shadow_x, &shadow_y),
        "Predicted spot-shadow core projects inside the render target");
    EXPECT_TRUE(project_world_to_pixel(
                    &canvas, -1.05f, 0.0f, -0.55f, width, height, &lit_left_x, &lit_left_y),
                "Lit plane sample before the penumbra projects inside the render target");
    EXPECT_TRUE(project_world_to_pixel(
                    &canvas, 1.45f, 0.0f, -0.55f, width, height, &lit_right_x, &lit_right_y),
                "Lit plane sample after the penumbra projects inside the render target");
    rt_canvas3d_draw_mesh_matrix(&canvas, plane, plane_model, plane_mat);
    rt_canvas3d_draw_mesh_matrix(&canvas, sphere, sphere_model, sphere_mat);
    rt_canvas3d_end(&canvas);

    target_owner = (rt_rendertarget3d *)target_obj;
    target = target_owner->target;
    hash = hash_render_target_rgba(target);
    shadow_luma = sample_luminance_box(target, shadow_x, shadow_y, 2);
    lit_left_luma = sample_luminance_box(target, lit_left_x, lit_left_y, 2);
    lit_right_luma = sample_luminance_box(target, lit_right_x, lit_right_y, 2);

    EXPECT_TRUE(lit_left_luma > shadow_luma * 1.25f + 0.015f,
                "Spot shadow darkens the plane relative to the lit side before the penumbra");
    EXPECT_TRUE(lit_right_luma > shadow_luma * 1.25f + 0.015f,
                "Spot shadow darkens the plane relative to the lit side after the penumbra");
    EXPECT_EQ_U64(hash, expected_hash, "Software spot-shadow render snapshot remains stable");

cleanup:
    if (canvas.in_frame)
        rt_canvas3d_end(&canvas);
    rt_canvas3d_clear_lights(&canvas);
    rt_canvas3d_disable_shadows(&canvas);
    rt_canvas3d_reset_render_target(&canvas);
    if (canvas.backend && canvas.backend_ctx && canvas.backend->destroy_ctx) {
        canvas.backend->destroy_ctx(canvas.backend_ctx);
        canvas.backend_ctx = nullptr;
    }
    release_obj(sphere_mat);
    release_obj(plane_mat);
    release_obj(sphere);
    release_obj(plane);
    release_obj(spot);
    release_obj(light_dir);
    release_obj(light_pos);
    release_obj(up);
    release_obj(look);
    release_obj(eye);
    release_obj(camera);
    release_obj(target_obj);
}

int main() {
    test_null_canvas_has_no_capabilities();
    test_software_backend_reports_canvas_fallback_features();
    test_gpu_backend_capability_bits_and_names();
    test_gpu_backend_native_texture_capability_hook();
    test_frustum_culling_aliases_share_state();
    test_canvas_render_state_sanitizes_inputs();
    test_software_spot_light_shadow_render_is_stable();

    if (tests_passed != tests_run) {
        std::fprintf(
            stderr, "test_rt_canvas3d_production: %d/%d checks passed\n", tests_passed, tests_run);
        return 1;
    }

    std::printf("test_rt_canvas3d_production: %d/%d checks passed\n", tests_passed, tests_run);
    return 0;
}
