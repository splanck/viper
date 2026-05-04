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
#include "rt_tilemap_internal.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_physics2d.h"
#include "rt_physics2d_internal.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Collision type constants — use public enum from rt_tilemap.h.
#define TILE_COLLISION_NONE RT_TILE_COLLISION_NONE
#define TILE_COLLISION_SOLID RT_TILE_COLLISION_SOLID
#define TILE_COLLISION_ONE_WAY RT_TILE_COLLISION_ONE_WAY_UP

/// @brief Integer floor division that rounds toward -∞ rather than toward zero.
/// @details C's built-in `/` operator truncates toward zero, which produces incorrect
///   tile-coordinate results for negative world positions. For example, world pixel -1
///   on a 16-pixel tile should map to tile -1, not tile 0. The correction subtracts 1
///   when the quotient was rounded toward zero away from the true floor (i.e., when the
///   remainder is non-zero and the operands have different signs).
/// @param divisor Must not be zero; returns 0 if it is (defensive).
/// @return ⌊value / divisor⌋ with floor semantics.
static int64_t tilemap_floor_div(int64_t value, int64_t divisor) {
    if (divisor == 0)
        return 0;
    int64_t quot = value / divisor;
    int64_t rem = value % divisor;
    if (rem != 0 && ((rem < 0) != (divisor < 0)))
        quot--;
    return quot;
}

/// @brief Convert a double to int64_t with saturation, writing the result to *out.
/// @details Handles NaN/infinity (returns 0) and values at or beyond the int64_t bounds
///          (saturates to INT64_MAX / INT64_MIN). Used to convert floating-point camera
///          scroll positions to tile coordinates without undefined-behavior casts.
/// @return 1 on success (finite value in range or saturated); 0 if `out` is NULL or value is non-finite.
static int8_t tilemap_double_to_i64_sat(double value, int64_t *out) {
    if (!out || !isfinite(value))
        return 0;
    if (value >= (double)INT64_MAX) {
        *out = INT64_MAX;
        return 1;
    }
    if (value <= (double)INT64_MIN) {
        *out = INT64_MIN;
        return 1;
    }
    *out = (int64_t)value;
    return 1;
}

/// @brief Validate tilemap dimensions and compute allocation sizes without overflow.
/// @details Returns 0 for non-positive dimensions or if width × height would overflow int64_t,
///          or if tile_count × sizeof(int64_t) would overflow size_t. On success writes the
///          tile count and byte size to the optional output pointers.
/// @return 1 if the grid is valid and sizes were computed; 0 otherwise.
static int32_t tilemap_checked_grid_size(int64_t width,
                                         int64_t height,
                                         int64_t *tile_count_out,
                                         size_t *tiles_size_out) {
    if (width <= 0 || height <= 0)
        return 0;
    if (width > INT64_MAX / height)
        return 0;
    int64_t tile_count = width * height;
    if (tile_count > INT64_MAX / (int64_t)sizeof(int64_t))
        return 0;
    size_t tiles_size = (size_t)tile_count * sizeof(int64_t);
    if (tile_count_out)
        *tile_count_out = tile_count;
    if (tiles_size_out)
        *tiles_size_out = tiles_size;
    return 1;
}

static rt_tilemap_impl *tilemap_checked(void *tilemap_ptr, const char *trap_message) {
    if (!tilemap_ptr) {
        if (trap_message)
            rt_trap(trap_message);
        return NULL;
    }
    if (rt_obj_class_id(tilemap_ptr) != RT_TILEMAP_CLASS_ID)
        return NULL;
    return (rt_tilemap_impl *)tilemap_ptr;
}

/// @brief Return the absolute distance from @p value to zero as an unsigned integer.
/// @details Equivalent to (uint64_t)(-value) but safe for all int64_t inputs including
///          INT64_MIN, which has no positive two's-complement representation. The result
///          is used to measure how far a negative tile coordinate is from the origin so
///          the caller can skip that many tiles without signed-overflow arithmetic.
static uint64_t tilemap_distance_to_zero(int64_t value) {
    return (uint64_t)(-(value + 1)) + 1u;
}

/// @brief Negate @p value with saturation — INT64_MIN has no positive representation
///        in two's complement, so it saturates to INT64_MAX instead.
static int64_t tilemap_negate_saturating(int64_t value) {
    return value == INT64_MIN ? INT64_MAX : -value;
}

/// @brief Add 2 to @p value with saturation — used when computing tile collision bounds
///        to extend a range by one tile without overflowing near INT64_MAX.
static int64_t tilemap_add_two_saturating(int64_t value) {
    return value > INT64_MAX - 2 ? INT64_MAX : value + 2;
}

