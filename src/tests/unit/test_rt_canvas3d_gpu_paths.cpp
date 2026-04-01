#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_instbatch3d.h"
#include "rt_morphtarget3d.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "vgfx3d_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
extern void *rt_mat4_identity(void);
extern rt_string rt_const_cstr(const char *s);
extern void *rt_pixels_new(int64_t width, int64_t height);
extern void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);
extern void *rt_canvas3d_screenshot(void *canvas);
}

static int tests_run = 0;
static int tests_passed = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

typedef struct {
    vgfx3d_draw_cmd_t cmd;
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count;
    float ambient[3];
    int8_t wireframe;
    int8_t backface_cull;
    float sort_key;
} test_deferred_draw_t;

static vgfx3d_backend_t make_backend(const char *name) {
    vgfx3d_backend_t backend = {};
    backend.name = name;
    return backend;
}

static vgfx3d_backend_t kOpenGLBackend = make_backend("opengl");
static vgfx3d_backend_t kD3D11Backend = make_backend("d3d11");
static vgfx3d_backend_t kSoftwareBackend = make_backend("software");

static int skybox_draw_calls = 0;
static vgfx3d_draw_cmd_t last_instanced_cmd;
static const float *last_instance_matrices = nullptr;
static int32_t last_instance_count = 0;
static int32_t last_readback_w = 0;
static int32_t last_readback_h = 0;
static int32_t last_readback_stride = 0;
static void noop_end_frame(void *) {}
static void record_draw_skybox(void *, const void *) { skybox_draw_calls++; }
static void record_draw_instanced(void *,
                                  vgfx_window_t,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  const float *instance_matrices,
                                  int32_t instance_count,
                                  const vgfx3d_light_params_t *,
                                  int32_t,
                                  const float *,
                                  int8_t,
                                  int8_t) {
    if (cmd)
        last_instanced_cmd = *cmd;
    last_instance_matrices = instance_matrices;
    last_instance_count = instance_count;
}
static int record_readback_rgba(void *, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    last_readback_w = w;
    last_readback_h = h;
    last_readback_stride = stride;
    if (!dst_rgba || stride < w * 4)
        return 0;
    std::memset(dst_rgba, 0, static_cast<size_t>(stride) * static_cast<size_t>(h));
    dst_rgba[0] = 0x12;
    dst_rgba[1] = 0x34;
    dst_rgba[2] = 0x56;
    dst_rgba[3] = 0x78;
    return 1;
}

static void init_fake_canvas(rt_canvas3d *canvas, const vgfx3d_backend_t *backend) {
    std::memset(canvas, 0, sizeof(*canvas));
    canvas->backend = backend;
    canvas->gfx_win = (vgfx_window_t)1;
    canvas->in_frame = 1;
}

static void cleanup_fake_canvas(rt_canvas3d *canvas) {
    for (int32_t i = 0; i < canvas->temp_buf_count; i++)
        std::free(canvas->temp_buffers[i]);
    std::free(canvas->temp_buffers);
    std::free(canvas->draw_cmds);
    canvas->temp_buffers = nullptr;
    canvas->draw_cmds = nullptr;
    canvas->temp_buf_count = canvas->temp_buf_capacity = 0;
    canvas->draw_count = canvas->draw_capacity = 0;
}

static void reset_canvas_frame(rt_canvas3d *canvas, int64_t frame_serial) {
    canvas->draw_count = 0;
    canvas->frame_serial = frame_serial;
}

static void *make_test_mesh(void) {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    for (int64_t i = 0; i < 3; i++)
        rt_mesh3d_set_bone_weights(mesh, i, 0, 1.0, 0, 0.0, 0, 0.0, 0, 0.0);
    return mesh;
}

static void *make_test_player(void) {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);
    return rt_anim_player3d_new(skel);
}

