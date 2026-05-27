//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_canvas3d_internal.h"
#include "rt_particles3d.h"
}

#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::jmp_buf g_env;
const char *g_last_trap = nullptr;
bool g_expect_trap = false;
constexpr int kAlphaModeOpaque = 0;
constexpr int kAlphaModeBlend = 2;
int g_draw_mesh_calls = 0;
int g_draw_mesh_matrix_keyed_calls = 0;
int g_last_mesh_vertex_count = 0;
int g_last_mesh_index_count = 0;
double g_last_mesh_quad_z[16] = {0.0};
double g_keyed_draw_z[16] = {0.0};
double g_keyed_draw_alpha[16] = {0.0};
int g_keyed_draw_additive[16] = {0};
double g_last_draw_alpha = 0.0;
int g_last_draw_additive = 0;
int g_last_draw_alpha_mode = 0;

struct StubMaterial {
    void *vptr = nullptr;
    double diffuse[4] = {0.0, 0.0, 0.0, 1.0};
    double specular[3] = {0.0, 0.0, 0.0};
    double shininess = 0.0;
    int32_t workflow = 0;
    void *texture = nullptr;
    void *normal_map = nullptr;
    void *specular_map = nullptr;
    void *emissive_map = nullptr;
    void *metallic_roughness_map = nullptr;
    void *ao_map = nullptr;
    double emissive[3] = {0.0, 0.0, 0.0};
    double metallic = 0.0;
    double roughness = 0.0;
    double ao = 0.0;
    double emissive_intensity = 0.0;
    double normal_scale = 1.0;
    double alpha = 1.0;
    double alpha_cutoff = 0.5;
    void *env_map = nullptr;
    double reflectivity = 0.0;
    int8_t unlit = 0;
    int8_t double_sided = 0;
    int8_t additive_blend = 0;
    int32_t alpha_mode = kAlphaModeOpaque;
    int32_t shading_model = 0;
    double custom_params[8] = {0.0};
};

struct ParticlesView {
    void *vptr;
    void *particles;
    int32_t count;
    int32_t max_particles;
    double position[3];
    double emit_dir[3];
    double emit_spread;
    double speed_min;
    double speed_max;
    double life_min;
    double life_max;
    double size_start;
    double size_end;
    double gravity[3];
    float color_start[3];
    float color_end[3];
    double alpha_start;
    double alpha_end;
    double rate;
    double accumulator;
    int8_t emitting;
    int8_t additive_blend;
    void *texture;
    int32_t emitter_shape;
    double emitter_size[3];
    uint32_t prng_state;
    void *cached_material;
};

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" int64_t rt_obj_class_id(void *) {
    return RT_G3D_PARTICLES3D_CLASS_ID;
}

extern "C" int8_t rt_obj_is_instance(void *obj, int64_t class_id, size_t) {
    return obj && rt_obj_class_id(obj) == class_id;
}

