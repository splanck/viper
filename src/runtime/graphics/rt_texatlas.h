//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_texatlas.h
// Purpose: 2D texture atlas with named rectangular regions over a Pixels buffer.
//          Enables efficient sprite sheet workflows by mapping string names to
//          sub-rectangles, so game code can reference frames by name instead of
//          raw pixel coordinates.
//
// Key invariants:
//   - The atlas retains a reference to its backing Pixels object.
//   - Region names longer than 31 bytes are rejected.
//   - Maximum 512 named regions per atlas.
//   - Region coordinates are not bounds-checked against the Pixels dimensions
//     at add time; out-of-bounds regions are clipped at draw time.
//
// Ownership/Lifetime:
//   - TextureAtlas objects are GC-managed (rt_obj_new_i64).
//   - The backing Pixels reference is retained and released in the finalizer.
//
// Links: src/runtime/graphics/rt_texatlas.c (implementation),
//        src/runtime/graphics/rt_spritebatch.h (batch drawing with atlas)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// TextureAtlas Creation
//=========================================================================

/// @brief Create a new texture atlas backed by a Pixels buffer.
/// @param pixels Backing Pixels object (retained by the atlas).
/// @return New TextureAtlas object, or NULL on failure.
void *rt_texatlas_new(void *pixels);

/// @brief Create a texture atlas by slicing a Pixels buffer into a grid.
/// @param pixels Backing Pixels object.
/// @param frame_w Width of each grid cell in pixels.
/// @param frame_h Height of each grid cell in pixels.
/// @return New TextureAtlas with auto-named regions ("0", "1", "2", ...).
void *rt_texatlas_load_grid(void *pixels, int64_t frame_w, int64_t frame_h);

//=========================================================================
// TextureAtlas Region Management
//=========================================================================

/// @brief Add a named rectangular region to the atlas.
/// @param atlas TextureAtlas object.
/// @param name Region name (max 31 characters, truncated if longer).
/// @param x Left edge of the region in the backing Pixels.
/// @param y Top edge of the region.
/// @param w Width of the region.
/// @param h Height of the region.
void rt_texatlas_add(void *atlas, void *name, int64_t x, int64_t y, int64_t w, int64_t h);

/// @brief Check whether a named region exists.
/// @return 1 if found, 0 otherwise.
int8_t rt_texatlas_has(void *atlas, void *name);

/// @brief Get the X coordinate of a named region.
/// @return Region X, or 0 if not found.
int64_t rt_texatlas_get_x(void *atlas, void *name);

/// @brief Get the Y coordinate of a named region.
int64_t rt_texatlas_get_y(void *atlas, void *name);

/// @brief Get the width of a named region.
int64_t rt_texatlas_get_w(void *atlas, void *name);

/// @brief Get the height of a named region.
int64_t rt_texatlas_get_h(void *atlas, void *name);

/// @brief Get the backing Pixels object.
void *rt_texatlas_get_pixels(void *atlas);

/// @brief Get the number of named regions.
int64_t rt_texatlas_region_count(void *atlas);

//=========================================================================
// SpriteBatch Atlas Drawing Extensions
//=========================================================================

/// @brief Draw a named atlas region through the sprite batch.
void rt_spritebatch_draw_atlas(void *batch, void *atlas, void *name, int64_t x, int64_t y);

/// @brief Draw a named atlas region with uniform scale.
void rt_spritebatch_draw_atlas_scaled(
    void *batch, void *atlas, void *name, int64_t x, int64_t y, int64_t scale);

/// @brief Draw a named atlas region with full transform.
void rt_spritebatch_draw_atlas_ex(void *batch,
                                  void *atlas,
                                  void *name,
                                  int64_t x,
                                  int64_t y,
                                  int64_t scale,
                                  int64_t rotation,
                                  int64_t depth);

#ifdef __cplusplus
}
#endif