static void test_gpu_skinning_bypass_for_opengl(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *player = make_test_player();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "OpenGL skinned draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0, "OpenGL skinned draw avoids CPU temp buffer");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "OpenGL skinned draw keeps original mesh vertices for GPU skinning");
    EXPECT_TRUE(draws[0].cmd.bone_palette != nullptr, "OpenGL skinned draw forwards bone palette");
    EXPECT_TRUE(draws[0].cmd.bone_count == 1, "OpenGL skinned draw forwards bone count");

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_skinning_bypass_for_d3d11(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kD3D11Backend);

    void *mesh = make_test_mesh();
    void *player = make_test_player();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "D3D11 skinned draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0, "D3D11 skinned draw avoids CPU temp buffer");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "D3D11 skinned draw keeps original mesh vertices for GPU skinning");
    EXPECT_TRUE(draws[0].cmd.bone_palette != nullptr, "D3D11 skinned draw forwards bone palette");
    EXPECT_TRUE(draws[0].cmd.bone_count == 1, "D3D11 skinned draw forwards bone count");

    cleanup_fake_canvas(&canvas);
}

static void test_cpu_skinning_fallback_for_software(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kSoftwareBackend);

    void *mesh = make_test_mesh();
    void *player = make_test_player();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Software skinned draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 1, "Software skinned draw allocates CPU temp buffer");
    EXPECT_TRUE(draws[0].cmd.vertices != mesh_view->vertices,
                "Software skinned draw uses CPU-skinned vertex buffer");
    EXPECT_TRUE(draws[0].cmd.bone_palette != nullptr, "Software skinned draw still forwards bone palette");

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_morph_payload_for_opengl(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);

    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "OpenGL morphed draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 2, "OpenGL morphed draw registers packed deltas and weights");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "OpenGL morphed draw keeps original mesh vertices for GPU morphing");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr, "OpenGL morphed draw forwards packed morph deltas");
    EXPECT_TRUE(draws[0].cmd.morph_normal_deltas == nullptr,
                "OpenGL morphed draw leaves normal-delta payload null when absent");
    EXPECT_TRUE(draws[0].cmd.morph_weights != nullptr, "OpenGL morphed draw forwards packed morph weights");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1, "OpenGL morphed draw forwards shape count");
    if (draws[0].cmd.morph_deltas && draws[0].cmd.morph_weights) {
        EXPECT_TRUE(draws[0].cmd.morph_deltas[0] == 1.0f &&
                        draws[0].cmd.morph_deltas[1] == 2.0f &&
                        draws[0].cmd.morph_deltas[2] == 3.0f,
                    "OpenGL morphed draw packs vertex deltas in XYZ order");
        EXPECT_TRUE(draws[0].cmd.morph_weights[0] == 0.5f,
                    "OpenGL morphed draw forwards morph weights");
    }

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_morph_payload_for_d3d11(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kD3D11Backend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);

    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "D3D11 morphed draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 2, "D3D11 morphed draw registers packed deltas and weights");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "D3D11 morphed draw keeps original mesh vertices for GPU morphing");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr, "D3D11 morphed draw forwards packed morph deltas");
    EXPECT_TRUE(draws[0].cmd.morph_weights != nullptr, "D3D11 morphed draw forwards packed morph weights");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1, "D3D11 morphed draw forwards shape count");

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_morph_normal_payload_for_d3d11(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kD3D11Backend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_normal_delta(morph, 0, 0, 0.25, 0.5, 0.75);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);

    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "D3D11 morphed-normal draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 3,
                "D3D11 morphed-normal draw registers packed deltas, normal deltas, and weights");
    EXPECT_TRUE(draws[0].cmd.morph_normal_deltas != nullptr,
                "D3D11 morphed-normal draw forwards packed morph normal deltas");
    if (draws[0].cmd.morph_normal_deltas) {
        EXPECT_TRUE(draws[0].cmd.morph_normal_deltas[0] == 0.25f &&
                        draws[0].cmd.morph_normal_deltas[1] == 0.5f &&
                        draws[0].cmd.morph_normal_deltas[2] == 0.75f,
                    "D3D11 morphed-normal draw packs normal deltas in XYZ order");
    }

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_morph_normal_payload_for_opengl(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_normal_delta(morph, 0, 0, 0.25, 0.5, 0.75);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);

    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "OpenGL morphed-normal draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 3,
                "OpenGL morphed-normal draw registers packed deltas, normal deltas, and weights");
    EXPECT_TRUE(draws[0].cmd.morph_normal_deltas != nullptr,
                "OpenGL morphed-normal draw forwards packed morph normal deltas");
    if (draws[0].cmd.morph_normal_deltas) {
        EXPECT_TRUE(draws[0].cmd.morph_normal_deltas[0] == 0.25f &&
                        draws[0].cmd.morph_normal_deltas[1] == 0.5f &&
                        draws[0].cmd.morph_normal_deltas[2] == 0.75f,
                    "OpenGL morphed-normal draw packs normal deltas in XYZ order");
    }

    cleanup_fake_canvas(&canvas);
}

