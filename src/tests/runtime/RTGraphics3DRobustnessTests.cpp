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

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

extern "C" {
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_decal3d.h"
#include "rt_collider3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_instbatch3d.h"
#include "rt_joints3d.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_navmesh3d.h"
#include "rt_object.h"
#include "rt_path3d.h"
#include "rt_particles3d.h"
#include "rt_perlin.h"
#include "rt_pixels.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_sprite3d.h"
#include "rt_string.h"
#include "rt_terrain3d.h"
#include "rt_texatlas3d.h"
#include "rt_transform3d.h"
#include "rt_vec3.h"
#include "rt_vegetation3d.h"
#include "rt_water3d.h"
}

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

extern "C" {
void *rt_mesh3d_new(void);
void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
int64_t rt_mesh3d_get_vertex_count(void *m);
int64_t rt_mesh3d_get_triangle_count(void *m);
void rt_mesh3d_transform(void *m, void *mat4);
void rt_mesh3d_calc_tangents(void *m);
void *rt_material3d_new(void);
void rt_material3d_set_unlit(void *obj, int8_t unlit);
void *rt_light3d_new_directional(void *direction, double r, double g, double b);
void *rt_light3d_new_point(void *position, double r, double g, double b, double attenuation);
void *rt_light3d_new_ambient(double r, double g, double b);
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
    int32_t shading_model;
    double custom_params[8];
};

struct ParticleView {
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
    double waves[8][5];
    int32_t wave_count;
    int32_t resolution;
    int8_t mesh_dirty;
};

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

struct InstBatchView {
    void *vptr;
    void *mesh;
    void *material;
    float *transforms;
    float *current_snapshot;
    float *prev_transforms;
    int32_t instance_count;
    int32_t instance_capacity;
    int32_t motion_snapshot_count;
    int32_t prev_count;
    int64_t last_motion_frame;
    int8_t has_prev_snapshot;
};

static double triangle_area_sq(const rt_mesh3d *mesh, uint32_t i0, uint32_t i1, uint32_t i2) {
    const float *a = mesh->vertices[i0].pos;
    const float *b = mesh->vertices[i1].pos;
    const float *c = mesh->vertices[i2].pos;
    double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double cross[3] = {
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    };
    return cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];
}

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

static void test_camera_center_ray_and_projection_layout() {
    void *camera = rt_camera3d_new(90.0, 1.0, 0.1, 100.0);
    void *center_ray = rt_camera3d_screen_to_ray(camera, 50, 50, 100, 100);
    assert(std::fabs(rt_vec3_x(center_ray)) < 1e-6);
    assert(std::fabs(rt_vec3_y(center_ray)) < 1e-6);
    assert(std::fabs(rt_vec3_z(center_ray) + 1.0) < 1e-6);

    float projection[16] = {};
    rt_camera3d_get_render_projection(camera, 1.0, projection);
    assert(std::isfinite(projection[10]));
    assert(std::isfinite(projection[11]));
    assert(projection[14] == -1.0f);
}

static void test_camera_sanitizes_extreme_projection_and_basis_inputs() {
    void *camera = rt_camera3d_new(90.0, 1.0e300, 1.0e-300, 1.0e300);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    rt_camera3d_orbit(camera, target, 1.0e300, 35.0, 20.0);

    float projection[16] = {};
    rt_camera3d_get_render_projection(camera, 1.0e300, projection);
    for (float value : projection)
        assert(std::isfinite(value));

    void *forward = rt_camera3d_get_forward(camera);
    void *right = rt_camera3d_get_right(camera);
    double flen = std::sqrt(rt_vec3_x(forward) * rt_vec3_x(forward) +
                            rt_vec3_y(forward) * rt_vec3_y(forward) +
                            rt_vec3_z(forward) * rt_vec3_z(forward));
    double rlen = std::sqrt(rt_vec3_x(right) * rt_vec3_x(right) +
                            rt_vec3_y(right) * rt_vec3_y(right) +
                            rt_vec3_z(right) * rt_vec3_z(right));
    assert(std::isfinite(flen) && std::fabs(flen - 1.0) < 1e-6);
    assert(std::isfinite(rlen) && std::fabs(rlen - 1.0) < 1e-6);
}

