//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_spritebatch.c
// Purpose: Batched sprite renderer for Viper games. Accumulates a list of
//   draw commands (sprite + position + optional transform) each frame and
//   submits them to the Canvas in a single pass. Reduces per-sprite overhead
//   compared to individual canvas.Blit() calls from Zia, and enables sorting
//   by depth (Z-order) before submission. Typical use: rendering dozens to
//   hundreds of sprites per frame (enemies, projectiles, particles).
//
// Key invariants:
//   - The batch accumulates draw calls until rt_spritebatch_flush() (or the
//     equivalent of an end-of-frame reset) is called. Commands are stored in
//     a dynamic array that grows as needed.
//   - Draw commands contain: Pixels reference, destination x/y, optional
//     source region (for sprite sheets), and optional Z-order integer.
//   - If depth-sort is enabled, commands are sorted by Z ascending before
//     blitting, so lower Z values appear behind higher Z values.
//   - The batch holds retained references to Pixels objects. All references
//     are released when the batch is cleared or destroyed.
//   - After flushing, the command list is cleared (count reset to 0) but the
//     backing array is NOT freed — it is reused next frame to avoid repeated
//     allocation. Call rt_spritebatch_destroy() to fully free.
//
// Ownership/Lifetime:
//   - SpriteBatch objects are GC-managed (rt_obj_new_i64). The command array
//     is freed by the GC finalizer. Any retained Pixels refs are also released.
//
// Links: src/runtime/graphics/rt_spritebatch.h (public API),
//        src/runtime/graphics/rt_sprite.h (single-sprite API),
//        docs/viperlib/game.md (SpriteBatch section)
//
//===----------------------------------------------------------------------===//

#include "rt_spritebatch.h"
#include "rt_graphics.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_sprite.h"

#include <stdlib.h>
#include <string.h>

// Forward declaration from rt_io.c
#include "rt_trap.h"

//=============================================================================
// Internal Types
//=============================================================================

#define DEFAULT_CAPACITY 256
#define GROWTH_FACTOR 2

typedef enum { BATCH_ITEM_SPRITE, BATCH_ITEM_PIXELS, BATCH_ITEM_REGION } batch_item_type;

typedef struct {
    batch_item_type type;
    void *source;     // Sprite or Pixels object
    int64_t x;        // Destination X
    int64_t y;        // Destination Y
    int64_t scale_x;  // Scale X (100 = 100%)
    int64_t scale_y;  // Scale Y (100 = 100%)
    int64_t rotation; // Rotation in degrees
    int64_t depth;    // For depth sorting
    // For region drawing
    int64_t src_x;
    int64_t src_y;
    int64_t src_w;
    int64_t src_h;
} batch_item;

typedef struct {
    batch_item *items;
    int64_t count;
    int64_t capacity;
    int8_t active;
    int8_t sort_by_depth;
    int64_t tint_color;
    int64_t alpha;
} spritebatch_impl;

typedef struct {
    int64_t width;
    int64_t height;
    uint32_t *data;
} spritebatch_pixels_view;

//=============================================================================
// Helper Functions
//=============================================================================

static int compare_depth(const void *a, const void *b) {
    const batch_item *ia = (const batch_item *)a;
    const batch_item *ib = (const batch_item *)b;
    if (ia->depth < ib->depth)
        return -1;
    if (ia->depth > ib->depth)
        return 1;
    return 0;
}

static void ensure_capacity(spritebatch_impl *batch, int64_t needed) {
    if (batch->count + needed <= batch->capacity)
        return;

    int64_t new_capacity = batch->capacity * GROWTH_FACTOR;
    if (new_capacity < batch->count + needed)
        new_capacity = batch->count + needed;

    batch_item *new_items =
        (batch_item *)realloc(batch->items, (size_t)new_capacity * sizeof(batch_item));
    if (!new_items) {
        rt_trap("SpriteBatch: memory allocation failed");
        return;
    }
    batch->items = new_items;
    batch->capacity = new_capacity;
}

