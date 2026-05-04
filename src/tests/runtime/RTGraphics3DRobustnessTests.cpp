//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGraphics3DRobustnessTests.cpp
// Purpose: Graphics3D correctness contracts for handle validation, numeric
//   sanitization, packing, and degenerate navigation cases.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_decal3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_navmesh3d.h"
#include "rt_object.h"
#include "rt_path3d.h"
#include "rt_pixels.h"
#include "rt_sprite3d.h"
#include "rt_terrain3d.h"
#include "rt_texatlas3d.h"
#include "rt_vec3.h"
#include "rt_water3d.h"
}

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

extern "C" {
void *rt_mesh3d_new(void);
void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
int64_t rt_mesh3d_get_vertex_count(void *m);
void rt_mesh3d_transform(void *m, void *mat4);
void *rt_material3d_new(void);
void rt_material3d_set_import_texture_slot(void *obj,
                                           int64_t slot,
                                           int64_t uv_set,
                                           double offset_u,
                                           double offset_v,
                                           double scale_u,
                                           double scale_v,
                                           double rotation,
                                           int64_t wrap_s,
                                           int64_t wrap_t,
                                           int64_t filter);
double rt_terrain3d_get_height_at(void *obj, double wx, double wz);
int8_t rt_navmesh3d_is_walkable(void *obj, void *point);
void rt_navmesh3d_set_max_slope(void *obj, double degrees);
}

