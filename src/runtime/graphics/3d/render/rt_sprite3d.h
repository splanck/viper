//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_sprite3d.h
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
extern "C" {
#endif

/// @brief Create a billboarded 3D sprite from @p texture.
void *rt_sprite3d_new(void *texture);
/// @brief Set the sprite's world position.
void rt_sprite3d_set_position(void *spr, double x, double y, double z);
/// @brief Set the sprite's world-space width/height.
void rt_sprite3d_set_scale(void *spr, double w, double h);
/// @brief Set the pivot/anchor in [0,1] sprite-local coords (0.5,0.5 = center).
void rt_sprite3d_set_anchor(void *spr, double ax, double ay);
/// @brief Select a sub-rectangle (fx,fy,fw,fh) of the texture as the frame
///        (for sprite-sheet animation).
void rt_sprite3d_set_frame(void *spr, int64_t fx, int64_t fy, int64_t fw, int64_t fh);
/// @brief Shift sprite world-space state by the inverse of a floating-origin delta.
void rt_sprite3d_rebase_origin(void *spr, double dx, double dy, double dz);
/// @brief Toggle additive blending (glows/tracers) vs default alpha blending.
void rt_sprite3d_set_additive(void *spr, int8_t additive);
/// @brief Current additive-blend flag.
int8_t rt_sprite3d_get_additive(void *spr);
/// @brief Set a packed 0xRRGGBB tint multiplied into the sprite texture.
void rt_sprite3d_set_color(void *spr, int64_t rgb);
/// @brief Draw @p sprite billboarded toward @p camera onto the 3D canvas.
void rt_canvas3d_draw_sprite3d(void *canvas, void *sprite, void *camera);

#ifdef __cplusplus
}
#endif
