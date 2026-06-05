//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_terrain3d.h
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

/// @brief Create a heightmap terrain of (width × depth) grid samples.
void *rt_terrain3d_new(int64_t width, int64_t depth);
/// @brief Procedurally generate the heightmap from a Perlin noise instance.
void rt_terrain3d_generate_perlin(
    void *terrain, void *perlin, double scale, int64_t octaves, double persistence);
/// @brief Load the heightmap from a Pixels grayscale image (R channel = height).
void rt_terrain3d_set_heightmap(void *terrain, void *pixels);
/// @brief Bind a Material3D applied to all terrain chunks.
void rt_terrain3d_set_material(void *terrain, void *material);
/// @brief Set per-axis world-space scale (sx along X, sy along Y/height, sz along Z).
void rt_terrain3d_set_scale(void *terrain, double sx, double sy, double sz);
/// @brief Bind a 4-channel splat map (RGBA selects which of 4 layer textures shows at each texel).
void rt_terrain3d_set_splat_map(void *terrain, void *pixels);
/// @brief Bind a texture for one of the 4 splat layers (@p layer in 0..3).
void rt_terrain3d_set_layer_texture(void *terrain, int64_t layer, void *pixels);
/// @brief Set the UV tiling scale of one splat layer.
void rt_terrain3d_set_layer_scale(void *terrain, int64_t layer, double scale);
/// @brief Bilinearly interpolate the terrain height at world-space (x, z).
double rt_terrain3d_get_height_at(void *terrain, double x, double z);
/// @brief Compute the surface normal at world-space (x, z) as a Vec3.
void *rt_terrain3d_get_normal_at(void *terrain, double x, double z);
/// @brief Configure level-of-detail switch distances (chunks past @p far_dist render as low-poly).
void rt_terrain3d_set_lod_distances(void *terrain, double near_dist, double far_dist);
/// @brief Configure the LOD hysteresis band that prevents threshold-edge chunk flicker.
/// @details The distance is measured in world units. Positive values make a chunk keep its current
///   LOD until the camera moves beyond a threshold plus/minus the band; zero restores immediate
///   threshold switching. The runtime clamps the band to a safe fraction of the configured
///   near/far LOD interval.
void rt_terrain3d_set_lod_hysteresis(void *terrain, double distance);
/// @brief Set the depth of skirts dropped from chunk edges (hides cracks between LOD seams).
void rt_terrain3d_set_skirt_depth(void *terrain, double depth);
/// @brief Enable or disable Terrain3D participation in Canvas3D CPU occlusion testing/writes.
/// @details Disabled by default because terrain chunk AABBs are not solid occluders and can hide
///   visible floor triangles at the horizon. Frustum culling remains active either way.
void rt_terrain3d_set_cpu_occlusion(void *terrain, int8_t enabled);
/// @brief Return whether Terrain3D CPU occlusion participation is enabled.
int8_t rt_terrain3d_get_cpu_occlusion(void *terrain);
/// @brief Number of chunks in the most recent terrain draw attempt.
int64_t rt_terrain3d_get_last_chunk_count(void *terrain);
/// @brief Number of chunks submitted to Canvas3D by the most recent terrain draw.
int64_t rt_terrain3d_get_last_drawn_chunk_count(void *terrain);
/// @brief Number of chunks rejected by Terrain3D's conservative frustum pre-pass.
int64_t rt_terrain3d_get_last_frustum_culled_chunk_count(void *terrain);
/// @brief Number of chunks whose selected LOD was missing and required fallback or skipped.
int64_t rt_terrain3d_get_last_missing_lod_count(void *terrain);
/// @brief Number of chunks whose selected LOD was clamped to maintain neighbor seam safety.
int64_t rt_terrain3d_get_last_lod_clamped_chunk_count(void *terrain);
/// @brief Number of chunks drawn at LOD0 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod0_chunk_count(void *terrain);
/// @brief Number of chunks drawn at LOD1 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod1_chunk_count(void *terrain);
/// @brief Number of chunks drawn at LOD2 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod2_chunk_count(void *terrain);

/// @brief Internal edge identifiers used by streaming terrain seam stitching.
enum {
    RT_TERRAIN3D_EDGE_WEST = 0,
    RT_TERRAIN3D_EDGE_EAST = 1,
    RT_TERRAIN3D_EDGE_NORTH = 2,
    RT_TERRAIN3D_EDGE_SOUTH = 3,
};

/// @brief Internal helper: average two terrain border edges in world-height space.
int64_t rt_terrain3d_stitch_edge(void *terrain,
                                 int64_t edge,
                                 void *neighbor,
                                 int64_t neighbor_edge);
/// @brief Internal helper: build a Pixels heightmap from the terrain's current height grid.
void *rt_terrain3d_build_heightmap_pixels(void *terrain);
/// @brief Internal helper: build a Mesh3D approximation of the terrain for nav baking.
void *rt_terrain3d_build_nav_mesh(void *terrain, int64_t step);
/// @brief Render @p terrain (LOD-selected, frustum-culled chunks) onto the
///        3D canvas.
void rt_canvas3d_draw_terrain(void *canvas, void *terrain);
/// @brief Internal helper: render @p terrain translated into world space.
void rt_canvas3d_draw_terrain_at(void *canvas, void *terrain, double x, double y, double z);

#ifdef __cplusplus
}
#endif