static void test_generated_sphere_has_no_degenerate_triangles() {
    auto *mesh = static_cast<rt_mesh3d *>(rt_mesh3d_new_sphere(1.0, 8));
    assert(mesh != nullptr);
    assert(rt_mesh3d_get_triangle_count(mesh) > 0);
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        assert(triangle_area_sq(mesh, mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]) >
               1e-12);
    }
}

static void test_generated_plane_faces_positive_y() {
    auto *mesh = static_cast<rt_mesh3d *>(rt_mesh3d_new_plane(2.0, 2.0));
    assert(mesh != nullptr);
    assert(mesh->index_count == 6);

    const uint32_t i0 = mesh->indices[0];
    const uint32_t i1 = mesh->indices[1];
    const uint32_t i2 = mesh->indices[2];
    const float *a = mesh->vertices[i0].pos;
    const float *b = mesh->vertices[i1].pos;
    const float *c = mesh->vertices[i2].pos;
    const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const double normal_y = ab[2] * ac[0] - ab[0] * ac[2];
    assert(normal_y > 0.0);
}

static void test_obj_loader_handles_long_lines() {
    const std::string path = "/tmp/viper_rt_graphics3d_long_line.obj";
    {
        std::ofstream out(path);
        out << '#';
        out << std::string(6000, 'x') << "\n";
        out << "v 0 0 0\n";
        out << "v 1 0 0\n";
        out << "v 0 1 0\n";
        out << "f 1 2 3\n";
    }

    rt_string path_string = rt_string_from_bytes(path.c_str(), static_cast<int64_t>(path.size()));
    void *mesh = rt_mesh3d_from_obj(path_string);
    assert(mesh != nullptr);
    assert(rt_mesh3d_get_triangle_count(mesh) == 1);
    std::remove(path.c_str());
}

static void test_scene_particles_water_and_render_targets_reject_wrong_handles() {
    void *fake = rt_obj_new_i64(0, 8);
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    rt_scene_node3d_add_lod(node, 4.0, fake);
    assert(rt_scene_node3d_get_lod_count(node) == 0);
    rt_scene_node3d_add_lod(node, 4.0, mesh);
    assert(rt_scene_node3d_get_lod_count(node) == 1);
    assert(rt_scene_node3d_get_lod_mesh(node, 0) == mesh);

    void *particles_obj = rt_particles3d_new(4);
    auto *particles = static_cast<ParticleView *>(particles_obj);
    void *pixels = rt_pixels_new(1, 1);
    rt_particles3d_set_texture(particles_obj, pixels);
    assert(particles->texture == pixels);
    rt_particles3d_set_texture(particles_obj, fake);
    assert(particles->texture == pixels);

    void *water_obj = rt_water3d_new(2.0, 2.0);
    auto *water = static_cast<WaterView *>(water_obj);
    rt_water3d_set_texture(water_obj, pixels);
    rt_water3d_set_normal_map(water_obj, pixels);
    assert(water->texture == pixels);
    assert(water->normal_map == pixels);
    rt_water3d_set_texture(water_obj, fake);
    rt_water3d_set_normal_map(water_obj, fake);
    assert(water->texture == pixels);
    assert(water->normal_map == pixels);

    void *cubemap = rt_cubemap3d_new(pixels, pixels, pixels, pixels, pixels, pixels);
    rt_water3d_set_env_map(water_obj, cubemap);
    assert(water->env_map == cubemap);
    rt_water3d_set_env_map(water_obj, fake);
    assert(water->env_map == cubemap);

    assert(rt_rendertarget3d_get_width(fake) == 0);
    assert(rt_rendertarget3d_get_height(fake) == 0);
    assert(rt_rendertarget3d_as_pixels(fake) == nullptr);
}