static void add_item(spritebatch_impl *batch, batch_item *item) {
    ensure_capacity(batch, 1);
    batch->items[batch->count] = *item;
    batch->count++;
}

static void *apply_batch_color(void *pixels, int64_t tint_color, int64_t alpha) {
    if (!pixels || (tint_color == 0 && alpha >= 255))
        return pixels;

    void *result = pixels;
    if (tint_color != 0) {
        void *tinted = rt_pixels_tint(result, tint_color);
        if (tinted)
            result = tinted;
    }

    if (alpha < 255) {
        if (result == pixels) {
            void *cloned = rt_pixels_clone(result);
            if (cloned)
                result = cloned;
        }

        spritebatch_pixels_view *impl = (spritebatch_pixels_view *)result;
        if (impl && impl->data) {
            int64_t pixel_count = impl->width * impl->height;
            for (int64_t i = 0; i < pixel_count; ++i) {
                uint32_t rgba = impl->data[i];
                uint32_t a = rgba & 0xFFu;
                a = (a * (uint32_t)alpha + 127u) / 255u;
                impl->data[i] = (rgba & 0xFFFFFF00u) | a;
            }
        }
    }

    return result;
}

static void *extract_region_pixels(void *pixels, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    if (!pixels || sw <= 0 || sh <= 0)
        return NULL;

    void *region = rt_pixels_new(sw, sh);
    if (!region)
        return NULL;

    rt_pixels_copy(region, 0, 0, pixels, sx, sy, sw, sh);
    return region;
}

static void draw_region_item(spritebatch_impl *batch, void *canvas, const batch_item *item) {
    if (!item->source)
        return;

    const bool needsTransform = item->scale_x != 100 || item->scale_y != 100 || item->rotation != 0;
    const bool needsColor = batch->tint_color != 0 || batch->alpha < 255;
    if (!needsTransform && !needsColor) {
        rt_canvas_blit_region(canvas,
                              item->x,
                              item->y,
                              item->source,
                              item->src_x,
                              item->src_y,
                              item->src_w,
                              item->src_h);
        return;
    }

    void *transformed =
        extract_region_pixels(item->source, item->src_x, item->src_y, item->src_w, item->src_h);
    if (!transformed)
        return;

    if (item->scale_x != 100 || item->scale_y != 100) {
        int64_t new_w = item->src_w * item->scale_x / 100;
        int64_t new_h = item->src_h * item->scale_y / 100;
        if (new_w < 1)
            new_w = 1;
        if (new_h < 1)
            new_h = 1;
        void *scaled = rt_pixels_scale(transformed, new_w, new_h);
        if (scaled)
            transformed = scaled;
    }

    if (item->rotation != 0) {
        int64_t src_w = rt_pixels_width(transformed);
        int64_t src_h = rt_pixels_height(transformed);
        int64_t half_w = src_w;
        int64_t half_h = src_h;
        void *padded = rt_pixels_new(half_w * 2, half_h * 2);
        if (padded) {
            rt_pixels_copy(padded, half_w, half_h, transformed, 0, 0, src_w, src_h);
            transformed = padded;
        }
        void *rotated = rt_pixels_rotate(transformed, (double)item->rotation);
        if (rotated)
            transformed = rotated;
    }

    transformed = apply_batch_color(transformed, batch->tint_color, batch->alpha);

    int64_t blit_x = item->x;
    int64_t blit_y = item->y;
    if (item->rotation != 0) {
        blit_x = item->x - rt_pixels_width(transformed) / 2;
        blit_y = item->y - rt_pixels_height(transformed) / 2;
    }

    rt_canvas_blit_alpha(canvas, blit_x, blit_y, transformed);
}

//=============================================================================
// SpriteBatch Creation
//=============================================================================

static void spritebatch_finalize(void *obj) {
    spritebatch_impl *batch = (spritebatch_impl *)obj;
    free(batch->items);
    batch->items = NULL;
}

