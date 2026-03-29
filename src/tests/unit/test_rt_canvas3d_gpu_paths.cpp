#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
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
static vgfx3d_backend_t kSoftwareBackend = make_backend("software");

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

int main() {
    test_gpu_skinning_bypass_for_opengl();
    test_cpu_skinning_fallback_for_software();
    test_gpu_morph_payload_for_opengl();
    test_cpu_morph_fallback_for_software();

    std::printf("Canvas3D GPU path tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