/// @brief Add two int64_t values with saturation at INT64_MAX / INT64_MIN.
/// @details Used for all pixel-coordinate addition in tilemap rendering and collision
///   to prevent wrapping when a world coordinate plus offset exceeds the int64_t range.
static int64_t tilemap_add_saturating(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Multiply two int64_t values with saturation at INT64_MAX / INT64_MIN.
/// @details All intermediate tilemap coordinate computations (tile_x * tile_width,
///   tile_y * tile_height, etc.) pass through this function to prevent undefined
///   behavior from signed integer overflow. The special case `(-1) * INT64_MIN` is
///   handled explicitly because it is the only multiply whose absolute value exceeds
///   INT64_MAX.
static int64_t tilemap_mul_saturating(int64_t a, int64_t b) {
    if (a == 0 || b == 0)
        return 0;
    if (a == -1 && b == INT64_MIN)
        return INT64_MAX;
    if (b == -1 && a == INT64_MIN)
        return INT64_MAX;
    if (a > 0) {
        if (b > 0 && a > INT64_MAX / b)
            return INT64_MAX;
        if (b < 0 && b < INT64_MIN / a)
            return INT64_MIN;
    } else {
        if (b > 0 && a < INT64_MIN / b)
            return INT64_MIN;
        if (b < 0 && a < INT64_MAX / b)
            return INT64_MAX;
    }
    return a * b;
}

/// @brief Clip a 1-D span [*start, *start + *length) so it fits within [0, limit).
/// @details Adjusts *start and *length in-place: negative starts are advanced to 0 (consuming
///          the leading skipped tiles from *length); spans that extend past @p limit are
///          truncated. Returns 0 and zeroes *length for degenerate inputs (null pointers,
///          non-positive length or limit, span fully outside [0, limit)).
/// @return 1 if the clipped span has length > 0; 0 if the span was fully clipped or invalid.
static int32_t tilemap_clip_span_to_bounds(int64_t *start, int64_t *length, int64_t limit) {
    if (!start || !length || *length <= 0 || limit <= 0) {
        if (length)
            *length = 0;
        return 0;
    }
    if (*start < 0) {
        uint64_t skip = tilemap_distance_to_zero(*start);
        if (skip >= (uint64_t)*length) {
            *start = 0;
            *length = 0;
            return 0;
        }
        *start = 0;
        *length -= (int64_t)skip;
    }
    if (*start >= limit) {
        *length = 0;
        return 0;
    }
    int64_t remaining = limit - *start;
    if (*length > remaining)
        *length = remaining;
    return *length > 0;
}

//=============================================================================
// Tilemap Creation
//=============================================================================

/// @brief GC finalizer for Tilemap — release the tileset, per-layer tiles, and per-layer tilesets.
static void tilemap_finalize(void *obj) {
    rt_tilemap_impl *tm = tilemap_checked(obj, NULL);
    if (!tm)
        return;
    /* Release base tileset */
    if (tm->tileset)
        rt_heap_release(tm->tileset);
    /* Free per-layer owned tiles and release per-layer tilesets */
    for (int32_t i = 0; i < tm->layer_count; i++) {
        if (tm->layers[i].owns_tiles && tm->layers[i].tiles)
            free(tm->layers[i].tiles);
        if (tm->layers[i].tileset)
            rt_heap_release(tm->layers[i].tileset);
    }
}

/// @brief Create a `width × height` tile grid with cells of `tile_width × tile_height` pixels.
///
/// Allocates the tile array inline with the Tilemap struct (single
/// GC allocation). Layer 0 is the implicit "base" layer; additional
/// layers are added via `rt_tilemap_add_layer`. All tiles start at 0
/// (empty). Dimension args are clamped to ≥1 (defaults: tile size 16×16).
void *rt_tilemap_new(int64_t width, int64_t height, int64_t tile_width, int64_t tile_height) {
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    if (tile_width <= 0)
        tile_width = 16;
    if (tile_height <= 0)
        tile_height = 16;

    int64_t tile_count = 0;
    size_t tiles_size = 0;
    if (!tilemap_checked_grid_size(width, height, &tile_count, &tiles_size)) {
        rt_trap("Tilemap: dimensions too large");
        return NULL;
    }

    size_t total_size = sizeof(rt_tilemap_impl) + tiles_size;
    if (total_size < sizeof(rt_tilemap_impl) || total_size > (size_t)INT64_MAX) {
        rt_trap("Tilemap: dimensions too large");
        return NULL;
    }

    rt_tilemap_impl *tilemap =
        (rt_tilemap_impl *)rt_obj_new_i64(RT_TILEMAP_CLASS_ID, (int64_t)total_size);
    if (!tilemap)
        return NULL;

    memset(tilemap, 0, sizeof(rt_tilemap_impl));
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

    // Initialize layer 0 (base layer)
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

// ===========================================================================
// Tilemap property accessors — width/height in tiles, tile size in
// pixels. Each traps on null tilemap (these are programmer errors,
// not runtime conditions).
// ===========================================================================

/// @brief Number of tiles across the map (width). Traps on null.
int64_t rt_tilemap_get_width(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.Width: null tilemap");
    return tilemap ? tilemap->width : 0;
}

/// @brief Number of tiles down the map (height). Traps on null.
int64_t rt_tilemap_get_height(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.Height: null tilemap");
    return tilemap ? tilemap->height : 0;
}

/// @brief Width of a single tile in pixels.
int64_t rt_tilemap_get_tile_width(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.TileWidth: null tilemap");
    return tilemap ? tilemap->tile_width : 0;
}

/// @brief Height of a single tile in pixels.
int64_t rt_tilemap_get_tile_height(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.TileHeight: null tilemap");
    return tilemap ? tilemap->tile_height : 0;
}

//=============================================================================
// Tileset Management
//=============================================================================

/// @brief Bind the base tileset Pixels — auto-derives `tileset_cols/rows` from its dimensions.
///
/// The tileset is laid out as a regular grid of `tile_width × tile_height`
/// cells. Tile indices are 1-based (0 means "empty"), reading
/// left-to-right top-to-bottom in the tileset image.
void rt_tilemap_set_tileset(void *tilemap_ptr, void *pixels) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.SetTileset: null tilemap");
    if (!tilemap)
        return;
    if (!pixels) {
        rt_trap("Tilemap.SetTileset: null pixels");
        return;
    }

    // Clone the pixels and store, releasing the old tileset first
    void *cloned = rt_pixels_clone(pixels);
    if (!cloned)
        return;

    if (tilemap->tileset)
        rt_heap_release(tilemap->tileset);

    tilemap->tileset = cloned;

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

/// @brief Total number of distinct tiles available in the bound tileset (cols × rows).
int64_t rt_tilemap_get_tile_count(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.TileCount: null tilemap");
    return tilemap ? tilemap->tile_count : 0;
}

//=============================================================================
// Tile Access
//=============================================================================

/// @brief Place tile `tile_index` at grid `(x, y)` on the base layer (silently no-op out of
/// bounds).
void rt_tilemap_set_tile(void *tilemap_ptr, int64_t x, int64_t y, int64_t tile_index) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.SetTile: null tilemap");
    if (!tilemap)
        return;

    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return;

    tilemap->tiles[y * tilemap->width + x] = tile_index;
}

/// @brief Read the tile index at grid `(x, y)`. Returns 0 (empty) for out-of-bounds.
int64_t rt_tilemap_get_tile(void *tilemap_ptr, int64_t x, int64_t y) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.GetTile: null tilemap");
    if (!tilemap)
        return 0;

    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return 0;

    return tilemap->tiles[y * tilemap->width + x];
}

