//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_sprite.c
// Purpose: Sprite class implementation for 2D game development.
//
//===----------------------------------------------------------------------===//

#include "rt_sprite.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <stdlib.h>
#include <string.h>

// Maximum number of animation frames
#define MAX_SPRITE_FRAMES 64

/// @brief Sprite implementation structure.
typedef struct rt_sprite_impl
{
    int64_t x;                       ///< X position
    int64_t y;                       ///< Y position
    int64_t scale_x;                 ///< Horizontal scale (100 = 100%)
    int64_t scale_y;                 ///< Vertical scale (100 = 100%)
    int64_t rotation;                ///< Rotation in degrees
    int64_t visible;                 ///< Visibility flag
    int64_t origin_x;                ///< Origin X for rotation/scaling
    int64_t origin_y;                ///< Origin Y for rotation/scaling
    int64_t current_frame;           ///< Current animation frame
    int64_t frame_count;             ///< Number of frames
    int64_t frame_delay_ms;          ///< Delay between frames
    int64_t last_frame_time;         ///< Last frame update time
    void *frames[MAX_SPRITE_FRAMES]; ///< Frame pixel buffers
} rt_sprite_impl;

// Forward declaration for time function
extern int64_t rt_timer_ms(void);

/// @brief Allocate a new sprite.
static rt_sprite_impl *sprite_alloc(void)
{
    rt_sprite_impl *sprite = (rt_sprite_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sprite_impl));
    if (!sprite)
        return NULL;

    sprite->x = 0;
    sprite->y = 0;
    sprite->scale_x = 100;
    sprite->scale_y = 100;
    sprite->rotation = 0;
    sprite->visible = 1;
    sprite->origin_x = 0;
    sprite->origin_y = 0;
    sprite->current_frame = 0;
    sprite->frame_count = 0;
    sprite->frame_delay_ms = 100;
    sprite->last_frame_time = 0;

    for (int i = 0; i < MAX_SPRITE_FRAMES; i++)
        sprite->frames[i] = NULL;

    return sprite;
}

//=============================================================================
// Sprite Creation
//=============================================================================

void *rt_sprite_new(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Sprite.New: null pixels");
        return NULL;
    }

    rt_sprite_impl *sprite = sprite_alloc();
    if (!sprite)
        return NULL;

    // Clone the pixels and store as first frame
    void *cloned = rt_pixels_clone(pixels);
    if (cloned)
    {
        sprite->frames[0] = cloned;
        sprite->frame_count = 1;
        rt_heap_retain(cloned);
    }

    return sprite;
}

void *rt_sprite_from_file(void *path)
{
    if (!path)
        return NULL;

    void *pixels = rt_pixels_load_bmp(path);
    if (!pixels)
        return NULL;

    rt_sprite_impl *sprite = sprite_alloc();
    if (!sprite)
        return NULL;

    sprite->frames[0] = pixels;
    sprite->frame_count = 1;
    rt_heap_retain(pixels);

    return sprite;
}

//=============================================================================
// Sprite Properties
//=============================================================================

int64_t rt_sprite_get_x(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.X: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->x;
}

void rt_sprite_set_x(void *sprite_ptr, int64_t x)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.X: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->x = x;
}

int64_t rt_sprite_get_y(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Y: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->y;
}

void rt_sprite_set_y(void *sprite_ptr, int64_t y)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Y: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->y = y;
}

int64_t rt_sprite_get_width(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Width: null sprite");
        return 0;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count == 0 || !sprite->frames[sprite->current_frame])
        return 0;
    return rt_pixels_width(sprite->frames[sprite->current_frame]);
}

int64_t rt_sprite_get_height(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Height: null sprite");
        return 0;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count == 0 || !sprite->frames[sprite->current_frame])
        return 0;
    return rt_pixels_height(sprite->frames[sprite->current_frame]);
}

int64_t rt_sprite_get_scale_x(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.ScaleX: null sprite");
        return 100;
    }
    return ((rt_sprite_impl *)sprite_ptr)->scale_x;
}

void rt_sprite_set_scale_x(void *sprite_ptr, int64_t scale)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.ScaleX: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->scale_x = scale;
}

int64_t rt_sprite_get_scale_y(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.ScaleY: null sprite");
        return 100;
    }
    return ((rt_sprite_impl *)sprite_ptr)->scale_y;
}

void rt_sprite_set_scale_y(void *sprite_ptr, int64_t scale)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.ScaleY: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->scale_y = scale;
}

int64_t rt_sprite_get_rotation(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Rotation: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->rotation;
}

void rt_sprite_set_rotation(void *sprite_ptr, int64_t degrees)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Rotation: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->rotation = degrees;
}

int64_t rt_sprite_get_visible(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Visible: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->visible;
}

void rt_sprite_set_visible(void *sprite_ptr, int64_t visible)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Visible: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->visible = visible ? 1 : 0;
}

int64_t rt_sprite_get_frame(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Frame: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->current_frame;
}

void rt_sprite_set_frame(void *sprite_ptr, int64_t frame)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Frame: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (frame >= 0 && frame < sprite->frame_count)
        sprite->current_frame = frame;
}

int64_t rt_sprite_get_frame_count(void *sprite_ptr)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.FrameCount: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->frame_count;
}

//=============================================================================
// Sprite Methods
//=============================================================================

