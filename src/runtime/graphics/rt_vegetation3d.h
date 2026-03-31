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

void *rt_vegetation3d_new(void *blade_texture);
void rt_vegetation3d_set_density_map(void *veg, void *pixels);
void rt_vegetation3d_set_wind_params(void *veg, double speed, double strength,
                                      double turbulence);
void rt_vegetation3d_set_lod_distances(void *veg, double near_dist, double far_dist);
void rt_vegetation3d_set_blade_size(void *veg, double width, double height,
                                     double variation);
void rt_vegetation3d_populate(void *veg, void *terrain, int64_t count);
void rt_vegetation3d_update(void *veg, double dt, double camX, double camY,
                             double camZ);
void rt_canvas3d_draw_vegetation(void *canvas, void *veg);

#ifdef __cplusplus
}
#endif