namespace {

struct SpriteView {
    void *vptr;
    void *texture;
    double position[3];
    double scale_wh[2];
    double anchor[2];
    int32_t frame_x;
    int32_t frame_y;
    int32_t frame_w;
    int32_t frame_h;
    int32_t tex_w;
    int32_t tex_h;
    void *cached_mesh;
    void *cached_material;
    void *cached_texture;
};

struct DecalView {
    void *vptr;
    double position[3];
    double normal[3];
    double size;
    void *texture;
    double lifetime;
    double max_lifetime;
    double alpha;
    void *mesh;
    void *material;
};

struct MaterialView {
    void *vptr;
    double diffuse[4];
    double specular[3];
    double shininess;
    int32_t workflow;
    void *texture;
    void *normal_map;
    void *specular_map;
    void *emissive_map;
    void *metallic_roughness_map;
    void *ao_map;
    double emissive[3];
    double metallic;
    double roughness;
    double ao;
    double emissive_intensity;
    double normal_scale;
    double alpha;
    double alpha_cutoff;
    void *env_map;
    double reflectivity;
    int8_t unlit;
    int8_t double_sided;
    int8_t additive_blend;
    int32_t alpha_mode;
    int8_t alpha_mode_auto;
    int32_t texture_wrap_s;
    int32_t texture_wrap_t;
    int32_t texture_filter;
    int32_t texture_slot_wrap_s[6];
    int32_t texture_slot_wrap_t[6];
    int32_t texture_slot_filter[6];
    int32_t texture_slot_uv_set[6];
    double texture_slot_uv_transform[6][6];
};

static void test_graphics3d_class_ids_are_stable() {
    void *atlas = rt_texatlas3d_new(16, 16);
    void *path = rt_path3d_new();
    void *terrain = rt_terrain3d_new(2, 2);
    void *water = rt_water3d_new(1.0, 1.0);
    void *sprite = rt_sprite3d_new(nullptr);
    void *mat = rt_mat4_identity();

    assert(rt_obj_class_id(atlas) == RT_G3D_TEXTUREATLAS3D_CLASS_ID);
    assert(rt_obj_class_id(path) == RT_G3D_PATH3D_CLASS_ID);
    assert(rt_obj_class_id(terrain) == RT_G3D_TERRAIN3D_CLASS_ID);
    assert(rt_obj_class_id(water) == RT_G3D_WATER3D_CLASS_ID);
    assert(rt_obj_class_id(sprite) == RT_G3D_SPRITE3D_CLASS_ID);
    assert(rt_obj_class_id(mat) == RT_MAT4_CLASS_ID);
}

static void test_texture_atlas_copies_pixels_and_reports_uvs() {
    void *atlas = rt_texatlas3d_new(16, 16);
    void *pixels = rt_pixels_new(2, 2);
    rt_pixels_set(pixels, 0, 0, 0x11223344);
    rt_pixels_set(pixels, 1, 0, 0x55667788);
    rt_pixels_set(pixels, 0, 1, (int64_t)0x99AABBCC);
    rt_pixels_set(pixels, 1, 1, (int64_t)0xDDEEFF00);

    int64_t id = rt_texatlas3d_add(atlas, pixels);
    assert(id == 0);

    double u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
    rt_texatlas3d_get_uv_rect(atlas, id, &u0, &v0, &u1, &v1);
    assert(std::fabs(u0 - (1.0 / 16.0)) < 1e-12);
    assert(std::fabs(v0 - (1.0 / 16.0)) < 1e-12);
    assert(std::fabs(u1 - (3.0 / 16.0)) < 1e-12);
    assert(std::fabs(v1 - (3.0 / 16.0)) < 1e-12);

    void *texture = rt_texatlas3d_get_texture(atlas);
    assert(texture != nullptr);
    assert(rt_pixels_get(texture, 1, 1) == 0x11223344);
    assert(rt_pixels_get(texture, 2, 1) == 0x55667788);
    assert(rt_pixels_get(texture, 1, 2) == (int64_t)0x99AABBCC);
    assert(rt_pixels_get(texture, 2, 2) == (int64_t)0xDDEEFF00);
    assert(rt_pixels_get(texture, 1, 0) == 0x11223344);
    assert(rt_pixels_get(texture, 2, 0) == 0x55667788);
    assert(rt_pixels_get(texture, 0, 1) == 0x11223344);
    assert(rt_pixels_get(texture, 3, 1) == 0x55667788);
    assert(rt_pixels_get(texture, 0, 2) == (int64_t)0x99AABBCC);
    assert(rt_pixels_get(texture, 3, 2) == (int64_t)0xDDEEFF00);
    assert(rt_pixels_get(texture, 1, 3) == (int64_t)0x99AABBCC);
    assert(rt_pixels_get(texture, 2, 3) == (int64_t)0xDDEEFF00);
}

static void test_material_rejects_non_pixels_texture_handles() {
    void *fake = rt_obj_new_i64(0, 8);
    void *pixels = rt_pixels_new(1, 1);
    void *mat_obj = rt_material3d_new_textured(fake);
    auto *mat = static_cast<MaterialView *>(mat_obj);
    assert(mat->texture == nullptr);

    rt_material3d_set_texture(mat_obj, pixels);
    assert(mat->texture == pixels);
    rt_material3d_set_texture(mat_obj, fake);
    assert(mat->texture == pixels);
}

static void test_mesh_apis_reject_wrong_class_handles() {
    void *fake = rt_obj_new_i64(0, 8);
    void *mesh = rt_mesh3d_new();
    void *bad_mat4 = rt_obj_new_i64(0, 8);

    rt_mesh3d_add_vertex(fake, 1.0, 2.0, 3.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    assert(rt_mesh3d_get_vertex_count(fake) == 0);

    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    assert(rt_mesh3d_get_vertex_count(mesh) == 1);
    rt_mesh3d_transform(mesh, bad_mat4);
    assert(rt_mesh3d_get_vertex_count(mesh) == 1);
}

static void test_material_import_texture_transform_clamps_float_uniforms() {
    auto *mat = static_cast<MaterialView *>(rt_material3d_new());
    assert(mat != nullptr);

    rt_material3d_set_import_texture_slot(mat,
                                          0,
                                          0,
                                          1.0e300,
                                          -1.0e300,
                                          1.0e300,
                                          -1.0e300,
                                          1.0e300,
                                          0,
                                          0,
                                          0);

    for (int i = 0; i < 6; i++) {
        double value = mat->texture_slot_uv_transform[0][i];
        assert(std::isfinite(value));
        assert(std::fabs(value) <= 1000000.0);
    }
}

static void test_terrain_heightmap_and_scale_sanitize_inputs() {
    void *terrain = rt_terrain3d_new(2, 2);
    void *heightmap = rt_pixels_new(2, 2);
    rt_pixels_set(heightmap, 0, 0, 0x000000FF);
    rt_pixels_set(heightmap, 1, 0, 0x000000FF);
    rt_pixels_set(heightmap, 0, 1, 0x000000FF);
    rt_pixels_set(heightmap, 1, 1, (int64_t)0xFFFFFFFF);

    rt_terrain3d_set_heightmap(terrain, heightmap);
    rt_terrain3d_set_scale(terrain,
                           -4.0,
                           std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity());
    assert(std::fabs(rt_terrain3d_get_height_at(terrain, 1.0, 1.0) - 1.0) < 1e-6);
}

static void test_sprite3d_clamps_frame_anchor_and_scale() {
    void *texture = rt_pixels_new(4, 4);
    void *sprite = rt_sprite3d_new(texture);
    auto *s = static_cast<SpriteView *>(sprite);

    rt_sprite3d_set_scale(sprite, -1.0, std::numeric_limits<double>::quiet_NaN());
    rt_sprite3d_set_anchor(sprite, -2.0, 4.0);
    rt_sprite3d_set_frame(sprite, -5, 100, 999, 999);

    assert(s->scale_wh[0] == 1.0);
    assert(s->scale_wh[1] == 1.0);
    assert(s->anchor[0] == 0.0);
    assert(s->anchor[1] == 1.0);
    assert(s->frame_x == 0);
    assert(s->frame_y == 3);
    assert(s->frame_w == 4);
    assert(s->frame_h == 1);
}

static void test_decal3d_normal_and_lifetime_are_sanitized() {
    void *pos = rt_vec3_new(std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0);
    void *normal = rt_vec3_new(0.0, 0.0, 0.0);
    void *decal = rt_decal3d_new(pos, normal, -8.0, nullptr);
    auto *d = static_cast<DecalView *>(decal);

    assert(d->position[0] == 0.0);
    assert(d->position[1] == 2.0);
    assert(d->position[2] == 3.0);
    assert(d->normal[0] == 0.0);
    assert(d->normal[1] == 1.0);
    assert(d->normal[2] == 0.0);
    assert(d->size == 1.0);

    rt_decal3d_set_lifetime(decal, std::numeric_limits<double>::quiet_NaN());
    rt_decal3d_update(decal, 1.0);
    assert(rt_decal3d_is_expired(decal) == 0);
}

static void test_path3d_growth_preserves_points() {
    void *path = rt_path3d_new();
    for (int i = 0; i < 40; i++) {
        void *point = rt_vec3_new((double)i, 0.0, 0.0);
        rt_path3d_add_point(path, point);
    }

    assert(rt_path3d_get_point_count(path) == 40);
    void *mid = rt_path3d_get_position_at(path, std::numeric_limits<double>::quiet_NaN());
    assert(rt_vec3_x(mid) == 0.0);
    assert(rt_path3d_get_length(path) > 38.0);
}

static void test_navmesh_sample_position_handles_empty_mesh() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);

