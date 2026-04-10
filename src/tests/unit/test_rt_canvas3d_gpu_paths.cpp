#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_instbatch3d.h"
#include "rt_morphtarget3d.h"
#include "rt_postfx3d.h"
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
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
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
    int kind;
    int pass_kind;
    vgfx3d_draw_cmd_t cmd;
    const float *instance_matrices;
    int32_t instance_count;
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count;
    float ambient[3];
    int8_t wireframe;
    int8_t backface_cull;
    int8_t has_local_bounds;
    float local_bounds_min[3];
    float local_bounds_max[3];
    float sort_key;
} test_deferred_draw_t;

static vgfx3d_backend_t make_backend(const char *name) {
    vgfx3d_backend_t backend = {};
    backend.name = name;
    return backend;
}

static vgfx3d_backend_t kOpenGLBackend = make_backend("opengl");
static vgfx3d_backend_t kD3D11Backend = make_backend("d3d11");
static vgfx3d_backend_t kMetalBackend = make_backend("metal");
static vgfx3d_backend_t kSoftwareBackend = make_backend("software");

static int skybox_draw_calls = 0;
static int shadow_begin_calls = 0;
static int shadow_draw_calls = 0;
static int shadow_end_calls = 0;
static vgfx3d_draw_cmd_t last_instanced_cmd;
static const float *last_instance_matrices = nullptr;
static int32_t last_instance_count = 0;
static float *last_instance_matrices_copy = nullptr;
static float *last_prev_instance_matrices_copy = nullptr;
static int32_t last_readback_w = 0;
static int32_t last_readback_h = 0;
static int32_t last_readback_stride = 0;
static int begin_frame_calls = 0;
static vgfx3d_camera_params_t begin_frame_params[4];
static int set_gpu_postfx_enabled_calls = 0;
static int8_t set_gpu_postfx_enabled_values[4];
static int set_gpu_postfx_snapshot_calls = 0;
static int8_t set_gpu_postfx_snapshot_present[4];
static vgfx3d_postfx_snapshot_t set_gpu_postfx_snapshots[4];

static void noop_end_frame(void *) {}
static void noop_present_postfx(void *, const vgfx3d_postfx_snapshot_t *) {}

static void noop_draw(void *,
                      vgfx_window_t,
                      const vgfx3d_draw_cmd_t *,
                      const vgfx3d_light_params_t *,
                      int32_t,
                      const float *,
                      int8_t,
                      int8_t) {}

static void record_draw_skybox(void *, const void *) {
    skybox_draw_calls++;
}

static void reset_shadow_counts(void) {
    shadow_begin_calls = 0;
    shadow_draw_calls = 0;
    shadow_end_calls = 0;
}

static void record_shadow_begin(void *, float *, int32_t, int32_t, const float *) {
    shadow_begin_calls++;
}

static void record_shadow_draw(void *, const vgfx3d_draw_cmd_t *) {
    shadow_draw_calls++;
}

static void record_shadow_end(void *, float) {
    shadow_end_calls++;
}

static void reset_recorded_instancing(void) {
    std::free(last_instance_matrices_copy);
    std::free(last_prev_instance_matrices_copy);
    last_instance_matrices_copy = nullptr;
    last_prev_instance_matrices_copy = nullptr;
    last_instance_matrices = nullptr;
    last_instance_count = 0;
    std::memset(&last_instanced_cmd, 0, sizeof(last_instanced_cmd));
}

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
    reset_recorded_instancing();
    if (cmd)
        last_instanced_cmd = *cmd;
    last_instance_count = instance_count;
    if (instance_matrices && instance_count > 0) {
        size_t bytes = static_cast<size_t>(instance_count) * 16u * sizeof(float);
        last_instance_matrices_copy = (float *)std::malloc(bytes);
        if (last_instance_matrices_copy) {
            std::memcpy(last_instance_matrices_copy, instance_matrices, bytes);
            last_instance_matrices = last_instance_matrices_copy;
        }
    }
    if (cmd && cmd->prev_instance_matrices && cmd->has_prev_instance_matrices &&
        instance_count > 0) {
        size_t bytes = static_cast<size_t>(instance_count) * 16u * sizeof(float);
        last_prev_instance_matrices_copy = (float *)std::malloc(bytes);
        if (last_prev_instance_matrices_copy) {
            std::memcpy(last_prev_instance_matrices_copy, cmd->prev_instance_matrices, bytes);
            last_instanced_cmd.prev_instance_matrices = last_prev_instance_matrices_copy;
        } else {
            last_instanced_cmd.prev_instance_matrices = nullptr;
        }
    }
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

