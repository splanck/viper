//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_graphics2d_tilemap.c
// Purpose: Tilemap-family 2D graphics classes — TileSet2D, TileLayer2D,
//          ObjectLayer2D, and AutoTile2D. Split out of rt_graphics2d.c; shares
//          the 2D foundation helpers via rt_graphics2d_internal.h.
//
// Key invariants:
//   - Class handles are validated through the *_checked casts before use.
//   - Grid/object containers grow via the shared saturating-size helpers so a
//     hostile width*height can never overflow an allocation.
//
// Ownership/Lifetime:
//   - Class impls are GC objects; retained Pixels / tile storage is released on
//     finalize. Helpers borrow caller handles.
//
// Links: src/runtime/graphics/2d/rt_graphics2d.c (other 2D classes + helpers),
//        src/runtime/graphics/2d/rt_graphics2d_internal.h (shared helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"
#include "rt_graphics2d_internal.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_tilemap.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Clamp a value to [min, max] (file-local copy; the name is shared by
///        several runtime modules as a static helper).
static int64_t clamp_i64(int64_t value, int64_t min, int64_t max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// Tilemap class IDs
#define RT2D_TILESET_CLASS_ID INT64_C(-0x620106)
#define RT2D_TILELAYER_CLASS_ID INT64_C(-0x620107)
#define RT2D_OBJECTLAYER_CLASS_ID INT64_C(-0x620108)
#define RT2D_AUTOTILE_CLASS_ID INT64_C(-0x620109)

// Tilemap impl structs
typedef struct {
    void *pixels;
    int64_t tile_width;
    int64_t tile_height;
} rt_tileset2d_impl;

typedef struct {
    int64_t width;
    int64_t height;
    int64_t visible;
    int64_t opacity;
    int64_t *tiles;
} rt_tilelayer2d_impl;

typedef struct {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t type;
} rt_objectlayer2d_entry;

typedef struct {
    rt_objectlayer2d_entry *items;
    int64_t count;
    int64_t capacity;
} rt_objectlayer2d_impl;

typedef struct {
    int64_t variants[16];
} rt_autotile2d_impl;

static rt_tileset2d_impl *tileset2d_checked(void *tileset) {
    return rt2d_has_class_min(tileset, RT2D_TILESET_CLASS_ID, sizeof(rt_tileset2d_impl))
               ? (rt_tileset2d_impl *)tileset
               : NULL;
}

/// @brief Safe-cast an opaque handle to rt_tilelayer2d_impl, or NULL.
static rt_tilelayer2d_impl *tilelayer2d_checked(void *layer) {
    return rt2d_has_class_min(layer, RT2D_TILELAYER_CLASS_ID, sizeof(rt_tilelayer2d_impl))
               ? (rt_tilelayer2d_impl *)layer
               : NULL;
}

/// @brief Test whether @p value falls in the half-open interval `[start, start+length)`.

// TileSet2D
//=============================================================================
// Uniform grid over a Pixels image. `columns × rows` is derived from the
// image size and per-tile dimensions; individual tiles can be extracted as
// their own Pixels for rendering or inspection.

/// @brief GC finalizer — releases the retained backing Pixels.
static void tileset2d_finalize(void *obj) {
    rt_tileset2d_impl *tileset = tileset2d_checked(obj);
    if (!tileset)
        return;
    rt2d_release_ref_slot(&tileset->pixels);
}

/// @brief Wrap a Pixels image as a tileset with fixed `tile_width × tile_height`
///        cells.
/// @details Returns NULL (no trap) if `pixels` is null, either tile dimension is
///          non-positive, or the image is smaller than one tile — callers can
///          probe for "is this a usable tileset" without catching a trap. The
///          grid doesn't have to divide evenly; any partial column/row on the
///          right or bottom is inaccessible.
void *rt_tileset2d_new(void *pixels, int64_t tile_width, int64_t tile_height) {
    if (!pixels || tile_width <= 0 || tile_height <= 0)
        return NULL;
    if (!rt_obj_is_instance(pixels, RT_PIXELS_CLASS_ID, sizeof(rt_pixels_impl)))
        return NULL;
    int64_t pixels_width = rt_pixels_width(pixels);
    int64_t pixels_height = rt_pixels_height(pixels);
    if (pixels_width < tile_width || pixels_height < tile_height)
        return NULL;
    rt2d_retain_ref(pixels);
    rt_tileset2d_impl *tileset = (rt_tileset2d_impl *)rt_obj_new_i64(
        RT2D_TILESET_CLASS_ID, (int64_t)sizeof(rt_tileset2d_impl));
    if (!tileset) {
        void *owned_pixels = pixels;
        rt2d_release_ref_slot(&owned_pixels);
        return NULL;
    }
    tileset->pixels = pixels;
    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    rt_obj_set_finalizer(tileset, tileset2d_finalize);
    return tileset;
}

/// @brief Return the number of whole tile columns in the tileset image.
int64_t rt_tileset2d_columns(void *tileset) {
    rt_tileset2d_impl *impl = tileset2d_checked(tileset);
    if (!impl || impl->tile_width <= 0)
        return 0;
    return rt_pixels_width(impl->pixels) / impl->tile_width;
}

/// @brief Return the number of whole tile rows in the tileset image.
int64_t rt_tileset2d_rows(void *tileset) {
    rt_tileset2d_impl *impl = tileset2d_checked(tileset);
    if (!impl || impl->tile_height <= 0)
        return 0;
    return rt_pixels_height(impl->pixels) / impl->tile_height;
}

/// @brief Return the total number of tiles (`columns * rows`) in the tileset.
int64_t rt_tileset2d_tile_count(void *tileset) {
    return rt_tileset2d_columns(tileset) * rt_tileset2d_rows(tileset);
}

/// @brief Extract one tile by index and return it as a new Pixels buffer.
/// @details Tiles are indexed left-to-right, top-to-bottom starting at 0. Returns NULL for
///   out-of-range indices; caller owns the returned Pixels.
void *rt_tileset2d_get_tile_pixels(void *tileset, int64_t tile_index) {
    if (!tileset || tile_index < 0 || tile_index >= rt_tileset2d_tile_count(tileset))
        return NULL;
    rt_tileset2d_impl *impl = tileset2d_checked(tileset);
    if (!impl)
        return NULL;
    int64_t columns = rt_tileset2d_columns(tileset);
    if (columns <= 0)
        return NULL;
    int64_t sx = (tile_index % columns) * impl->tile_width;
    int64_t sy = (tile_index / columns) * impl->tile_height;
    return rt2d_copy_region_pixels(impl->pixels, sx, sy, impl->tile_width, impl->tile_height);
}

//=============================================================================
// TileLayer2D
//=============================================================================
// Dense `width × height` grid of int64 tile IDs, plus visibility flag and
// per-layer opacity. Zero IDs conventionally mean "empty cell," but the
// interpretation is ultimately the consumer's. Paired with `TileSet2D` at
// draw time to resolve an ID to a renderable tile.

/// @brief GC finalizer — frees the `tiles` buffer.
static void tilelayer2d_finalize(void *obj) {
    rt_tilelayer2d_impl *layer = tilelayer2d_checked(obj);
    if (!layer)
        return;
    free(layer->tiles);
}

/// @brief Allocate a tile-grid layer `width × height`, zero-initialized and visible
///        at full opacity.
/// @details Validates dimensions through `rt2d_checked_count` with an int64-sized
///          element cost (one int64 per tile ID) so an enormous grid can't
///          overflow the total byte count. Traps on invalid dimensions — the
///          caller gets a clear error rather than a silent NULL. On malloc
///          failure the partially-constructed impl is torn down cleanly.
void *rt_tilelayer2d_new(int64_t width, int64_t height) {
    int64_t count = 0;
    if (!rt2d_checked_count(width, height, (int64_t)sizeof(int64_t), &count)) {
        rt_trap("TileLayer2D.New: invalid dimensions");
        return NULL;
    }
    rt_tilelayer2d_impl *layer = (rt_tilelayer2d_impl *)rt_obj_new_i64(
        RT2D_TILELAYER_CLASS_ID, (int64_t)sizeof(rt_tilelayer2d_impl));
    if (!layer)
        return NULL;
    layer->tiles = (int64_t *)calloc((size_t)count, sizeof(int64_t));
    if (!layer->tiles) {
        if (rt_obj_release_check0(layer))
            rt_obj_free(layer);
        return NULL;
    }
    layer->width = width;
    layer->height = height;
    layer->visible = 1;
    layer->opacity = 100;
    rt_obj_set_finalizer(layer, tilelayer2d_finalize);
    return layer;
}

/// @brief Return the width (in tiles) of the layer.
int64_t rt_tilelayer2d_width(void *layer) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    return impl ? impl->width : 0;
}