static void test_cpu_morph_fallback_for_software(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kSoftwareBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);

    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Software morphed draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 1, "Software morphed draw allocates one CPU temp buffer");
    EXPECT_TRUE(draws[0].cmd.vertices != mesh_view->vertices,
                "Software morphed draw uses CPU-morphed vertices");
    EXPECT_TRUE(draws[0].cmd.morph_deltas == nullptr &&
                    draws[0].cmd.morph_weights == nullptr &&
                    draws[0].cmd.morph_shape_count == 0,
                "Software morphed draw leaves GPU morph payload empty");

    cleanup_fake_canvas(&canvas);
}

static void test_env_map_payload_forwarded(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *px = rt_pixels_new(1, 1);
    rt_pixels_set(px, 0, 0, 0xFF0000FF);
    void *cubemap = rt_cubemap3d_new(px, px, px, px, px, px);
    rt_material3d_set_env_map(material, cubemap);
    rt_material3d_set_reflectivity(material, 0.75);

    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Env-map draw enqueues one draw");
    EXPECT_TRUE(draws[0].cmd.env_map == cubemap, "Env-map draw forwards cubemap payload");
    EXPECT_TRUE(draws[0].cmd.reflectivity == 0.75f,
                "Env-map draw forwards reflectivity payload");

    cleanup_fake_canvas(&canvas);
}

static void test_backend_skybox_hook_used(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.end_frame = noop_end_frame;
    backend.draw_skybox = record_draw_skybox;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    canvas.backend_ctx = &canvas;
    skybox_draw_calls = 0;

    void *px = rt_pixels_new(1, 1);
    rt_pixels_set(px, 0, 0, 0x00FF00FF);
    canvas.skybox = (rt_cubemap3d *)rt_cubemap3d_new(px, px, px, px, px, px);

    rt_canvas3d_end(&canvas);

    EXPECT_TRUE(skybox_draw_calls == 1,
                "Canvas3D.End delegates skybox rendering to the backend hook when available");
    EXPECT_TRUE(canvas.in_frame == 0, "Canvas3D.End completes cleanly for skybox-only scenes");

    cleanup_fake_canvas(&canvas);
}

static void test_transform_history_forwarded_for_motion_blur(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.has_prev_model_matrix == 0,
                "First keyed draw has no previous model matrix");

    ((mat4_impl *)transform)->m[3] = 5.0;
    reset_canvas_frame(&canvas, 2);
    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);
    draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.has_prev_model_matrix == 1,
                "Second keyed draw forwards previous model-matrix history");
    EXPECT_TRUE(draws[0].cmd.prev_model_matrix[3] == 0.0f,
                "Previous model matrix preserves the prior translation");

    cleanup_fake_canvas(&canvas);
}