static void reset_postfx_records(void) {
    begin_frame_calls = 0;
    std::memset(begin_frame_params, 0, sizeof(begin_frame_params));
    set_gpu_postfx_enabled_calls = 0;
    std::memset(set_gpu_postfx_enabled_values, 0, sizeof(set_gpu_postfx_enabled_values));
    set_gpu_postfx_snapshot_calls = 0;
    std::memset(set_gpu_postfx_snapshot_present, 0, sizeof(set_gpu_postfx_snapshot_present));
    std::memset(set_gpu_postfx_snapshots, 0, sizeof(set_gpu_postfx_snapshots));
}

static void record_begin_frame(void *, const vgfx3d_camera_params_t *cam) {
    if (begin_frame_calls < (int)(sizeof(begin_frame_params) / sizeof(begin_frame_params[0])) && cam)
        begin_frame_params[begin_frame_calls] = *cam;
    begin_frame_calls++;
}

static void record_set_gpu_postfx_enabled(void *, int8_t enabled) {
    if (set_gpu_postfx_enabled_calls <
        (int)(sizeof(set_gpu_postfx_enabled_values) / sizeof(set_gpu_postfx_enabled_values[0]))) {
        set_gpu_postfx_enabled_values[set_gpu_postfx_enabled_calls] = enabled;
    }
    set_gpu_postfx_enabled_calls++;
}

static void record_set_gpu_postfx_snapshot(void *, const vgfx3d_postfx_snapshot_t *snapshot) {
    if (set_gpu_postfx_snapshot_calls <
        (int)(sizeof(set_gpu_postfx_snapshot_present) /
              sizeof(set_gpu_postfx_snapshot_present[0]))) {
        set_gpu_postfx_snapshot_present[set_gpu_postfx_snapshot_calls] = snapshot ? 1 : 0;
        if (snapshot)
            set_gpu_postfx_snapshots[set_gpu_postfx_snapshot_calls] = *snapshot;
    }
    set_gpu_postfx_snapshot_calls++;
}

static void set_identity4x4(float *m) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void set_identity4x4d(double *m) {
    std::memset(m, 0, sizeof(double) * 16);
    m[0] = 1.0;
    m[5] = 1.0;
    m[10] = 1.0;
    m[15] = 1.0;
}

static void init_fake_canvas(rt_canvas3d *canvas, const vgfx3d_backend_t *backend) {
    std::memset(canvas, 0, sizeof(*canvas));
    canvas->backend = backend;
    canvas->gfx_win = (vgfx_window_t)1;
    canvas->in_frame = 1;
    set_identity4x4(canvas->cached_vp);
    std::memcpy(canvas->last_scene_vp, canvas->cached_vp, sizeof(canvas->last_scene_vp));
    canvas->has_last_scene_vp = 1;
}

