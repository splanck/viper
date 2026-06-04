//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTVegetation3DContractTests.cpp
// Purpose: Isolated behavioral contract test for the Graphics3D vegetation
//   subsystem (src/runtime/graphics/3d/world/rt_vegetation3d.c). Pins blade-mesh
//   construction, deterministic density-gated population (LCG seed = 42),
//   wind-time advance, distance LOD culling, and the single instanced draw
//   submission — without a GPU backend, GC, or mesh allocator.
// Key invariants:
//   - rt_vegetation3d.c is compiled against the stubs below; only its own logic
//     is under test. Stubs mirror the private struct as VegetationView and the
//     terrain handle as FakeTerrain.
//   - Population is deterministic: a no-density populate(count) yields exactly
//     `count` blades; a full-density (R=255) map keeps all; a zero map keeps none.
// Ownership/Lifetime:
//   - Fixtures (terrain, density Pixels) are caller-owned and never freed by the
//     subsystem in these tests (no finalize/reassign paths exercised).
// Links: rt_vegetation3d.c, RTParticles3DContractTests.cpp (sibling pattern)
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_canvas3d_internal.h"
#include "rt_pixels_internal.h"
#include "rt_vegetation3d.h"
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

// Tracked handle sets so rt_obj_class_id reports the right class per pointer.
void *g_meshes[128] = {nullptr};
int g_mesh_count = 0;
void *g_materials[128] = {nullptr};
int g_material_count = 0;
void *g_terrain = nullptr;

// Instanced-draw interception records.
int g_instanced_batch_calls = 0;
int g_last_instance_count = 0;
int g_cull_during_batch = -1;

// White-box mirror of the private rt_vegetation3d struct (rt_vegetation3d.c).
struct VegetationView {
    void *vptr;
    void *blade_mesh;
    void *blade_material;
    double blade_width;
    double blade_height;
    double size_variation;
    float *base_transforms;
    float *positions;
    int32_t total_count;
    int32_t capacity;
    void *density_map;
    double wind_speed;
    double wind_strength;
    double wind_turbulence;
    double time;
    float lod_near;
    float lod_far;
    float *visible_transforms;
    int32_t visible_count;
    int32_t visible_capacity;
};

// Layout the local terrain_view in rt_vegetation3d_populate casts the handle to.
struct FakeTerrain {
    void *vptr;
    float *heights;
    int32_t width;
    int32_t depth;
    double scale[3];
};

bool tracked(void *const *items, int count, void *value) {
    for (int i = 0; i < count; i++) {
        if (items[i] == value)
            return true;
    }
    return false;
}

void track(void **items, int *count, int capacity, void *value) {
    if (!value || tracked(items, *count, value))
        return;
    if (*count < capacity)
        items[(*count)++] = value;
}

void reset_draw_records() {
    g_instanced_batch_calls = 0;
    g_last_instance_count = 0;
    g_cull_during_batch = -1;
}

// Build a density Pixels fixture whose R channel is `r` for every texel.
void fill_density(rt_pixels_impl *px, uint32_t *data, int64_t w, int64_t h, uint32_t r) {
    px->width = w;
    px->height = h;
    px->data = data;
    px->generation = 0;
    px->cache_identity = 0;
    uint32_t texel = (r & 0xFFu) << 24; // RRGGBBAA, R channel only
    for (int64_t i = 0; i < w * h; i++)
        data[i] = texel;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" int64_t rt_obj_class_id(void *p) {
    if (!p)
        return 0;
    if (p == g_terrain)
        return RT_G3D_TERRAIN3D_CLASS_ID;
    if (tracked(g_meshes, g_mesh_count, p))
        return RT_G3D_MESH3D_CLASS_ID;
    if (tracked(g_materials, g_material_count, p))
        return RT_G3D_MATERIAL3D_CLASS_ID;
    return RT_G3D_VEGETATION3D_CLASS_ID;
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

extern "C" void rt_obj_free(void *p) {
    std::free(p);
}

extern "C" void rt_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_env, 1);
    std::abort();
}

extern "C" void *rt_mesh3d_new(void) {
    void *m = std::calloc(1, sizeof(rt_mesh3d));
    track(g_meshes, &g_mesh_count, 128, m);
    return m;
}

extern "C" void rt_mesh3d_add_vertex(
    void *m, double, double, double, double, double, double, double, double) {
    if (m)
        static_cast<rt_mesh3d *>(m)->vertex_count++;
}

extern "C" void rt_mesh3d_add_triangle(void *m, int64_t, int64_t, int64_t) {
    if (m)
        static_cast<rt_mesh3d *>(m)->index_count += 3;
}