/// @brief Fill the entire base layer with the given tile index.
void rt_tilemap_fill(void *tilemap_ptr, int64_t tile_index) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.Fill: null tilemap");
    if (!tilemap)
        return;

    int64_t count = tilemap->width * tilemap->height;
    for (int64_t i = 0; i < count; i++)
        tilemap->tiles[i] = tile_index;
}

/// @brief Reset every tile on the base layer to 0 (empty).
void rt_tilemap_clear(void *tilemap_ptr) {
    rt_tilemap_fill(tilemap_ptr, 0);
}

/// @brief Fill a rectangular region of the base layer with `tile_index`. Bounds-clamped.
void rt_tilemap_fill_rect(
    void *tilemap_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t tile_index) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.FillRect: null tilemap");
    if (!tilemap)
        return;

    if (!tilemap_clip_span_to_bounds(&x, &w, tilemap->width) ||
        !tilemap_clip_span_to_bounds(&y, &h, tilemap->height))
        return;

    int64_t y_end = y + h;
    int64_t x_end = x + w;
    for (int64_t ty = y; ty < y_end; ty++)
        for (int64_t tx = x; tx < x_end; tx++)
            tilemap->tiles[ty * tilemap->width + tx] = tile_index;
}

//=============================================================================
// Rendering
//=============================================================================

/// @brief Render one tilemap layer over a rectangular region of tile coordinates.
/// @details Iterates over tile rows [view_y, view_y+view_h) and columns
///   [view_x, view_x+view_w), skipping empty (tile_index == 0) or out-of-range
///   tiles. For each valid tile, the source rectangle is computed from the tile index
///   in the tileset image (`ts_x = (ti % cols) * tw`, `ts_y = (ti / cols) * th`) and
///   blitted to the canvas at the screen-space position `(tx * tw + offset_x, ty * th + offset_y)`.
///   All coordinate multiplications use `tilemap_mul_saturating` and additions use
///   `tilemap_add_saturating` to prevent overflow on extreme map sizes. The per-layer
///   tileset (if set) overrides the tilemap's default tileset.
/// @param view_x,view_y    Top-left tile coordinate of the visible region.
/// @param view_w,view_h    Width and height of the visible region in tiles.
/// @param offset_x,offset_y Canvas pixel offset for the top-left tile.
static void rt_tilemap_draw_region_layer_impl(rt_tilemap_impl *tilemap,
                                              void *tilemap_ptr,
                                              void *canvas_ptr,
                                              tm_layer *layer,
                                              int64_t offset_x,
                                              int64_t offset_y,
                                              int64_t view_x,
                                              int64_t view_y,
                                              int64_t view_w,
                                              int64_t view_h) {
    if (!tilemap || !layer || !canvas_ptr || !layer->visible || !layer->tiles)
        return;

    void *tileset = layer->tileset ? layer->tileset : tilemap->tileset;
    int64_t tileset_cols = layer->tileset ? layer->tileset_cols : tilemap->tileset_cols;
    int64_t tile_count = layer->tileset ? layer->tile_count : tilemap->tile_count;
    if (!tileset || tile_count <= 0 || tileset_cols <= 0)
        return;

    int64_t tw = tilemap->tile_width;
    int64_t th = tilemap->tile_height;

    int64_t end_y = view_y + view_h;
    int64_t end_x = view_x + view_w;
    for (int64_t ty = view_y; ty < end_y; ty++) {
        for (int64_t tx = view_x; tx < end_x; tx++) {
            int64_t tile_index =
                rt_tilemap_resolve_anim_tile(tilemap_ptr, layer->tiles[ty * tilemap->width + tx]);
            if (tile_index <= 0 || tile_index > tile_count)
                continue;

            int64_t ti = tile_index - 1;
            int64_t ts_x = tilemap_mul_saturating(ti % tileset_cols, tw);
            int64_t ts_y = tilemap_mul_saturating(ti / tileset_cols, th);
            int64_t screen_x = tilemap_add_saturating(tilemap_mul_saturating(tx, tw), offset_x);
            int64_t screen_y = tilemap_add_saturating(tilemap_mul_saturating(ty, th), offset_y);

            rt_canvas_blit_region(canvas_ptr, screen_x, screen_y, tileset, ts_x, ts_y, tw, th);
        }
    }
}