static void cleanup_fake_canvas(rt_canvas3d *canvas) {
    for (int32_t i = 0; i < canvas->temp_buf_count; i++)
        std::free(canvas->temp_buffers[i]);
    for (int32_t i = 0; i < canvas->temp_obj_count; i++) {
        if (canvas->temp_objects[i] && rt_obj_release_check0(canvas->temp_objects[i]))
            rt_obj_free(canvas->temp_objects[i]);
    }
    std::free(canvas->temp_buffers);
    std::free(canvas->temp_objects);
    std::free(canvas->draw_cmds);
    std::free(canvas->motion_history);
    if (canvas->postfx && rt_obj_release_check0(canvas->postfx))
        rt_obj_free(canvas->postfx);
    canvas->temp_buffers = nullptr;
    canvas->temp_objects = nullptr;
    canvas->draw_cmds = nullptr;
    canvas->motion_history = nullptr;
    canvas->postfx = nullptr;
    canvas->temp_buf_count = canvas->temp_buf_capacity = 0;
    canvas->temp_obj_count = canvas->temp_obj_capacity = 0;
    canvas->draw_count = canvas->draw_capacity = 0;
    canvas->motion_history_count = canvas->motion_history_capacity = 0;
    reset_recorded_instancing();
}

static void reset_canvas_frame(rt_canvas3d *canvas, int64_t frame_serial) {
    canvas->draw_count = 0;
    canvas->frame_serial = frame_serial;
    canvas->in_frame = 1;
    canvas->frame_is_2d = 0;
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

static void test_gpu_skinning_bypass_for_metal(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kMetalBackend);

    void *mesh = make_test_mesh();
    void *player = make_test_player();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    rt_canvas3d_draw_mesh_skinned(&canvas, mesh, transform, material, player);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Metal skinned draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0, "Metal skinned draw avoids CPU temp buffer");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "Metal skinned draw keeps original mesh vertices for GPU skinning");
    EXPECT_TRUE(draws[0].cmd.bone_palette != nullptr, "Metal skinned draw forwards bone palette");
    EXPECT_TRUE(draws[0].cmd.bone_count == 1, "Metal skinned draw forwards bone count");

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
    EXPECT_TRUE(draws[0].cmd.bone_palette != nullptr,
                "Software skinned draw still forwards bone palette");

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
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "OpenGL morphed draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "OpenGL morphed draw retains the morph object until frame end");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "OpenGL morphed draw keeps original mesh vertices for GPU morphing");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr,
                "OpenGL morphed draw forwards packed morph deltas");
    EXPECT_TRUE(draws[0].cmd.morph_normal_deltas == nullptr,
                "OpenGL morphed draw leaves normal-delta payload null when absent");
    EXPECT_TRUE(draws[0].cmd.morph_weights != nullptr,
                "OpenGL morphed draw forwards packed morph weights");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1, "OpenGL morphed draw forwards shape count");
    EXPECT_TRUE(draws[0].cmd.morph_key == morph,
                "OpenGL morphed draw forwards the stable morph identity");
    EXPECT_TRUE(draws[0].cmd.morph_revision == rt_morphtarget3d_get_payload_generation(morph),
                "OpenGL morphed draw forwards the morph payload revision");
    if (draws[0].cmd.morph_deltas && draws[0].cmd.morph_weights) {
        EXPECT_TRUE(draws[0].cmd.morph_deltas[0] == 1.0f && draws[0].cmd.morph_deltas[1] == 2.0f &&
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
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "D3D11 morphed draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "D3D11 morphed draw retains the morph object until frame end");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "D3D11 morphed draw keeps original mesh vertices for GPU morphing");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr,
                "D3D11 morphed draw forwards packed morph deltas");
    EXPECT_TRUE(draws[0].cmd.morph_weights != nullptr,
                "D3D11 morphed draw forwards packed morph weights");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1, "D3D11 morphed draw forwards shape count");
    EXPECT_TRUE(draws[0].cmd.morph_key == morph,
                "D3D11 morphed draw forwards the stable morph identity");
    EXPECT_TRUE(draws[0].cmd.morph_revision == rt_morphtarget3d_get_payload_generation(morph),
                "D3D11 morphed draw forwards the morph payload revision");

    cleanup_fake_canvas(&canvas);
}