    void *navmesh = rt_navmesh3d_build(mesh, -1.0, std::numeric_limits<double>::quiet_NaN());
    assert(navmesh != nullptr);
    assert(rt_navmesh3d_get_triangle_count(navmesh) == 0);

    void *point = rt_vec3_new(4.0, 5.0, 6.0);
    void *sampled = rt_navmesh3d_sample_position(navmesh, point);
    assert(rt_vec3_x(sampled) == 4.0);
    assert(rt_vec3_y(sampled) == 5.0);
    assert(rt_vec3_z(sampled) == 6.0);
    assert(rt_navmesh3d_is_walkable(navmesh, point) == 0);
}

static void test_navmesh_slope_refilter_and_sloped_height_projection() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 2.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);

    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    assert(navmesh != nullptr);
    assert(rt_navmesh3d_get_triangle_count(navmesh) == 0);

    rt_navmesh3d_set_max_slope(navmesh, 80.0);
    assert(rt_navmesh3d_get_triangle_count(navmesh) == 1);

    void *query = rt_vec3_new(0.25, 10.0, 0.25);
    void *sampled = rt_navmesh3d_sample_position(navmesh, query);
    assert(std::fabs(rt_vec3_x(sampled) - 0.25) < 1e-6);
    assert(std::fabs(rt_vec3_y(sampled) - 0.5) < 1e-6);
    assert(std::fabs(rt_vec3_z(sampled) - 0.25) < 1e-6);
}

static void test_morphtarget_sanitizes_nonfinite_weights_and_deltas() {
    void *mt = rt_morphtarget3d_new(1);
    assert(mt != nullptr);
    int64_t shape = rt_morphtarget3d_add_shape(mt, nullptr);
    assert(shape == 0);

    rt_morphtarget3d_set_weight(mt, shape, std::numeric_limits<double>::quiet_NaN());
    assert(rt_morphtarget3d_get_weight(mt, shape) == 0.0);

    rt_morphtarget3d_set_delta(
        mt, shape, 0, std::numeric_limits<double>::infinity(), 2.0, -3.0);
    const float *packed = rt_morphtarget3d_get_packed_deltas(mt);
    assert(packed != nullptr);
    assert(packed[0] == 0.0f);
    assert(packed[1] == 2.0f);
    assert(packed[2] == -3.0f);
}

} // namespace

int main() {
    test_graphics3d_class_ids_are_stable();
    test_texture_atlas_copies_pixels_and_reports_uvs();
    test_material_rejects_non_pixels_texture_handles();
    test_mesh_apis_reject_wrong_class_handles();
    test_material_import_texture_transform_clamps_float_uniforms();
    test_terrain_heightmap_and_scale_sanitize_inputs();
    test_sprite3d_clamps_frame_anchor_and_scale();
    test_decal3d_normal_and_lifetime_are_sanitized();
    test_path3d_growth_preserves_points();
    test_navmesh_sample_position_handles_empty_mesh();
    test_navmesh_slope_refilter_and_sloped_height_projection();
    test_morphtarget_sanitizes_nonfinite_weights_and_deltas();
    std::printf("RTGraphics3DRobustnessTests passed.\n");
    return 0;
}
