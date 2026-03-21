//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_tilemap.c
// Purpose: Tile-based 2D map renderer for Viper games. Manages a 2D array of
//   tile IDs, a corresponding SpriteSheet atlas, and draws the visible portion
//   of the map to a Canvas each frame by blitting individual tile cells from
//   the sheet. Supports scrolling via a camera offset, solid/passable tile
//   classification for physics integration, and efficient viewport culling to
//   skip tiles outside the visible region.
//
// Key invariants:
//   - Tile dimensions (tile_w, tile_h) and map dimensions (cols, rows) are
//     fixed at creation. The tile array is a flat row-major int64 array with
//       index = row × cols + col
//     Tile ID 0 conventionally means "empty" (not drawn).
//   - The tilemap blits tiles using the associated SpriteSheet. Tile IDs map
//     to sprite sheet frames in row-major order: tile N → frame N.
//   - Solid tiles are tracked separately via a passability bitmask or boolean
//     array. rt_tilemap_is_solid() enables the Physics2D integration to treat
//     tile cells as static collision geometry.
//   - Camera offset (scroll_x, scroll_y) is in world-pixel coordinates.
//     During Draw(), the visible tile range is computed from the offset and
//     viewport size to avoid drawing off-screen tiles.
//   - No dirty-region tracking: the full visible tile range is redrawn every
//     frame. For static maps this is efficient; for dynamic maps consider
//     partial-update strategies externally.
//
// Ownership/Lifetime:
//   - Tilemap objects are GC-managed (rt_obj_new_i64). The tile array and any
//     solid-flag array are freed by the GC finalizer.
//
// Links: src/runtime/graphics/rt_tilemap.h (public API),
//        src/runtime/graphics/rt_spritesheet.h (tile atlas),
//        docs/viperlib/game.md (Tilemap section)
//
//===----------------------------------------------------------------------===//

#include "rt_tilemap.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_physics2d.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Collision type constants — use public enum from rt_tilemap.h.
#define TILE_COLLISION_NONE RT_TILE_COLLISION_NONE
#define TILE_COLLISION_SOLID RT_TILE_COLLISION_SOLID
#define TILE_COLLISION_ONE_WAY RT_TILE_COLLISION_ONE_WAY_UP

/// Maximum distinct tile IDs that can have collision types set
#define MAX_TILE_COLLISION_IDS 4096

/// Maximum number of layers per tilemap
#define TM_MAX_LAYERS 16

/// Per-layer metadata and tile grid
typedef struct
{
    int64_t *tiles; ///< width * height tile indices (NULL if unused)
    void *tileset;  ///< Per-layer tileset (NULL = use base)
    int64_t tileset_cols;
    int64_t tileset_rows;
    int64_t tile_count; ///< Tiles in this layer's tileset
    char name[32];      ///< Layer name
    int8_t visible;     ///< 1 = visible, 0 = hidden
    int8_t owns_tiles;  ///< 1 = tiles was malloc'd (layers 1+), 0 = inline (layer 0)
} tm_layer;

/// @brief Tilemap implementation structure.
typedef struct rt_tilemap_impl
{
    int64_t width;                            ///< Width in tiles
    int64_t height;                           ///< Height in tiles
    int64_t tile_width;                       ///< Tile width in pixels
    int64_t tile_height;                      ///< Tile height in pixels
    int64_t tileset_cols;                     ///< Number of columns in base tileset
    int64_t tileset_rows;                     ///< Number of rows in base tileset
    int64_t tile_count;                       ///< Total tiles in base tileset
    void *tileset;                            ///< Base tileset pixels
    int64_t *tiles;                           ///< Layer 0 tile indices (row-major, inline)
    int8_t collision[MAX_TILE_COLLISION_IDS]; ///< Collision type per tile ID
    tm_layer layers[TM_MAX_LAYERS];           ///< Layer array
    int32_t layer_count;                      ///< Current number of layers (starts at 1)
    int32_t collision_layer;                  ///< Which layer has collision data (default 0)
} rt_tilemap_impl;

