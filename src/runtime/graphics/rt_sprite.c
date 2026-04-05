//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_sprite.c
// Purpose: 2D sprite object for Viper games. Wraps a Pixels (or SpriteSheet
//   region) reference with position, scale, rotation, color tint, and
//   visibility attributes. Provides an Anchor system for offset-based
//   placement (e.g. center the sprite on the entity position rather than
//   placing its top-left corner there). Sprites are drawn by blitting their
//   pixel data to a Canvas, optionally transformed by a Camera.
//
// Key invariants:
//   - A sprite holds a reference to a Pixels buffer or SpriteSheet frame. The
//     Pixels buffer is retained by the sprite and released on destroy.
//   - Position (x, y) is the world-space anchor point in integer pixels.
//     The actual blit origin is offset by (anchor_x * width, anchor_y * height)
//     where anchor values are in [0.0, 1.0]: 0.0 = top-left, 0.5 = center,
//     1.0 = bottom-right.
//   - Scale is an integer percentage (100 = 1×, 200 = 2×). It is applied to
//     the destination blit rectangle; the source Pixels buffer is unchanged.
//   - Rotation is an integer degree value. Rotation is performed by the
//     Canvas/Pixels layer — the sprite stores only the requested angle.
//   - A sprite with visible == 0 is skipped during Draw() calls.
//
// Ownership/Lifetime:
//   - Sprite objects are GC-managed (rt_obj_new_i64). The Pixels reference is
//     retained on assignment and released in the finalizer.
//
// Links: src/runtime/graphics/rt_sprite.h (public API),
//        src/runtime/graphics/rt_spritesheet.h (atlas frames),
//        docs/viperlib/game.md (Sprite section)
//
//===----------------------------------------------------------------------===//

#include "rt_sprite.h"

#include "rt_gif.h"
#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Maximum number of animation frames
#define MAX_SPRITE_FRAMES 64

/// @brief Sprite implementation structure.
typedef struct rt_sprite_impl {
    int64_t x;                       ///< X position
    int64_t y;                       ///< Y position
    int64_t scale_x;                 ///< Horizontal scale (100 = 100%)
    int64_t scale_y;                 ///< Vertical scale (100 = 100%)
    int64_t rotation;                ///< Rotation in degrees
    int64_t depth;                   ///< Depth used by SpriteBatch sorting
    int64_t visible;                 ///< Visibility flag
    int64_t origin_x;                ///< Origin X for rotation/scaling
    int64_t origin_y;                ///< Origin Y for rotation/scaling
    int64_t current_frame;           ///< Current animation frame
    int64_t frame_count;             ///< Number of frames
    int64_t frame_delay_ms;          ///< Delay between frames
    int64_t last_frame_time;         ///< Last frame update time
    int64_t flip_x;                  ///< Horizontal flip flag
    int64_t flip_y;                  ///< Vertical flip flag
    void *frames[MAX_SPRITE_FRAMES]; ///< Frame pixel buffers
} rt_sprite_impl;

// Forward declaration for time function
extern int64_t rt_timer_ms(void);

static void *sprite_get_current_frame_ptr(rt_sprite_impl *sprite) {
    if (!sprite || sprite->frame_count <= 0 || sprite->current_frame < 0 ||
        sprite->current_frame >= sprite->frame_count)
        return NULL;
    return sprite->frames[sprite->current_frame];
}

static void sprite_apply_alpha(void *pixels, int64_t alpha) {
    if (!pixels || alpha >= 255)
        return;
    if (alpha < 0)
        alpha = 0;
    rt_pixels_impl *impl = (rt_pixels_impl *)pixels;
    if (!impl->data)
        return;
    uint32_t *data = impl->data;
    int64_t count = impl->width * impl->height;
    for (int64_t i = 0; i < count; i++) {
        uint32_t rgba = data[i];
        uint32_t a = rgba & 0xFFu;
        a = (a * (uint32_t)alpha + 127u) / 255u;
        data[i] = (rgba & 0xFFFFFF00u) | a;
    }
}

static int64_t sprite_scale_origin(int64_t origin, int64_t scale) {
    return (origin * scale) / 100;
}

