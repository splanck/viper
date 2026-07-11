//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_lightbaker3d.h
// Purpose: Public C ABI for baked GI — LightBaker3D (deterministic CPU
//   lightmap baker over static scene geometry) and LightProbeGrid3D (SH-9
//   irradiance probe grid with trilinear sampling and .vlpg serialization).
// Key invariants:
//   - Bakes are deterministic (fixed seeds); BakeStep is chunked and
//     main-thread only.
// Ownership/Lifetime:
//   - Objects are GC-managed; getters returning objects retain for the caller.
// Links: rt_lightbaker3d.c, misc/plans/thirdpersonupgrade/14-baked-gi.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LightBaker3D */
/// @brief Create a baker over @p scene (static-flagged mesh nodes participate).
void *rt_lightbaker3d_new(void *scene);
/// @brief Lightmap texel density in texels per world unit (default 8).
void rt_lightbaker3d_set_texels_per_unit(void *baker, double texels);
double rt_lightbaker3d_get_texels_per_unit(void *baker);
/// @brief Hemisphere samples per texel (default 64, clamped to 1024).
void rt_lightbaker3d_set_samples(void *baker, int64_t samples);
int64_t rt_lightbaker3d_get_samples(void *baker);
/// @brief Indirect bounce count (default 2, clamped to 8).
void rt_lightbaker3d_set_bounces(void *baker, int64_t bounces);
int64_t rt_lightbaker3d_get_bounces(void *baker);
/// @brief Sky radiance for rays that escape the scene (default black).
void rt_lightbaker3d_set_sky_color(void *baker, double r, double g, double b);
/// @brief Bake progress in [0, 1].
double rt_lightbaker3d_get_progress(void *baker);
/// @brief Register an explicit bake light (directional/point/spot; ambient skipped).
void rt_lightbaker3d_add_light(void *baker, void *light);
/// @brief Run one deterministic bake slice; returns 1 when the bake is complete.
int8_t rt_lightbaker3d_bake_step(void *baker);
/// @brief Install the baked atlas on baked nodes via material instances.
void rt_lightbaker3d_apply(void *baker);
/// @brief Retained baked atlas Pixels (NULL before completion).
void *rt_lightbaker3d_get_atlas(void *baker);

/* LightProbeGrid3D */
/// @brief Create a regular probe grid across [min, max] with @p spacing.
void *rt_lightprobegrid3d_new(void *min_v, void *max_v, double spacing);
/// @brief Total probe count.
int64_t rt_lightprobegrid3d_get_probe_count(void *grid);
/// @brief Bake probes against @p baker's scene/lights (shares its BVH + tracer).
void rt_lightprobegrid3d_bake(void *grid, void *baker);
/// @brief Trilinear SH irradiance sample for @p normal at @p position (Vec3).
void *rt_lightprobegrid3d_sample(void *grid, void *position, void *normal);
/// @brief Serialize to a versioned little-endian .vlpg file; 1 on success.
int8_t rt_lightprobegrid3d_save(void *grid, rt_string path);
/// @brief Load a .vlpg file, replacing this grid's payload; 1 on success.
int8_t rt_lightprobegrid3d_load(void *grid, rt_string path);

#ifdef __cplusplus
}
#endif