//=============================================================================
// Tilemap Creation
//=============================================================================

static void tilemap_finalize(void *obj)
{
    rt_tilemap_impl *tm = (rt_tilemap_impl *)obj;
    /* Release base tileset */
    if (tm->tileset)
        rt_heap_release(tm->tileset);
    /* Free per-layer owned tiles and release per-layer tilesets */
    for (int32_t i = 0; i < tm->layer_count; i++)
    {
        if (tm->layers[i].owns_tiles && tm->layers[i].tiles)
            free(tm->layers[i].tiles);
        if (tm->layers[i].tileset)
            rt_heap_release(tm->layers[i].tileset);
    }
}

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

    // Initialize layer 0 (base layer)
    memset(tilemap->layers, 0, sizeof(tilemap->layers));
    tilemap->layer_count = 1;
    tilemap->collision_layer = 0;
    tilemap->layers[0].tiles = tilemap->tiles;
    tilemap->layers[0].tileset = NULL; // uses base tileset
    tilemap->layers[0].tileset_cols = 0;
    tilemap->layers[0].tileset_rows = 0;
    tilemap->layers[0].tile_count = 0;
    tilemap->layers[0].visible = 1;
    tilemap->layers[0].owns_tiles = 0; // inline allocation
    memcpy(tilemap->layers[0].name, "base", 5);

    rt_obj_set_finalizer(tilemap, tilemap_finalize);
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

    // Clone the pixels and store, releasing the old tileset first
    void *cloned = rt_pixels_clone(pixels);
    if (!cloned)
        return;

    if (tilemap->tileset)
        rt_heap_release(tilemap->tileset);

    tilemap->tileset = cloned;
    rt_heap_retain(cloned);

    // Calculate tileset dimensions
    int64_t ts_width = rt_pixels_width(cloned);
    int64_t ts_height = rt_pixels_height(cloned);

    tilemap->tileset_cols = ts_width / tilemap->tile_width;
    tilemap->tileset_rows = ts_height / tilemap->tile_height;
    tilemap->tile_count = tilemap->tileset_cols * tilemap->tileset_rows;

    // Sync layer 0 tileset info
    tilemap->layers[0].tileset_cols = tilemap->tileset_cols;
    tilemap->layers[0].tileset_rows = tilemap->tileset_rows;
    tilemap->layers[0].tile_count = tilemap->tile_count;
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
    int64_t tw = tilemap->tile_width > 0 ? tilemap->tile_width : 1;
    int64_t th = tilemap->tile_height > 0 ? tilemap->tile_height : 1;

    /* Compute the first tile that is visible (partially or fully) on screen */
    int64_t first_x = (-offset_x) / tw;
    int64_t first_y = (-offset_y) / th;
    if (first_x < 0)
        first_x = 0;
    if (first_y < 0)
        first_y = 0;

    /* Compute how many tiles fit in the canvas (plus two for partial edges) */
    int64_t canvas_w = rt_canvas_width(canvas_ptr);
    int64_t canvas_h = rt_canvas_height(canvas_ptr);
    int64_t vis_w = canvas_w / tw + 2;
    int64_t vis_h = canvas_h / th + 2;

    /* Clamp to tilemap dimensions */
    if (first_x + vis_w > tilemap->width)
        vis_w = tilemap->width - first_x;
    if (first_y + vis_h > tilemap->height)
        vis_h = tilemap->height - first_y;
    if (vis_w <= 0 || vis_h <= 0)
        return;

    rt_tilemap_draw_region(
        tilemap_ptr, canvas_ptr, offset_x, offset_y, first_x, first_y, vis_w, vis_h);
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
    int32_t cl = tilemap->collision_layer;
    if (cl < 0 || cl >= tilemap->layer_count || !tilemap->layers[cl].tiles)
        return 0;
    int64_t tile_id = tilemap->layers[cl].tiles[ty * tilemap->width + tx];
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

    int64_t tw = tilemap->tile_width;
    int64_t th = tilemap->tile_height;
    int8_t collided = 0;

    // Use the designated collision layer's tile grid
    int32_t cl = tilemap->collision_layer;
    if (cl < 0 || cl >= tilemap->layer_count || !tilemap->layers[cl].tiles)
        return 0;
    int64_t *coll_tiles = tilemap->layers[cl].tiles;

    // Read body state via the public physics2d API (avoids fragile struct cast)
    double bx = rt_physics2d_body_x(body_ptr);
    double by = rt_physics2d_body_y(body_ptr);
    double bw = rt_physics2d_body_w(body_ptr);
    double bh = rt_physics2d_body_h(body_ptr);
    double bvx = rt_physics2d_body_vx(body_ptr);
    double bvy = rt_physics2d_body_vy(body_ptr);

    // Determine the range of tiles the body overlaps
    int64_t left = (int64_t)bx / tw;
    int64_t right = (int64_t)(bx + bw - 1) / tw;
    int64_t top = (int64_t)by / th;
    int64_t bottom = (int64_t)(by + bh - 1) / th;

    // Clamp to tilemap bounds
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= tilemap->width)
        right = tilemap->width - 1;
    if (bottom >= tilemap->height)
        bottom = tilemap->height - 1;

    // Resolve collisions against each overlapping solid tile
    for (int64_t ty = top; ty <= bottom; ty++)
    {
        for (int64_t tx = left; tx <= right; tx++)
        {
            int64_t tile_id = coll_tiles[ty * tilemap->width + tx];
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
            double bx1 = bx, by1 = by;
            double bx2 = bx + bw, by2 = by + bh;

            // Check overlap
            if (bx2 <= tile_x1 || bx1 >= tile_x2 || by2 <= tile_y1 || by1 >= tile_y2)
                continue;

            // One-way platform: only collide if body is moving down AND came from above.
            // by1 > tile_y1 + 2.0 means the body's top is already below the tile surface —
            // it entered from below and should pass through. Frame-rate independent.
            if (ctype == TILE_COLLISION_ONE_WAY)
            {
                if (bvy <= 0.0 || by1 > tile_y1 + 2.0)
                    continue;
            }

            // Calculate overlap on each axis
            double ox = (bx2 < tile_x2) ? (bx2 - tile_x1) : (tile_x2 - bx1);
            double oy = (by2 < tile_y2) ? (by2 - tile_y1) : (tile_y2 - by1);

            // Resolve along minimum overlap axis
            if (ox < oy)
            {
                // Horizontal resolution
                if (bx1 + bw * 0.5 < tile_x1 + (double)tw * 0.5)
                    bx = tile_x1 - bw; // Push left
                else
                    bx = tile_x2; // Push right
                bvx = 0.0;
                // Refresh derived coordinates for subsequent iterations
                bx1 = bx;
                bx2 = bx + bw;
            }
            else
            {
                // Vertical resolution
                if (by1 + bh * 0.5 < tile_y1 + (double)th * 0.5)
                    by = tile_y1 - bh; // Push up (landing)
                else
                    by = tile_y2; // Push down (hit ceiling)
                bvy = 0.0;
                by1 = by;
                by2 = by + bh;
            }
            collided = 1;
        }
    }

    if (collided)
    {
        rt_physics2d_body_set_pos(body_ptr, bx, by);
        rt_physics2d_body_set_vel(body_ptr, bvx, bvy);
    }

    return collided;
}

