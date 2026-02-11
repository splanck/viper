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

/// Collision type constants
#define TILE_COLLISION_NONE     0
#define TILE_COLLISION_SOLID    1
#define TILE_COLLISION_ONE_WAY  2

/// Maximum distinct tile IDs that can have collision types set
#define MAX_TILE_COLLISION_IDS 4096

/// @brief Tilemap implementation structure.
typedef struct rt_tilemap_impl
{
    int64_t width;        ///< Width in tiles
    int64_t height;       ///< Height in tiles
    int64_t tile_width;   ///< Tile width in pixels
    int64_t tile_height;  ///< Tile height in pixels
    int64_t tileset_cols; ///< Number of columns in tileset
    int64_t tileset_rows; ///< Number of rows in tileset
    int64_t tile_count;   ///< Total tiles in tileset
    void *tileset;        ///< Tileset pixels
    int64_t *tiles;       ///< Tile indices (row-major)
    int8_t collision[MAX_TILE_COLLISION_IDS]; ///< Collision type per tile ID
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

    // Initialize collision array to NONE
    memset(tilemap->collision, TILE_COLLISION_NONE, sizeof(tilemap->collision));

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

//=============================================================================
// Tile Collision
//=============================================================================

void rt_tilemap_set_collision(void *tilemap_ptr, int64_t tile_id, int64_t coll_type)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.SetCollision: null tilemap");
        return;
    }
    if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    tilemap->collision[tile_id] = (int8_t)coll_type;
}

int64_t rt_tilemap_get_collision(void *tilemap_ptr, int64_t tile_id)
{
    if (!tilemap_ptr)
    {
        rt_trap("Tilemap.GetCollision: null tilemap");
        return 0;
    }
    if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
        return 0;
    return ((rt_tilemap_impl *)tilemap_ptr)->collision[tile_id];
}

int8_t rt_tilemap_is_solid_at(void *tilemap_ptr, int64_t pixel_x, int64_t pixel_y)
{
    if (!tilemap_ptr)
        return 0;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    int64_t tx = pixel_x / tilemap->tile_width;
    int64_t ty = pixel_y / tilemap->tile_height;
    if (tx < 0 || tx >= tilemap->width || ty < 0 || ty >= tilemap->height)
        return 0;
    int64_t tile_id = tilemap->tiles[ty * tilemap->width + tx];
    if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
        return 0;
    return tilemap->collision[tile_id] == TILE_COLLISION_SOLID ? 1 : 0;
}

/// @brief Resolve an AABB against solid tiles. Returns 1 if any collision occurred.
/// Updates the position (out_x, out_y) and velocity (out_vx, out_vy) in-place.
int8_t rt_tilemap_collide_body(void *tilemap_ptr, void *body_ptr)
{
    if (!tilemap_ptr || !body_ptr)
        return 0;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    // Access body fields directly via the physics2d body struct layout.
    // The body struct starts with vptr, then x, y, w, h, vx, vy.
    typedef struct
    {
        void *vptr;
        double x, y, w, h, vx, vy;
    } body_header;
    body_header *body = (body_header *)body_ptr;

    int64_t tw = tilemap->tile_width;
    int64_t th = tilemap->tile_height;
    int8_t collided = 0;

    // Determine the range of tiles the body overlaps
    int64_t left   = (int64_t)body->x / tw;
    int64_t right  = (int64_t)(body->x + body->w - 1) / tw;
    int64_t top    = (int64_t)body->y / th;
    int64_t bottom = (int64_t)(body->y + body->h - 1) / th;

    // Clamp to tilemap bounds
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= tilemap->width) right = tilemap->width - 1;
    if (bottom >= tilemap->height) bottom = tilemap->height - 1;

    // Resolve collisions against each overlapping solid tile
    for (int64_t ty = top; ty <= bottom; ty++)
    {
        for (int64_t tx = left; tx <= right; tx++)
        {
            int64_t tile_id = tilemap->tiles[ty * tilemap->width + tx];
            if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
                continue;
            int8_t ctype = tilemap->collision[tile_id];
            if (ctype == TILE_COLLISION_NONE)
                continue;

            // Tile AABB in pixels
            double tile_x1 = (double)(tx * tw);
            double tile_y1 = (double)(ty * th);
            double tile_x2 = tile_x1 + (double)tw;
            double tile_y2 = tile_y1 + (double)th;

            // Body AABB
            double bx1 = body->x, by1 = body->y;
            double bx2 = body->x + body->w, by2 = body->y + body->h;

            // Check overlap
            if (bx2 <= tile_x1 || bx1 >= tile_x2 || by2 <= tile_y1 || by1 >= tile_y2)
                continue;

            // One-way platform: only collide if body is above tile and moving down
            if (ctype == TILE_COLLISION_ONE_WAY)
            {
                // Only collide if body bottom was above tile top last frame
                // Approximate: body velocity is downward and body center is above tile top
                if (body->vy <= 0.0 || (by2 - body->vy * 0.017) > tile_y1 + 2.0)
                    continue;
            }

            // Calculate overlap on each axis
            double ox = (bx2 < tile_x2) ? (bx2 - tile_x1) : (tile_x2 - bx1);
            double oy = (by2 < tile_y2) ? (by2 - tile_y1) : (tile_y2 - by1);

            // Resolve along minimum overlap axis
            if (ox < oy)
            {
                // Horizontal resolution
                if (bx1 + body->w * 0.5 < tile_x1 + (double)tw * 0.5)
                    body->x = tile_x1 - body->w; // Push left
                else
                    body->x = tile_x2; // Push right
                body->vx = 0.0;
            }
            else
            {
                // Vertical resolution
                if (by1 + body->h * 0.5 < tile_y1 + (double)th * 0.5)
                    body->y = tile_y1 - body->h; // Push up (landing)
                else
                    body->y = tile_y2; // Push down (hit ceiling)
                body->vy = 0.0;
            }
            collided = 1;
        }
    }

    return collided;
}