static void test_morph_weight_history_forwarded(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 0.0, 0.0);

    reset_canvas_frame(&canvas, 1);
    rt_morphtarget3d_set_weight(morph, 0, 0.25);
    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.prev_morph_weights == nullptr,
                "First GPU morph draw has no previous-weight history");

    reset_canvas_frame(&canvas, 2);
    rt_morphtarget3d_set_weight(morph, 0, 0.75);
    rt_canvas3d_draw_mesh_morphed(&canvas, mesh, transform, material, morph);
    draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.prev_morph_weights != nullptr,
                "Second GPU morph draw forwards previous morph weights");
    if (draws[0].cmd.prev_morph_weights)
        EXPECT_TRUE(draws[0].cmd.prev_morph_weights[0] == 0.25f,
                    "Previous morph weights preserve the prior frame value");

    cleanup_fake_canvas(&canvas);
}

static void test_skinning_palette_history_forwarded(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *player = make_test_player();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.prev_bone_palette == nullptr,
                "First GPU skinned draw has no previous palette history");

    reset_canvas_frame(&canvas, 2);
    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);
    draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(draws[0].cmd.prev_bone_palette != nullptr,
                "Second GPU skinned draw forwards previous palette history");

    cleanup_fake_canvas(&canvas);
}

static void test_instanced_transform_history_forwarded(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.submit_draw_instanced = record_draw_instanced;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    last_instance_matrices = nullptr;
    last_instance_count = 0;
    std::memset(&last_instanced_cmd, 0, sizeof(last_instanced_cmd));

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *batch = rt_instbatch3d_new(mesh, material);
    void *t0 = rt_mat4_identity();
    void *t1 = rt_mat4_identity();
    ((mat4_impl *)t0)->m[3] = 1.0;
    ((mat4_impl *)t1)->m[3] = 3.0;
    rt_instbatch3d_add(batch, t0);
    rt_instbatch3d_add(batch, t1);

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_instanced(&canvas, batch);
    EXPECT_TRUE(last_instance_count == 2, "Instanced draw submits both instances");
    EXPECT_TRUE(last_instanced_cmd.has_prev_instance_matrices == 0,
                "First instanced draw has no previous transform history");

    ((mat4_impl *)t0)->m[3] = 2.0;
    rt_instbatch3d_set(batch, 0, t0);
    reset_canvas_frame(&canvas, 2);
    rt_canvas3d_draw_instanced(&canvas, batch);
    EXPECT_TRUE(last_instanced_cmd.has_prev_instance_matrices == 1,
                "Second instanced draw forwards previous instance transforms");
    EXPECT_TRUE(last_instanced_cmd.prev_instance_matrices != nullptr,
                "Instanced draw exposes previous instance matrix payload");
    if (last_instanced_cmd.prev_instance_matrices)
        EXPECT_TRUE(last_instanced_cmd.prev_instance_matrices[3] == 1.0f,
                    "Previous instance matrix preserves the prior translation");

    cleanup_fake_canvas(&canvas);
}