static void test_cubemap_sampling_sanitizes_inputs() {
    void *px = rt_pixels_new(1, 1);
    void *face = rt_pixels_new(1, 1);
    rt_pixels_set(px, 0, 0, (int64_t)0xFF0000FF);
    rt_pixels_set(face, 0, 0, (int64_t)0x000000FF);
    auto *cubemap = static_cast<rt_cubemap3d *>(rt_cubemap3d_new(px, face, face, face, face, face));
    assert(cubemap != nullptr);

    float out[3] = {1.0f, 1.0f, 1.0f};
    rt_cubemap_sample(
        cubemap, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, &out[0], &out[1], &out[2]);
    assert(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);

    rt_cubemap_sample_roughness(cubemap,
                                1.0f,
                                0.0f,
                                0.0f,
                                std::numeric_limits<float>::quiet_NaN(),
                                &out[0],
                                &out[1],
                                &out[2]);
    assert(std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]));
    assert(out[0] > 0.9f);
}

static void test_physics_checked_handles_and_trigger_removal_exit() {
    void *fake = rt_obj_new_i64(0, 8);
    void *world = rt_world3d_new(0.0, 0.0, 0.0);
    void *body = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(body, 0.0, 0.0, 0.0);

    rt_world3d_add(fake, body);
    assert(rt_world3d_body_count(fake) == 0);
    rt_world3d_add(world, body);
    assert(rt_world3d_body_count(world) == 1);

    void *bad_pos = rt_body3d_get_position(fake);
    assert(rt_vec3_x(bad_pos) == 0.0 && rt_vec3_y(bad_pos) == 0.0 && rt_vec3_z(bad_pos) == 0.0);
    assert(rt_physics_hit3d_get_body(fake) == nullptr);
    assert(rt_physics_hit_list3d_get_count(fake) == 0);
    assert(rt_collision_event3d_get_body_a(fake) == nullptr);

    void *trigger = rt_trigger3d_new(-1.0, -1.0, -1.0, 1.0, 1.0, 1.0);
    rt_trigger3d_update(trigger, world);
    assert(rt_trigger3d_get_enter_count(trigger) == 1);
    assert(rt_trigger3d_get_exit_count(trigger) == 0);

    rt_world3d_remove(world, body);
    rt_trigger3d_update(trigger, world);
    assert(rt_trigger3d_get_enter_count(trigger) == 0);
    assert(rt_trigger3d_get_exit_count(trigger) == 1);
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

static void test_material_scalar_setters_clamp_renderer_uniforms() {
    auto *mat = static_cast<MaterialView *>(rt_material3d_new());
    assert(mat != nullptr);

    rt_material3d_set_shininess(mat, 1.0e300);
    rt_material3d_set_emissive_intensity(mat, 1.0e300);
    rt_material3d_set_normal_scale(mat, -1.0e300);
    rt_material3d_set_custom_param(mat, 3, -1.0e300);

    assert(std::isfinite(mat->shininess) && mat->shininess <= 8192.0);
    assert(std::isfinite(mat->emissive_intensity) && mat->emissive_intensity <= 1000000.0);
    assert(std::isfinite(mat->normal_scale) && mat->normal_scale >= -1000.0);
    assert(std::isfinite(mat->custom_params[3]) && mat->custom_params[3] >= -1000000.0);
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

    assert(std::isfinite(rt_terrain3d_get_height_at(terrain, 1.0e300, -1.0e300)));
    void *normal = rt_terrain3d_get_normal_at(terrain, 1.0e300, -1.0e300);
    double nlen = std::sqrt(rt_vec3_x(normal) * rt_vec3_x(normal) +
                            rt_vec3_y(normal) * rt_vec3_y(normal) +
                            rt_vec3_z(normal) * rt_vec3_z(normal));
    assert(std::isfinite(nlen) && nlen > 0.9 && nlen < 1.1);
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

    void *huge_normal = rt_vec3_new(1.0e300, 0.0, 0.0);
    void *huge_decal = rt_decal3d_new(pos, huge_normal, 1.0e300, nullptr);
    auto *hd = static_cast<DecalView *>(huge_decal);
    assert(std::isfinite(hd->size) && hd->size <= 1000000.0);
    assert(hd->normal[0] == 0.0);
    assert(hd->normal[1] == 1.0);
    assert(hd->normal[2] == 0.0);
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

static void test_path3d_looping_includes_closing_segment() {
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0.0, 0.0, 0.0));
    rt_path3d_add_point(path, rt_vec3_new(10.0, 0.0, 0.0));
    rt_path3d_add_point(path, rt_vec3_new(10.0, 0.0, 10.0));

    double open_length = rt_path3d_get_length(path);
    rt_path3d_set_looping(path, 1);
    double loop_length = rt_path3d_get_length(path);
    assert(loop_length > open_length + 5.0);

    void *near_start = rt_path3d_get_position_at(path, 0.999);
    double dist = std::sqrt(rt_vec3_x(near_start) * rt_vec3_x(near_start) +
                            rt_vec3_y(near_start) * rt_vec3_y(near_start) +
                            rt_vec3_z(near_start) * rt_vec3_z(near_start));
    assert(dist < 0.5);
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

static void test_navmesh_sample_position_uses_closest_triangle_point() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);

    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    assert(navmesh != nullptr);
    assert(rt_navmesh3d_get_triangle_count(navmesh) == 1);

    void *query = rt_vec3_new(2.0, 5.0, 2.0);
    void *sampled = rt_navmesh3d_sample_position(navmesh, query);
    assert(std::fabs(rt_vec3_x(sampled) - 1.0) < 1e-6);
    assert(std::fabs(rt_vec3_y(sampled)) < 1e-6);
    assert(std::fabs(rt_vec3_z(sampled) - 1.0) < 1e-6);
}