extern "C" void rt_mesh3d_clear(void *m) {
    if (!m)
        return;
    rt_mesh3d *mesh = static_cast<rt_mesh3d *>(m);
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

extern "C" void *rt_material3d_new(void) {
    void *mat = std::calloc(1, 64); // opaque to vegetation; only identity matters
    track(g_materials, &g_material_count, 128, mat);
    return mat;
}

extern "C" void rt_material3d_set_texture(void *, void *) {}

extern "C" void rt_material3d_set_unlit(void *, int8_t) {}

extern "C" double rt_terrain3d_get_height_at(void *, double, double) {
    return 0.0;
}

extern "C" void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                                  void *,
                                                  void *,
                                                  const float *,
                                                  int32_t instance_count,
                                                  const float *,
                                                  int8_t) {
    g_instanced_batch_calls++;
    g_last_instance_count = static_cast<int>(instance_count);
    g_cull_during_batch = canvas_obj ? static_cast<rt_canvas3d *>(canvas_obj)->backface_cull : -1;
}

extern "C" void rt_canvas3d_queue_instanced_batch_bounds(void *canvas_obj,
                                                         void *mesh_obj,
                                                         void *material_obj,
                                                         const float *instance_matrices,
                                                         int32_t instance_count,
                                                         const float *prev_instance_matrices,
                                                         int8_t has_prev_instance_matrices,
                                                         const float *,
                                                         const float *,
                                                         int8_t,
                                                         int8_t) {
    rt_canvas3d_queue_instanced_batch(canvas_obj,
                                      mesh_obj,
                                      material_obj,
                                      instance_matrices,
                                      instance_count,
                                      prev_instance_matrices,
                                      has_prev_instance_matrices);
}

extern "C" void vgfx3d_compute_mesh_aabb(const void *,
                                         uint32_t,
                                         uint32_t,
                                         float out_min[3],
                                         float out_max[3]) {
    out_min[0] = out_min[1] = out_min[2] = 0.0f;
    out_max[0] = out_max[1] = out_max[2] = 1.0f;
}

static FakeTerrain make_terrain(int32_t w, int32_t d) {
    FakeTerrain t = {};
    t.vptr = nullptr;
    t.heights = nullptr; // populate samples height via rt_terrain3d_get_height_at
    t.width = w;
    t.depth = d;
    t.scale[0] = 1.0;
    t.scale[1] = 1.0;
    t.scale[2] = 1.0;
    return t;
}

static rt_canvas3d make_frame_canvas() {
    static int dummy_backend_storage = 0;
    rt_canvas3d canvas = {};
    canvas.in_frame = 1;
    canvas.frame_is_2d = 0;
    canvas.backface_cull = 1;
    canvas.backend = reinterpret_cast<const vgfx3d_backend_t *>(&dummy_backend_storage);
    return canvas;
}

static void test_new_builds_blade_mesh_and_defaults() {
    void *veg = rt_vegetation3d_new(nullptr);
    assert(veg != nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);

    assert(v->blade_mesh != nullptr);
    assert(v->blade_material != nullptr);

    // Cross-billboard blade: 2 quads = 8 vertices, 4 triangles = 12 indices.
    rt_mesh3d *mesh = static_cast<rt_mesh3d *>(v->blade_mesh);
    assert(mesh->vertex_count == 8);
    assert(mesh->index_count == 12);

    assert(v->blade_width == 0.4);
    assert(v->blade_height == 1.2);
    assert(v->size_variation == 0.3);
    assert(v->wind_speed == 2.0);
    assert(v->wind_strength == 0.15);
    assert(v->lod_near == 40.0f);
    assert(v->lod_far == 100.0f);
    assert(v->total_count == 0);
    assert(v->visible_count == 0);
}

static void test_populate_scatters_requested_count() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 100);
    assert(v->total_count == 100); // no density map => every candidate placed

    rt_vegetation3d_populate(veg, &terrain, 250);
    assert(v->total_count == 250); // re-populate resets and refills deterministically
    g_terrain = nullptr;
}

static void test_populate_nonpositive_count_clears() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 0);
    assert(v->total_count == 0);
    rt_vegetation3d_populate(veg, &terrain, -5);
    assert(v->total_count == 0);
    g_terrain = nullptr;
}

static void test_populate_overflow_count_traps() {
    void *veg = rt_vegetation3d_new(nullptr);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    g_last_trap = nullptr;
    g_expect_trap = true;
    if (setjmp(g_env) == 0) {
        rt_vegetation3d_populate(veg, &terrain, 2000001); // > VEGETATION3D_MAX_BLADES
        assert(false && "expected rt_trap for oversized blade count");
    }
    g_expect_trap = false;
    assert(g_last_trap != nullptr);
    assert(std::strstr(g_last_trap, "count exceeds supported range") != nullptr);
    g_terrain = nullptr;
}

