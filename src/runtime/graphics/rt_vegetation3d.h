//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_vegetation3d.h
// Purpose: Instanced vegetation rendering — grass blades, foliage, etc.
//   Cross-billboard geometry with wind animation and distance LOD.
//
// Key invariants:
//   - Blade mesh is two perpendicular quads (cross-billboard, 8 verts).
//   - Instances stored as float[16] transforms (same as InstanceBatch3D).
//   - Wind applied as Y-axis shear per-frame before rendering.
//   - LOD: blades thinned by distance, culled beyond far threshold.
//
// Links: rt_canvas3d.h, rt_terrain3d.h, rt_instbatch3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a vegetation system that uses @p blade_texture for each grass quad.
void *rt_vegetation3d_new(void *blade_texture);
/// @brief Bind a Pixels density map (R channel modulates per-tile blade count).
void rt_vegetation3d_set_density_map(void *veg, void *pixels);
/// @brief Configure wind animation parameters (speed = waving frequency, strength = max bend, turbulence = noise factor).
void rt_vegetation3d_set_wind_params(void *veg, double speed, double strength, double turbulence);
/// @brief Set near (full density) and far (cull) LOD distances in world units.
void rt_vegetation3d_set_lod_distances(void *veg, double near_dist, double far_dist);
/// @brief Set per-blade dimensions: average width, average height, and ±variation factor.
void rt_vegetation3d_set_blade_size(void *veg, double width, double height, double variation);
/// @brief Spawn @p count blades scattered across the terrain (sampled using density map if set).
void rt_vegetation3d_populate(void *veg, void *terrain, int64_t count);
/// @brief Advance wind animation by @p dt and apply distance-based culling against the camera.
void rt_vegetation3d_update(void *veg, double dt, double camX, double camY, double camZ);
/// @brief Render all visible blades as a single instanced draw call.
void rt_canvas3d_draw_vegetation(void *canvas, void *veg);

#ifdef __cplusplus
}
#endif
