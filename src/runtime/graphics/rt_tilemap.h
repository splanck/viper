//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_tilemap.h
// Purpose: Tile-based 2D map for efficient grid rendering, providing per-tile attribute storage,
// layer support, and optimized batch rendering to a canvas.
//
// Key invariants:
//   - Tile IDs are non-negative integers; 0 conventionally means empty.
//   - Multiple layers allow background/foreground separation.
//   - Tile dimensions are fixed at creation time.
//   - rt_tilemap_draw renders only visible tiles using viewport culling.
//
// Ownership/Lifetime:
//   - Tilemap objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_tilemap.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Tile collision type constants.
typedef enum {
    RT_TILE_COLLISION_NONE = 0,       ///< No collision (passable).
    RT_TILE_COLLISION_SOLID = 1,      ///< Fully solid (blocks movement).
    RT_TILE_COLLISION_ONE_WAY_UP = 2, ///< One-way platform (passable from below).
} rt_tilemap_collision_t;

//=========================================================================
// Tilemap Creation
//=========================================================================

/// @brief Create a new tilemap.
/// @param width Width in tiles.
/// @param height Height in tiles.
/// @param tile_width Width of each tile in pixels.
/// @param tile_height Height of each tile in pixels.
/// @return New Tilemap object.
void *rt_tilemap_new(int64_t width, int64_t height, int64_t tile_width, int64_t tile_height);

//=========================================================================
// Tilemap Properties
//=========================================================================

/// @brief Get tilemap width in tiles.
int64_t rt_tilemap_get_width(void *tilemap);

/// @brief Get tilemap height in tiles.
int64_t rt_tilemap_get_height(void *tilemap);

/// @brief Get tile width in pixels.
int64_t rt_tilemap_get_tile_width(void *tilemap);

/// @brief Get tile height in pixels.
int64_t rt_tilemap_get_tile_height(void *tilemap);

//=========================================================================
// Tileset Management
//=========================================================================

/// @brief Set the tileset image (sprite sheet).
/// @param tilemap Tilemap object.
/// @param pixels Pixels object containing tile graphics arranged in a grid.
/// @note Tiles are numbered left-to-right, top-to-bottom starting at 0.
void rt_tilemap_set_tileset(void *tilemap, void *pixels);

/// @brief Get number of tiles in the tileset.
int64_t rt_tilemap_get_tile_count(void *tilemap);

//=========================================================================
// Tile Access
//=========================================================================

/// @brief Set a tile at the given position.
/// @param tilemap Tilemap object.
/// @param x Tile X coordinate.
/// @param y Tile Y coordinate.
/// @param tile_index Index of the tile in the tileset (0 = empty/transparent).
void rt_tilemap_set_tile(void *tilemap, int64_t x, int64_t y, int64_t tile_index);

/// @brief Get the tile index at the given position.
/// @param tilemap Tilemap object.
/// @param x Tile X coordinate.
/// @param y Tile Y coordinate.
/// @return Tile index (0 = empty).
int64_t rt_tilemap_get_tile(void *tilemap, int64_t x, int64_t y);

/// @brief Fill the entire tilemap with a single tile.
/// @param tilemap Tilemap object.
/// @param tile_index Tile index to fill with.
void rt_tilemap_fill(void *tilemap, int64_t tile_index);

/// @brief Clear the tilemap (set all tiles to 0).
void rt_tilemap_clear(void *tilemap);

/// @brief Fill a rectangular region with a tile.
/// @param tilemap Tilemap object.
/// @param x Start X coordinate.
/// @param y Start Y coordinate.
/// @param w Width in tiles.
/// @param h Height in tiles.
/// @param tile_index Tile index to fill with.
void rt_tilemap_fill_rect(
    void *tilemap, int64_t x, int64_t y, int64_t w, int64_t h, int64_t tile_index);

//=========================================================================
// Rendering
//=========================================================================

/// @brief Draw the tilemap to a canvas.
/// @param tilemap Tilemap object.
/// @param canvas Canvas to draw on.
/// @param offset_x X offset for scrolling.
/// @param offset_y Y offset for scrolling.
void rt_tilemap_draw(void *tilemap, void *canvas, int64_t offset_x, int64_t offset_y);