//=============================================================================
// Layer Management
//=============================================================================

int64_t rt_tilemap_add_layer(void *tilemap_ptr, rt_string name)
{
    if (!tilemap_ptr)
        return -1;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    if (tilemap->layer_count >= TM_MAX_LAYERS)
        return -1;

    int32_t idx = tilemap->layer_count;
    size_t grid_size = (size_t)(tilemap->width * tilemap->height) * sizeof(int64_t);

    int64_t *grid = (int64_t *)malloc(grid_size);
    if (!grid)
        return -1;
    memset(grid, 0, grid_size);

    tm_layer *layer = &tilemap->layers[idx];
    layer->tiles = grid;
    layer->tileset = NULL;
    layer->tileset_cols = 0;
    layer->tileset_rows = 0;
    layer->tile_count = 0;
    layer->visible = 1;
    layer->owns_tiles = 1;

    // Copy layer name (truncate to 31 chars)
    memset(layer->name, 0, sizeof(layer->name));
    if (name)
    {
        const char *cstr = rt_string_cstr(name);
        if (cstr)
        {
            size_t len = strlen(cstr);
            if (len > 31)
                len = 31;
            memcpy(layer->name, cstr, len);
        }
    }

    tilemap->layer_count = idx + 1;
    return (int64_t)idx;
}