static void test_instanced_material_payload_forwarded(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "d3d11";
    backend.submit_draw_instanced = record_draw_instanced;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    last_instance_matrices = nullptr;
    last_instance_count = 0;
    std::memset(&last_instanced_cmd, 0, sizeof(last_instanced_cmd));

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    rt_material3d *mat_view = (rt_material3d *)material;
    mat_view->diffuse[0] = 0.2;
    mat_view->diffuse[1] = 0.4;
    mat_view->diffuse[2] = 0.6;
    mat_view->diffuse[3] = 0.8;
    mat_view->specular[0] = 0.9;
    mat_view->specular[1] = 0.7;
    mat_view->specular[2] = 0.5;
    mat_view->shininess = 48.0;
    mat_view->alpha = 0.65;
    mat_view->emissive[0] = 0.1;
    mat_view->emissive[1] = 0.2;
    mat_view->emissive[2] = 0.3;

    void *px = rt_pixels_new(1, 1);
    rt_pixels_set(px, 0, 0, 0xFFAA00FF);
    void *cubemap = rt_cubemap3d_new(px, px, px, px, px, px);
    mat_view->texture = px;
    mat_view->normal_map = px;
    mat_view->specular_map = px;
    mat_view->emissive_map = px;
    mat_view->env_map = cubemap;
    mat_view->reflectivity = 0.55;

    void *batch = rt_instbatch3d_new(mesh, material);
    void *transform = rt_mat4_identity();
    rt_instbatch3d_add(batch, transform);

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_instanced(&canvas, batch);

    EXPECT_TRUE(last_instance_count == 1, "Instanced material draw submits one instance");
    EXPECT_TRUE(last_instanced_cmd.texture == px, "Instanced draw forwards diffuse texture");
    EXPECT_TRUE(last_instanced_cmd.normal_map == px, "Instanced draw forwards normal map");
    EXPECT_TRUE(last_instanced_cmd.specular_map == px, "Instanced draw forwards specular map");
    EXPECT_TRUE(last_instanced_cmd.emissive_map == px, "Instanced draw forwards emissive map");
    EXPECT_TRUE(last_instanced_cmd.env_map == cubemap, "Instanced draw forwards environment map");
    EXPECT_TRUE(last_instanced_cmd.reflectivity == 0.55f,
                "Instanced draw forwards reflectivity");
    EXPECT_TRUE(last_instanced_cmd.specular[0] == 0.9f &&
                    last_instanced_cmd.specular[1] == 0.7f &&
                    last_instanced_cmd.specular[2] == 0.5f,
                "Instanced draw forwards specular color");
    EXPECT_TRUE(last_instanced_cmd.diffuse_color[3] == 0.8f,
                "Instanced draw preserves diffuse alpha separate from material alpha");
    EXPECT_TRUE(last_instanced_cmd.alpha == 0.65f, "Instanced draw forwards material opacity");

    cleanup_fake_canvas(&canvas);
}

static void test_screenshot_prefers_backend_readback(void) {
    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } pixels_view_t;

    vgfx3d_backend_t backend = {};
    backend.name = "metal";
    backend.readback_rgba = record_readback_rgba;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    canvas.width = 2;
    canvas.height = 2;
    last_readback_w = 0;
    last_readback_h = 0;
    last_readback_stride = 0;

    void *shot = rt_canvas3d_screenshot(&canvas);
    pixels_view_t *view = (pixels_view_t *)shot;
    EXPECT_TRUE(shot != nullptr, "Canvas3D.Screenshot produces a Pixels object");
    EXPECT_TRUE(last_readback_w == 2 && last_readback_h == 2,
                "Canvas3D.Screenshot requests backend readback at canvas dimensions");
    EXPECT_TRUE(last_readback_stride == 8, "Canvas3D.Screenshot uses tightly packed RGBA rows");
    if (view && view->data) {
        EXPECT_TRUE(view->data[0] == 0x12345678u,
                    "Canvas3D.Screenshot stores backend RGBA bytes in Pixels order");
    }
}

int main() {
    test_gpu_skinning_bypass_for_opengl();
    test_gpu_skinning_bypass_for_d3d11();
    test_cpu_skinning_fallback_for_software();
    test_gpu_morph_payload_for_opengl();
    test_gpu_morph_payload_for_d3d11();
    test_gpu_morph_normal_payload_for_opengl();
    test_gpu_morph_normal_payload_for_d3d11();
    test_cpu_morph_fallback_for_software();
    test_env_map_payload_forwarded();
    test_backend_skybox_hook_used();
    test_transform_history_forwarded_for_motion_blur();
    test_morph_weight_history_forwarded();
    test_skinning_palette_history_forwarded();
    test_instanced_transform_history_forwarded();
    test_instanced_material_payload_forwarded();
    test_screenshot_prefers_backend_readback();

    std::printf("Canvas3D GPU path tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