/// @brief Construct a SpriteBatch with initial command-array capacity. `capacity <= 0` falls
/// back to 256. The batch starts inactive (default tint 0, alpha 255, depth-sort off). Use
/// `_begin` / draw calls / `_end` to submit batched draws to a Canvas in one pass.
void *rt_spritebatch_new(int64_t capacity) {
    spritebatch_impl *batch =
        (spritebatch_impl *)rt_obj_new_i64(0, (int64_t)sizeof(spritebatch_impl));
    memset(batch, 0, sizeof(spritebatch_impl));

    if (capacity <= 0)
        capacity = DEFAULT_CAPACITY;

    batch->items = (batch_item *)malloc((size_t)capacity * sizeof(batch_item));
    if (!batch->items) {
        rt_trap("SpriteBatch: memory allocation failed");
    }

    batch->count = 0;
    batch->capacity = capacity;
    batch->active = 0;
    batch->sort_by_depth = 0;
    batch->tint_color = 0;
    batch->alpha = 255;

    rt_obj_set_finalizer(batch, spritebatch_finalize);
    return batch;
}

//=============================================================================
// SpriteBatch Operations
//=============================================================================

/// @brief Begin the spritebatch.
void rt_spritebatch_begin(void *batch_ptr) {
    if (!batch_ptr)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    batch->count = 0;
    batch->active = 1;
}

/// @brief End the spritebatch.
void rt_spritebatch_end(void *batch_ptr, void *canvas) {
    if (!batch_ptr)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    if (!batch->active)
        return;
    if (!canvas) {
        batch->active = 0;
        return;
    }

    // Sort by depth if enabled
    if (batch->sort_by_depth && batch->count > 1) {
        qsort(batch->items, (size_t)batch->count, sizeof(batch_item), compare_depth);
    }

    // Render all items
    for (int64_t i = 0; i < batch->count; i++) {
        batch_item *item = &batch->items[i];

        switch (item->type) {
            case BATCH_ITEM_SPRITE:
                if (item->source) {
                    rt_sprite_draw_transformed(item->source,
                                               canvas,
                                               item->x,
                                               item->y,
                                               item->scale_x,
                                               item->scale_y,
                                               item->rotation,
                                               batch->tint_color,
                                               batch->alpha);
                }
                break;

            case BATCH_ITEM_PIXELS:
                if (item->source) {
                    void *draw_src =
                        apply_batch_color(item->source, batch->tint_color, batch->alpha);
                    if (batch->alpha < 255 || batch->tint_color != 0) {
                        rt_canvas_blit_alpha(canvas, item->x, item->y, draw_src);
                    } else {
                        rt_canvas_blit(canvas, item->x, item->y, draw_src);
                    }
                }
                break;

            case BATCH_ITEM_REGION:
                draw_region_item(batch, canvas, item);
                break;
        }
    }

    batch->active = 0;
}

/// @brief Draw the spritebatch.
void rt_spritebatch_draw(void *batch_ptr, void *sprite, int64_t x, int64_t y) {
    rt_spritebatch_draw_ex(batch_ptr, sprite, x, y, 100, 100, 0);
}

/// @brief Draw the scaled of the spritebatch.
void rt_spritebatch_draw_scaled(
    void *batch_ptr, void *sprite, int64_t x, int64_t y, int64_t scale) {
    rt_spritebatch_draw_ex(batch_ptr, sprite, x, y, scale, scale, 0);
}

/// @brief Append a sprite draw command to the batch with custom scale (×100) and rotation
/// (degrees). Depth defaults to the sprite's own depth so depth-sort keeps Z-order. Silently
/// no-ops if the batch is not currently `_begin`/`_end`-bracketed.
void rt_spritebatch_draw_ex(void *batch_ptr,
                            void *sprite,
                            int64_t x,
                            int64_t y,
                            int64_t scale_x,
                            int64_t scale_y,
                            int64_t rotation) {
    if (!batch_ptr || !sprite)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    if (!batch->active)
        return;

    batch_item item = {0};
    item.type = BATCH_ITEM_SPRITE;
    item.source = sprite;
    item.x = x;
    item.y = y;
    item.scale_x = scale_x;
    item.scale_y = scale_y;
    item.rotation = rotation;
    item.depth = rt_sprite_get_depth(sprite);

    add_item(batch, &item);
}