static void test_navmesh_walkable_uses_vertical_tolerance() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);

    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    assert(navmesh != nullptr);

    void *near_point = rt_vec3_new(0.25, 0.5, 0.25);
    void *high_point = rt_vec3_new(0.25, 10.0, 0.25);
    void *near_goal = rt_vec3_new(0.5, 0.0, 0.5);
    assert(rt_navmesh3d_is_walkable(navmesh, near_point) == 1);
    assert(rt_navmesh3d_is_walkable(navmesh, high_point) == 0);
    assert(rt_navmesh3d_find_path(navmesh, near_point, near_goal) != nullptr);
    assert(rt_navmesh3d_find_path(navmesh, high_point, near_goal) == nullptr);

    void *sampled = rt_navmesh3d_sample_position(navmesh, high_point);
    assert(std::fabs(rt_vec3_x(sampled) - 0.25) < 1e-6);
    assert(std::fabs(rt_vec3_y(sampled)) < 1e-6);
    assert(std::fabs(rt_vec3_z(sampled) - 0.25) < 1e-6);
}

static void test_capsule_collider_clamps_total_height_to_diameter() {
    void *capsule = rt_collider3d_new_capsule(2.0, 1.0);
    double min_v[3];
    double max_v[3];
    rt_collider3d_get_local_bounds_raw(capsule, min_v, max_v);
    assert(std::fabs(rt_collider3d_get_radius_raw(capsule) - 2.0) < 1e-9);
    assert(std::fabs(rt_collider3d_get_height_raw(capsule) - 4.0) < 1e-9);
    assert(std::fabs(min_v[0] + 2.0) < 1e-9);
    assert(std::fabs(min_v[1] + 2.0) < 1e-9);
    assert(std::fabs(max_v[1] - 2.0) < 1e-9);
    assert(std::fabs(max_v[2] - 2.0) < 1e-9);
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

    rt_morphtarget3d_set_delta(mt, shape, 0, 1.0e300, -1.0e300, 4.0);
    packed = rt_morphtarget3d_get_packed_deltas(mt);
    assert(packed[0] == 0.0f);
    assert(packed[1] == 0.0f);
    assert(packed[2] == 4.0f);
}