int64_t rt_tilemap_get_layer_count(void *tilemap_ptr)
{
    if (!tilemap_ptr)
        return 0;
    return ((rt_tilemap_impl *)tilemap_ptr)->layer_count;
}

int64_t rt_tilemap_get_layer_by_name(void *tilemap_ptr, rt_string name)
{
    if (!tilemap_ptr || !name)
        return -1;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return -1;

    for (int32_t i = 0; i < tilemap->layer_count; i++)
    {
        if (strcmp(tilemap->layers[i].name, cstr) == 0)
            return (int64_t)i;
    }
    return -1;
}

void rt_tilemap_remove_layer(void *tilemap_ptr, int64_t layer)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;

    // Cannot remove layer 0 (base layer) or invalid indices
    if (layer <= 0 || layer >= tilemap->layer_count)
        return;

    tm_layer *lyr = &tilemap->layers[layer];

    // Free the tile grid if owned
    if (lyr->owns_tiles && lyr->tiles)
        free(lyr->tiles);

    // Free per-layer tileset if set
    if (lyr->tileset)
        rt_heap_release(lyr->tileset);

    // Shift layers down
    for (int32_t i = (int32_t)layer; i < tilemap->layer_count - 1; i++)
        tilemap->layers[i] = tilemap->layers[i + 1];

    tilemap->layer_count--;

    // Clear the now-unused slot
    memset(&tilemap->layers[tilemap->layer_count], 0, sizeof(tm_layer));

    // Adjust collision layer if needed
    if (tilemap->collision_layer == (int32_t)layer)
        tilemap->collision_layer = 0;
    else if (tilemap->collision_layer > (int32_t)layer)
        tilemap->collision_layer--;
}

void rt_tilemap_set_layer_visible(void *tilemap_ptr, int64_t layer, int8_t visible)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    tilemap->layers[layer].visible = visible ? 1 : 0;
}

int8_t rt_tilemap_get_layer_visible(void *tilemap_ptr, int64_t layer)
{
    if (!tilemap_ptr)
        return 0;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return 0;
    return tilemap->layers[layer].visible;
}

//=============================================================================
// Per-Layer Tile Access
//=============================================================================

void rt_tilemap_set_tile_layer(void *tilemap_ptr, int64_t layer, int64_t x, int64_t y, int64_t tile)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return;
    if (!tilemap->layers[layer].tiles)
        return;
    tilemap->layers[layer].tiles[y * tilemap->width + x] = tile;
}

int64_t rt_tilemap_get_tile_layer(void *tilemap_ptr, int64_t layer, int64_t x, int64_t y)
{
    if (!tilemap_ptr)
        return 0;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return 0;
    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return 0;
    if (!tilemap->layers[layer].tiles)
        return 0;
    return tilemap->layers[layer].tiles[y * tilemap->width + x];
}

void rt_tilemap_fill_layer(void *tilemap_ptr, int64_t layer, int64_t tile)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    if (!tilemap->layers[layer].tiles)
        return;
    int64_t count = tilemap->width * tilemap->height;
    for (int64_t i = 0; i < count; i++)
        tilemap->layers[layer].tiles[i] = tile;
}

void rt_tilemap_clear_layer(void *tilemap_ptr, int64_t layer)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    if (!tilemap->layers[layer].tiles)
        return;
    size_t grid_size = (size_t)(tilemap->width * tilemap->height) * sizeof(int64_t);
    memset(tilemap->layers[layer].tiles, 0, grid_size);
}

