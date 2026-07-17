//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_sky3d.h
// Purpose: Public C ABI for the procedural analytic sky (CPU-generated cubemap
//   installed through the existing skybox + IBL path).
// Key invariants:
//   - Deterministic function of sun direction, turbidity, and ground albedo.
// Ownership/Lifetime:
//   - GC-managed; getters returning objects retain for the caller.
// Links: rt_sky3d.c, misc/plans/thirdpersonupgrade/16-timeofday-weather.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a procedural sky with a mid-morning default sun.
void *rt_sky3d_new(void);
/// @brief Set the (normalized internally) direction TOWARD the sun; marks dirty.
void rt_sky3d_set_sun_direction(void *sky, void *direction);
/// @brief Atmospheric haze, 1 (clear) to 10 (hazy); marks dirty on change.
void rt_sky3d_set_turbidity(void *sky, double turbidity);
double rt_sky3d_get_turbidity(void *sky);
/// @brief Ground hemisphere albedo used below the horizon; marks dirty.
void rt_sky3d_set_ground_albedo(void *sky, double r, double g, double b);
/// @brief Cubemap face resolution (16..256, default 64); marks dirty on change.
void rt_sky3d_set_resolution(void *sky, int64_t resolution);
int64_t rt_sky3d_get_resolution(void *sky);
/// @brief True while the cubemap needs regeneration.
int8_t rt_sky3d_get_dirty(void *sky);
/// @brief Regenerate if dirty and install as @p canvas's skybox (NULL = no install).
int8_t rt_sky3d_update(void *sky, void *canvas);
/// @brief Retained generated CubeMap3D (NULL before the first Update).
void *rt_sky3d_get_cubemap(void *sky);

#ifdef __cplusplus
}
#endif