void rt_sprite_draw(void *sprite_ptr, void *canvas_ptr)
{
    if (!sprite_ptr || !canvas_ptr)
        return;

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;

    // Don't draw if not visible
    if (!sprite->visible)
        return;

    // Get current frame
    if (sprite->frame_count == 0)
        return;
    void *frame = sprite->frames[sprite->current_frame];
    if (!frame)
        return;

    int64_t w = rt_pixels_width(frame);
    int64_t h = rt_pixels_height(frame);

    // If no scaling or rotation, use simple blit
    if (sprite->scale_x == 100 && sprite->scale_y == 100 && sprite->rotation == 0)
    {
        rt_canvas_blit_alpha(canvas_ptr, sprite->x, sprite->y, frame);
        return;
    }

    // Scale the frame if needed
    void *transformed = frame;

    if (sprite->scale_x != 100 || sprite->scale_y != 100)
    {
        int64_t new_w = w * sprite->scale_x / 100;
        int64_t new_h = h * sprite->scale_y / 100;
        if (new_w < 1)
            new_w = 1;
        if (new_h < 1)
            new_h = 1;
        void *scaled = rt_pixels_scale(frame, new_w, new_h);
        if (scaled)
            transformed = scaled;
    }

    // Rotate the (scaled) frame if needed
    if (sprite->rotation != 0)
    {
        void *rotated = rt_pixels_rotate(transformed, (double)sprite->rotation);
        if (rotated)
            transformed = rotated;
    }

    // Calculate blit position, accounting for origin and transformed dimensions
    int64_t tw = rt_pixels_width(transformed);
    int64_t th = rt_pixels_height(transformed);

    // The origin point should stay at the sprite's (x, y) position
    // After rotation, the image size may have changed, so we need to center it
    int64_t blit_x = sprite->x - tw / 2;
    int64_t blit_y = sprite->y - th / 2;

    // If origin is set, use it as the center point relative to original size
    if (sprite->origin_x != 0 || sprite->origin_y != 0)
    {
        // The sprite position should be at the original origin point
        // which is now at the center of the rotated image
        blit_x = sprite->x - tw / 2;
        blit_y = sprite->y - th / 2;
    }

    rt_canvas_blit_alpha(canvas_ptr, blit_x, blit_y, transformed);

    // Note: transformed pixels will be GC'd
}

void rt_sprite_set_origin(void *sprite_ptr, int64_t x, int64_t y)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.SetOrigin: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    sprite->origin_x = x;
    sprite->origin_y = y;
}

void rt_sprite_add_frame(void *sprite_ptr, void *pixels)
{
    if (!sprite_ptr || !pixels)
    {
        rt_trap("Sprite.AddFrame: null argument");
        return;
    }

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count >= MAX_SPRITE_FRAMES)
        return;

    void *cloned = rt_pixels_clone(pixels);
    if (cloned)
    {
        sprite->frames[sprite->frame_count] = cloned;
        sprite->frame_count++;
        rt_heap_retain(cloned);
    }
}

void rt_sprite_set_frame_delay(void *sprite_ptr, int64_t ms)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.SetFrameDelay: null sprite");
        return;
    }
    if (ms < 1)
        ms = 1;
    ((rt_sprite_impl *)sprite_ptr)->frame_delay_ms = ms;
}

void rt_sprite_update(void *sprite_ptr)
{
    if (!sprite_ptr)
        return;

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count <= 1)
        return;

    int64_t now = rt_timer_ms();
    if (sprite->last_frame_time == 0)
        sprite->last_frame_time = now;

    if (now - sprite->last_frame_time >= sprite->frame_delay_ms)
    {
        sprite->current_frame = (sprite->current_frame + 1) % sprite->frame_count;
        sprite->last_frame_time = now;
    }
}

bool rt_sprite_overlaps(void *sprite_ptr, void *other_ptr)
{
    if (!sprite_ptr || !other_ptr)
        return false;

    rt_sprite_impl *s1 = (rt_sprite_impl *)sprite_ptr;
    rt_sprite_impl *s2 = (rt_sprite_impl *)other_ptr;

    if (!s1->visible || !s2->visible)
        return false;

    // Get bounding boxes
    int64_t w1 = rt_sprite_get_width(sprite_ptr) * s1->scale_x / 100;
    int64_t h1 = rt_sprite_get_height(sprite_ptr) * s1->scale_y / 100;
    int64_t w2 = rt_sprite_get_width(other_ptr) * s2->scale_x / 100;
    int64_t h2 = rt_sprite_get_height(other_ptr) * s2->scale_y / 100;

    int64_t x1 = s1->x - s1->origin_x;
    int64_t y1 = s1->y - s1->origin_y;
    int64_t x2 = s2->x - s2->origin_x;
    int64_t y2 = s2->y - s2->origin_y;

    // AABB collision test
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

bool rt_sprite_contains(void *sprite_ptr, int64_t px, int64_t py)
{
    if (!sprite_ptr)
        return false;

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (!sprite->visible)
        return false;

    int64_t w = rt_sprite_get_width(sprite_ptr) * sprite->scale_x / 100;
    int64_t h = rt_sprite_get_height(sprite_ptr) * sprite->scale_y / 100;
    int64_t x = sprite->x - sprite->origin_x;
    int64_t y = sprite->y - sprite->origin_y;

    return (px >= x && px < x + w && py >= y && py < y + h);
}

void rt_sprite_move(void *sprite_ptr, int64_t dx, int64_t dy)
{
    if (!sprite_ptr)
    {
        rt_trap("Sprite.Move: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    sprite->x += dx;
    sprite->y += dy;
}