/// @brief Render the entire tilemap (every visible layer) onto a canvas at `(offset_x, offset_y)`.
///
/// Walks layers in order; layer 0 (base) draws first, followed by
/// each added layer's tiles. Per-tile draws blit the source rect
/// from the tileset. Out-of-canvas tiles are skipped early.
void rt_tilemap_draw(void *tilemap_ptr, void *canvas_ptr, int64_t offset_x, int64_t offset_y) {
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    int64_t tw = tilemap->tile_width > 0 ? tilemap->tile_width : 1;
    int64_t th = tilemap->tile_height > 0 ? tilemap->tile_height : 1;

    /* Compute the first tile that is visible (partially or fully) on screen */
    int64_t first_x = tilemap_floor_div(tilemap_negate_saturating(offset_x), tw);
    int64_t first_y = tilemap_floor_div(tilemap_negate_saturating(offset_y), th);
    if (first_x < 0)
        first_x = 0;
    if (first_y < 0)
        first_y = 0;

    /* Compute how many tiles fit in the canvas (plus two for partial edges) */
    int64_t canvas_w = rt_canvas_width(canvas_ptr);
    int64_t canvas_h = rt_canvas_height(canvas_ptr);
    int64_t vis_w = tilemap_add_two_saturating(canvas_w / tw);
    int64_t vis_h = tilemap_add_two_saturating(canvas_h / th);

    /* Clamp to tilemap dimensions */
    if (!tilemap_clip_span_to_bounds(&first_x, &vis_w, tilemap->width) ||
        !tilemap_clip_span_to_bounds(&first_y, &vis_h, tilemap->height))
        return;

    rt_tilemap_draw_region(
        tilemap_ptr, canvas_ptr, offset_x, offset_y, first_x, first_y, vis_w, vis_h);
}

/// @brief Render only a sub-rectangle of the tilemap (camera-clipped draw).
///
/// `(view_x, view_y, view_w, view_h)` defines a rectangle in
/// tile coordinates within the tilemap. Saves work for large maps
/// when only a small viewport needs drawing.
void rt_tilemap_draw_region(void *tilemap_ptr,
                            void *canvas_ptr,
                            int64_t offset_x,
                            int64_t offset_y,
                            int64_t view_x,
                            int64_t view_y,
                            int64_t view_w,
                            int64_t view_h) {
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;

    if (!tilemap_clip_span_to_bounds(&view_x, &view_w, tilemap->width) ||
        !tilemap_clip_span_to_bounds(&view_y, &view_h, tilemap->height))
        return;

    for (int32_t layer = 0; layer < tilemap->layer_count; layer++) {
        rt_tilemap_draw_region_layer_impl(tilemap,
                                          tilemap_ptr,
                                          canvas_ptr,
                                          &tilemap->layers[layer],
                                          offset_x,
                                          offset_y,
                                          view_x,
                                          view_y,
                                          view_w,
                                          view_h);
    }
}

//=============================================================================
// Utility
//=============================================================================

// ===========================================================================
// Coordinate conversion — convert between pixel space and tile-grid space.
// Each pair (`*_to_tile_x/y`, `*_to_pixel_x/y`) divides or multiplies by
// the tile dimensions. Vector versions (`pixel_to_tile`, `tile_to_pixel`)
// convert both axes in one call.
// ===========================================================================

/// @brief Convert pixel `(px, py)` to tile-grid coordinates, written to `*tx, *ty`.
void rt_tilemap_pixel_to_tile(
    void *tilemap_ptr, int64_t pixel_x, int64_t pixel_y, int64_t *tile_x, int64_t *tile_y) {
    if (!tilemap_ptr || !tile_x || !tile_y)
        return;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    *tile_x = tilemap_floor_div(pixel_x, tilemap->tile_width);
    *tile_y = tilemap_floor_div(pixel_y, tilemap->tile_height);
}