extern "C" int8_t rt_heap_is_payload(void *) {
    return 0;
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" void rt_obj_retain_maybe(void *) {}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" int rt_canvas3d_add_temp_buffer(void *, void *) {
    return 1;
}

extern "C" int rt_canvas3d_remove_temp_buffer(void *, void *) {
    return 1;
}

extern "C" void *rt_material3d_new(void) {
    return std::calloc(1, sizeof(StubMaterial));
}

extern "C" void rt_material3d_set_color(void *m, double r, double g, double b) {
    StubMaterial *mat = static_cast<StubMaterial *>(m);
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
    mat->diffuse[3] = mat->alpha;
}

extern "C" void rt_material3d_set_unlit(void *m, int8_t enabled) {
    static_cast<StubMaterial *>(m)->unlit = enabled;
}

extern "C" void rt_material3d_set_alpha(void *m, double a) {
    static_cast<StubMaterial *>(m)->alpha = a;
}

extern "C" void rt_material3d_set_alpha_mode(void *m, int64_t mode) {
    static_cast<StubMaterial *>(m)->alpha_mode = static_cast<int32_t>(mode);
}

extern "C" void rt_material3d_set_texture(void *m, void *tex) {
    static_cast<StubMaterial *>(m)->texture = tex;
}

extern "C" void *rt_mat4_identity(void) {
    static double identity[16] = {
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    return identity;
}

extern "C" void rt_canvas3d_draw_mesh(void *, void *mesh, void *, void *material) {
    rt_mesh3d *m = static_cast<rt_mesh3d *>(mesh);
    g_draw_mesh_calls++;
    g_last_mesh_vertex_count = m ? (int)m->vertex_count : 0;
    g_last_mesh_index_count = m ? (int)m->index_count : 0;
    std::memset(g_last_mesh_quad_z, 0, sizeof(g_last_mesh_quad_z));
    if (m && m->vertices) {
        int quad_count = m->vertex_count / 4;
        if (quad_count > (int)(sizeof(g_last_mesh_quad_z) / sizeof(g_last_mesh_quad_z[0])))
            quad_count = (int)(sizeof(g_last_mesh_quad_z) / sizeof(g_last_mesh_quad_z[0]));
        for (int i = 0; i < quad_count; ++i)
            g_last_mesh_quad_z[i] = m->vertices[i * 4].pos[2];
    }
    g_last_draw_alpha = material ? static_cast<StubMaterial *>(material)->alpha : 0.0;
    g_last_draw_additive = material ? static_cast<StubMaterial *>(material)->additive_blend : 0;
    g_last_draw_alpha_mode = material ? static_cast<StubMaterial *>(material)->alpha_mode : 0;
}

extern "C" void rt_canvas3d_draw_mesh_matrix(void *canvas,
                                             void *mesh,
                                             const double *,
                                             void *material) {
    rt_canvas3d_draw_mesh(canvas, mesh, nullptr, material);
}

extern "C" void rt_canvas3d_draw_mesh_matrix_keyed(void *,
                                                   void *,
                                                   const double *model_matrix,
                                                   void *material,
                                                   const void *,
                                                   const float *,
                                                   const float *) {
    int draw_index = g_draw_mesh_matrix_keyed_calls++;
    if (draw_index < (int)(sizeof(g_keyed_draw_z) / sizeof(g_keyed_draw_z[0]))) {
        g_keyed_draw_z[draw_index] = model_matrix ? model_matrix[11] : 0.0;
        g_keyed_draw_alpha[draw_index] = material ? static_cast<StubMaterial *>(material)->alpha : 0.0;
        g_keyed_draw_additive[draw_index] =
            material ? static_cast<StubMaterial *>(material)->additive_blend : 0;
    }
}

extern "C" void rt_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_env, 1);
    std::abort();
}

static void expect_trap_on_invalid_capacity() {
    g_expect_trap = true;
    if (setjmp(g_env) == 0) {
        (void)rt_particles3d_new(0);
        assert(false && "expected rt_trap");
    }
    g_expect_trap = false;
    assert(g_last_trap != nullptr);
    assert(std::strstr(g_last_trap, "max_particles") != nullptr);
}

static void test_burst_and_clear() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);
    assert(rt_particles3d_get_count(ps) == 0);
    assert(rt_particles3d_get_emitting(ps) == 0);

    rt_particles3d_burst(ps, 3);
    assert(rt_particles3d_get_count(ps) == 3);

    rt_particles3d_clear(ps);
    assert(rt_particles3d_get_count(ps) == 0);
}

static void test_burst_caps_to_available_pool() {
    void *ps = rt_particles3d_new(4);
    assert(ps != nullptr);

    rt_particles3d_burst(ps, 100);
    assert(rt_particles3d_get_count(ps) == 4);

    rt_particles3d_burst(ps, 100);
    assert(rt_particles3d_get_count(ps) == 4);
}

