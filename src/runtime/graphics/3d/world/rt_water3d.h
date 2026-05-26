//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_water3d.h
// Purpose: Water plane with animated sine-based waves, transparency,
//   and optional tinted color. Rendered as a dynamic mesh updated each frame.
//
// Key invariants:
//   - Wave height = amplitude * sin(frequency * (x + z) + time * speed).
//   - Mesh regenerated each Update() call with new vertex positions.
//   - Transparent material (alpha blend) with configurable color.
//
// Links: rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a water plane sized (@p width × @p depth) world units centered at the origin.
void *rt_water3d_new(double width, double depth);
/// @brief Set the world Y-coordinate of the water surface.
void rt_water3d_set_height(void *water, double y);
/// @brief Configure the legacy single-sine-wave parameters (speed, amplitude, frequency).
void rt_water3d_set_wave_params(void *water, double speed, double amplitude, double frequency);
/// @brief Set the water's tint color and per-pixel alpha (0–1).
void rt_water3d_set_color(void *water, double r, double g, double b, double alpha);
/// @brief Bind a Pixels surface texture (tiled across the water plane).
void rt_water3d_set_texture(void *water, void *pixels);
/// @brief Bind a tangent-space normal map for additional wave detail.
void rt_water3d_set_normal_map(void *water, void *pixels);
/// @brief Bind a CubeMap3D for environment reflections on the water surface.
void rt_water3d_set_env_map(void *water, void *cubemap);
/// @brief Set how strongly the env map reflects (0–1).
void rt_water3d_set_reflectivity(void *water, double r);
/// @brief Set the mesh tessellation resolution (clamped to [8, 256]).
void rt_water3d_set_resolution(void *water, int64_t resolution);
/// @brief Add a Gerstner wave (direction, speed, amplitude, wavelength). Up to 8 waves total.
void rt_water3d_add_wave(
    void *water, double dirX, double dirZ, double speed, double amplitude, double wavelength);
/// @brief Remove all Gerstner waves (reverts to the legacy single-sine wave).
void rt_water3d_clear_waves(void *water);
/// @brief Tick the water clock by @p dt seconds and rebuild the deformed mesh.
void rt_water3d_update(void *water, double dt);
/// @brief Render the water surface onto @p canvas (drawn with backface culling disabled).
void rt_canvas3d_draw_water(void *canvas, void *water, void *camera);

#ifdef __cplusplus
}
#endif