//=============================================================================
// Per-Layer Tileset
//=============================================================================

void rt_tilemap_set_layer_tileset(void *tilemap_ptr, int64_t layer, void *pixels)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;

    tm_layer *lyr = &tilemap->layers[layer];

    if (!pixels)
    {
        // Reset to base tileset
        if (lyr->tileset)
            rt_heap_release(lyr->tileset);
        lyr->tileset = NULL;
        lyr->tileset_cols = 0;
        lyr->tileset_rows = 0;
        lyr->tile_count = 0;
        return;
    }

    void *cloned = rt_pixels_clone(pixels);
    if (!cloned)
        return;

    if (lyr->tileset)
        rt_heap_release(lyr->tileset);

    lyr->tileset = cloned;
    rt_heap_retain(cloned);

    int64_t ts_width = rt_pixels_width(cloned);
    int64_t ts_height = rt_pixels_height(cloned);

    lyr->tileset_cols = ts_width / tilemap->tile_width;
    lyr->tileset_rows = ts_height / tilemap->tile_height;
    lyr->tile_count = lyr->tileset_cols * lyr->tileset_rows;
}

//=============================================================================
// Per-Layer Rendering
//=============================================================================

void rt_tilemap_draw_layer(
    void *tilemap_ptr, void *canvas_ptr, int64_t layer, int64_t cam_x, int64_t cam_y)
{
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;

    tm_layer *lyr = &tilemap->layers[layer];
    if (!lyr->visible || !lyr->tiles)
        return;

    // Determine which tileset to use: per-layer or base
    void *ts = lyr->tileset ? lyr->tileset : tilemap->tileset;
    int64_t ts_cols = lyr->tileset ? lyr->tileset_cols : tilemap->tileset_cols;
    int64_t tc = lyr->tileset ? lyr->tile_count : tilemap->tile_count;

    if (!ts || tc == 0)
        return;

    int64_t tw = tilemap->tile_width > 0 ? tilemap->tile_width : 1;
    int64_t th = tilemap->tile_height > 0 ? tilemap->tile_height : 1;

    // Viewport culling
    int64_t first_x = (-cam_x) / tw;
    int64_t first_y = (-cam_y) / th;
    if (first_x < 0)
        first_x = 0;
    if (first_y < 0)
        first_y = 0;

    int64_t canvas_w = rt_canvas_width(canvas_ptr);
    int64_t canvas_h = rt_canvas_height(canvas_ptr);
    int64_t vis_w = canvas_w / tw + 2;
    int64_t vis_h = canvas_h / th + 2;

    if (first_x + vis_w > tilemap->width)
        vis_w = tilemap->width - first_x;
    if (first_y + vis_h > tilemap->height)
        vis_h = tilemap->height - first_y;
    if (vis_w <= 0 || vis_h <= 0)
        return;

    for (int64_t ty = first_y; ty < first_y + vis_h; ty++)
    {
        for (int64_t tx = first_x; tx < first_x + vis_w; tx++)
        {
            int64_t tile_index = lyr->tiles[ty * tilemap->width + tx];
            if (tile_index <= 0 || tile_index > tc)
                continue;

            int64_t ti = tile_index - 1;
            int64_t ts_x = (ti % ts_cols) * tw;
            int64_t ts_y = (ti / ts_cols) * th;
            int64_t screen_x = tx * tw + cam_x;
            int64_t screen_y = ty * th + cam_y;

            rt_canvas_blit_region(canvas_ptr, screen_x, screen_y, ts, ts_x, ts_y, tw, th);
        }
    }
}

//=============================================================================
// Collision Layer
//=============================================================================

void rt_tilemap_set_collision_layer(void *tilemap_ptr, int64_t layer)
{
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tilemap_ptr;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    tilemap->collision_layer = (int32_t)layer;
}

int64_t rt_tilemap_get_collision_layer(void *tilemap_ptr)
{
    if (!tilemap_ptr)
        return 0;
    return ((rt_tilemap_impl *)tilemap_ptr)->collision_layer;
}