static void test_start_stop_and_update_spawns_particles() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);

    rt_particles3d_set_rate(ps, 4.0);
    rt_particles3d_start(ps);
    assert(rt_particles3d_get_emitting(ps) == 1);

    rt_particles3d_update(ps, 0.5);
    assert(rt_particles3d_get_count(ps) == 2);

    rt_particles3d_stop(ps);
    assert(rt_particles3d_get_emitting(ps) == 0);
    int64_t count_after_stop = rt_particles3d_get_count(ps);
    rt_particles3d_update(ps, 0.5);
    assert(rt_particles3d_get_count(ps) <= count_after_stop);
}

static void test_particles_expire_after_lifetime() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);

    rt_particles3d_set_lifetime(ps, 0.1, 0.1);
    rt_particles3d_burst(ps, 4);
    assert(rt_particles3d_get_count(ps) == 4);

    rt_particles3d_update(ps, 0.2);
    assert(rt_particles3d_get_count(ps) == 0);
}

static void test_update_clamps_large_finite_delta_time() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);

    rt_particles3d_set_lifetime(ps, 10.0, 10.0);
    rt_particles3d_burst(ps, 1);
    rt_particles3d_update(ps, 1.0e300);
    assert(rt_particles3d_get_count(ps) == 1);
}

static void test_setters_sanitize_nonfinite_ranges() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);
    ParticlesView *view = static_cast<ParticlesView *>(ps);

    rt_particles3d_set_position(ps, NAN, 2.0, INFINITY);
    assert(view->position[0] == 0.0);
    assert(view->position[1] == 2.0);
    assert(view->position[2] == 0.0);

    rt_particles3d_set_direction(ps, NAN, 0.0, INFINITY, INFINITY);
    assert(std::fabs(view->emit_dir[0]) < 1e-9);
    assert(std::fabs(view->emit_dir[1] - 1.0) < 1e-9);
    assert(std::fabs(view->emit_dir[2]) < 1e-9);
    assert(view->emit_spread == 0.0);
    rt_particles3d_set_direction(ps, 1.0, 0.0, 0.0, 90.0);
    assert(std::fabs(view->emit_dir[0] - 1.0) < 1e-9);
    assert(std::fabs(view->emit_spread - 1.5707963267948966) < 1e-9);
    rt_particles3d_set_direction(ps, 0.0, 1.0, 0.0, 720.0);
    assert(std::fabs(view->emit_spread - 3.1415926535897932) < 1e-9);

    rt_particles3d_set_speed(ps, 4.0, -2.0);
    assert(view->speed_min == 0.0);
    assert(view->speed_max == 4.0);

    rt_particles3d_set_lifetime(ps, NAN, -1.0);
    assert(view->life_min >= 0.01);
    assert(view->life_max >= 0.01);

    rt_particles3d_set_size(ps, NAN, -3.0);
    assert(view->size_start == 0.0);
    assert(view->size_end == 0.0);

    rt_particles3d_set_gravity(ps, INFINITY, -9.0, NAN);
    assert(view->gravity[0] == 0.0);
    assert(view->gravity[1] == -9.0);
    assert(view->gravity[2] == 0.0);

    rt_particles3d_set_alpha(ps, -1.0, 2.0);
    assert(view->alpha_start == 0.0);
    assert(view->alpha_end == 1.0);

    rt_particles3d_set_rate(ps, INFINITY);
    assert(view->rate == 0.0);
    rt_particles3d_set_additive(ps, 7);
    assert(view->additive_blend == 1);

    rt_particles3d_set_emitter_shape(ps, 99);
    assert(view->emitter_shape == 2);
    rt_particles3d_set_emitter_shape(ps, -1);
    assert(view->emitter_shape == 0);
    rt_particles3d_set_emitter_size(ps, NAN, -2.0, INFINITY);
    assert(view->emitter_size[0] == 0.0);
    assert(view->emitter_size[1] == 2.0);
    assert(view->emitter_size[2] == 0.0);
}