/// @brief Draw a portion of the tilemap (for culling).
/// @param tilemap Tilemap object.
/// @param canvas Canvas to draw on.
/// @param offset_x X offset for scrolling.
/// @param offset_y Y offset for scrolling.
/// @param view_x View start X in tiles.
/// @param view_y View start Y in tiles.
/// @param view_w View width in tiles.
/// @param view_h View height in tiles.
void rt_tilemap_draw_region(void *tilemap,
                            void *canvas,
                            int64_t offset_x,
                            int64_t offset_y,
                            int64_t view_x,
                            int64_t view_y,
                            int64_t view_w,
                            int64_t view_h);

//=========================================================================
// Utility
//=========================================================================

/// @brief Convert pixel coordinates to tile coordinates.
/// @param tilemap Tilemap object.
/// @param pixel_x Pixel X coordinate.
/// @param pixel_y Pixel Y coordinate.
/// @param tile_x Output: Tile X coordinate.
/// @param tile_y Output: Tile Y coordinate.
void rt_tilemap_pixel_to_tile(
    void *tilemap, int64_t pixel_x, int64_t pixel_y, int64_t *tile_x, int64_t *tile_y);

/// @brief Get tile X from pixel X.
int64_t rt_tilemap_to_tile_x(void *tilemap, int64_t pixel_x);

/// @brief Get tile Y from pixel Y.
int64_t rt_tilemap_to_tile_y(void *tilemap, int64_t pixel_y);

/// @brief Convert tile coordinates to pixel coordinates.
/// @param tilemap Tilemap object.
/// @param tile_x Tile X coordinate.
/// @param tile_y Tile Y coordinate.
/// @param pixel_x Output: Pixel X coordinate.
/// @param pixel_y Output: Pixel Y coordinate.
void rt_tilemap_tile_to_pixel(
    void *tilemap, int64_t tile_x, int64_t tile_y, int64_t *pixel_x, int64_t *pixel_y);

/// @brief Get pixel X from tile X.
int64_t rt_tilemap_to_pixel_x(void *tilemap, int64_t tile_x);

/// @brief Get pixel Y from tile Y.
int64_t rt_tilemap_to_pixel_y(void *tilemap, int64_t tile_y);

//=========================================================================
// Tile Collision
//=========================================================================

/// @brief Set collision type for a tile ID.
/// @param tilemap Tilemap object.
/// @param tile_id Tile index (0-4095).
/// @param coll_type Collision type (RT_TILE_COLLISION_NONE, _SOLID, or _ONE_WAY_UP).
void rt_tilemap_set_collision(void *tilemap, int64_t tile_id, int64_t coll_type);

/// @brief Get collision type for a tile ID.
int64_t rt_tilemap_get_collision(void *tilemap, int64_t tile_id);

/// @brief Check if a pixel position is on a solid tile.
int8_t rt_tilemap_is_solid_at(void *tilemap, int64_t pixel_x, int64_t pixel_y);

/// @brief Resolve a Physics2D.Body against solid/one-way tiles.
/// @param tilemap Tilemap object.
/// @param body Physics2D.Body object.
/// @return 1 if any collision occurred, 0 otherwise.
int8_t rt_tilemap_collide_body(void *tilemap, void *body);

//=========================================================================
// Layer Management
//=========================================================================

/// @brief Add a named layer to the tilemap.
/// @param tilemap Tilemap object.
/// @param name Layer name (up to 31 characters; truncated if longer).
/// @return Layer ID (1–15) on success, -1 if at maximum (16 layers).
int64_t rt_tilemap_add_layer(void *tilemap, rt_string name);

/// @brief Get the number of layers.
int64_t rt_tilemap_get_layer_count(void *tilemap);

/// @brief Find a layer by name.
/// @return Layer ID, or -1 if not found.
int64_t rt_tilemap_get_layer_by_name(void *tilemap, rt_string name);

/// @brief Remove a layer. Layer 0 (base) cannot be removed.
void rt_tilemap_remove_layer(void *tilemap, int64_t layer);

/// @brief Set layer visibility.
void rt_tilemap_set_layer_visible(void *tilemap, int64_t layer, int8_t visible);

/// @brief Get layer visibility.
int8_t rt_tilemap_get_layer_visible(void *tilemap, int64_t layer);

//=========================================================================
// Per-Layer Tile Access
//=========================================================================

/// @brief Set a tile on a specific layer.
void rt_tilemap_set_tile_layer(void *tilemap, int64_t layer, int64_t x, int64_t y, int64_t tile);

