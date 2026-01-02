//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_tilemap.c
// Purpose: Tilemap class implementation for tile-based 2D rendering.
//
//===----------------------------------------------------------------------===//

#include "rt_tilemap.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <stdlib.h>
#include <string.h>

/// @brief Tilemap implementation structure.
typedef struct rt_tilemap_impl
{
    int64_t width;       ///< Width in tiles
    int64_t height;      ///< Height in tiles
    int64_t tile_width;  ///< Tile width in pixels
    int64_t tile_height; ///< Tile height in pixels
    int64_t tileset_cols;///< Number of columns in tileset
    int64_t tileset_rows;///< Number of rows in tileset
    int64_t tile_count;  ///< Total tiles in tileset
    void *tileset;       ///< Tileset pixels
    int64_t *tiles;      ///< Tile indices (row-major)
} rt_tilemap_impl;

//=============================================================================
// Tilemap Creation
//=============================================================================

void *rt_tilemap_new(int64_t width, int64_t height, int64_t tile_width, int64_t tile_height)
{
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    if (tile_width <= 0)
        tile_width = 16;
    if (tile_height <= 0)
        tile_height = 16;

    int64_t tile_count = width * height;

    // Check for overflow
    if (tile_count / width != height)
    {
        rt_trap("Tilemap: dimensions too large");
        return NULL;
    }

    size_t tiles_size = (size_t)tile_count * sizeof(int64_t);
    size_t total_size = sizeof(rt_tilemap_impl) + tiles_size;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)rt_obj_new_i64(0, (int64_t)total_size);
    if (!tilemap)
        return NULL;

    tilemap->width = width;
    tilemap->height = height;
    tilemap->tile_width = tile_width;
    tilemap->tile_height = tile_height;
    tilemap->tileset_cols = 0;
    tilemap->tileset_rows = 0;
    tilemap->tile_count = 0;
    tilemap->tileset = NULL;
    tilemap->tiles = (int64_t *)((uint8_t *)tilemap + sizeof(rt_tilemap_impl));

    // Initialize all tiles to 0 (empty)
    memset(tilemap->tiles, 0, tiles_size);

    return tilemap;
}

//=============================================================================
// Tilemap Properties
//=============================================================================

int64_t rt_tilemap_get_width(void *tilemap_ptr)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.Width: null tilemap");
        return 0;
    }
    return ((rt_tilemap_impl *)tilemap_ptr)->width;
}

int64_t rt_tilemap_get_height(void *tilemap_ptr)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.Height: null tilemap");
        return 0;
    }
    return ((rt_tilemap_impl *)tilemap_ptr)->height;
}

int64_t rt_tilemap_get_tile_width(void *tilemap_ptr)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.TileWidth: null tilemap");
        return 0;
    }
    return ((rt_tilemap_impl *)tilemap_ptr)->tile_width;
}

int64_t rt_tilemap_get_tile_height(void *tilemap_ptr)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.TileHeight: null tilemap");
        return 0;
    }
    return ((rt_tilemap_impl *)tilemap_ptr)->tile_height;
}

//=============================================================================
// Tileset Management
//=============================================================================

void rt_tilemap_set_tileset(void *tilemap_ptr, void *pixels)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.SetTileset: null tilemap");
        return;
    }
    if (!pixels)
    {
        rt_trap("Tilemap.SetTileset: null pixels");
        return;
    }

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    // Clone the pixels and store
    void *cloned = rt_pixels_clone(pixels);
    if (!cloned)
        return;

    tilemap->tileset = cloned;
    rt_heap_retain(cloned);

    // Calculate tileset dimensions
    int64_t ts_width = rt_pixels_width(cloned);
    int64_t ts_height = rt_pixels_height(cloned);

    tilemap->tileset_cols = ts_width / tilemap->tile_width;
    tilemap->tileset_rows = ts_height / tilemap->tile_height;
    tilemap->tile_count = tilemap->tileset_cols * tilemap->tileset_rows;
}

int64_t rt_tilemap_get_tile_count(void *tilemap_ptr)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.TileCount: null tilemap");
        return 0;
    }
    return ((rt_tilemap_impl *)tilemap_ptr)->tile_count;
}

//=============================================================================
// Tile Access
//=============================================================================

void rt_tilemap_set_tile(void *tilemap_ptr, int64_t x, int64_t y, int64_t tile_index)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.SetTile: null tilemap");
        return;
    }
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return;

    tilemap->tiles[y * tilemap->width + x] = tile_index;
}

int64_t rt_tilemap_get_tile(void *tilemap_ptr, int64_t x, int64_t y)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.GetTile: null tilemap");
        return 0;
    }
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return 0;

    return tilemap->tiles[y * tilemap->width + x];
}