static void *sprite_prepare_pixels(rt_sprite_impl *sprite,
                                   int64_t scale_x,
                                   int64_t scale_y,
                                   int64_t rotation,
                                   int64_t tint_color,
                                   int64_t alpha,
                                   int64_t *origin_x_out,
                                   int64_t *origin_y_out,
                                   int8_t *origin_centered_out) {
    void *frame = sprite_get_current_frame_ptr(sprite);
    if (!frame)
        return NULL;

    void *transformed = frame;

    if (sprite->flip_x) {
        transformed = rt_pixels_clone(transformed);
        if (transformed)
            rt_pixels_flip_h(transformed);
    }
    if (sprite->flip_y) {
        if (transformed == frame)
            transformed = rt_pixels_clone(transformed);
        if (transformed)
            rt_pixels_flip_v(transformed);
    }

    if (!transformed)
        return NULL;

    if (scale_x != 100 || scale_y != 100) {
        int64_t new_w = rt_pixels_width(transformed) * scale_x / 100;
        int64_t new_h = rt_pixels_height(transformed) * scale_y / 100;
        if (new_w < 1)
            new_w = 1;
        if (new_h < 1)
            new_h = 1;
        void *scaled = rt_pixels_scale(transformed, new_w, new_h);
        if (scaled)
            transformed = scaled;
    }

    int64_t origin_x = sprite_scale_origin(sprite->origin_x, scale_x);
    int64_t origin_y = sprite_scale_origin(sprite->origin_y, scale_y);
    int8_t origin_centered = 0;

    if (rotation != 0) {
        int64_t src_w = rt_pixels_width(transformed);
        int64_t src_h = rt_pixels_height(transformed);
        int64_t center_x = src_w / 2;
        int64_t center_y = src_h / 2;

        if (origin_x != center_x || origin_y != center_y) {
            int64_t half_w = llabs(origin_x);
            int64_t edge_w = llabs(src_w - origin_x);
            int64_t half_h = llabs(origin_y);
            int64_t edge_h = llabs(src_h - origin_y);
            if (edge_w > half_w)
                half_w = edge_w;
            if (edge_h > half_h)
                half_h = edge_h;
            if (half_w < 1)
                half_w = 1;
            if (half_h < 1)
                half_h = 1;

            void *padded = rt_pixels_new(half_w * 2, half_h * 2);
            if (padded) {
                rt_pixels_copy(
                    padded, half_w - origin_x, half_h - origin_y, transformed, 0, 0, src_w, src_h);
                transformed = padded;
                origin_x = half_w;
                origin_y = half_h;
            }
        }

        void *rotated = rt_pixels_rotate(transformed, (double)rotation);
        if (rotated)
            transformed = rotated;
        origin_centered = 1;
    }

    if (tint_color != 0) {
        void *tinted = rt_pixels_tint(transformed, tint_color);
        if (tinted)
            transformed = tinted;
    }

    if (alpha < 255) {
        if (transformed == frame) {
            void *cloned = rt_pixels_clone(transformed);
            if (!cloned)
                return transformed;
            transformed = cloned;
        }
        sprite_apply_alpha(transformed, alpha);
    }

    if (origin_x_out)
        *origin_x_out = origin_x;
    if (origin_y_out)
        *origin_y_out = origin_y;
    if (origin_centered_out)
        *origin_centered_out = origin_centered;
    return transformed;
}

static void sprite_finalize(void *obj) {
    rt_sprite_impl *sprite = (rt_sprite_impl *)obj;
    for (int i = 0; i < MAX_SPRITE_FRAMES; i++) {
        if (sprite->frames[i])
            rt_heap_release(sprite->frames[i]);
    }
}