/// @brief X-axis: convert pixel coordinate to tile-grid column.
int64_t rt_tilemap_to_tile_x(void *tilemap_ptr, int64_t pixel_x) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap_floor_div(pixel_x, tilemap->tile_width) : 0;
}

/// @brief Y-axis: convert pixel coordinate to tile-grid row.
int64_t rt_tilemap_to_tile_y(void *tilemap_ptr, int64_t pixel_y) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap_floor_div(pixel_y, tilemap->tile_height) : 0;
}

/// @brief Convert (tile_x, tile_y) grid coordinates to top-left pixel coordinates of that cell.
/// Writes the result into the provided out-parameters; no-ops on null inputs.
void rt_tilemap_tile_to_pixel(
    void *tilemap_ptr, int64_t tile_x, int64_t tile_y, int64_t *pixel_x, int64_t *pixel_y) {
    if (!tilemap_ptr || !pixel_x || !pixel_y)
        return;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    *pixel_x = tilemap_mul_saturating(tile_x, tilemap->tile_width);
    *pixel_y = tilemap_mul_saturating(tile_y, tilemap->tile_height);
}

/// @brief X-axis: convert tile column to pixel coordinate (tile_x * tile_width).
int64_t rt_tilemap_to_pixel_x(void *tilemap_ptr, int64_t tile_x) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap_mul_saturating(tile_x, tilemap->tile_width) : 0;
}

/// @brief Y-axis: convert tile row to pixel coordinate (tile_y * tile_height).
int64_t rt_tilemap_to_pixel_y(void *tilemap_ptr, int64_t tile_y) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap_mul_saturating(tile_y, tilemap->tile_height) : 0;
}

//=============================================================================
// Tile Collision
//=============================================================================

/// @brief Tag a tile id with a collision type (e.g. SOLID). Out-of-range ids are silently ignored.
/// Collision is keyed on the raw tile id, not the layer; one table is shared by every layer.
void rt_tilemap_set_collision(void *tilemap_ptr, int64_t tile_id, int64_t coll_type) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.SetCollision: null tilemap");
    if (!tilemap)
        return;
    if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
        return;
    if (coll_type != RT_TILE_COLLISION_NONE && coll_type != RT_TILE_COLLISION_SOLID &&
        coll_type != RT_TILE_COLLISION_ONE_WAY_UP)
        return;
    tilemap->collision[tile_id] = (int8_t)coll_type;
}

/// @brief Read the collision type previously set for a tile id; 0 (NONE) for unset/out-of-range
/// ids.
int64_t rt_tilemap_get_collision(void *tilemap_ptr, int64_t tile_id) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, "Tilemap.GetCollision: null tilemap");
    if (!tilemap)
        return 0;
    if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
        return 0;
    return tilemap->collision[tile_id];
}

