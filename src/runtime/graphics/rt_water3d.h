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

void *rt_water3d_new(double width, double depth);
void rt_water3d_set_height(void *water, double y);
void rt_water3d_set_wave_params(void *water, double speed, double amplitude, double frequency);
void rt_water3d_set_color(void *water, double r, double g, double b, double alpha);
void rt_water3d_update(void *water, double dt);
void rt_canvas3d_draw_water(void *canvas, void *water, void *camera);

#ifdef __cplusplus
}
#endif
