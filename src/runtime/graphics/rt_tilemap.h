//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_tilemap.h
// Purpose: Tile-based 2D map for efficient grid rendering, providing per-tile attribute storage, layer support, and optimized batch rendering to a canvas.
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

#ifdef __cplusplus
extern "C"
{
#endif

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
    /// @param coll_type 0=none, 1=solid, 2=one_way_up.
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

#ifdef __cplusplus
}
#endif
