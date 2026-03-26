//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_sprite3d.h
// Purpose: 3D sprite — camera-facing textured billboard with sprite sheet
//   frame support. Used for 2D characters in 3D worlds, RPG items, trees.
//
// Key invariants:
//   - Always faces camera (billboard rendering).
//   - Frame rect selects sub-region of texture atlas.
//   - Anchor point controls pivot (0.5,0.5 = center).
//
// Links: rt_canvas3d.h, rt_particles3d.c (billboard math)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_sprite3d_new(void *texture);
    void rt_sprite3d_set_position(void *spr, double x, double y, double z);
    void rt_sprite3d_set_scale(void *spr, double w, double h);
    void rt_sprite3d_set_anchor(void *spr, double ax, double ay);
    void rt_sprite3d_set_frame(void *spr, int64_t fx, int64_t fy, int64_t fw, int64_t fh);
    void rt_canvas3d_draw_sprite3d(void *canvas, void *sprite, void *camera);

#ifdef __cplusplus
}
#endif