/// @brief Sample the designated collision layer at a pixel coordinate; returns 1 if SOLID.
/// Collision is keyed by the base tile id stored in the map. Animated tiles
/// may change their rendered frame, but their collision remains stable.
int8_t rt_tilemap_is_solid_at(void *tilemap_ptr, int64_t pixel_x, int64_t pixel_y) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return 0;
    int64_t tx = tilemap_floor_div(pixel_x, tilemap->tile_width);
    int64_t ty = tilemap_floor_div(pixel_y, tilemap->tile_height);
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
int8_t rt_tilemap_collide_body(void *tilemap_ptr, void *body_ptr) {
    if (!tilemap_ptr || !body_ptr)
        return 0;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return 0;

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
    double prev_by = rt_physics2d_body_prev_y(body_ptr);
    if (!isfinite(bvx))
        bvx = 0.0;
    if (!isfinite(bvy))
        bvy = 0.0;
    if (!isfinite(prev_by))
        prev_by = by;

    // Determine the range of tiles the body overlaps
    double right_d = bx + bw - 1.0;
    double bottom_d = by + bh - 1.0;
    int64_t bx_i = 0;
    int64_t by_i = 0;
    int64_t right_i = 0;
    int64_t bottom_i = 0;
    if (bw <= 0.0 || bh <= 0.0 || !tilemap_double_to_i64_sat(bx, &bx_i) ||
        !tilemap_double_to_i64_sat(by, &by_i) ||
        !tilemap_double_to_i64_sat(right_d, &right_i) ||
        !tilemap_double_to_i64_sat(bottom_d, &bottom_i))
        return 0;

    int64_t left = tilemap_floor_div(bx_i, tw);
    int64_t right = tilemap_floor_div(right_i, tw);
    int64_t top = tilemap_floor_div(by_i, th);
    int64_t bottom = tilemap_floor_div(bottom_i, th);

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
    for (int64_t ty = top; ty <= bottom; ty++) {
        for (int64_t tx = left; tx <= right; tx++) {
            int64_t tile_id = coll_tiles[ty * tilemap->width + tx];
            if (tile_id < 0 || tile_id >= MAX_TILE_COLLISION_IDS)
                continue;
            int8_t ctype = tilemap->collision[tile_id];
            if (ctype == TILE_COLLISION_NONE)
                continue;

            // Tile AABB in pixels
            double tile_x1 = (double)tilemap_mul_saturating(tx, tw);
            double tile_y1 = (double)tilemap_mul_saturating(ty, th);
            double tile_x2 = tile_x1 + (double)tw;
            double tile_y2 = tile_y1 + (double)th;

            // Body AABB
            double bx1 = bx, by1 = by;
            double bx2 = bx + bw, by2 = by + bh;

            // Check overlap
            if (bx2 <= tile_x1 || bx1 >= tile_x2 || by2 <= tile_y1 || by1 >= tile_y2)
                continue;

            // One-way platforms only resolve when the body's previous bottom edge was above
            // the platform top and the current frame crossed the surface while moving down.
            if (ctype == TILE_COLLISION_ONE_WAY) {
                double prev_bottom = prev_by + bh;
                if (bvy <= 0.0 || prev_bottom > tile_y1 + 1.0)
                    continue;
                by = tile_y1 - bh;
                bvy = 0.0;
                collided = 1;
                continue;
            }

            // Calculate overlap on each axis
            double ox = (bx2 < tile_x2) ? (bx2 - tile_x1) : (tile_x2 - bx1);
            double oy = (by2 < tile_y2) ? (by2 - tile_y1) : (tile_y2 - by1);

            // Resolve along minimum overlap axis
            if (ox < oy) {
                // Horizontal resolution
                if (bx1 + bw * 0.5 < tile_x1 + (double)tw * 0.5)
                    bx = tile_x1 - bw; // Push left
                else
                    bx = tile_x2; // Push right
                bvx = 0.0;
                // Refresh derived coordinates for subsequent iterations
                bx1 = bx;
                bx2 = bx + bw;
            } else {
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

    if (collided) {
        rt_physics2d_body_set_pos(body_ptr, bx, by);
        rt_physics2d_body_set_vel(body_ptr, bvx, bvy);
    }

    return collided;
}

//=============================================================================
// Layer Management
//=============================================================================

/// @brief Append a new tile layer (allocating its own zeroed grid) and return its index.
/// Names must fit in the fixed 31-byte layer name slot; layers default to visible with no per-layer
/// tileset.
/// Returns -1 on null input, on hitting `TM_MAX_LAYERS`, or on allocation failure.
int64_t rt_tilemap_add_layer(void *tilemap_ptr, rt_string name) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return -1;

    if (tilemap->layer_count >= TM_MAX_LAYERS)
        return -1;

    size_t grid_size = 0;
    if (!tilemap_checked_grid_size(tilemap->width, tilemap->height, NULL, &grid_size)) {
        rt_trap("Tilemap.AddLayer: dimensions too large");
        return -1;
    }

    int32_t idx = tilemap->layer_count;
    char layer_name[sizeof(tilemap->layers[0].name)];
    memset(layer_name, 0, sizeof(layer_name));
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len >= sizeof(layer_name))
                return -1;
            memcpy(layer_name, cstr, len);
        }
    }

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

    memcpy(layer->name, layer_name, sizeof(layer->name));

    tilemap->layer_count = idx + 1;
    return (int64_t)idx;
}

/// @brief Number of layers currently present (always >= 1 for a valid tilemap).
int64_t rt_tilemap_get_layer_count(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap->layer_count : 0;
}

/// @brief Linear lookup of a layer by name (case-sensitive `strcmp`); returns -1 if not found.
int64_t rt_tilemap_get_layer_by_name(void *tilemap_ptr, rt_string name) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap || !name)
        return -1;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return -1;

    for (int32_t i = 0; i < tilemap->layer_count; i++) {
        if (strcmp(tilemap->layers[i].name, cstr) == 0)
            return (int64_t)i;
    }
    return -1;
}

/// @brief Remove a non-base layer (index 0 is permanent), shifting subsequent layers down.
/// Frees the owned tile grid + per-layer tileset, and rebases `collision_layer` so it still points
/// at a valid layer (resets to 0 if removed, or decrements if it was above the removed slot).
void rt_tilemap_remove_layer(void *tilemap_ptr, int64_t layer) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;

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

/// @brief Toggle a layer's visibility flag (drawing skips invisible layers).
void rt_tilemap_set_layer_visible(void *tilemap_ptr, int64_t layer, int8_t visible) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    tilemap->layers[layer].visible = visible ? 1 : 0;
}

/// @brief Read a layer's visibility flag (0 = hidden, 1 = visible). Returns 0 for invalid layers.
int8_t rt_tilemap_get_layer_visible(void *tilemap_ptr, int64_t layer) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return 0;
    if (layer < 0 || layer >= tilemap->layer_count)
        return 0;
    return tilemap->layers[layer].visible;
}

//=============================================================================
// Per-Layer Tile Access
//=============================================================================