static void test_density_map_gates_population() {
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    uint32_t data[64];
    rt_pixels_impl density = {};

    // Full density (R=255) keeps every candidate.
    void *full = rt_vegetation3d_new(nullptr);
    VegetationView *vf = static_cast<VegetationView *>(full);
    fill_density(&density, data, 8, 8, 255);
    rt_vegetation3d_set_density_map(full, &density);
    rt_vegetation3d_populate(full, &terrain, 200);
    assert(vf->total_count == 200);

    // Zero density (R=0) rejects every candidate.
    void *empty = rt_vegetation3d_new(nullptr);
    VegetationView *ve = static_cast<VegetationView *>(empty);
    rt_pixels_impl zero = {};
    uint32_t zero_data[64];
    fill_density(&zero, zero_data, 8, 8, 0);
    rt_vegetation3d_set_density_map(empty, &zero);
    rt_vegetation3d_populate(empty, &terrain, 200);
    assert(ve->total_count == 0);
    g_terrain = nullptr;
}

static void test_update_advances_time_and_keeps_near_visible() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 100);
    rt_vegetation3d_set_lod_distances(veg, 1000.0, 2000.0); // everything inside near band
    rt_vegetation3d_update(veg, 0.5, 32.0, 0.0, 32.0);      // camera at terrain center

    assert(std::fabs(v->time - 0.5) < 1e-9);
    assert(v->total_count == 100);
    assert(v->visible_count == v->total_count); // no thinning within near distance
    g_terrain = nullptr;
}

static void test_update_far_camera_culls_all() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 100);
    rt_vegetation3d_set_lod_distances(veg, 10.0, 20.0);
    rt_vegetation3d_update(veg, 0.016, 100000.0, 0.0, 100000.0); // far beyond lod_far

    assert(v->visible_count == 0);
    g_terrain = nullptr;
}

static void test_draw_submits_one_instanced_batch_without_mutating_cull() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 64);
    rt_vegetation3d_set_lod_distances(veg, 1000.0, 2000.0);
    rt_vegetation3d_update(veg, 0.016, 32.0, 0.0, 32.0);
    assert(v->visible_count > 0);

    rt_canvas3d canvas = make_frame_canvas();
    reset_draw_records();
    rt_canvas3d_draw_vegetation(&canvas, veg);

    assert(g_instanced_batch_calls == 1);
    assert(g_last_instance_count == v->visible_count);
    assert(g_cull_during_batch == 1);  // grass uses a double-sided material, not canvas mutation
    assert(canvas.backface_cull == 1); // previous state remains untouched
    g_terrain = nullptr;
}

static void test_draw_is_noop_outside_frame() {
    void *veg = rt_vegetation3d_new(nullptr);
    FakeTerrain terrain = make_terrain(64, 64);
    g_terrain = &terrain;

    rt_vegetation3d_populate(veg, &terrain, 32);
    rt_vegetation3d_set_lod_distances(veg, 1000.0, 2000.0);
    rt_vegetation3d_update(veg, 0.016, 32.0, 0.0, 32.0);

    rt_canvas3d canvas = make_frame_canvas();
    canvas.in_frame = 0; // not between Begin/End
    reset_draw_records();
    rt_canvas3d_draw_vegetation(&canvas, veg);
    assert(g_instanced_batch_calls == 0);
    g_terrain = nullptr;
}

static void test_set_blade_size_rebuilds_mesh() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);

    rt_vegetation3d_set_blade_size(veg, 1.0, 2.0, 0.5);
    rt_mesh3d *mesh = static_cast<rt_mesh3d *>(v->blade_mesh);
    assert(mesh->vertex_count == 8); // cleared and rebuilt to the cross-billboard quad set
    assert(mesh->index_count == 12);
    assert(v->blade_width == 1.0);
    assert(v->blade_height == 2.0);
    assert(v->size_variation == 0.5);
}

static void test_setters_sanitize_nonfinite_inputs() {
    void *veg = rt_vegetation3d_new(nullptr);
    VegetationView *v = static_cast<VegetationView *>(veg);

    rt_vegetation3d_set_wind_params(veg, NAN, -3.0, INFINITY);
    assert(v->wind_speed == 0.0);     // NaN -> fallback 0
    assert(v->wind_strength == 0.0);  // negative -> 0
    assert(v->wind_turbulence == 0.0); // Inf -> 0

    rt_vegetation3d_set_lod_distances(veg, NAN, NAN);
    assert(v->lod_near == 40.0f); // non-finite -> defaults restored
    assert(v->lod_far == 100.0f);

    rt_vegetation3d_set_blade_size(veg, NAN, -1.0, 5.0);
    assert(v->blade_width == 0.4);   // NaN -> default
    assert(v->blade_height == 1.2);  // non-positive -> default
    assert(v->size_variation == 1.0); // clamped to [0,1]
}

int main() {
    test_new_builds_blade_mesh_and_defaults();
    test_populate_scatters_requested_count();
    test_populate_nonpositive_count_clears();
    test_populate_overflow_count_traps();
    test_density_map_gates_population();
    test_update_advances_time_and_keeps_near_visible();
    test_update_far_camera_culls_all();
    test_draw_submits_one_instanced_batch_without_mutating_cull();
    test_draw_is_noop_outside_frame();
    test_set_blade_size_rebuilds_mesh();
    test_setters_sanitize_nonfinite_inputs();
    std::printf("RTVegetation3DContractTests passed.\n");
    return 0;
}