/// @brief Allocate a new sprite.
static rt_sprite_impl *sprite_alloc(void) {
    rt_sprite_impl *sprite = (rt_sprite_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sprite_impl));
    if (!sprite)
        return NULL;

    sprite->x = 0;
    sprite->y = 0;
    sprite->scale_x = 100;
    sprite->scale_y = 100;
    sprite->rotation = 0;
    sprite->depth = 0;
    sprite->visible = 1;
    sprite->origin_x = 0;
    sprite->origin_y = 0;
    sprite->current_frame = 0;
    sprite->frame_count = 0;
    sprite->frame_delay_ms = 100;
    sprite->last_frame_time = 0;
    sprite->flip_x = 0;
    sprite->flip_y = 0;

    for (int i = 0; i < MAX_SPRITE_FRAMES; i++)
        sprite->frames[i] = NULL;

    rt_obj_set_finalizer(sprite, sprite_finalize);
    return sprite;
}

//=============================================================================
// Sprite Creation
//=============================================================================

void *rt_sprite_new(void *pixels) {
    if (!pixels) {
        rt_trap("Sprite.New: null pixels");
        return NULL;
    }

    rt_sprite_impl *sprite = sprite_alloc();
    if (!sprite)
        return NULL;

    // Clone the pixels and store as first frame
    void *cloned = rt_pixels_clone(pixels);
    if (cloned) {
        sprite->frames[0] = cloned;
        sprite->frame_count = 1;
        rt_heap_retain(cloned);
    }

    return sprite;
}

/// @brief Detect image format from file magic bytes.
/// @return 1=BMP, 2=PNG, 0=unknown
static int detect_image_format(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return 0;
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, f);
    fclose(f);
    if (n >= 2 && hdr[0] == 'B' && hdr[1] == 'M')
        return 1; // BMP
    if (n >= 8 && hdr[0] == 137 && hdr[1] == 80 && hdr[2] == 78 && hdr[3] == 71)
        return 2; // PNG
    if (n >= 2 && hdr[0] == 0xFF && hdr[1] == 0xD8)
        return 3; // JPEG
    if (n >= 3 && hdr[0] == 'G' && hdr[1] == 'I' && hdr[2] == 'F')
        return 4; // GIF
    return 0;
}

void *rt_sprite_from_file(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    int fmt = detect_image_format(filepath);

    // Animated GIF: decode all frames directly into the sprite
    if (fmt == 4) {
        gif_frame_t *gif_frames = NULL;
        int gif_count = 0, gif_w = 0, gif_h = 0;
        if (gif_decode_file(filepath, &gif_frames, &gif_count, &gif_w, &gif_h) <= 0)
            return NULL;

        rt_sprite_impl *sprite = sprite_alloc();
        if (!sprite) {
            free(gif_frames);
            return NULL;
        }
        int n = gif_count < MAX_SPRITE_FRAMES ? gif_count : MAX_SPRITE_FRAMES;
        for (int i = 0; i < n; i++) {
            sprite->frames[i] = gif_frames[i].pixels;
            rt_heap_retain(gif_frames[i].pixels);
        }
        sprite->frame_count = n;
        if (gif_count > 0 && gif_frames[0].delay_ms > 0)
            sprite->frame_delay_ms = gif_frames[0].delay_ms;
        free(gif_frames);
        return sprite;
    }

    // Single-image formats: BMP, PNG, JPEG
    void *pixels = NULL;
    switch (fmt) {
        case 1:
            pixels = rt_pixels_load_bmp(path);
            break;
        case 2:
            pixels = rt_pixels_load_png(path);
            break;
        case 3:
            pixels = rt_pixels_load_jpeg(path);
            break;
        default:
            return NULL;
    }
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

int64_t rt_sprite_get_x(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.X: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->x;
}

void rt_sprite_set_x(void *sprite_ptr, int64_t x) {
    if (!sprite_ptr) {
        rt_trap("Sprite.X: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->x = x;
}

int64_t rt_sprite_get_y(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Y: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->y;
}

void rt_sprite_set_y(void *sprite_ptr, int64_t y) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Y: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->y = y;
}

int64_t rt_sprite_get_width(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Width: null sprite");
        return 0;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count == 0 || !sprite->frames[sprite->current_frame])
        return 0;
    return rt_pixels_width(sprite->frames[sprite->current_frame]);
}

int64_t rt_sprite_get_height(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Height: null sprite");
        return 0;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count == 0 || !sprite->frames[sprite->current_frame])
        return 0;
    return rt_pixels_height(sprite->frames[sprite->current_frame]);
}