static void test_scene_reparent_preserves_child_and_counts() {
    void *p1 = rt_scene_node3d_new();
    void *p2 = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();

    rt_scene_node3d_add_child(p1, child);
    assert(rt_scene_node3d_child_count(p1) == 1);
    assert(rt_scene_node3d_get_parent(child) == p1);

    rt_scene_node3d_add_child(p2, child);
    assert(rt_scene_node3d_child_count(p1) == 0);
    assert(rt_scene_node3d_child_count(p2) == 1);
    assert(rt_scene_node3d_get_child(p2, 0) == child);
    assert(rt_scene_node3d_get_parent(child) == p2);

    void *scene = rt_scene3d_new();
    assert(scene != nullptr);
    assert(rt_scene3d_get_node_count(scene) == 1);
}

static void test_scene_deep_hierarchy_traversal_and_transform_clamps() {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene3d_get_root(scene);
    void *deepest = nullptr;

    for (int i = 0; i < 3000; i++) {
        void *node = rt_scene_node3d_new();
        rt_scene_node3d_add_child(parent, node);
        parent = node;
        deepest = node;
    }

    rt_string name = rt_string_from_bytes("deepest", 7);
    rt_scene_node3d_set_name(deepest, name);
    assert(rt_scene3d_get_node_count(scene) == 3001);
    assert(rt_scene_node3d_find(rt_scene3d_get_root(scene), name) == deepest);

    rt_scene_node3d_set_position(deepest, 1.0e300, -1.0e300, 2.0);
    void *pos = rt_scene_node3d_get_position(deepest);
    assert(std::isfinite(rt_vec3_x(pos)) && std::fabs(rt_vec3_x(pos)) <= 1000000000000.0);
    assert(std::isfinite(rt_vec3_y(pos)) && std::fabs(rt_vec3_y(pos)) <= 1000000000000.0);

    void *world = rt_scene_node3d_get_world_matrix(deepest);
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++)
            assert(std::isfinite(rt_mat4_get(world, row, col)));
    }
}

static void test_mesh_bone_weights_are_validated_and_dirty_geometry() {
    auto *mesh = static_cast<rt_mesh3d *>(rt_mesh3d_new());
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    uint32_t before = mesh->geometry_revision;

    rt_mesh3d_set_bone_weights(mesh,
                               0,
                               1,
                               2.0,
                               999,
                               3.0,
                               3,
                               -1.0,
                               4,
                               std::numeric_limits<double>::quiet_NaN());

    assert(mesh->vertices[0].bone_indices[0] == 1);
    assert(mesh->vertices[0].bone_indices[1] == 0);
    assert(mesh->vertices[0].bone_indices[2] == 3);
    assert(mesh->vertices[0].bone_indices[3] == 4);
    assert(std::fabs(mesh->vertices[0].bone_weights[0] - 1.0f) < 1e-6f);
    assert(mesh->vertices[0].bone_weights[1] == 0.0f);
    assert(mesh->vertices[0].bone_weights[2] == 0.0f);
    assert(mesh->vertices[0].bone_weights[3] == 0.0f);
    assert(mesh->bone_count == 5);
    assert(mesh->geometry_revision != before);

    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, nullptr, -1, nullptr);
    rt_skeleton3d_add_bone(skel, nullptr, 0, nullptr);
    rt_mesh3d_set_skeleton(mesh, skel);
    assert(mesh->skeleton_ref == skel);
    assert(mesh->bone_count == 2);
}

