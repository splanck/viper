//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_clusters.h
// Purpose: Internal declarations for clustered forward+ CPU froxel binning
//   (Plan 07). Split from rt_canvas3d_internal.h because these signatures use
//   backend types (vgfx3d_light_params_t / vgfx3d_cluster_table_t) that the
//   internal header deliberately stays free of.
// Key invariants:
//   - Engine-internal; included only by canvas TUs that already include
//     vgfx3d_backend.h and by the unit tests.
//   - Binning is deterministic and conservatively over-inclusive (see
//     rt_canvas3d_clusters.c).
// Ownership/Lifetime:
//   - Tables are POD owned by the canvas's revision-keyed ring.
// Links: rt_canvas3d_clusters.c, vgfx3d_backend.h,
//   misc/plans/3d_overhaul/07-clustered-lighting.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief World influence radius of a point/spot light (0 = none, <0 = unbounded).
float canvas3d_cluster_light_radius(float intensity, float attenuation);
/// @brief Exponential Z slice for a view depth (clamped to the grid).
int32_t canvas3d_cluster_z_slice(float depth, float znear, float zfar);
/// @brief 1 for directional/ambient light types (the global prefix), else 0.
int canvas3d_cluster_light_is_global(int32_t type);
/// @brief Build a froxel table for a globals-first flattened light array.
void canvas3d_build_cluster_table(const rt_canvas3d *c,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  uint32_t lights_revision,
                                  vgfx3d_cluster_table_t *out);
/// @brief Shader-mirror cluster index for (uv in [0,1], view depth).
int32_t canvas3d_cluster_index_for_point(float u, float v, float depth, float znear, float zfar);
/// @brief Fetch-or-build the froxel table matching a light revision.
/// @return NULL when clustering is off or the backend keeps the flat loop.
const vgfx3d_cluster_table_t *canvas3d_cluster_table_for_revision(
    rt_canvas3d *c, const vgfx3d_light_params_t *lights, int32_t light_count, uint32_t revision);

#ifdef __cplusplus
}
#endif