/// @brief Return the height (in tiles) of the layer.
int64_t rt_tilelayer2d_height(void *layer) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    return impl ? impl->height : 0;
}

/// @brief Return non-zero if tile cell (x, y) is within the layer's bounds.
static int32_t tilelayer2d_in_bounds(rt_tilelayer2d_impl *layer, int64_t x, int64_t y) {
    return layer && x >= 0 && y >= 0 && x < layer->width && y < layer->height;
}

/// @brief Write a tile ID into a cell, using row-major flat indexing.
/// @details Bounds are checked via `tilelayer2d_in_bounds`; out-of-range writes
///          are silently dropped. Tile ID 0 conventionally means "empty".
void rt_tilelayer2d_set(void *layer, int64_t x, int64_t y, int64_t tile) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    if (!tilelayer2d_in_bounds(impl, x, y))
        return;
    impl->tiles[y * impl->width + x] = tile;
}

/// @brief Read the tile ID at `(x, y)`, returning -1 for out-of-range coords.
int64_t rt_tilelayer2d_get(void *layer, int64_t x, int64_t y) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    if (!tilelayer2d_in_bounds(impl, x, y))
        return -1;
    return impl->tiles[y * impl->width + x];
}

/// @brief Fill every cell in the layer with the same tile ID.
void rt_tilelayer2d_fill(void *layer, int64_t tile) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    if (!impl)
        return;
    int64_t count = impl->width * impl->height;
    for (int64_t i = 0; i < count; i++)
        impl->tiles[i] = tile;
}

