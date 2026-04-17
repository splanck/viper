//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_canvas3d_internal.h"
#include "rt_water3d.h"

#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

struct StubMaterial {
    double alpha = 0.0;
    double reflectivity = 0.0;
    void *texture = nullptr;
    void *normal_map = nullptr;
    void *env_map = nullptr;
};

int g_draw_mesh_calls = 0;
int g_backface_cull_off = 0;
int g_backface_cull_on = 0;
int g_backface_cull_values[8] = {0};
int g_backface_cull_call_count = 0;

struct WaterWaveView {
    double dir[2];
    double speed;
    double amplitude;
    double frequency;
};

struct WaterView {
    void *vptr;
    double width;
    double depth;
    double height;
    double wave_speed;
    double wave_amplitude;
    double wave_frequency;
    double color[3];
    double alpha;
    double time;
    void *mesh;
    void *material;
    void *texture;
    void *normal_map;
    void *env_map;
    double reflectivity;
    WaterWaveView waves[8];
    int32_t wave_count;
    int32_t resolution;
};

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" void rt_obj_retain_maybe(void *) {}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *p) {
    std::free(p);
}

extern "C" void rt_trap(const char *) {
    std::abort();
}

extern "C" void *rt_mesh3d_new(void) {
    return std::calloc(1, sizeof(rt_mesh3d));
}

extern "C" void rt_mesh3d_clear(void *m) {
    rt_mesh3d *mesh = static_cast<rt_mesh3d *>(m);
    if (!mesh)
        return;
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

extern "C" void rt_mesh3d_add_vertex(
    void *m, double, double, double, double, double, double, double, double) {
    static_cast<rt_mesh3d *>(m)->vertex_count++;
}

extern "C" void rt_mesh3d_add_triangle(void *m, int64_t, int64_t, int64_t) {
    static_cast<rt_mesh3d *>(m)->index_count += 3;
}

extern "C" void *rt_mat4_identity(void) {
    static double identity[16] = {
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    return identity;
}

extern "C" void rt_canvas3d_draw_mesh(void *, void *, void *, void *) {
    g_draw_mesh_calls++;
}

extern "C" void *rt_material3d_new_color(double, double, double) {
    return std::calloc(1, sizeof(StubMaterial));
}

extern "C" void rt_material3d_set_alpha(void *m, double a) {
    static_cast<StubMaterial *>(m)->alpha = a;
}

extern "C" void rt_material3d_set_shininess(void *, double) {}

extern "C" void rt_material3d_set_texture(void *m, void *tex) {
    static_cast<StubMaterial *>(m)->texture = tex;
}

extern "C" void rt_material3d_set_normal_map(void *m, void *tex) {
    static_cast<StubMaterial *>(m)->normal_map = tex;
}

extern "C" void rt_material3d_set_env_map(void *m, void *cubemap) {
    static_cast<StubMaterial *>(m)->env_map = cubemap;
}

extern "C" void rt_material3d_set_reflectivity(void *m, double r) {
    static_cast<StubMaterial *>(m)->reflectivity = r;
}

extern "C" void rt_material3d_set_color(void *, double, double, double) {}

extern "C" void rt_canvas3d_set_backface_cull(void *canvas, int8_t enabled) {
    if (canvas)
        static_cast<rt_canvas3d *>(canvas)->backface_cull = enabled;
    if (g_backface_cull_call_count < (int)(sizeof(g_backface_cull_values) /
                                           sizeof(g_backface_cull_values[0]))) {
        g_backface_cull_values[g_backface_cull_call_count] = enabled ? 1 : 0;
    }
    g_backface_cull_call_count++;
    if (enabled)
        g_backface_cull_on++;
    else
        g_backface_cull_off++;
}

static void test_resolution_clamp_drives_mesh_size() {
    void *water = rt_water3d_new(10.0, 10.0);
    assert(water != nullptr);

    rt_water3d_set_resolution(water, 4);
    rt_water3d_update(water, 0.1);

    auto *mesh = static_cast<rt_mesh3d *>(static_cast<WaterView *>(water)->mesh);
    assert(mesh != nullptr);
    assert(mesh->vertex_count == 81);
    assert(mesh->index_count == 8 * 8 * 6);
}

static void test_material_wiring_and_reflectivity() {
    void *water = rt_water3d_new(8.0, 8.0);
    int dummy_texture = 1;
    int dummy_normal = 2;
    int dummy_env = 3;

    rt_water3d_set_texture(water, &dummy_texture);
    rt_water3d_set_normal_map(water, &dummy_normal);
    rt_water3d_set_reflectivity(water, 0.75);
    rt_water3d_update(water, 0.1);

    auto *material = static_cast<StubMaterial *>(static_cast<WaterView *>(water)->material);
    assert(material != nullptr);
    assert(material->texture == &dummy_texture);
    assert(material->normal_map == &dummy_normal);
    assert(material->reflectivity == 0.0);

    rt_water3d_set_env_map(water, &dummy_env);
    rt_water3d_update(water, 0.1);
    assert(material->env_map == &dummy_env);
    assert(material->reflectivity == 0.75);

    rt_water3d_set_reflectivity(water, 5.0);
    rt_water3d_update(water, 0.1);
    assert(material->reflectivity == 1.0);
}

static void test_draw_restores_backface_cull_state() {
    void *water = rt_water3d_new(4.0, 4.0);
    rt_canvas3d canvas = {};
    rt_water3d_update(water, 0.1);

    canvas.backface_cull = 0;
    g_draw_mesh_calls = 0;
    g_backface_cull_off = 0;
    g_backface_cull_on = 0;
    g_backface_cull_call_count = 0;
    std::memset(g_backface_cull_values, 0, sizeof(g_backface_cull_values));
    rt_canvas3d_draw_water(&canvas, water, nullptr);

    assert(g_draw_mesh_calls == 1);
    assert(g_backface_cull_off == 2);
    assert(g_backface_cull_on == 0);
    assert(g_backface_cull_call_count == 2);
    assert(g_backface_cull_values[0] == 0);
    assert(g_backface_cull_values[1] == 0);
    assert(canvas.backface_cull == 0);
}

int main() {
    test_resolution_clamp_drives_mesh_size();
    test_material_wiring_and_reflectivity();
    test_draw_restores_backface_cull_state();
    std::printf("RTWater3DContractTests passed.\n");
    return 0;
}