/// @brief Write a tile id at (x, y) on a specific layer. Silently no-ops on out-of-range
/// layer/coords.
void rt_tilemap_set_tile_layer(
    void *tilemap_ptr, int64_t layer, int64_t x, int64_t y, int64_t tile) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return;
    if (!tilemap->layers[layer].tiles)
        return;
    tilemap->layers[layer].tiles[y * tilemap->width + x] = tile;
}

/// @brief Read the tile id at (x, y) on a specific layer; 0 for out-of-range queries or empty
/// cells.
int64_t rt_tilemap_get_tile_layer(void *tilemap_ptr, int64_t layer, int64_t x, int64_t y) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return 0;
    if (layer < 0 || layer >= tilemap->layer_count)
        return 0;
    if (x < 0 || x >= tilemap->width || y < 0 || y >= tilemap->height)
        return 0;
    if (!tilemap->layers[layer].tiles)
        return 0;
    return tilemap->layers[layer].tiles[y * tilemap->width + x];
}

/// @brief Fill every cell of a single layer with the given tile id.
void rt_tilemap_fill_layer(void *tilemap_ptr, int64_t layer, int64_t tile) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    if (!tilemap->layers[layer].tiles)
        return;
    int64_t count = tilemap->width * tilemap->height;
    for (int64_t i = 0; i < count; i++)
        tilemap->layers[layer].tiles[i] = tile;
}

/// @brief Zero every cell of a single layer (`memset` shortcut for `fill_layer(layer, 0)`).
void rt_tilemap_clear_layer(void *tilemap_ptr, int64_t layer) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
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

/// @brief Bind a per-layer tileset (overrides the base tileset when drawing this layer).
/// Pass `pixels=NULL` to clear and fall back to the tilemap-wide tileset. The image is cloned
/// (via `rt_pixels_clone`) and retained on the heap; the previous binding is released.
/// `tile_count` is recomputed from the cloned image's dimensions divided by tile_width/height.
void rt_tilemap_set_layer_tileset(void *tilemap_ptr, int64_t layer, void *pixels) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;

    tm_layer *lyr = &tilemap->layers[layer];

    if (!pixels) {
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

    int64_t ts_width = rt_pixels_width(cloned);
    int64_t ts_height = rt_pixels_height(cloned);

    lyr->tileset_cols = ts_width / tilemap->tile_width;
    lyr->tileset_rows = ts_height / tilemap->tile_height;
    lyr->tile_count = lyr->tileset_cols * lyr->tileset_rows;
}

//=============================================================================
// Per-Layer Rendering
//=============================================================================

/// @brief Render one layer with viewport culling and per-tile animation resolution.
/// Falls back to the tilemap-wide tileset when no per-layer tileset is bound. Skips invisible
/// layers, layers without tile storage, and tiles outside the visible rect (camera + canvas size).
/// Tile id 0 is treated as empty; ids are 1-based into the chosen tileset (so id `n` maps to
/// `(n-1) % cols, (n-1) / cols`). `cam_x`/`cam_y` are world→screen offsets in pixels.
void rt_tilemap_draw_layer(
    void *tilemap_ptr, void *canvas_ptr, int64_t layer, int64_t cam_x, int64_t cam_y) {
    if (!tilemap_ptr || !canvas_ptr)
        return;

    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;

    int64_t tw = tilemap->tile_width > 0 ? tilemap->tile_width : 1;
    int64_t th = tilemap->tile_height > 0 ? tilemap->tile_height : 1;

    // Viewport culling
    int64_t first_x = tilemap_floor_div(tilemap_negate_saturating(cam_x), tw);
    int64_t first_y = tilemap_floor_div(tilemap_negate_saturating(cam_y), th);
    if (first_x < 0)
        first_x = 0;
    if (first_y < 0)
        first_y = 0;

    int64_t canvas_w = rt_canvas_width(canvas_ptr);
    int64_t canvas_h = rt_canvas_height(canvas_ptr);
    int64_t vis_w = tilemap_add_two_saturating(canvas_w / tw);
    int64_t vis_h = tilemap_add_two_saturating(canvas_h / th);

    if (!tilemap_clip_span_to_bounds(&first_x, &vis_w, tilemap->width) ||
        !tilemap_clip_span_to_bounds(&first_y, &vis_h, tilemap->height))
        return;

    rt_tilemap_draw_region_layer_impl(tilemap,
                                      tilemap_ptr,
                                      canvas_ptr,
                                      &tilemap->layers[layer],
                                      cam_x,
                                      cam_y,
                                      first_x,
                                      first_y,
                                      vis_w,
                                      vis_h);
}

//=============================================================================
// Collision Layer
//=============================================================================

/// @brief Designate which layer's tile grid is consulted by `is_solid_at` and `collide_body`.
void rt_tilemap_set_collision_layer(void *tilemap_ptr, int64_t layer) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    if (!tilemap)
        return;
    if (layer < 0 || layer >= tilemap->layer_count)
        return;
    tilemap->collision_layer = (int32_t)layer;
}