/// @brief Fill every cell with tile ID 0 (empty).
void rt_tilelayer2d_clear(void *layer) {
    rt_tilelayer2d_fill(layer, 0);
}

/// @brief Set whether the layer is included in tilemap renders.
void rt_tilelayer2d_set_visible(void *layer, int64_t visible) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    if (impl)
        impl->visible = visible != 0;
}

/// @brief Return non-zero if the layer is currently marked visible.
int64_t rt_tilelayer2d_is_visible(void *layer) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    return impl ? impl->visible : 0;
}

/// @brief Set the layer opacity in percent [0, 100]; values are clamped.
void rt_tilelayer2d_set_opacity(void *layer, int64_t opacity) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    if (impl)
        impl->opacity = clamp_i64(opacity, 0, 100);
}

/// @brief Return the current layer opacity in percent [0, 100].
int64_t rt_tilelayer2d_get_opacity(void *layer) {
    rt_tilelayer2d_impl *impl = tilelayer2d_checked(layer);
    return impl ? impl->opacity : 0;
}

//=============================================================================
// ObjectLayer2D
//=============================================================================
// Dynamic list of rect objects — used for collision volumes, triggers, spawn
// points, and editor metadata. Each entry has `(x, y, width, height, type)`;
// interpretation of `type` is app-specific.

/// @brief GC finalizer — frees the dynamic `items` array.
static void objectlayer2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_OBJECTLAYER_CLASS_ID))
        return;
    rt_objectlayer2d_impl *layer = (rt_objectlayer2d_impl *)obj;
    free(layer->items);
}

/// @brief Allocate an object layer with the given initial capacity.
/// @details Capacity is clamped by `rt2d_initial_capacity` (floor 16, ceiling 1Mi).
///          Returns NULL on allocation failure without trapping — the caller
///          can fall back to a smaller capacity or handle the error.
void *rt_objectlayer2d_new(int64_t capacity) {
    rt_objectlayer2d_impl *layer = (rt_objectlayer2d_impl *)rt_obj_new_i64(
        RT2D_OBJECTLAYER_CLASS_ID, (int64_t)sizeof(rt_objectlayer2d_impl));
    if (!layer)
        return NULL;
    layer->capacity = rt2d_initial_capacity(capacity);
    layer->items = (rt_objectlayer2d_entry *)calloc((size_t)layer->capacity, sizeof(*layer->items));
    if (!layer->items) {
        if (rt_obj_release_check0(layer))
            rt_obj_free(layer);
        return NULL;
    }
    rt_obj_set_finalizer(layer, objectlayer2d_finalize);
    return layer;
}

/// @brief Grow the object-entry array to fit at least `needed` entries.
/// @details Same doubling pattern as `renderer2d_reserve` with both element-count
///          and byte-count overflow guards. Returns 0 on failure so the caller
///          can decide whether to trap or drop-the-add — used by `Add` to
///          trap-on-overflow with a clear message.
static int32_t objectlayer2d_reserve(rt_objectlayer2d_impl *layer, int64_t needed) {
    if (!layer || needed <= layer->capacity)
        return 1;
    int64_t cap = layer->capacity > 0 ? layer->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_objectlayer2d_entry))
        return 0;
    rt_objectlayer2d_entry *items =
        (rt_objectlayer2d_entry *)realloc(layer->items, (size_t)cap * sizeof(*items));
    if (!items)
        return 0;
    memset(items + layer->capacity, 0, (size_t)(cap - layer->capacity) * sizeof(*items));
    layer->items = items;
    layer->capacity = cap;
    return 1;
}

