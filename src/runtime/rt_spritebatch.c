//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_spritebatch.c
/// @brief SpriteBatch implementation for efficient batched sprite rendering.
///
//===----------------------------------------------------------------------===//

#include "rt_spritebatch.h"
#include "rt_graphics.h"
#include "rt_pixels.h"
#include "rt_sprite.h"

#include <stdlib.h>
#include <string.h>

// Forward declaration from rt_io.c
extern void rt_trap(const char *msg);

//=============================================================================
// Internal Types
//=============================================================================

#define DEFAULT_CAPACITY 256
#define GROWTH_FACTOR 2

typedef enum
{
    BATCH_ITEM_SPRITE,
    BATCH_ITEM_PIXELS,
    BATCH_ITEM_REGION
} batch_item_type;

typedef struct
{
    batch_item_type type;
    void *source;      // Sprite or Pixels object
    int64_t x;         // Destination X
    int64_t y;         // Destination Y
    int64_t scale_x;   // Scale X (100 = 100%)
    int64_t scale_y;   // Scale Y (100 = 100%)
    int64_t rotation;  // Rotation in degrees
    int64_t depth;     // For depth sorting
    // For region drawing
    int64_t src_x;
    int64_t src_y;
    int64_t src_w;
    int64_t src_h;
} batch_item;

typedef struct
{
    batch_item *items;
    int64_t count;
    int64_t capacity;
    int8_t active;
    int8_t sort_by_depth;
    int64_t tint_color;
    int64_t alpha;
} spritebatch_impl;

//=============================================================================
// Helper Functions
//=============================================================================

static int compare_depth(const void *a, const void *b)
{
    const batch_item *ia = (const batch_item *)a;
    const batch_item *ib = (const batch_item *)b;
    if (ia->depth < ib->depth)
        return -1;
    if (ia->depth > ib->depth)
        return 1;
    return 0;
}

static void ensure_capacity(spritebatch_impl *batch, int64_t needed)
{
    if (batch->count + needed <= batch->capacity)
        return;

    int64_t new_capacity = batch->capacity * GROWTH_FACTOR;
    if (new_capacity < batch->count + needed)
        new_capacity = batch->count + needed;

    batch_item *new_items = (batch_item *)realloc(batch->items, (size_t)new_capacity * sizeof(batch_item));
    if (!new_items)
        rt_trap("SpriteBatch: memory allocation failed");

    batch->items = new_items;
    batch->capacity = new_capacity;
}

static void add_item(spritebatch_impl *batch, batch_item *item)
{
    ensure_capacity(batch, 1);
    batch->items[batch->count] = *item;
    batch->count++;
}

//=============================================================================
// SpriteBatch Creation
//=============================================================================

void *rt_spritebatch_new(int64_t capacity)
{
    spritebatch_impl *batch = (spritebatch_impl *)calloc(1, sizeof(spritebatch_impl));
    if (!batch)
        rt_trap("SpriteBatch: memory allocation failed");

    if (capacity <= 0)
        capacity = DEFAULT_CAPACITY;

    batch->items = (batch_item *)malloc((size_t)capacity * sizeof(batch_item));
    if (!batch->items)
    {
        free(batch);
        rt_trap("SpriteBatch: memory allocation failed");
    }

    batch->count = 0;
    batch->capacity = capacity;
    batch->active = 0;
    batch->sort_by_depth = 0;
    batch->tint_color = 0;
    batch->alpha = 255;

    return batch;
}

//=============================================================================
// SpriteBatch Operations
//=============================================================================

void rt_spritebatch_begin(void *batch_ptr)
{
    if (!batch_ptr)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    batch->count = 0;
    batch->active = 1;
}