/// @brief Draw the pixels of the spritebatch.
void rt_spritebatch_draw_pixels(void *batch_ptr, void *pixels, int64_t x, int64_t y) {
    if (!batch_ptr || !pixels)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    if (!batch->active)
        return;

    batch_item item = {0};
    item.type = BATCH_ITEM_PIXELS;
    item.source = pixels;
    item.x = x;
    item.y = y;
    item.scale_x = 100;
    item.scale_y = 100;
    item.rotation = 0;
    item.depth = 0;

    add_item(batch, &item);
}

/// @brief Append a region (sub-rectangle) draw of `pixels` at (dx, dy) with native size and no
/// rotation. Convenience for drawing one frame from a sprite-sheet without computing transforms.
void rt_spritebatch_draw_region(void *batch_ptr,
                                void *pixels,
                                int64_t dx,
                                int64_t dy,
                                int64_t sx,
                                int64_t sy,
                                int64_t sw,
                                int64_t sh) {
    rt_spritebatch_draw_region_ex(batch_ptr, pixels, dx, dy, sx, sy, sw, sh, 100, 100, 0, 0);
}

/// @brief Full region-draw command: source rect within `pixels`, destination (dx, dy), per-axis
/// scale (×100), rotation (degrees), and explicit Z `depth`. The depth-sort pass uses `depth`
/// when enabled — lower values draw first (behind higher).
void rt_spritebatch_draw_region_ex(void *batch_ptr,
                                   void *pixels,
                                   int64_t dx,
                                   int64_t dy,
                                   int64_t sx,
                                   int64_t sy,
                                   int64_t sw,
                                   int64_t sh,
                                   int64_t scale_x,
                                   int64_t scale_y,
                                   int64_t rotation,
                                   int64_t depth) {
    if (!batch_ptr || !pixels)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    if (!batch->active)
        return;

    batch_item item = {0};
    item.type = BATCH_ITEM_REGION;
    item.source = pixels;
    item.x = dx;
    item.y = dy;
    item.src_x = sx;
    item.src_y = sy;
    item.src_w = sw;
    item.src_h = sh;
    item.scale_x = scale_x;
    item.scale_y = scale_y;
    item.rotation = rotation;
    item.depth = depth;

    add_item(batch, &item);
}

//=============================================================================
// SpriteBatch Properties
//=============================================================================

/// @brief Return the count of elements in the spritebatch.
int64_t rt_spritebatch_count(void *batch_ptr) {
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->count;
}

/// @brief Capacity the spritebatch.
int64_t rt_spritebatch_capacity(void *batch_ptr) {
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->capacity;
}

/// @brief Is the active of the spritebatch.
int8_t rt_spritebatch_is_active(void *batch_ptr) {
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->active;
}

//=============================================================================
// SpriteBatch Settings
//=============================================================================

/// @brief Set the sort by depth of the spritebatch.
void rt_spritebatch_set_sort_by_depth(void *batch_ptr, int8_t enabled) {
    if (!batch_ptr)
        return;
    ((spritebatch_impl *)batch_ptr)->sort_by_depth = enabled ? 1 : 0;
}

/// @brief Set the tint of the spritebatch.
void rt_spritebatch_set_tint(void *batch_ptr, int64_t color) {
    if (!batch_ptr)
        return;
    ((spritebatch_impl *)batch_ptr)->tint_color = color;
}

/// @brief Set the alpha of the spritebatch.
void rt_spritebatch_set_alpha(void *batch_ptr, int64_t alpha) {
    if (!batch_ptr)
        return;
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    ((spritebatch_impl *)batch_ptr)->alpha = alpha;
}

/// @brief Reset the settings of the spritebatch.
void rt_spritebatch_reset_settings(void *batch_ptr) {
    if (!batch_ptr)
        return;
    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    batch->sort_by_depth = 0;
    batch->tint_color = 0;
    batch->alpha = 255;
}
