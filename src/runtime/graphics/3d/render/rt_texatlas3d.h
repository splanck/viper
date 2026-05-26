//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_texatlas3d.h
// Purpose: Texture atlas — packs multiple textures into one large texture
//   to reduce texture switches during rendering.
//
// Key invariants:
//   - Shelf-based bin packing (row-by-row placement).
//   - 1-pixel border padding to prevent texture bleeding.
//   - UV rects returned as normalized [0,1] coordinates.
//
// Links: rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 3D texture atlas of the given pixel dimensions.
void *rt_texatlas3d_new(int64_t width, int64_t height);
/// @brief Pack a Pixels image into the atlas; returns its region id (or -1).
int64_t rt_texatlas3d_add(void *atlas, void *pixels);
/// @brief Get the GPU/runtime texture handle backing the whole atlas.
void *rt_texatlas3d_get_texture(void *atlas);
/// @brief Get the normalized UV rect (u0,v0)-(u1,v1) for a packed region id.
void rt_texatlas3d_get_uv_rect(
    void *atlas, int64_t id, double *u0, double *v0, double *u1, double *v1);

#ifdef __cplusplus
}
#endif