void rt_spritebatch_end(void *batch_ptr, void *canvas)
{
    if (!batch_ptr || !canvas)
        return;

    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    if (!batch->active)
        return;

    // Sort by depth if enabled
    if (batch->sort_by_depth && batch->count > 1)
    {
        qsort(batch->items, (size_t)batch->count, sizeof(batch_item), compare_depth);
    }

    // Render all items
    for (int64_t i = 0; i < batch->count; i++)
    {
        batch_item *item = &batch->items[i];

        switch (item->type)
        {
        case BATCH_ITEM_SPRITE:
            if (item->source)
            {
                // Save original sprite state
                int64_t old_x = rt_sprite_get_x(item->source);
                int64_t old_y = rt_sprite_get_y(item->source);
                int64_t old_sx = rt_sprite_get_scale_x(item->source);
                int64_t old_sy = rt_sprite_get_scale_y(item->source);
                int64_t old_rot = rt_sprite_get_rotation(item->source);

                // Apply batch transform
                rt_sprite_set_x(item->source, item->x);
                rt_sprite_set_y(item->source, item->y);
                rt_sprite_set_scale_x(item->source, item->scale_x);
                rt_sprite_set_scale_y(item->source, item->scale_y);
                rt_sprite_set_rotation(item->source, item->rotation);

                // Draw
                rt_sprite_draw(item->source, canvas);

                // Restore original state
                rt_sprite_set_x(item->source, old_x);
                rt_sprite_set_y(item->source, old_y);
                rt_sprite_set_scale_x(item->source, old_sx);
                rt_sprite_set_scale_y(item->source, old_sy);
                rt_sprite_set_rotation(item->source, old_rot);
            }
            break;

        case BATCH_ITEM_PIXELS:
            if (item->source)
            {
                if (batch->alpha < 255 || batch->tint_color != 0)
                {
                    // With alpha/tint, use alpha blit
                    rt_canvas_blit_alpha(canvas, item->x, item->y, item->source);
                }
                else
                {
                    // Simple blit
                    rt_canvas_blit(canvas, item->x, item->y, item->source);
                }
            }
            break;

        case BATCH_ITEM_REGION:
            if (item->source)
            {
                rt_canvas_blit_region(canvas, item->x, item->y, item->source,
                                       item->src_x, item->src_y, item->src_w, item->src_h);
            }
            break;
        }
    }

    batch->active = 0;
}

void rt_spritebatch_draw(void *batch_ptr, void *sprite, int64_t x, int64_t y)
{
    rt_spritebatch_draw_ex(batch_ptr, sprite, x, y, 100, 100, 0);
}

void rt_spritebatch_draw_scaled(void *batch_ptr, void *sprite, int64_t x, int64_t y, int64_t scale)
{
    rt_spritebatch_draw_ex(batch_ptr, sprite, x, y, scale, scale, 0);
}

void rt_spritebatch_draw_ex(void *batch_ptr, void *sprite, int64_t x, int64_t y,
                             int64_t scale_x, int64_t scale_y, int64_t rotation)
{
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
    item.depth = 0; // Could be set via sprite's depth property if needed

    add_item(batch, &item);
}

void rt_spritebatch_draw_pixels(void *batch_ptr, void *pixels, int64_t x, int64_t y)
{
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

void rt_spritebatch_draw_region(void *batch_ptr, void *pixels,
                                 int64_t dx, int64_t dy,
                                 int64_t sx, int64_t sy,
                                 int64_t sw, int64_t sh)
{
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
    item.scale_x = 100;
    item.scale_y = 100;
    item.rotation = 0;
    item.depth = 0;

    add_item(batch, &item);
}

//=============================================================================
// SpriteBatch Properties
//=============================================================================

int64_t rt_spritebatch_count(void *batch_ptr)
{
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->count;
}

int64_t rt_spritebatch_capacity(void *batch_ptr)
{
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->capacity;
}

int8_t rt_spritebatch_is_active(void *batch_ptr)
{
    if (!batch_ptr)
        return 0;
    return ((spritebatch_impl *)batch_ptr)->active;
}

//=============================================================================
// SpriteBatch Settings
//=============================================================================

void rt_spritebatch_set_sort_by_depth(void *batch_ptr, int8_t enabled)
{
    if (!batch_ptr)
        return;
    ((spritebatch_impl *)batch_ptr)->sort_by_depth = enabled ? 1 : 0;
}

void rt_spritebatch_set_tint(void *batch_ptr, int64_t color)
{
    if (!batch_ptr)
        return;
    ((spritebatch_impl *)batch_ptr)->tint_color = color;
}

void rt_spritebatch_set_alpha(void *batch_ptr, int64_t alpha)
{
    if (!batch_ptr)
        return;
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    ((spritebatch_impl *)batch_ptr)->alpha = alpha;
}

void rt_spritebatch_reset_settings(void *batch_ptr)
{
    if (!batch_ptr)
        return;
    spritebatch_impl *batch = (spritebatch_impl *)batch_ptr;
    batch->sort_by_depth = 0;
    batch->tint_color = 0;
    batch->alpha = 255;
}