static void test_transform_and_instance_batch_sanitize_extreme_matrices() {
    void *xf = rt_transform3d_new();
    rt_transform3d_set_position(xf, 1.0e300, -1.0e300, 2.0);
    rt_transform3d_set_scale(xf, 0.0, std::numeric_limits<double>::quiet_NaN(), 1.0e300);
    rt_transform3d_translate(xf, rt_vec3_new(-1.0e300, 1.0e300, 1.0e300));

    void *pos = rt_transform3d_get_position(xf);
    void *scale = rt_transform3d_get_scale(xf);
    assert(std::isfinite(rt_vec3_x(pos)) && std::fabs(rt_vec3_x(pos)) <= 1000000000000.0);
    assert(std::isfinite(rt_vec3_y(pos)) && std::fabs(rt_vec3_y(pos)) <= 1000000000000.0);
    assert(std::isfinite(rt_vec3_x(scale)) && std::fabs(rt_vec3_x(scale)) >= 1.0);
    assert(std::isfinite(rt_vec3_z(scale)) && std::fabs(rt_vec3_z(scale)) <= 1000000000000.0);

    void *matrix = rt_transform3d_get_matrix(xf);
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++)
            assert(std::isfinite(rt_mat4_get(matrix, row, col)));
    }

    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new();
    auto *batch = static_cast<InstBatchView *>(rt_instbatch3d_new(mesh, mat));
    void *bad_matrix = rt_mat4_new(1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300,
                                   1.0e300,
                                   -1.0e300);
    rt_instbatch3d_add(batch, bad_matrix);
    assert(rt_instbatch3d_count(batch) == 1);
    for (int i = 0; i < 16; i++)
        assert(std::isfinite(batch->transforms[i]));
    assert(batch->transforms[0] == 1.0f);
    assert(batch->transforms[5] == 1.0f);
    assert(batch->transforms[10] == 1.0f);
    assert(batch->transforms[15] == 1.0f);
}

static void test_obj_loader_recalculates_mixed_missing_normals() {
    const std::string path = "/tmp/viper_rt_graphics3d_mixed_normals.obj";
    {
        std::ofstream out(path);
        out << "v 0 0 0\n";
        out << "v 1 0 0\n";
        out << "v 0 1 0\n";
        out << "vn 1 0 0\n";
        out << "f 1 2 3\n";
    }

    rt_string path_string = rt_string_from_bytes(path.c_str(), static_cast<int64_t>(path.size()));
    auto *mesh = static_cast<rt_mesh3d *>(rt_mesh3d_from_obj(path_string));
    assert(mesh != nullptr);
    assert(mesh->vertex_count == 3);
    for (uint32_t i = 0; i < mesh->vertex_count; i++) {
        assert(std::fabs(mesh->vertices[i].normal[2] - 1.0f) < 1e-6f);
    }
    std::remove(path.c_str());
}

static void test_skeleton_bind_pose_and_animation_duration_sanitize() {
    void *bad_bind = rt_mat4_new(std::numeric_limits<double>::quiet_NaN(),
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 1.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 1.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 1.0);
    void *skel = rt_skeleton3d_new();
    assert(rt_skeleton3d_add_bone(skel, nullptr, -1, bad_bind) == 0);
    void *pose = rt_skeleton3d_get_bone_bind_pose(skel, 0);
    assert(rt_mat4_get(pose, 0, 0) == 1.0);
    assert(rt_mat4_get(pose, 1, 1) == 1.0);
    assert(rt_mat4_get(pose, 2, 2) == 1.0);
    assert(rt_mat4_get(pose, 3, 3) == 1.0);

    void *anim = rt_animation3d_new(nullptr, std::numeric_limits<double>::quiet_NaN());
    assert(std::fabs(rt_animation3d_get_duration(anim) - 1.0) < 1e-12);
}

static void test_light_and_material_boolean_state_is_initialized() {
    void *dir = rt_vec3_new(0.0, -1.0, 0.0);
    auto *directional = static_cast<rt_light3d *>(rt_light3d_new_directional(dir, 1.0, 1.0, 1.0));
    auto *point = static_cast<rt_light3d *>(rt_light3d_new_point(dir, 1.0, 0.0, 0.0, 1.0));
    auto *ambient = static_cast<rt_light3d *>(rt_light3d_new_ambient(0.1, 0.2, 0.3));
    assert(directional->inner_cos == 0.0 && directional->outer_cos == 0.0);
    assert(point->inner_cos == 0.0 && point->outer_cos == 0.0);
    assert(ambient->inner_cos == 0.0 && ambient->outer_cos == 0.0);

    auto *mat = static_cast<rt_material3d *>(rt_material3d_new());
    rt_material3d_set_unlit(mat, 42);
    assert(mat->unlit == 1);
    rt_material3d_set_unlit(mat, 0);
    assert(mat->unlit == 0);
}