/// @brief Append a new rect entry and return its index.
/// @details Grows the backing array if needed (doubling, checked). Traps via
///          `rt_trap` on capacity overflow. Returns -1 if `layer` is NULL.
///          The `type` field is opaque — callers use it as a category tag
///          (e.g. collision=1, trigger=2, spawn=3).
int64_t rt_objectlayer2d_add_rect(
    void *layer, int64_t x, int64_t y, int64_t width, int64_t height, int64_t type) {
    rt_objectlayer2d_impl *impl =
        rt2d_has_class(layer, RT2D_OBJECTLAYER_CLASS_ID) ? (rt_objectlayer2d_impl *)layer : NULL;
    if (!impl)
        return -1;
    if (!objectlayer2d_reserve(impl, impl->count + 1)) {
        rt_trap("ObjectLayer2D: capacity overflow");
        return -1;
    }
    if (width == INT64_MIN || height == INT64_MIN)
        return -1;
    if (width < 0) {
        x = rt2d_saturating_add_i64(x, width);
        width = -width;
    }
    if (height < 0) {
        y = rt2d_saturating_add_i64(y, height);
        height = -height;
    }
    if (width <= 0 || height <= 0)
        return -1;
    int64_t index = impl->count++;
    impl->items[index].x = x;
    impl->items[index].y = y;
    impl->items[index].width = width;
    impl->items[index].height = height;
    impl->items[index].type = type;
    return index;
}

/// @brief Return the number of rect entries currently in the layer.
int64_t rt_objectlayer2d_count(void *layer) {
    return rt2d_has_class(layer, RT2D_OBJECTLAYER_CLASS_ID)
               ? ((rt_objectlayer2d_impl *)layer)->count
               : 0;
}

/// @brief Reset the entry count to zero without freeing the backing array.
void rt_objectlayer2d_clear(void *layer) {
    if (rt2d_has_class(layer, RT2D_OBJECTLAYER_CLASS_ID))
        ((rt_objectlayer2d_impl *)layer)->count = 0;
}

/// @brief Return a pointer to the entry at @p index in the object layer, or NULL if out of range.
static rt_objectlayer2d_entry *objectlayer2d_get_entry(void *layer, int64_t index) {
    rt_objectlayer2d_impl *impl =
        rt2d_has_class(layer, RT2D_OBJECTLAYER_CLASS_ID) ? (rt_objectlayer2d_impl *)layer : NULL;
    if (!impl || index < 0 || index >= impl->count)
        return NULL;
    return &impl->items[index];
}

/// @brief Return the X position of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_x(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->x : 0;
}

/// @brief Return the Y position of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_y(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->y : 0;
}

/// @brief Return the width of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_width(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->width : 0;
}

/// @brief Return the height of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_height(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->height : 0;
}

/// @brief Return the application-defined type tag of the entry at `index`,
///        or 0 if out of range.
int64_t rt_objectlayer2d_get_type(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->type : 0;
}

//=============================================================================
// AutoTile2D
//=============================================================================
// 16-variant autotile resolver. Given a 4-bit neighbour mask (one bit per
// cardinal direction: N / E / S / W typically), returns the pre-configured
// tile ID to place so edges and corners join up seamlessly. The mask-to-ID
// table is filled via `SetVariant`; `Apply` writes the resolved ID directly
// into a TileLayer2D cell.

/// @brief Allocate a zeroed autotile variant table (16 entries, all 0).
void *rt_autotile2d_new(void) {
    return rt_obj_new_i64(RT2D_AUTOTILE_CLASS_ID, (int64_t)sizeof(rt_autotile2d_impl));
}

/// @brief Register the tile ID to use when the neighbour mask equals `mask`.
/// @details Only the low 4 bits of `mask` are used (16 possible combinations).
void rt_autotile2d_set_variant(void *autotile, int64_t mask, int64_t tile) {
    if (!rt2d_has_class(autotile, RT2D_AUTOTILE_CLASS_ID))
        return;
    ((rt_autotile2d_impl *)autotile)->variants[mask & 15] = tile;
}

/// @brief Look up the tile ID for a given 4-bit neighbour `mask`.
/// @details Returns 0 if the autotile pointer is NULL or the variant was never
///          set (variants default to 0 on allocation).
int64_t rt_autotile2d_resolve(void *autotile, int64_t mask) {
    return rt2d_has_class(autotile, RT2D_AUTOTILE_CLASS_ID)
               ? ((rt_autotile2d_impl *)autotile)->variants[mask & 15]
               : 0;
}

/// @brief Resolve `mask` to a tile ID and write it into the TileLayer2D cell at `(x, y)`.
/// @details Combines `rt_autotile2d_resolve` + `rt_tilelayer2d_set` so game
///          code can update a cell in one call without a temporary variable.
void rt_autotile2d_apply(void *autotile, void *layer, int64_t x, int64_t y, int64_t mask) {
    if (!rt2d_has_class(autotile, RT2D_AUTOTILE_CLASS_ID) || !layer)
        return;
    rt_tilelayer2d_set(layer, x, y, rt_autotile2d_resolve(autotile, mask));
}

//=============================================================================