int64_t rt_sprite_get_scale_x(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.ScaleX: null sprite");
        return 100;
    }
    return ((rt_sprite_impl *)sprite_ptr)->scale_x;
}

void rt_sprite_set_scale_x(void *sprite_ptr, int64_t scale) {
    if (!sprite_ptr) {
        rt_trap("Sprite.ScaleX: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->scale_x = scale;
}

int64_t rt_sprite_get_scale_y(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.ScaleY: null sprite");
        return 100;
    }
    return ((rt_sprite_impl *)sprite_ptr)->scale_y;
}

void rt_sprite_set_scale_y(void *sprite_ptr, int64_t scale) {
    if (!sprite_ptr) {
        rt_trap("Sprite.ScaleY: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->scale_y = scale;
}

int64_t rt_sprite_get_rotation(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Rotation: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->rotation;
}

void rt_sprite_set_rotation(void *sprite_ptr, int64_t degrees) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Rotation: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->rotation = degrees;
}

int64_t rt_sprite_get_depth(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Depth: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->depth;
}

void rt_sprite_set_depth(void *sprite_ptr, int64_t depth) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Depth: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->depth = depth;
}

int64_t rt_sprite_get_visible(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Visible: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->visible;
}

void rt_sprite_set_visible(void *sprite_ptr, int64_t visible) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Visible: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->visible = visible ? 1 : 0;
}

int64_t rt_sprite_get_frame(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Frame: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->current_frame;
}

void rt_sprite_set_frame(void *sprite_ptr, int64_t frame) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Frame: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (frame >= 0 && frame < sprite->frame_count) {
        sprite->current_frame = frame;
        sprite->last_frame_time = 0; // Reset timer so animation resumes cleanly
    }
}

int64_t rt_sprite_get_frame_count(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.FrameCount: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->frame_count;
}

int64_t rt_sprite_get_flip_x(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.FlipX: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->flip_x;
}