void rt_tilemap_fill(void *tilemap_ptr, int64_t tile_index)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.Fill: null tilemap");
        return;
    }
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    int64_t count = tilemap->width * tilemap->height;
    for (int64_t i = 0; i < count; i++)
        tilemap->tiles[i] = tile_index;
}

void rt_tilemap_clear(void *tilemap_ptr)
{
    rt_tilemap_fill(tilemap_ptr, 0);
}

void rt_tilemap_fill_rect(
    void *tilemap_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t tile_index)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.FillRect: null tilemap");
        return;
    }
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    // Clamp to bounds
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > tilemap->width)
        w = tilemap->width - x;
    if (y + h > tilemap->height)
        h = tilemap->height - y;

    for (int64_t ty = y; ty < y + h; ty++)
        for (int64_t tx = x; tx < x + w; tx++)
            tilemap->tiles[ty * tilemap->width + tx] = tile_index;
}

//=============================================================================
// Rendering
//=============================================================================

void rt_tilemap_draw(void *tilemap_ptr, void *canvas_ptr, int64_t offset_x, int64_t offset_y)
{
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    rt_tilemap_draw_region(
        tilemap_ptr, canvas_ptr, offset_x, offset_y, 0, 0, tilemap->width, tilemap->height);
}

void rt_tilemap_draw_region(void *tilemap_ptr,
                            void *canvas_ptr,
                            int64_t offset_x,
                            int64_t offset_y,
                            int64_t view_x,
                            int64_t view_y,
                            int64_t view_w,
                            int64_t view_h)
{
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    if (!tilemap->tileset || tilemap->tile_count == 0)
        return;

    // Clamp view to tilemap bounds
    if (view_x < 0)
        view_x = 0;
    if (view_y < 0)
        view_y = 0;
    if (view_x + view_w > tilemap->width)
        view_w = tilemap->width - view_x;
    if (view_y + view_h > tilemap->height)
        view_h = tilemap->height - view_y;

    int64_t tw = tilemap->tile_width;
    int64_t th = tilemap->tile_height;
    int64_t ts_cols = tilemap->tileset_cols;

    // Draw visible tiles
    for (int64_t ty = view_y; ty < view_y + view_h; ty++)
    {
        for (int64_t tx = view_x; tx < view_x + view_w; tx++)
        {
            int64_t tile_index = tilemap->tiles[ty * tilemap->width + tx];

            // Skip empty tiles (index 0)
            if (tile_index <= 0 || tile_index > tilemap->tile_count)
                continue;

            // Adjust for 1-based indexing (0 = empty)
            int64_t ti = tile_index - 1;

            // Calculate tile position in tileset
            int64_t ts_x = (ti % ts_cols) * tw;
            int64_t ts_y = (ti / ts_cols) * th;

            // Calculate screen position
            int64_t screen_x = tx * tw + offset_x;
            int64_t screen_y = ty * th + offset_y;

            // Blit the tile
            rt_canvas_blit_region(
                canvas_ptr, screen_x, screen_y, tilemap->tileset, ts_x, ts_y, tw, th);
        }
    }
}

//=============================================================================
// Utility
//=============================================================================

void rt_tilemap_pixel_to_tile(
    void *tilemap_ptr, int64_t pixel_x, int64_t pixel_y, int64_t *tile_x, int64_t *tile_y)
{
    if (!tilemap_ptr || !tile_x || !tile_y)
        return;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    *tile_x = pixel_x / tilemap->tile_width;
    *tile_y = pixel_y / tilemap->tile_height;
}

int64_t rt_tilemap_to_tile_x(void *tilemap_ptr, int64_t pixel_x)
{
    if (!tilemap_ptr)
        return 0;
    return pixel_x / ((rt_tilemap_impl *)tilemap_ptr)->tile_width;
}

int64_t rt_tilemap_to_tile_y(void *tilemap_ptr, int64_t pixel_y)
{
    if (!tilemap_ptr)
        return 0;
    return pixel_y / ((rt_tilemap_impl *)tilemap_ptr)->tile_height;
}

void rt_tilemap_tile_to_pixel(
    void *tilemap_ptr, int64_t tile_x, int64_t tile_y, int64_t *pixel_x, int64_t *pixel_y)
{
    if (!tilemap_ptr || !pixel_x || !pixel_y)
        return;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    *pixel_x = tile_x * tilemap->tile_width;
    *pixel_y = tile_y * tilemap->tile_height;
}

int64_t rt_tilemap_to_pixel_x(void *tilemap_ptr, int64_t tile_x)
{
    if (!tilemap_ptr)
        return 0;
    return tile_x * ((rt_tilemap_impl *)tilemap_ptr)->tile_width;
}

int64_t rt_tilemap_to_pixel_y(void *tilemap_ptr, int64_t tile_y)
{
    if (!tilemap_ptr)
        return 0;
    return tile_y * ((rt_tilemap_impl *)tilemap_ptr)->tile_height;
}