static void test_gpu_morph_payload_for_metal(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kMetalBackend);

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
    EXPECT_TRUE(canvas.draw_count == 1, "Metal morphed draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "Metal morphed draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "Metal morphed draw retains the morph object until frame end");
    EXPECT_TRUE(draws[0].cmd.vertices == mesh_view->vertices,
                "Metal morphed draw keeps original mesh vertices for GPU morphing");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr,
                "Metal morphed draw forwards packed morph deltas");
    EXPECT_TRUE(draws[0].cmd.morph_weights != nullptr,
                "Metal morphed draw forwards packed morph weights");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1, "Metal morphed draw forwards shape count");
    EXPECT_TRUE(draws[0].cmd.morph_key == morph,
                "Metal morphed draw forwards the stable morph identity");
    EXPECT_TRUE(draws[0].cmd.morph_revision == rt_morphtarget3d_get_payload_generation(morph),
                "Metal morphed draw forwards the morph payload revision");

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
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "D3D11 morphed-normal draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "D3D11 morphed-normal draw retains the morph object until frame end");
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
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "OpenGL morphed-normal draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "OpenGL morphed-normal draw retains the morph object until frame end");
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

static void test_gpu_morph_normal_payload_for_metal(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kMetalBackend);

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
    EXPECT_TRUE(canvas.draw_count == 1, "Metal morphed-normal draw enqueues one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "Metal morphed-normal draw avoids transient packed payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "Metal morphed-normal draw retains the morph object until frame end");
    EXPECT_TRUE(draws[0].cmd.morph_normal_deltas != nullptr,
                "Metal morphed-normal draw forwards packed morph normal deltas");
    if (draws[0].cmd.morph_normal_deltas) {
        EXPECT_TRUE(draws[0].cmd.morph_normal_deltas[0] == 0.25f &&
                        draws[0].cmd.morph_normal_deltas[1] == 0.5f &&
                        draws[0].cmd.morph_normal_deltas[2] == 0.75f,
                    "Metal morphed-normal draw packs normal deltas in XYZ order");
    }

    cleanup_fake_canvas(&canvas);
}

static void test_attached_morph_targets_route_through_draw_mesh(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *morph = rt_morphtarget3d_new(3);
    rt_morphtarget3d_add_shape(morph, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(morph, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(morph, 0, 0.5);
    rt_mesh3d_set_morph_targets(mesh, morph);

    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Attached morph targets still enqueue one draw");
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "Attached morph targets avoid transient GPU morph payload buffers");
    EXPECT_TRUE(canvas.temp_obj_count == 1,
                "Attached morph targets retain the morph object until frame end");
    EXPECT_TRUE(draws[0].cmd.morph_deltas != nullptr,
                "Attached morph targets route DrawMesh through the morph payload path");
    EXPECT_TRUE(draws[0].cmd.morph_shape_count == 1,
                "Attached morph targets preserve the shape count on DrawMesh");

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
    EXPECT_TRUE(draws[0].cmd.morph_deltas == nullptr && draws[0].cmd.morph_weights == nullptr &&
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
    EXPECT_TRUE(draws[0].cmd.reflectivity == 0.75f, "Env-map draw forwards reflectivity payload");

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

static void test_static_mesh_geometry_identity_forwarded(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();

    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);

    rt_mesh3d *mesh_view = (rt_mesh3d *)mesh;
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Static mesh draw enqueues one draw");
    EXPECT_TRUE(draws[0].cmd.geometry_key == mesh,
                "Static mesh draw forwards a stable geometry identity for backend caches");
    EXPECT_TRUE(draws[0].cmd.geometry_revision == mesh_view->geometry_revision,
                "Static mesh draw forwards the current geometry revision");

    cleanup_fake_canvas(&canvas);
}

static void test_rect2d_queues_overlay_pass(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);

    rt_canvas3d_draw_rect2d(&canvas, 10, 20, 30, 40, 0xFFAA00FF);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "Rect2D enqueues one overlay draw");
    EXPECT_TRUE(draws[0].pass_kind == 1,
                "Rect2D routes through the screen-overlay deferred pass during 3D frames");
    EXPECT_TRUE(draws[0].cmd.unlit == 1, "Rect2D overlay draw is submitted as unlit geometry");

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
    backend.end_frame = noop_end_frame;
    backend.submit_draw_instanced = record_draw_instanced;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    reset_recorded_instancing();

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *batch = rt_instbatch3d_new(mesh, material);
    void *t0 = rt_mat4_identity();
    void *t1 = rt_mat4_identity();
    ((mat4_impl *)t0)->m[3] = -0.75;
    ((mat4_impl *)t1)->m[3] = -0.25;
    rt_instbatch3d_add(batch, t0);
    rt_instbatch3d_add(batch, t1);

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_instanced(&canvas, batch);
    rt_canvas3d_end(&canvas);
    EXPECT_TRUE(last_instance_count == 2, "Instanced draw submits both instances");
    EXPECT_TRUE(last_instanced_cmd.has_prev_instance_matrices == 0,
                "First instanced draw has no previous transform history");

    ((mat4_impl *)t0)->m[3] = 0.0;
    rt_instbatch3d_set(batch, 0, t0);
    reset_canvas_frame(&canvas, 2);
    rt_canvas3d_draw_instanced(&canvas, batch);
    rt_canvas3d_end(&canvas);
    EXPECT_TRUE(last_instanced_cmd.has_prev_instance_matrices == 1,
                "Second instanced draw forwards previous instance transforms");
    EXPECT_TRUE(last_instanced_cmd.prev_instance_matrices != nullptr,
                "Instanced draw exposes previous instance matrix payload");
    if (last_instanced_cmd.prev_instance_matrices)
        EXPECT_TRUE(last_instanced_cmd.prev_instance_matrices[3] == -0.75f,
                    "Previous instance matrix preserves the prior translation");

    cleanup_fake_canvas(&canvas);
}