void rt_sprite_set_flip_x(void *sprite_ptr, int64_t flip) {
    if (!sprite_ptr) {
        rt_trap("Sprite.FlipX: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->flip_x = flip ? 1 : 0;
}

int64_t rt_sprite_get_flip_y(void *sprite_ptr) {
    if (!sprite_ptr) {
        rt_trap("Sprite.FlipY: null sprite");
        return 0;
    }
    return ((rt_sprite_impl *)sprite_ptr)->flip_y;
}

void rt_sprite_set_flip_y(void *sprite_ptr, int64_t flip) {
    if (!sprite_ptr) {
        rt_trap("Sprite.FlipY: null sprite");
        return;
    }
    ((rt_sprite_impl *)sprite_ptr)->flip_y = flip ? 1 : 0;
}

//=============================================================================
// Sprite Methods
//=============================================================================

void rt_sprite_draw_transformed(void *sprite_ptr,
                                void *canvas_ptr,
                                int64_t x,
                                int64_t y,
                                int64_t scale_x,
                                int64_t scale_y,
                                int64_t rotation,
                                int64_t tint_color,
                                int64_t alpha) {
    if (!sprite_ptr || !canvas_ptr)
        return;

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;

    // Don't draw if not visible
    if (!sprite->visible)
        return;

    void *frame = sprite_get_current_frame_ptr(sprite);
    if (!frame)
        return;

    // If no transform at all, use simple blit
    if (scale_x == 100 && scale_y == 100 && rotation == 0 && !sprite->flip_x && !sprite->flip_y &&
        tint_color == 0 && alpha >= 255) {
        rt_canvas_blit_alpha(canvas_ptr, x - sprite->origin_x, y - sprite->origin_y, frame);
        return;
    }

    int64_t origin_x = 0;
    int64_t origin_y = 0;
    int8_t origin_centered = 0;
    void *transformed = sprite_prepare_pixels(sprite,
                                              scale_x,
                                              scale_y,
                                              rotation,
                                              tint_color,
                                              alpha,
                                              &origin_x,
                                              &origin_y,
                                              &origin_centered);
    if (!transformed)
        return;

    int64_t blit_x = x - origin_x;
    int64_t blit_y = y - origin_y;
    if (origin_centered) {
        blit_x = x - rt_pixels_width(transformed) / 2;
        blit_y = y - rt_pixels_height(transformed) / 2;
    }

    rt_canvas_blit_alpha(canvas_ptr, blit_x, blit_y, transformed);
}

void rt_sprite_draw(void *sprite_ptr, void *canvas_ptr) {
    if (!sprite_ptr)
        return;
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    rt_sprite_draw_transformed(sprite_ptr,
                               canvas_ptr,
                               sprite->x,
                               sprite->y,
                               sprite->scale_x,
                               sprite->scale_y,
                               sprite->rotation,
                               0,
                               255);
}

void rt_sprite_set_origin(void *sprite_ptr, int64_t x, int64_t y) {
    if (!sprite_ptr) {
        rt_trap("Sprite.SetOrigin: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    sprite->origin_x = x;
    sprite->origin_y = y;
}

void rt_sprite_add_frame(void *sprite_ptr, void *pixels) {
    if (!sprite_ptr || !pixels) {
        rt_trap("Sprite.AddFrame: null argument");
        return;
    }

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count >= MAX_SPRITE_FRAMES)
        return;

    void *cloned = rt_pixels_clone(pixels);
    if (cloned) {
        sprite->frames[sprite->frame_count] = cloned;
        sprite->frame_count++;
        rt_heap_retain(cloned);
    }
}

void rt_sprite_set_frame_delay(void *sprite_ptr, int64_t ms) {
    if (!sprite_ptr) {
        rt_trap("Sprite.SetFrameDelay: null sprite");
        return;
    }
    if (ms < 1)
        ms = 1;
    ((rt_sprite_impl *)sprite_ptr)->frame_delay_ms = ms;
}

void rt_sprite_update(void *sprite_ptr) {
    if (!sprite_ptr)
        return;

    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    if (sprite->frame_count <= 1)
        return;

    int64_t now = rt_timer_ms();
    if (sprite->last_frame_time == 0)
        sprite->last_frame_time = now;

    if (now - sprite->last_frame_time >= sprite->frame_delay_ms) {
        sprite->current_frame = (sprite->current_frame + 1) % sprite->frame_count;
        sprite->last_frame_time = now;
    }
}

int8_t rt_sprite_overlaps(void *sprite_ptr, void *other_ptr) {
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

int8_t rt_sprite_contains(void *sprite_ptr, int64_t px, int64_t py) {
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

void rt_sprite_move(void *sprite_ptr, int64_t dx, int64_t dy) {
    if (!sprite_ptr) {
        rt_trap("Sprite.Move: null sprite");
        return;
    }
    rt_sprite_impl *sprite = (rt_sprite_impl *)sprite_ptr;
    sprite->x += dx;
    sprite->y += dy;
}

//=============================================================================
// Sprite Animator — Named Animation Clip State Machine
//=============================================================================

rt_sprite_animator_t *rt_sprite_animator_new(void) {
    rt_sprite_animator_t *anim =
        (rt_sprite_animator_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sprite_animator_t));
    if (!anim)
        return NULL;
    memset(anim, 0, sizeof(rt_sprite_animator_t));
    anim->current_clip = -1;
    anim->playing = 0;
    return anim;
}

void rt_sprite_animator_destroy(rt_sprite_animator_t *animator) {
    if (!animator)
        return;
    if (rt_obj_release_check0(animator))
        rt_obj_free(animator);
}

int rt_sprite_animator_add_clip(rt_sprite_animator_t *animator,
                                const char *name,
                                int64_t start_frame,
                                int64_t frame_count,
                                int64_t frame_delay_ms,
                                int loop) {
    if (!animator || !name)
        return 0;
    if (animator->clip_count >= RT_ANIM_MAX_CLIPS)
        return 0;

    rt_anim_clip_t *clip = &animator->clips[animator->clip_count++];
    /* Safe string copy: name field is char[64], guarantee NUL termination */
    int i = 0;
    while (name[i] && i < 63) {
        clip->name[i] = name[i];
        i++;
    }
    clip->name[i] = '\0';
    clip->start_frame = start_frame;
    clip->frame_count = frame_count > 0 ? frame_count : 1;
    clip->frame_delay_ms = frame_delay_ms > 0 ? frame_delay_ms : 100;
    clip->loop = loop;
    return 1;
}

int8_t rt_sprite_animator_play(rt_sprite_animator_t *animator, const char *name) {
    if (!animator || !name)
        return 0;

    for (int i = 0; i < animator->clip_count; i++) {
        /* Simple string comparison (no strncmp dependency on all platforms) */
        const char *a = animator->clips[i].name;
        const char *b = name;
        int match = 1;
        while (*a || *b) {
            if (*a != *b) {
                match = 0;
                break;
            }
            a++;
            b++;
        }
        if (match) {
            /* Start the clip only if it differs from the currently playing one */
            if (animator->current_clip != i || !animator->playing) {
                animator->current_clip = i;
                animator->clip_frame = 0;
                animator->last_update_ms = 0; /* reset on next update */
                animator->playing = 1;
            }
            return 1;
        }
    }
    return 0; /* clip not found */
}

void rt_sprite_animator_stop(rt_sprite_animator_t *animator) {
    if (!animator)
        return;
    animator->playing = 0;
}

void rt_sprite_animator_update(rt_sprite_animator_t *animator, void *sprite_ptr) {
    if (!animator || !animator->playing || animator->current_clip < 0 || !sprite_ptr)
        return;

    rt_anim_clip_t *clip = &animator->clips[animator->current_clip];
    int64_t now = rt_timer_ms();

    if (animator->last_update_ms == 0)
        animator->last_update_ms = now;

    int64_t elapsed = now - animator->last_update_ms;
    if (elapsed >= clip->frame_delay_ms) {
        /* Advance one frame (may be multiple if behind) */
        int64_t steps = elapsed / clip->frame_delay_ms;
        animator->clip_frame += steps;
        animator->last_update_ms += steps * clip->frame_delay_ms;

        if (animator->clip_frame >= clip->frame_count) {
            if (clip->loop) {
                animator->clip_frame = animator->clip_frame % clip->frame_count;
            } else {
                /* Play-once: hold on last frame */
                animator->clip_frame = clip->frame_count - 1;
                animator->playing = 0;
            }
        }
    }

    /* Update the sprite's current frame */
    int64_t abs_frame = clip->start_frame + animator->clip_frame;
    rt_sprite_set_frame(sprite_ptr, abs_frame);
}

int8_t rt_sprite_animator_is_playing(rt_sprite_animator_t *animator) {
    return (animator && animator->playing) ? 1 : 0;
}

const char *rt_sprite_animator_get_current(rt_sprite_animator_t *animator) {
    if (!animator || !animator->playing || animator->current_clip < 0)
        return NULL;
    return animator->clips[animator->current_clip].name;
}

int8_t rt_sprite_animator_add_clip_str(void *animator,
                                       rt_string name,
                                       int64_t start_frame,
                                       int64_t frame_count,
                                       int64_t frame_delay_ms,
                                       int64_t loop) {
    const char *clipName = name ? rt_string_cstr(name) : NULL;
    int ok = rt_sprite_animator_add_clip((rt_sprite_animator_t *)animator,
                                         clipName,
                                         start_frame,
                                         frame_count,
                                         frame_delay_ms,
                                         loop != 0);
    return ok ? 1 : 0;
}

int8_t rt_sprite_animator_play_str(void *animator, rt_string name) {
    const char *clipName = name ? rt_string_cstr(name) : NULL;
    int8_t ok = rt_sprite_animator_play((rt_sprite_animator_t *)animator, clipName);
    return ok;
}

rt_string rt_sprite_animator_get_current_str(void *animator) {
    const char *name = rt_sprite_animator_get_current((rt_sprite_animator_t *)animator);
    if (!name)
        return rt_str_empty();
    return rt_string_from_bytes(name, strlen(name));
}