static void reset_draw_records() {
    g_draw_mesh_calls = 0;
    g_draw_mesh_matrix_keyed_calls = 0;
    g_last_mesh_vertex_count = 0;
    g_last_mesh_index_count = 0;
    g_last_draw_alpha = 0.0;
    g_last_draw_additive = 0;
    g_last_draw_alpha_mode = 0;
    std::memset(g_last_mesh_quad_z, 0, sizeof(g_last_mesh_quad_z));
    std::memset(g_keyed_draw_z, 0, sizeof(g_keyed_draw_z));
    std::memset(g_keyed_draw_alpha, 0, sizeof(g_keyed_draw_alpha));
    std::memset(g_keyed_draw_additive, 0, sizeof(g_keyed_draw_additive));
}

static rt_camera3d make_test_camera() {
    rt_camera3d cam = {};
    cam.view[0] = 1.0;
    cam.view[5] = 1.0;
    cam.view[10] = 1.0;
    cam.view[15] = 1.0;
    cam.projection[0] = 1.0;
    cam.projection[5] = 1.0;
    cam.projection[10] = 1.0;
    cam.projection[15] = 1.0;
    cam.eye[0] = 0.0;
    cam.eye[1] = 0.0;
    cam.eye[2] = 0.0;
    return cam;
}

static void test_draw_batches_additive_and_alpha_particles() {
    void *ps = rt_particles3d_new(8);
    rt_camera3d cam = make_test_camera();
    assert(ps != nullptr);

    rt_particles3d_set_position(ps, 0.0, 0.0, 1.0);
    rt_particles3d_burst(ps, 1);
    rt_particles3d_set_position(ps, 0.0, 0.0, 5.0);
    rt_particles3d_burst(ps, 1);
    rt_particles3d_set_position(ps, 0.0, 0.0, 3.0);
    rt_particles3d_burst(ps, 1);
    assert(rt_particles3d_get_count(ps) == 3);

    rt_particles3d_set_additive(ps, 1);
    reset_draw_records();
    rt_particles3d_draw(ps, reinterpret_cast<void *>(1), &cam);
    assert(g_draw_mesh_calls == 1);
    assert(g_draw_mesh_matrix_keyed_calls == 0);
    assert(g_last_mesh_vertex_count == 12);
    assert(g_last_mesh_index_count == 18);
    assert(std::fabs(g_last_mesh_quad_z[0] - 1.0) < 1e-6);
    assert(std::fabs(g_last_draw_alpha - 1.0) < 1e-6);
    assert(g_last_draw_additive == 1);
    assert(g_last_draw_alpha_mode == kAlphaModeBlend);

    rt_particles3d_set_additive(ps, 0);
    reset_draw_records();
    rt_particles3d_draw(ps, reinterpret_cast<void *>(1), &cam);
    assert(g_draw_mesh_calls == 1);
    assert(g_draw_mesh_matrix_keyed_calls == 0);
    assert(g_last_mesh_vertex_count == 12);
    assert(g_last_mesh_index_count == 18);
    assert(std::fabs(g_last_mesh_quad_z[0] - 5.0) < 1e-6);
    assert(std::fabs(g_last_mesh_quad_z[1] - 3.0) < 1e-6);
    assert(std::fabs(g_last_mesh_quad_z[2] - 1.0) < 1e-6);
    assert(std::fabs(g_last_draw_alpha - 1.0) < 1e-6);
    assert(g_last_draw_additive == 0);
    assert(g_last_draw_alpha_mode == kAlphaModeBlend);
}

int main() {
    expect_trap_on_invalid_capacity();
    test_burst_and_clear();
    test_burst_caps_to_available_pool();
    test_start_stop_and_update_spawns_particles();
    test_particles_expire_after_lifetime();
    test_update_clamps_large_finite_delta_time();
    test_setters_sanitize_nonfinite_ranges();
    test_draw_batches_additive_and_alpha_particles();
    std::printf("RTParticles3DContractTests passed.\n");
    return 0;
}