static void test_instanced_material_payload_forwarded(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "metal";
    backend.end_frame = noop_end_frame;
    backend.submit_draw_instanced = record_draw_instanced;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    reset_recorded_instancing();

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
    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;

    EXPECT_TRUE(canvas.draw_count == 1,
                "Transparent instanced material draw enqueues one mesh draw");
    EXPECT_TRUE(last_instance_count == 0,
                "Transparent instanced material draw avoids backend instancing so it can sort");
    EXPECT_TRUE(draws[0].cmd.texture == px, "Instanced draw forwards diffuse texture");
    EXPECT_TRUE(draws[0].cmd.normal_map == px, "Instanced draw forwards normal map");
    EXPECT_TRUE(draws[0].cmd.specular_map == px, "Instanced draw forwards specular map");
    EXPECT_TRUE(draws[0].cmd.emissive_map == px, "Instanced draw forwards emissive map");
    EXPECT_TRUE(draws[0].cmd.env_map == cubemap, "Instanced draw forwards environment map");
    EXPECT_TRUE(draws[0].cmd.reflectivity == 0.55f, "Instanced draw forwards reflectivity");
    EXPECT_TRUE(draws[0].cmd.specular[0] == 0.9f && draws[0].cmd.specular[1] == 0.7f &&
                    draws[0].cmd.specular[2] == 0.5f,
                "Instanced draw forwards specular color");
    EXPECT_TRUE(draws[0].cmd.diffuse_color[3] == 0.8f,
                "Instanced draw preserves diffuse alpha separate from material alpha");
    EXPECT_TRUE(draws[0].cmd.alpha == 0.65f, "Instanced draw forwards material opacity");

    cleanup_fake_canvas(&canvas);
}

