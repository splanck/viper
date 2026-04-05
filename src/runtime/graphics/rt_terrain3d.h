//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_terrain3d.h
// Purpose: Heightmap-based terrain with chunked rendering, bilinear height
//   queries, and normal computation for physics/AI.
//
// Key invariants:
//   - Heights stored as float array (width * depth).
//   - Chunks are 16x16 quads, lazily built as Mesh3D objects.
//   - GetHeightAt uses bilinear interpolation between grid points.
//
// Links: rt_canvas3d.h, rt_mesh3d, vgfx3d_frustum.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_terrain3d_new(int64_t width, int64_t depth);
void rt_terrain3d_generate_perlin(
    void *terrain, void *perlin, double scale, int64_t octaves, double persistence);
void rt_terrain3d_set_heightmap(void *terrain, void *pixels);
void rt_terrain3d_set_material(void *terrain, void *material);
void rt_terrain3d_set_scale(void *terrain, double sx, double sy, double sz);
void rt_terrain3d_set_splat_map(void *terrain, void *pixels);
void rt_terrain3d_set_layer_texture(void *terrain, int64_t layer, void *pixels);
void rt_terrain3d_set_layer_scale(void *terrain, int64_t layer, double scale);
double rt_terrain3d_get_height_at(void *terrain, double x, double z);
void *rt_terrain3d_get_normal_at(void *terrain, double x, double z);
void rt_terrain3d_set_lod_distances(void *terrain, double near_dist, double far_dist);
void rt_terrain3d_set_skirt_depth(void *terrain, double depth);
void rt_canvas3d_draw_terrain(void *canvas, void *terrain);

#ifdef __cplusplus
}
#endif