static void test_physics_joints_deduplicate_and_raycast_is_true_ray() {
    void *world = rt_world3d_new(0.0, 0.0, 0.0);
    void *a = rt_body3d_new_sphere(0.5, 1.0);
    void *b = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 0.0, 0.0, 2.0);
    void *joint = rt_distance_joint3d_new(a, b, 2.0);
    rt_world3d_add_joint(world, joint, RT_JOINT_DISTANCE);
    rt_world3d_add_joint(world, joint, RT_JOINT_DISTANCE);
    assert(rt_world3d_joint_count(world) == 1);

    void *ray_world = rt_world3d_new(0.0, 0.0, 0.0);
    void *near_miss = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(near_miss, 0.5005, 0.0, 5.0);
    rt_world3d_add(ray_world, near_miss);
    void *origin = rt_vec3_new(0.0, 0.0, 0.0);
    void *dir = rt_vec3_new(0.0, 0.0, 1.0);
    assert(rt_world3d_raycast(ray_world, origin, dir, 10.0, 0) == nullptr);

    void *hit_body = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(hit_body, 0.0, 0.0, 5.0);
    rt_world3d_add(ray_world, hit_body);
    void *hit = rt_world3d_raycast(ray_world, origin, dir, 10.0, 0);
    assert(hit != nullptr);
    assert(rt_physics_hit3d_get_body(hit) == hit_body);
    assert(std::fabs(rt_physics_hit3d_get_distance(hit) - 4.5) < 1e-6);
}

static void test_terrain_water_and_vegetation_zero_dt_paths() {
    void *terrain = rt_terrain3d_new(4, 4);
    void *perlin = rt_perlin_new(123);
    rt_terrain3d_generate_perlin(terrain,
                                 perlin,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 1000,
                                 std::numeric_limits<double>::quiet_NaN());
    double h = rt_terrain3d_get_height_at(terrain, 1.0, 1.0);
    assert(std::isfinite(h));
    assert(h >= 0.0 && h <= 1.0);

    auto *water = static_cast<WaterView *>(rt_water3d_new(2.0, 2.0));
    rt_water3d_update(water, 0.0);
    assert(water->mesh != nullptr);
    int64_t initial_vertices = rt_mesh3d_get_vertex_count(water->mesh);
    rt_water3d_set_resolution(water, 8);
    assert(water->mesh_dirty == 1);
    rt_water3d_update(water, 0.0);
    assert(water->mesh_dirty == 0);
    assert(rt_mesh3d_get_vertex_count(water->mesh) == 81);
    assert(rt_mesh3d_get_vertex_count(water->mesh) != initial_vertices);

    auto *veg = static_cast<VegetationView *>(rt_vegetation3d_new(nullptr));
    rt_vegetation3d_set_lod_distances(veg, 50.0, 100.0);
    rt_vegetation3d_populate(veg, terrain, 8);
    rt_vegetation3d_update(veg, 0.0, 0.0, 0.0, 0.0);
    assert(veg->total_count > 0);
    assert(veg->visible_count > 0);
}