static void test_pbr_material_payload_forwarded(void) {
    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &kOpenGLBackend);
    canvas.backface_cull = 1;

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new_pbr(0.6, 0.4, 0.2);
    void *transform = rt_mat4_identity();
    void *px = rt_pixels_new(1, 1);
    rt_pixels_set(px, 0, 0, 0x80C040CC);

    rt_material3d_set_albedo_map(material, px);
    rt_material3d_set_normal_map(material, px);
    rt_material3d_set_metallic_roughness_map(material, px);
    rt_material3d_set_ao_map(material, px);
    rt_material3d_set_emissive_map(material, px);
    rt_material3d_set_metallic(material, 0.75);
    rt_material3d_set_roughness(material, 0.35);
    rt_material3d_set_ao(material, 0.85);
    rt_material3d_set_emissive_intensity(material, 1.8);
    rt_material3d_set_normal_scale(material, 0.55);
    rt_material3d_set_alpha(material, 0.6);
    rt_material3d_set_alpha_mode(material, RT_MATERIAL3D_ALPHA_MODE_BLEND);
    rt_material3d_set_double_sided(material, 1);

    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);

    test_deferred_draw_t *draws = (test_deferred_draw_t *)canvas.draw_cmds;
    EXPECT_TRUE(canvas.draw_count == 1, "PBR material draw enqueues one draw");
    EXPECT_TRUE(draws[0].cmd.workflow == RT_MATERIAL3D_WORKFLOW_PBR,
                "PBR material draw forwards workflow");
    EXPECT_TRUE(draws[0].cmd.texture == px, "PBR material draw forwards albedo map");
    EXPECT_TRUE(draws[0].cmd.normal_map == px, "PBR material draw forwards normal map");
    EXPECT_TRUE(draws[0].cmd.metallic_roughness_map == px,
                "PBR material draw forwards metallic-roughness map");
    EXPECT_TRUE(draws[0].cmd.ao_map == px, "PBR material draw forwards AO map");
    EXPECT_TRUE(draws[0].cmd.emissive_map == px, "PBR material draw forwards emissive map");
    EXPECT_TRUE(draws[0].cmd.alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND,
                "PBR material draw forwards alpha mode");
    EXPECT_TRUE(draws[0].cmd.double_sided == 1, "PBR material draw forwards double-sided state");
    EXPECT_TRUE(draws[0].backface_cull == 0,
                "Double-sided PBR materials disable deferred backface culling");
    EXPECT_TRUE(draws[0].cmd.metallic == 0.75f && draws[0].cmd.roughness == 0.35f &&
                    draws[0].cmd.ao == 0.85f,
                "PBR material draw forwards metallic, roughness, and AO scalars");
    EXPECT_TRUE(draws[0].cmd.emissive_intensity == 1.8f,
                "PBR material draw forwards emissive intensity");
    EXPECT_TRUE(draws[0].cmd.normal_scale == 0.55f,
                "PBR material draw forwards normal scale");
    EXPECT_TRUE(draws[0].cmd.alpha == 0.6f,
                "PBR material draw forwards material opacity separately from alpha mode");

    cleanup_fake_canvas(&canvas);
}

static void test_instanced_runtime_culls_outside_frustum(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "metal";
    backend.end_frame = noop_end_frame;
    backend.submit_draw_instanced = record_draw_instanced;

    rt_canvas3d canvas;
    init_fake_canvas(&canvas, &backend);
    reset_recorded_instancing();

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *batch = rt_instbatch3d_new(mesh, material);
    void *visible = rt_mat4_identity();
    void *hidden = rt_mat4_identity();
    ((mat4_impl *)hidden)->m[3] = 4.0;
    rt_instbatch3d_add(batch, visible);
    rt_instbatch3d_add(batch, hidden);

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_instanced(&canvas, batch);
    rt_canvas3d_end(&canvas);
    EXPECT_TRUE(last_instance_count == 1,
                "Runtime instanced path drops off-frustum instances before backend submission");
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "Instanced-only frames release culled-instance temp buffers at End()");
    EXPECT_TRUE(last_instance_matrices != nullptr && last_instance_matrices[3] == 0.0f,
                "Instanced culling preserves the visible instance transform payload");

    ((mat4_impl *)visible)->m[3] = 0.5;
    rt_instbatch3d_set(batch, 0, visible);
    reset_canvas_frame(&canvas, 2);
    rt_canvas3d_draw_instanced(&canvas, batch);
    rt_canvas3d_end(&canvas);
    EXPECT_TRUE(
        last_instanced_cmd.has_prev_instance_matrices == 1,
        "Instanced culling keeps previous-transform history enabled for surviving instances");
    EXPECT_TRUE(
        last_instanced_cmd.prev_instance_matrices != nullptr &&
            last_instanced_cmd.prev_instance_matrices[3] == 0.0f,
        "Instanced culling keeps previous-transform history aligned to surviving instances");
    EXPECT_TRUE(canvas.temp_buf_count == 0,
                "Instanced culling cleanup remains correct on later frames");

    cleanup_fake_canvas(&canvas);
}