/// @brief Read the index of the layer currently designated as the collision source (default 0).
int64_t rt_tilemap_get_collision_layer(void *tilemap_ptr) {
    rt_tilemap_impl *tilemap = tilemap_checked(tilemap_ptr, NULL);
    return tilemap ? tilemap->collision_layer : 0;
}

//=============================================================================
// Tile Animation
//=============================================================================

/// @brief Register an animation that swaps `base_tile_id` for one of `frame_count` frames over
/// time. The frame table defaults to sequential ids `(base, base+1, ..., base+frame_count-1)`;
/// override individual frames with `set_tile_anim_frame`. Caps at `TM_MAX_TILE_ANIMS` registrations
/// and `TM_MAX_ANIM_FRAMES` per animation; duplicate base tile ids replace the existing animation.
void rt_tilemap_set_tile_anim(void *tilemap_ptr,
                              int64_t base_tile_id,
                              int64_t frame_count,
                              int64_t ms_per_frame) {
    if (!tilemap_ptr || frame_count < 1 || frame_count > TM_MAX_ANIM_FRAMES || ms_per_frame <= 0)
        return;
    rt_tilemap_impl *tm = tilemap_checked(tilemap_ptr, NULL);
    if (!tm)
        return;
    tm_tile_anim *anim = NULL;
    for (int32_t i = 0; i < tm->tile_anim_count; i++) {
        if (tm->tile_anims[i].base_tile_id == base_tile_id) {
            anim = &tm->tile_anims[i];
            break;
        }
    }
    if (!anim && tm->tile_anim_count >= TM_MAX_TILE_ANIMS)
        return;

    if (!anim)
        anim = &tm->tile_anims[tm->tile_anim_count++];
    memset(anim, 0, sizeof(tm_tile_anim));
    anim->base_tile_id = base_tile_id;
    anim->frame_count = (int32_t)frame_count;
    anim->ms_per_frame = ms_per_frame;
    // Default: sequential tile IDs (base, base+1, base+2, ...)
    for (int32_t i = 0; i < anim->frame_count; i++)
        anim->frame_tiles[i] = base_tile_id > INT64_MAX - i ? INT64_MAX : base_tile_id + i;
}

/// @brief Override one frame in an existing animation (selected by `base_tile_id`).
/// Useful for non-contiguous tilesets where animation frames don't sit on adjacent indices.
/// Silently no-ops if the animation is not registered or the frame index is out of range.
void rt_tilemap_set_tile_anim_frame(void *tilemap_ptr,
                                    int64_t base_tile_id,
                                    int64_t frame_idx,
                                    int64_t tile_id) {
    if (!tilemap_ptr)
        return;
    rt_tilemap_impl *tm = tilemap_checked(tilemap_ptr, NULL);
    if (!tm)
        return;
    for (int32_t i = 0; i < tm->tile_anim_count; i++) {
        if (tm->tile_anims[i].base_tile_id == base_tile_id) {
            if (frame_idx >= 0 && frame_idx < tm->tile_anims[i].frame_count)
                tm->tile_anims[i].frame_tiles[frame_idx] = tile_id;
            return;
        }
    }
}

/// @brief Advance all registered tile animations by `dt_ms` milliseconds.
/// Negative deltas are ignored. Large deltas advance by division/modulo so one call can span many
/// frames without looping once per elapsed frame.
void rt_tilemap_update_anims(void *tilemap_ptr, int64_t dt_ms) {
    if (!tilemap_ptr)
        return;
    if (dt_ms < 0)
        dt_ms = 0;
    rt_tilemap_impl *tm = tilemap_checked(tilemap_ptr, NULL);
    if (!tm)
        return;
    for (int32_t i = 0; i < tm->tile_anim_count; i++) {
        tm_tile_anim *anim = &tm->tile_anims[i];
        if (anim->ms_per_frame <= 0 || anim->frame_count <= 0)
            continue;
        if (dt_ms > INT64_MAX - anim->timer)
            anim->timer = INT64_MAX;
        else
            anim->timer += dt_ms;
        int64_t steps = anim->timer / anim->ms_per_frame;
        if (steps <= 0)
            continue;
        anim->timer %= anim->ms_per_frame;
        anim->current_frame =
            (anim->current_frame + (int32_t)(steps % anim->frame_count)) % anim->frame_count;
    }
}

/// @brief Map a tile id through the animation table, returning the current frame's tile id.
/// If `tile_id` is not the base of any registered animation it is returned unchanged. Called by
/// rendering paths so animated tiles display the current frame while collision
/// continues to use the base tile id stored in the map data.
int64_t rt_tilemap_resolve_anim_tile(void *tilemap_ptr, int64_t tile_id) {
    if (!tilemap_ptr)
        return tile_id;
    rt_tilemap_impl *tm = tilemap_checked(tilemap_ptr, NULL);
    if (!tm)
        return tile_id;
    for (int32_t i = 0; i < tm->tile_anim_count; i++) {
        if (tm->tile_anims[i].base_tile_id == tile_id)
            return tm->tile_anims[i].frame_tiles[tm->tile_anims[i].current_frame];
    }
    return tile_id;
}