static void test_particles_and_water_numeric_knobs_are_bounded() {
    auto *particles = static_cast<ParticleView *>(rt_particles3d_new(8));
    rt_particles3d_set_position(particles, 1.0e300, -1.0e300, 2.0);
    rt_particles3d_set_speed(particles, 1.0e300, -1.0e300);
    rt_particles3d_set_lifetime(particles, 1.0e300, -1.0e300);
    rt_particles3d_set_size(particles, 1.0e300, -1.0e300);
    rt_particles3d_set_gravity(particles, 1.0e300, -1.0e300, 0.0);
    rt_particles3d_set_rate(particles, 1.0e300);
    rt_particles3d_set_emitter_size(particles, 1.0e300, -1.0e300, 4.0);
    rt_particles3d_burst(particles, 16);
    rt_particles3d_update(particles, 0.016);

    assert(std::isfinite(particles->position[0]) && particles->position[0] <= 1000000000000.0);
    assert(std::isfinite(particles->position[1]) && particles->position[1] >= -1000000000000.0);
    assert(std::isfinite(particles->speed_min) && particles->speed_min >= 0.0);
    assert(std::isfinite(particles->speed_max) && particles->speed_max <= 1000000.0);
    assert(std::isfinite(particles->life_min) && particles->life_min >= 0.0);
    assert(std::isfinite(particles->size_start) && particles->size_start <= 1000000.0);
    assert(std::isfinite(particles->gravity[0]) && particles->gravity[0] <= 1000000.0);
    assert(std::isfinite(particles->emitter_size[0]) && particles->emitter_size[0] <= 1000000.0);
    assert(particles->count <= particles->max_particles);

    auto *water = static_cast<WaterView *>(rt_water3d_new(1.0e300, -1.0));
    assert(std::isfinite(water->width) && water->width <= 1000000.0);
    assert(water->depth == 1.0);
    rt_water3d_set_height(water, 1.0e300);
    rt_water3d_set_wave_params(water, 1.0e300, 1.0e300, -1.0e300);
    rt_water3d_add_wave(water, 1.0, 0.0, 1.0e300, 1.0e300, 1.0e300);
    rt_water3d_update(water, 0.0);

    assert(std::isfinite(water->height) && water->height <= 1000000000000.0);
    assert(std::isfinite(water->wave_speed) && water->wave_speed <= 1000000.0);
    assert(std::isfinite(water->wave_amplitude) && water->wave_amplitude <= 1000000.0);
    assert(std::isfinite(water->wave_frequency) && water->wave_frequency >= -1000000.0);
    assert(water->wave_count == 1);
    assert(std::isfinite(water->waves[0][2]) && water->waves[0][2] <= 1000000.0);
    assert(std::isfinite(water->waves[0][3]) && water->waves[0][3] <= 1000000.0);
    assert(water->mesh != nullptr);
    assert(static_cast<rt_mesh3d *>(water->mesh)->build_failed == 0);
}

} // namespace

int main() {
    test_graphics3d_class_ids_are_stable();
    test_texture_atlas_copies_pixels_and_reports_uvs();
    test_material_rejects_non_pixels_texture_handles();
    test_mesh_apis_reject_wrong_class_handles();
    test_camera_center_ray_and_projection_layout();
    test_camera_sanitizes_extreme_projection_and_basis_inputs();
    test_generated_sphere_has_no_degenerate_triangles();
    test_generated_plane_faces_positive_y();
    test_obj_loader_handles_long_lines();
    test_scene_particles_water_and_render_targets_reject_wrong_handles();
    test_cubemap_sampling_sanitizes_inputs();
    test_physics_checked_handles_and_trigger_removal_exit();
    test_material_import_texture_transform_clamps_float_uniforms();
    test_material_scalar_setters_clamp_renderer_uniforms();
    test_terrain_heightmap_and_scale_sanitize_inputs();
    test_sprite3d_clamps_frame_anchor_and_scale();
    test_decal3d_normal_and_lifetime_are_sanitized();
    test_path3d_growth_preserves_points();
    test_path3d_looping_includes_closing_segment();
    test_navmesh_sample_position_handles_empty_mesh();
    test_navmesh_slope_refilter_and_sloped_height_projection();
    test_navmesh_sample_position_uses_closest_triangle_point();
    test_navmesh_walkable_uses_vertical_tolerance();
    test_capsule_collider_clamps_total_height_to_diameter();
    test_morphtarget_sanitizes_nonfinite_weights_and_deltas();
    test_scene_reparent_preserves_child_and_counts();
    test_scene_deep_hierarchy_traversal_and_transform_clamps();
    test_mesh_bone_weights_are_validated_and_dirty_geometry();
    test_transform_and_instance_batch_sanitize_extreme_matrices();
    test_obj_loader_recalculates_mixed_missing_normals();
    test_skeleton_bind_pose_and_animation_duration_sanitize();
    test_light_and_material_boolean_state_is_initialized();
    test_physics_joints_deduplicate_and_raycast_is_true_ray();
    test_terrain_water_and_vegetation_zero_dt_paths();
    test_particles_and_water_numeric_knobs_are_bounded();
    std::printf("RTGraphics3DRobustnessTests passed.\n");
    return 0;
}