static void test_instanced_shadow_pass_includes_instances(void) {
    vgfx3d_backend_t backend = {};
    backend.name = "metal";
    backend.end_frame = noop_end_frame;
    backend.submit_draw_instanced = record_draw_instanced;
    backend.shadow_begin = record_shadow_begin;
    backend.shadow_draw = record_shadow_draw;
    backend.shadow_end = record_shadow_end;

    rt_canvas3d canvas;
    vgfx3d_rendertarget_t shadow_rt = {};
    float shadow_depth[16] = {};
    rt_light3d light = {};
    init_fake_canvas(&canvas, &backend);
    reset_recorded_instancing();
    reset_shadow_counts();

    shadow_rt.depth_buf = shadow_depth;
    shadow_rt.width = 4;
    shadow_rt.height = 4;
    canvas.shadow_rt = &shadow_rt;
    canvas.shadows_enabled = 1;
    canvas.shadow_bias = 0.0025f;
    light.type = 0;
    light.direction[1] = -1.0;
    light.color[0] = 1.0;
    light.color[1] = 1.0;
    light.color[2] = 1.0;
    light.intensity = 1.0;
    canvas.lights[0] = &light;

    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *batch = rt_instbatch3d_new(mesh, material);
    void *t0 = rt_mat4_identity();
    void *t1 = rt_mat4_identity();
    ((mat4_impl *)t0)->m[3] = -0.5;
    ((mat4_impl *)t1)->m[3] = 0.5;
    rt_instbatch3d_add(batch, t0);
    rt_instbatch3d_add(batch, t1);

    reset_canvas_frame(&canvas, 1);
    rt_canvas3d_draw_instanced(&canvas, batch);
    rt_canvas3d_end(&canvas);

    EXPECT_TRUE(shadow_begin_calls == 1, "Instanced draws participate in the shadow-map pass");
    EXPECT_TRUE(shadow_draw_calls == 2,
                "Shadow rendering expands instanced draws so each visible instance casts a shadow");
    EXPECT_TRUE(shadow_end_calls == 1, "Instanced shadow rendering finalizes the shadow pass once");

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

static void test_gpu_postfx_state_latches_across_overlay_pass(void) {
    vgfx3d_backend_t backend = {};
    rt_canvas3d canvas;
    rt_camera3d camera = {};
    void *mesh = make_test_mesh();
    void *material = rt_material3d_new();
    void *transform = rt_mat4_identity();
    void *fx = rt_postfx3d_new();

    backend.name = "d3d11";
    backend.begin_frame = record_begin_frame;
    backend.submit_draw = noop_draw;
    backend.end_frame = noop_end_frame;
    backend.present_postfx = noop_present_postfx;
    backend.set_gpu_postfx_enabled = record_set_gpu_postfx_enabled;
    backend.set_gpu_postfx_snapshot = record_set_gpu_postfx_snapshot;

    init_fake_canvas(&canvas, &backend);
    canvas.in_frame = 0;
    canvas.width = 320;
    canvas.height = 180;
    set_identity4x4d(camera.view);
    set_identity4x4d(camera.projection);
    camera.eye[2] = 3.0;
    rt_postfx3d_add_bloom(fx, 0.8, 1.5, 2);
    rt_canvas3d_set_post_fx(&canvas, fx);
    reset_postfx_records();

    rt_canvas3d_begin(&canvas, &camera);
    rt_canvas3d_draw_mesh(&canvas, mesh, transform, material);
    rt_canvas3d_draw_rect2d(&canvas, 10, 20, 30, 40, 0xFFFFFFFF);
    rt_canvas3d_end(&canvas);

    EXPECT_TRUE(begin_frame_calls == 2,
                "Canvas3D issues separate backend begin_frame calls for scene and overlay passes");
    EXPECT_TRUE(begin_frame_params[0].load_existing_color == 0,
                "Main scene pass starts with a fresh color target");
    EXPECT_TRUE(begin_frame_params[1].load_existing_color == 1,
                "Overlay pass preserves the main scene color");
    EXPECT_TRUE(set_gpu_postfx_enabled_calls == 2,
                "Canvas3D reapplies the latched GPU postfx state for the overlay pass");
    EXPECT_TRUE(set_gpu_postfx_enabled_values[0] == 1 && set_gpu_postfx_enabled_values[1] == 1,
                "Canvas3D keeps GPU postfx enabled across both backend passes");
    EXPECT_TRUE(set_gpu_postfx_snapshot_calls == 2,
                "Canvas3D forwards the latched postfx snapshot to both backend passes");
    EXPECT_TRUE(set_gpu_postfx_snapshot_present[0] == 1 && set_gpu_postfx_snapshot_present[1] == 1,
                "Canvas3D keeps the postfx snapshot alive across the overlay pass");
    EXPECT_TRUE(set_gpu_postfx_snapshots[0].bloom_enabled == 1 &&
                    set_gpu_postfx_snapshots[1].bloom_threshold == 0.8f &&
                    set_gpu_postfx_snapshots[1].bloom_intensity == 1.5f,
                "Canvas3D forwards the same latched postfx values to both backend passes");
    EXPECT_TRUE(canvas.frame_postfx_state_latched == 1,
                "Canvas3D keeps the frame postfx snapshot latched until Flip");
    EXPECT_TRUE(canvas.frame_gpu_postfx_enabled == 1,
                "Canvas3D records the frame as GPU-postfx-enabled");
    EXPECT_TRUE(canvas.frame_postfx_snapshot.bloom_enabled == 1 &&
                    canvas.frame_postfx_snapshot.bloom_threshold == 0.8f &&
                    canvas.frame_postfx_snapshot.bloom_intensity == 1.5f,
                "Canvas3D preserves the original postfx snapshot across the overlay pass");

    cleanup_fake_canvas(&canvas);
}

int main() {
    test_gpu_skinning_bypass_for_opengl();
    test_gpu_skinning_bypass_for_d3d11();
    test_gpu_skinning_bypass_for_metal();
    test_cpu_skinning_fallback_for_software();
    test_gpu_morph_payload_for_opengl();
    test_gpu_morph_payload_for_d3d11();
    test_gpu_morph_payload_for_metal();
    test_gpu_morph_normal_payload_for_opengl();
    test_gpu_morph_normal_payload_for_d3d11();
    test_gpu_morph_normal_payload_for_metal();
    test_attached_morph_targets_route_through_draw_mesh();
    test_cpu_morph_fallback_for_software();
    test_env_map_payload_forwarded();
    test_backend_skybox_hook_used();
    test_static_mesh_geometry_identity_forwarded();
    test_rect2d_queues_overlay_pass();
    test_transform_history_forwarded_for_motion_blur();
    test_morph_weight_history_forwarded();
    test_skinning_palette_history_forwarded();
    test_instanced_transform_history_forwarded();
    test_instanced_material_payload_forwarded();
    test_pbr_material_payload_forwarded();
    test_instanced_runtime_culls_outside_frustum();
    test_instanced_shadow_pass_includes_instances();
    test_screenshot_prefers_backend_readback();
    test_gpu_postfx_state_latches_across_overlay_pass();

    std::printf("Canvas3D GPU path tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