/// @brief Get a tile from a specific layer.
int64_t rt_tilemap_get_tile_layer(void *tilemap, int64_t layer, int64_t x, int64_t y);

/// @brief Fill an entire layer with a single tile.
void rt_tilemap_fill_layer(void *tilemap, int64_t layer, int64_t tile);

/// @brief Clear a layer (set all tiles to 0).
void rt_tilemap_clear_layer(void *tilemap, int64_t layer);

//=========================================================================
// Per-Layer Tileset
//=========================================================================

/// @brief Set a per-layer tileset. NULL = use base layer tileset.
void rt_tilemap_set_layer_tileset(void *tilemap, int64_t layer, void *pixels);

//=========================================================================
// Per-Layer Rendering
//=========================================================================

/// @brief Draw a single layer to a canvas.
void rt_tilemap_draw_layer(
    void *tilemap, void *canvas, int64_t layer, int64_t cam_x, int64_t cam_y);

//=========================================================================
// Collision Layer
//=========================================================================

/// @brief Set which layer is used for collision queries.
void rt_tilemap_set_collision_layer(void *tilemap, int64_t layer);

/// @brief Get the current collision layer index.
int64_t rt_tilemap_get_collision_layer(void *tilemap);

//=========================================================================
// File I/O
//=========================================================================

/// @brief Save tilemap to JSON file.
int8_t rt_tilemap_save_to_file(void *tm, rt_string path);

/// @brief Load tilemap from JSON file.
void *rt_tilemap_load_from_file(rt_string path);

/// @brief Load tilemap from CSV file (Tiled export format).
void *rt_tilemap_load_csv(rt_string path, int64_t tile_w, int64_t tile_h);

//=========================================================================
// Auto-Tiling
//=========================================================================

/// @brief Set auto-tile rule variants 0-7 (lower half of 4-bit bitmask).
void rt_tilemap_set_autotile_lo(void *tm,
                                int64_t base_tile,
                                int64_t v0,
                                int64_t v1,
                                int64_t v2,
                                int64_t v3,
                                int64_t v4,
                                int64_t v5,
                                int64_t v6,
                                int64_t v7);

/// @brief Set auto-tile rule variants 8-15 (upper half of 4-bit bitmask).
void rt_tilemap_set_autotile_hi(void *tm,
                                int64_t base_tile,
                                int64_t v8,
                                int64_t v9,
                                int64_t v10,
                                int64_t v11,
                                int64_t v12,
                                int64_t v13,
                                int64_t v14,
                                int64_t v15);

/// @brief Clear auto-tile rule for a base tile.
void rt_tilemap_clear_autotile(void *tm, int64_t base_tile);

/// @brief Apply auto-tiling to entire tilemap.
void rt_tilemap_apply_autotile(void *tm);

/// @brief Apply auto-tiling to a region.
void rt_tilemap_apply_autotile_region(void *tm, int64_t x, int64_t y, int64_t w, int64_t h);

//=========================================================================
// Tile Properties
//=========================================================================

/// @brief Set a property on a tile ID.
void rt_tilemap_set_tile_property(void *tm, int64_t tile_index, rt_string key, int64_t value);

/// @brief Get a property from a tile ID (returns default_val if not found).
int64_t rt_tilemap_get_tile_property(void *tm,
                                     int64_t tile_index,
                                     rt_string key,
                                     int64_t default_val);

/// @brief Check if a tile ID has a property.
int8_t rt_tilemap_has_tile_property(void *tm, int64_t tile_index, rt_string key);

//=========================================================================
// Tile Animation
//=========================================================================

/// @brief Register an animated tile. Frames default to base_id, base_id+1, ...
void rt_tilemap_set_tile_anim(void *tm,
                              int64_t base_tile_id,
                              int64_t frame_count,
                              int64_t ms_per_frame);

/// @brief Override a specific frame's tile ID for a registered animation.
void rt_tilemap_set_tile_anim_frame(void *tm,
                                    int64_t base_tile_id,
                                    int64_t frame_idx,
                                    int64_t tile_id);

/// @brief Advance all tile animations by dt milliseconds.
void rt_tilemap_update_anims(void *tm, int64_t dt_ms);

/// @brief Resolve a tile ID through animation (returns current frame's tile ID).
int64_t rt_tilemap_resolve_anim_tile(void *tm, int64_t tile_id);

#ifdef __cplusplus
}
#endif
