#include "rt_graphics2d.h"

#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define RT2D_MAX_DIM 1000000LL
#define RT2D_MAX_ELEMENTS 268435456LL
#define RT2D_INITIAL_CAP 16

typedef struct {
    void *pixels;
} rt_rendertarget2d_impl;

typedef struct {
    void *pixels;
    int64_t filter;
    int64_t wrap;
} rt_texture2d_impl;

typedef struct {
    void *source;
    int32_t source_kind;
    int64_t x;
    int64_t y;
    int64_t sx;
    int64_t sy;
    int64_t width;
    int64_t height;
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_renderer2d_cmd;

typedef struct {
    rt_renderer2d_cmd *cmds;
    int64_t count;
    int64_t capacity;
    int64_t active;
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_renderer2d_impl;

typedef struct {
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_material2d_impl;

typedef struct {
    int64_t effect;
    int64_t amount;
    int64_t color;
} rt_shader2d_impl;

typedef rt_shader2d_impl rt_postprocess2d_impl;

typedef struct {
    int64_t virtual_width;
    int64_t virtual_height;
    int64_t screen_width;
    int64_t screen_height;
    int64_t integer_scaling;
    int64_t scale;
    int64_t offset_x;
    int64_t offset_y;
} rt_viewport2d_impl;

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

typedef struct {
    int64_t x;
    int64_t y;
    int32_t move;
} rt_path2d_point;

typedef struct {
    rt_path2d_point *points;
    int64_t count;
    int64_t capacity;
} rt_path2d_impl;

typedef struct {
    int64_t stroke;
    int64_t fill;
} rt_shaperenderer2d_impl;

typedef struct {
    void *font;
    int64_t scale;
    int64_t color;
} rt_textrenderer2d_impl;

typedef struct {
    void *bitmap_font;
    int64_t spread;
} rt_sdffont_impl;

typedef struct {
    void *pixels;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
} rt_nineslice2d_impl;

typedef struct {
    int32_t type;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;
    int64_t value;
    int64_t color;
} rt_debugdraw2d_cmd;

typedef struct {
    rt_debugdraw2d_cmd *cmds;
    int64_t count;
    int64_t capacity;
} rt_debugdraw2d_impl;

static int64_t clamp_i64(int64_t value, int64_t min, int64_t max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static int64_t clamp_u8_i64(int64_t value) {
    return clamp_i64(value, 0, 255);
}

static int64_t normalized_dim(int64_t value) {
    return clamp_i64(value, 1, RT2D_MAX_DIM);
}

static int32_t checked_count(int64_t width, int64_t height, int64_t elem_size, int64_t *out_count) {
    if (width <= 0 || height <= 0 || elem_size <= 0)
        return 0;
    if (width > RT2D_MAX_DIM || height > RT2D_MAX_DIM)
        return 0;
    if (width > INT64_MAX / height)
        return 0;
    int64_t count = width * height;
    if (count > RT2D_MAX_ELEMENTS)
        return 0;
    if (count > INT64_MAX / elem_size)
        return 0;
    if (out_count)
        *out_count = count;
    return 1;
}

static int64_t initial_capacity(int64_t requested) {
    if (requested <= 0)
        return RT2D_INITIAL_CAP;
    if (requested > 1048576)
        return 1048576;
    return requested;
}

static void retain_ref(void *obj) {
    if (obj && rt_heap_is_payload(obj))
        rt_obj_retain_maybe(obj);
}

static void release_ref_slot(void **slot) {
    if (!slot || !*slot)
        return;
    void *obj = *slot;
    *slot = NULL;
    if (rt_heap_is_payload(obj) && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int64_t draw_rgb(int64_t color) {
    uint64_t c = (uint64_t)color;
    if (c <= 0x00FFFFFFu)
        return (int64_t)c;
    return (int64_t)((c >> 8) & 0x00FFFFFFu);
}

static uint32_t apply_tint_alpha(uint32_t rgba, int64_t tint, int64_t alpha) {
    int64_t r = (rgba >> 24) & 255;
    int64_t g = (rgba >> 16) & 255;
    int64_t b = (rgba >> 8) & 255;
    int64_t a = rgba & 255;
    if (tint >= 0) {
        int64_t tr = (tint >> 16) & 255;
        int64_t tg = (tint >> 8) & 255;
        int64_t tb = tint & 255;
        r = (r * tr + 127) / 255;
        g = (g * tg + 127) / 255;
        b = (b * tb + 127) / 255;
    }
    a = (a * clamp_u8_i64(alpha) + 127) / 255;
    return (uint32_t)((r << 24) | (g << 16) | (b << 8) | a);
}

static uint32_t blend_pixel(uint32_t dst, uint32_t src, int64_t blend_mode) {
    int64_t sr = (src >> 24) & 255;
    int64_t sg = (src >> 16) & 255;
    int64_t sb = (src >> 8) & 255;
    int64_t sa = src & 255;
    int64_t dr = (dst >> 24) & 255;
    int64_t dg = (dst >> 16) & 255;
    int64_t db = (dst >> 8) & 255;
    int64_t da = dst & 255;

    if (blend_mode == RT_GRAPHICS2D_BLEND_OPAQUE || sa >= 255)
        return src;

    if (blend_mode == RT_GRAPHICS2D_BLEND_ADD) {
        int64_t r = clamp_u8_i64(dr + (sr * sa + 127) / 255);
        int64_t g = clamp_u8_i64(dg + (sg * sa + 127) / 255);
        int64_t b = clamp_u8_i64(db + (sb * sa + 127) / 255);
        int64_t a = clamp_u8_i64(da + sa - (da * sa + 127) / 255);
        return (uint32_t)((r << 24) | (g << 16) | (b << 8) | a);
    }

    int64_t inv = 255 - sa;
    int64_t r = (sr * sa + dr * inv + 127) / 255;
    int64_t g = (sg * sa + dg * inv + 127) / 255;
    int64_t b = (sb * sa + db * inv + 127) / 255;
    int64_t a = sa + (da * inv + 127) / 255;
    return (uint32_t)((r << 24) | (g << 16) | (b << 8) | clamp_u8_i64(a));
}

static void blit_pixels(void *dst,
                        int64_t dx,
                        int64_t dy,
                        void *src,
                        int64_t sx,
                        int64_t sy,
                        int64_t width,
                        int64_t height,
                        int64_t tint,
                        int64_t alpha,
                        int64_t blend_mode) {
    if (!dst || !src || width <= 0 || height <= 0)
        return;

    int64_t dst_width = rt_pixels_width(dst);
    int64_t dst_height = rt_pixels_height(dst);
    int64_t src_width = rt_pixels_width(src);
    int64_t src_height = rt_pixels_height(src);

    if (sx < 0) {
        width += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0) {
        height += sy;
        dy -= sy;
        sy = 0;
    }
    if (dx < 0) {
        width += dx;
        sx -= dx;
        dx = 0;
    }
    if (dy < 0) {
        height += dy;
        sy -= dy;
        dy = 0;
    }
    if (sx + width > src_width)
        width = src_width - sx;
    if (sy + height > src_height)
        height = src_height - sy;
    if (dx + width > dst_width)
        width = dst_width - dx;
    if (dy + height > dst_height)
        height = dst_height - dy;
    if (width <= 0 || height <= 0)
        return;

    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t source = (uint32_t)rt_pixels_get(src, sx + x, sy + y);
            source = apply_tint_alpha(source, tint, alpha);
            if ((source & 255u) == 0)
                continue;
            uint32_t dest = (uint32_t)rt_pixels_get(dst, dx + x, dy + y);
            rt_pixels_set(dst, dx + x, dy + y, (int64_t)blend_pixel(dest, source, blend_mode));
        }
    }
}

static void apply_tint_alpha_in_place(void *pixels, int64_t tint, int64_t alpha) {
    if (!pixels)
        return;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t color = (uint32_t)rt_pixels_get(pixels, x, y);
            rt_pixels_set(pixels, x, y, (int64_t)apply_tint_alpha(color, tint, alpha));
        }
    }
}

static void *copy_region_pixels(
    void *pixels, int64_t sx, int64_t sy, int64_t width, int64_t height) {
    if (!pixels || width <= 0 || height <= 0)
        return NULL;
    void *out = rt_pixels_new(width, height);
    if (!out)
        return NULL;
    rt_pixels_copy(out, 0, 0, pixels, sx, sy, width, height);
    return out;
}

static void *processed_pixels_or_null(void *pixels, int64_t tint, int64_t alpha) {
    if (!pixels || (tint < 0 && clamp_u8_i64(alpha) == 255))
        return NULL;
    void *out = rt_pixels_clone(pixels);
    if (out)
        apply_tint_alpha_in_place(out, tint, alpha);
    return out;
}

static void rendertarget2d_finalize(void *obj) {
    rt_rendertarget2d_impl *target = (rt_rendertarget2d_impl *)obj;
    release_ref_slot(&target->pixels);
}

void *rt_rendertarget2d_new(int64_t width, int64_t height) {
    if (!checked_count(width, height, 4, NULL)) {
        rt_trap("RenderTarget2D.New: invalid dimensions");
        return NULL;
    }
    rt_rendertarget2d_impl *target =
        (rt_rendertarget2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rendertarget2d_impl));
    if (!target)
        return NULL;
    target->pixels = rt_pixels_new(width, height);
    if (!target->pixels) {
        if (rt_obj_release_check0(target))
            rt_obj_free(target);
        return NULL;
    }
    rt_obj_set_finalizer(target, rendertarget2d_finalize);
    return target;
}

int64_t rt_rendertarget2d_width(void *target) {
    return target ? rt_pixels_width(((rt_rendertarget2d_impl *)target)->pixels) : 0;
}

int64_t rt_rendertarget2d_height(void *target) {
    return target ? rt_pixels_height(((rt_rendertarget2d_impl *)target)->pixels) : 0;
}

void *rt_rendertarget2d_get_pixels(void *target) {
    if (!target)
        return NULL;
    return ((rt_rendertarget2d_impl *)target)->pixels;
}

void rt_rendertarget2d_clear(void *target, int64_t rgba) {
    if (!target)
        return;
    rt_pixels_fill(((rt_rendertarget2d_impl *)target)->pixels, rgba);
}

void rt_rendertarget2d_resize(void *target, int64_t width, int64_t height) {
    if (!target)
        return;
    if (!checked_count(width, height, 4, NULL)) {
        rt_trap("RenderTarget2D.Resize: invalid dimensions");
        return;
    }
    void *pixels = rt_pixels_new(width, height);
    if (!pixels)
        return;
    rt_rendertarget2d_impl *impl = (rt_rendertarget2d_impl *)target;
    release_ref_slot(&impl->pixels);
    impl->pixels = pixels;
}

void rt_rendertarget2d_draw_pixels(void *target, int64_t x, int64_t y, void *pixels) {
    if (!target || !pixels)
        return;
    blit_pixels(((rt_rendertarget2d_impl *)target)->pixels,
                x,
                y,
                pixels,
                0,
                0,
                rt_pixels_width(pixels),
                rt_pixels_height(pixels),
                -1,
                255,
                RT_GRAPHICS2D_BLEND_ALPHA);
}

void rt_rendertarget2d_draw_region(void *target,
                                   int64_t x,
                                   int64_t y,
                                   void *pixels,
                                   int64_t sx,
                                   int64_t sy,
                                   int64_t width,
                                   int64_t height) {
    if (!target || !pixels)
        return;
    blit_pixels(((rt_rendertarget2d_impl *)target)->pixels,
                x,
                y,
                pixels,
                sx,
                sy,
                width,
                height,
                -1,
                255,
                RT_GRAPHICS2D_BLEND_ALPHA);
}

static void texture2d_finalize(void *obj) {
    rt_texture2d_impl *texture = (rt_texture2d_impl *)obj;
    release_ref_slot(&texture->pixels);
}

void *rt_texture2d_new(void *pixels) {
    if (!pixels)
        return NULL;
    rt_texture2d_impl *texture =
        (rt_texture2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_texture2d_impl));
    if (!texture)
        return NULL;
    texture->pixels = pixels;
    texture->filter = RT_GRAPHICS2D_FILTER_NEAREST;
    texture->wrap = RT_GRAPHICS2D_WRAP_CLAMP;
    retain_ref(pixels);
    rt_obj_set_finalizer(texture, texture2d_finalize);
    return texture;
}

void *rt_texture2d_from_file(rt_string path) {
    void *pixels = rt_pixels_load(path);
    if (!pixels)
        return NULL;
    void *texture = rt_texture2d_new(pixels);
    release_ref_slot(&pixels);
    return texture;
}

int64_t rt_texture2d_width(void *texture) {
    if (!texture)
        return 0;
    return rt_pixels_width(((rt_texture2d_impl *)texture)->pixels);
}

int64_t rt_texture2d_height(void *texture) {
    if (!texture)
        return 0;
    return rt_pixels_height(((rt_texture2d_impl *)texture)->pixels);
}

void *rt_texture2d_get_pixels(void *texture) {
    return texture ? ((rt_texture2d_impl *)texture)->pixels : NULL;
}

void *rt_texture2d_clone_pixels(void *texture) {
    void *pixels = rt_texture2d_get_pixels(texture);
    return pixels ? rt_pixels_clone(pixels) : NULL;
}

void rt_texture2d_set_filter(void *texture, int64_t filter) {
    if (!texture)
        return;
    ((rt_texture2d_impl *)texture)->filter = (filter == RT_GRAPHICS2D_FILTER_LINEAR)
                                                 ? RT_GRAPHICS2D_FILTER_LINEAR
                                                 : RT_GRAPHICS2D_FILTER_NEAREST;
}

int64_t rt_texture2d_get_filter(void *texture) {
    return texture ? ((rt_texture2d_impl *)texture)->filter : RT_GRAPHICS2D_FILTER_NEAREST;
}

void rt_texture2d_set_wrap(void *texture, int64_t wrap) {
    if (!texture)
        return;
    ((rt_texture2d_impl *)texture)->wrap =
        (wrap == RT_GRAPHICS2D_WRAP_REPEAT) ? RT_GRAPHICS2D_WRAP_REPEAT : RT_GRAPHICS2D_WRAP_CLAMP;
}

int64_t rt_texture2d_get_wrap(void *texture) {
    return texture ? ((rt_texture2d_impl *)texture)->wrap : RT_GRAPHICS2D_WRAP_CLAMP;
}

static void renderer2d_release_cmd(rt_renderer2d_cmd *cmd) {
    if (!cmd)
        return;
    release_ref_slot(&cmd->source);
    memset(cmd, 0, sizeof(*cmd));
}

static void renderer2d_finalize(void *obj) {
    rt_renderer2d_impl *renderer = (rt_renderer2d_impl *)obj;
    if (renderer->cmds) {
        for (int64_t i = 0; i < renderer->count; i++)
            renderer2d_release_cmd(&renderer->cmds[i]);
        free(renderer->cmds);
    }
}

static int32_t renderer2d_reserve(rt_renderer2d_impl *renderer, int64_t needed) {
    if (!renderer || needed <= renderer->capacity)
        return 1;
    int64_t cap = renderer->capacity > 0 ? renderer->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_renderer2d_cmd))
        return 0;
    rt_renderer2d_cmd *cmds =
        (rt_renderer2d_cmd *)realloc(renderer->cmds, (size_t)cap * sizeof(rt_renderer2d_cmd));
    if (!cmds)
        return 0;
    memset(cmds + renderer->capacity, 0, (size_t)(cap - renderer->capacity) * sizeof(*cmds));
    renderer->cmds = cmds;
    renderer->capacity = cap;
    return 1;
}

void *rt_renderer2d_new(int64_t capacity) {
    rt_renderer2d_impl *renderer =
        (rt_renderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_renderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->capacity = initial_capacity(capacity);
    renderer->cmds =
        (rt_renderer2d_cmd *)calloc((size_t)renderer->capacity, sizeof(rt_renderer2d_cmd));
    if (!renderer->cmds) {
        if (rt_obj_release_check0(renderer))
            rt_obj_free(renderer);
        return NULL;
    }
    renderer->tint = -1;
    renderer->alpha = 255;
    renderer->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    rt_obj_set_finalizer(renderer, renderer2d_finalize);
    return renderer;
}

void rt_renderer2d_clear(void *renderer) {
    if (!renderer)
        return;
    rt_renderer2d_impl *impl = (rt_renderer2d_impl *)renderer;
    for (int64_t i = 0; i < impl->count; i++)
        renderer2d_release_cmd(&impl->cmds[i]);
    impl->count = 0;
}

void rt_renderer2d_begin(void *renderer) {
    if (!renderer)
        return;
    rt_renderer2d_clear(renderer);
    ((rt_renderer2d_impl *)renderer)->active = 1;
}

int64_t rt_renderer2d_count(void *renderer) {
    return renderer ? ((rt_renderer2d_impl *)renderer)->count : 0;
}

void rt_renderer2d_set_tint(void *renderer, int64_t rgb) {
    if (renderer)
        ((rt_renderer2d_impl *)renderer)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

void rt_renderer2d_set_alpha(void *renderer, int64_t alpha) {
    if (renderer)
        ((rt_renderer2d_impl *)renderer)->alpha = clamp_u8_i64(alpha);
}

void rt_renderer2d_set_blend_mode(void *renderer, int64_t blend_mode) {
    if (renderer)
        ((rt_renderer2d_impl *)renderer)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

static void renderer2d_queue(rt_renderer2d_impl *renderer,
                             int32_t source_kind,
                             void *source,
                             int64_t x,
                             int64_t y,
                             int64_t sx,
                             int64_t sy,
                             int64_t width,
                             int64_t height) {
    if (!renderer || !renderer->active || !source)
        return;
    if (!renderer2d_reserve(renderer, renderer->count + 1)) {
        rt_trap("Renderer2D: command capacity overflow");
        return;
    }
    rt_renderer2d_cmd *cmd = &renderer->cmds[renderer->count++];
    cmd->source = source;
    cmd->source_kind = source_kind;
    cmd->x = x;
    cmd->y = y;
    cmd->sx = sx;
    cmd->sy = sy;
    cmd->width = width;
    cmd->height = height;
    cmd->tint = renderer->tint;
    cmd->alpha = renderer->alpha;
    cmd->blend_mode = renderer->blend_mode;
    retain_ref(source);
}

void rt_renderer2d_draw_pixels(void *renderer, void *pixels, int64_t x, int64_t y) {
    if (!pixels)
        return;
    renderer2d_queue((rt_renderer2d_impl *)renderer,
                     0,
                     pixels,
                     x,
                     y,
                     0,
                     0,
                     rt_pixels_width(pixels),
                     rt_pixels_height(pixels));
}

void rt_renderer2d_draw_texture(void *renderer, void *texture, int64_t x, int64_t y) {
    if (!texture)
        return;
    renderer2d_queue((rt_renderer2d_impl *)renderer,
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture));
}

void rt_renderer2d_draw_region(void *renderer,
                               void *pixels,
                               int64_t x,
                               int64_t y,
                               int64_t sx,
                               int64_t sy,
                               int64_t width,
                               int64_t height) {
    renderer2d_queue((rt_renderer2d_impl *)renderer, 0, pixels, x, y, sx, sy, width, height);
}

static void renderer2d_flush_cmd_to_target(rt_renderer2d_cmd *cmd, void *target_pixels) {
    void *pixels = cmd->source_kind == 1 ? rt_texture2d_get_pixels(cmd->source) : cmd->source;
    if (!pixels)
        return;
    blit_pixels(target_pixels,
                cmd->x,
                cmd->y,
                pixels,
                cmd->sx,
                cmd->sy,
                cmd->width,
                cmd->height,
                cmd->tint,
                cmd->alpha,
                cmd->blend_mode);
}

void rt_renderer2d_flush_to_target(void *renderer, void *target) {
    if (!renderer || !target)
        return;
    void *target_pixels = rt_rendertarget2d_get_pixels(target);
    if (!target_pixels)
        return;
    rt_renderer2d_impl *impl = (rt_renderer2d_impl *)renderer;
    for (int64_t i = 0; i < impl->count; i++)
        renderer2d_flush_cmd_to_target(&impl->cmds[i], target_pixels);
}

void rt_renderer2d_end(void *renderer, void *canvas) {
    if (!renderer)
        return;
    rt_renderer2d_impl *impl = (rt_renderer2d_impl *)renderer;
    if (canvas) {
        for (int64_t i = 0; i < impl->count; i++) {
            rt_renderer2d_cmd *cmd = &impl->cmds[i];
            void *pixels =
                cmd->source_kind == 1 ? rt_texture2d_get_pixels(cmd->source) : cmd->source;
            if (!pixels)
                continue;
            void *region = NULL;
            void *draw_pixels = pixels;
            if (cmd->sx != 0 || cmd->sy != 0 || cmd->width != rt_pixels_width(pixels) ||
                cmd->height != rt_pixels_height(pixels)) {
                region = copy_region_pixels(pixels, cmd->sx, cmd->sy, cmd->width, cmd->height);
                draw_pixels = region;
            }
            void *processed = processed_pixels_or_null(draw_pixels, cmd->tint, cmd->alpha);
            if (processed)
                draw_pixels = processed;
            if (draw_pixels)
                rt_canvas_blit_alpha(canvas, cmd->x, cmd->y, draw_pixels);
            release_ref_slot(&processed);
            release_ref_slot(&region);
        }
    }
    impl->active = 0;
}

void *rt_material2d_new(void) {
    rt_material2d_impl *material =
        (rt_material2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_material2d_impl));
    if (!material)
        return NULL;
    material->tint = -1;
    material->alpha = 255;
    material->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    return material;
}

void rt_material2d_set_tint(void *material, int64_t rgb) {
    if (material)
        ((rt_material2d_impl *)material)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

int64_t rt_material2d_get_tint(void *material) {
    return material ? ((rt_material2d_impl *)material)->tint : -1;
}

void rt_material2d_set_alpha(void *material, int64_t alpha) {
    if (material)
        ((rt_material2d_impl *)material)->alpha = clamp_u8_i64(alpha);
}

int64_t rt_material2d_get_alpha(void *material) {
    return material ? ((rt_material2d_impl *)material)->alpha : 255;
}

void rt_material2d_set_blend_mode(void *material, int64_t blend_mode) {
    if (material)
        ((rt_material2d_impl *)material)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

int64_t rt_material2d_get_blend_mode(void *material) {
    return material ? ((rt_material2d_impl *)material)->blend_mode : RT_GRAPHICS2D_BLEND_ALPHA;
}

void *rt_material2d_apply(void *material, void *pixels) {
    if (!material || !pixels)
        return NULL;
    rt_material2d_impl *impl = (rt_material2d_impl *)material;
    void *out = rt_pixels_clone(pixels);
    if (out)
        apply_tint_alpha_in_place(out, impl->tint, impl->alpha);
    return out;
}

void *rt_shader2d_new(int64_t effect) {
    rt_shader2d_impl *shader =
        (rt_shader2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_shader2d_impl));
    if (!shader)
        return NULL;
    shader->effect = clamp_i64(effect, 0, 4);
    shader->amount = 1;
    shader->color = 0x00FFFFFF;
    return shader;
}

void rt_shader2d_set_effect(void *shader, int64_t effect) {
    if (shader)
        ((rt_shader2d_impl *)shader)->effect = clamp_i64(effect, 0, 4);
}

int64_t rt_shader2d_get_effect(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->effect : RT_GRAPHICS2D_EFFECT_NONE;
}

void rt_shader2d_set_amount(void *shader, int64_t amount) {
    if (shader)
        ((rt_shader2d_impl *)shader)->amount = clamp_i64(amount, 0, 10);
}

int64_t rt_shader2d_get_amount(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->amount : 0;
}

void rt_shader2d_set_color(void *shader, int64_t rgb) {
    if (shader)
        ((rt_shader2d_impl *)shader)->color = rgb & 0x00FFFFFF;
}

int64_t rt_shader2d_get_color(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->color : 0x00FFFFFF;
}

static void *apply_effect(int64_t effect, int64_t amount, int64_t color, void *pixels) {
    if (!pixels)
        return NULL;
    switch (effect) {
        case RT_GRAPHICS2D_EFFECT_INVERT:
            return rt_pixels_invert(pixels);
        case RT_GRAPHICS2D_EFFECT_GRAYSCALE:
            return rt_pixels_grayscale(pixels);
        case RT_GRAPHICS2D_EFFECT_TINT:
            return rt_pixels_tint(pixels, color);
        case RT_GRAPHICS2D_EFFECT_BLUR:
            return rt_pixels_blur(pixels, amount);
        case RT_GRAPHICS2D_EFFECT_NONE:
        default:
            return rt_pixels_clone(pixels);
    }
}

void *rt_shader2d_apply(void *shader, void *pixels) {
    if (!shader)
        return pixels ? rt_pixels_clone(pixels) : NULL;
    rt_shader2d_impl *impl = (rt_shader2d_impl *)shader;
    return apply_effect(impl->effect, impl->amount, impl->color, pixels);
}

void *rt_postprocess2d_new(void) {
    return rt_shader2d_new(RT_GRAPHICS2D_EFFECT_NONE);
}

void rt_postprocess2d_set_effect(void *postprocess, int64_t effect) {
    rt_shader2d_set_effect(postprocess, effect);
}

void rt_postprocess2d_set_amount(void *postprocess, int64_t amount) {
    rt_shader2d_set_amount(postprocess, amount);
}

void rt_postprocess2d_set_color(void *postprocess, int64_t rgb) {
    rt_shader2d_set_color(postprocess, rgb);
}

void *rt_postprocess2d_apply(void *postprocess, void *pixels) {
    return rt_shader2d_apply(postprocess, pixels);
}

static void viewport2d_recalculate(rt_viewport2d_impl *viewport) {
    if (!viewport)
        return;
    int64_t scale_x = viewport->screen_width * 1000 / viewport->virtual_width;
    int64_t scale_y = viewport->screen_height * 1000 / viewport->virtual_height;
    viewport->scale = scale_x < scale_y ? scale_x : scale_y;
    if (viewport->integer_scaling) {
        int64_t whole = viewport->scale / 1000;
        if (whole < 1)
            whole = 1;
        viewport->scale = whole * 1000;
    }
    int64_t scaled_width = viewport->virtual_width * viewport->scale / 1000;
    int64_t scaled_height = viewport->virtual_height * viewport->scale / 1000;
    viewport->offset_x = (viewport->screen_width - scaled_width) / 2;
    viewport->offset_y = (viewport->screen_height - scaled_height) / 2;
}

void *rt_viewport2d_new(int64_t virtual_width,
                        int64_t virtual_height,
                        int64_t screen_width,
                        int64_t screen_height) {
    rt_viewport2d_impl *viewport =
        (rt_viewport2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viewport2d_impl));
    if (!viewport)
        return NULL;
    viewport->virtual_width = normalized_dim(virtual_width);
    viewport->virtual_height = normalized_dim(virtual_height);
    viewport->screen_width = normalized_dim(screen_width);
    viewport->screen_height = normalized_dim(screen_height);
    viewport2d_recalculate(viewport);
    return viewport;
}

void rt_viewport2d_set_virtual_size(void *viewport, int64_t width, int64_t height) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->virtual_width = normalized_dim(width);
    impl->virtual_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

void rt_viewport2d_set_screen_size(void *viewport, int64_t width, int64_t height) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->screen_width = normalized_dim(width);
    impl->screen_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

void rt_viewport2d_set_integer_scaling(void *viewport, int64_t enabled) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->integer_scaling = enabled != 0;
    viewport2d_recalculate(impl);
}

int64_t rt_viewport2d_get_scale(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->scale : 1000;
}

int64_t rt_viewport2d_get_offset_x(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->offset_x : 0;
}

int64_t rt_viewport2d_get_offset_y(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->offset_y : 0;
}

int64_t rt_viewport2d_world_to_screen_x(void *viewport, int64_t x) {
    if (!viewport)
        return x;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    return impl->offset_x + x * impl->scale / 1000;
}

int64_t rt_viewport2d_world_to_screen_y(void *viewport, int64_t y) {
    if (!viewport)
        return y;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    return impl->offset_y + y * impl->scale / 1000;
}

int64_t rt_viewport2d_screen_to_world_x(void *viewport, int64_t x) {
    if (!viewport)
        return x;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    return (x - impl->offset_x) * 1000 / impl->scale;
}

int64_t rt_viewport2d_screen_to_world_y(void *viewport, int64_t y) {
    if (!viewport)
        return y;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    return (y - impl->offset_y) * 1000 / impl->scale;
}

static void tileset2d_finalize(void *obj) {
    rt_tileset2d_impl *tileset = (rt_tileset2d_impl *)obj;
    release_ref_slot(&tileset->pixels);
}

void *rt_tileset2d_new(void *pixels, int64_t tile_width, int64_t tile_height) {
    if (!pixels || tile_width <= 0 || tile_height <= 0)
        return NULL;
    if (rt_pixels_width(pixels) < tile_width || rt_pixels_height(pixels) < tile_height)
        return NULL;
    rt_tileset2d_impl *tileset =
        (rt_tileset2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tileset2d_impl));
    if (!tileset)
        return NULL;
    tileset->pixels = pixels;
    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    retain_ref(pixels);
    rt_obj_set_finalizer(tileset, tileset2d_finalize);
    return tileset;
}

int64_t rt_tileset2d_columns(void *tileset) {
    if (!tileset)
        return 0;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    return rt_pixels_width(impl->pixels) / impl->tile_width;
}

int64_t rt_tileset2d_rows(void *tileset) {
    if (!tileset)
        return 0;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    return rt_pixels_height(impl->pixels) / impl->tile_height;
}

int64_t rt_tileset2d_tile_count(void *tileset) {
    return rt_tileset2d_columns(tileset) * rt_tileset2d_rows(tileset);
}

void *rt_tileset2d_get_tile_pixels(void *tileset, int64_t tile_index) {
    if (!tileset || tile_index < 0 || tile_index >= rt_tileset2d_tile_count(tileset))
        return NULL;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    int64_t columns = rt_tileset2d_columns(tileset);
    int64_t sx = (tile_index % columns) * impl->tile_width;
    int64_t sy = (tile_index / columns) * impl->tile_height;
    return copy_region_pixels(impl->pixels, sx, sy, impl->tile_width, impl->tile_height);
}

static void tilelayer2d_finalize(void *obj) {
    rt_tilelayer2d_impl *layer = (rt_tilelayer2d_impl *)obj;
    free(layer->tiles);
}

void *rt_tilelayer2d_new(int64_t width, int64_t height) {
    int64_t count = 0;
    if (!checked_count(width, height, (int64_t)sizeof(int64_t), &count)) {
        rt_trap("TileLayer2D.New: invalid dimensions");
        return NULL;
    }
    rt_tilelayer2d_impl *layer =
        (rt_tilelayer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tilelayer2d_impl));
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
    layer->opacity = 255;
    rt_obj_set_finalizer(layer, tilelayer2d_finalize);
    return layer;
}

int64_t rt_tilelayer2d_width(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->width : 0;
}

int64_t rt_tilelayer2d_height(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->height : 0;
}

static int32_t tilelayer2d_in_bounds(rt_tilelayer2d_impl *layer, int64_t x, int64_t y) {
    return layer && x >= 0 && y >= 0 && x < layer->width && y < layer->height;
}

void rt_tilelayer2d_set(void *layer, int64_t x, int64_t y, int64_t tile) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!tilelayer2d_in_bounds(impl, x, y))
        return;
    impl->tiles[y * impl->width + x] = tile;
}

int64_t rt_tilelayer2d_get(void *layer, int64_t x, int64_t y) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!tilelayer2d_in_bounds(impl, x, y))
        return -1;
    return impl->tiles[y * impl->width + x];
}

void rt_tilelayer2d_fill(void *layer, int64_t tile) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!impl)
        return;
    int64_t count = impl->width * impl->height;
    for (int64_t i = 0; i < count; i++)
        impl->tiles[i] = tile;
}

void rt_tilelayer2d_clear(void *layer) {
    rt_tilelayer2d_fill(layer, 0);
}

void rt_tilelayer2d_set_visible(void *layer, int64_t visible) {
    if (layer)
        ((rt_tilelayer2d_impl *)layer)->visible = visible != 0;
}

int64_t rt_tilelayer2d_is_visible(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->visible : 0;
}

void rt_tilelayer2d_set_opacity(void *layer, int64_t opacity) {
    if (layer)
        ((rt_tilelayer2d_impl *)layer)->opacity = clamp_u8_i64(opacity);
}

int64_t rt_tilelayer2d_get_opacity(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->opacity : 0;
}

static void objectlayer2d_finalize(void *obj) {
    rt_objectlayer2d_impl *layer = (rt_objectlayer2d_impl *)obj;
    free(layer->items);
}

void *rt_objectlayer2d_new(int64_t capacity) {
    rt_objectlayer2d_impl *layer =
        (rt_objectlayer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_objectlayer2d_impl));
    if (!layer)
        return NULL;
    layer->capacity = initial_capacity(capacity);
    layer->items = (rt_objectlayer2d_entry *)calloc((size_t)layer->capacity, sizeof(*layer->items));
    if (!layer->items) {
        if (rt_obj_release_check0(layer))
            rt_obj_free(layer);
        return NULL;
    }
    rt_obj_set_finalizer(layer, objectlayer2d_finalize);
    return layer;
}

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

int64_t rt_objectlayer2d_add_rect(
    void *layer, int64_t x, int64_t y, int64_t width, int64_t height, int64_t type) {
    rt_objectlayer2d_impl *impl = (rt_objectlayer2d_impl *)layer;
    if (!impl)
        return -1;
    if (!objectlayer2d_reserve(impl, impl->count + 1)) {
        rt_trap("ObjectLayer2D: capacity overflow");
        return -1;
    }
    int64_t index = impl->count++;
    impl->items[index].x = x;
    impl->items[index].y = y;
    impl->items[index].width = width;
    impl->items[index].height = height;
    impl->items[index].type = type;
    return index;
}

int64_t rt_objectlayer2d_count(void *layer) {
    return layer ? ((rt_objectlayer2d_impl *)layer)->count : 0;
}

void rt_objectlayer2d_clear(void *layer) {
    if (layer)
        ((rt_objectlayer2d_impl *)layer)->count = 0;
}

static rt_objectlayer2d_entry *objectlayer2d_get_entry(void *layer, int64_t index) {
    rt_objectlayer2d_impl *impl = (rt_objectlayer2d_impl *)layer;
    if (!impl || index < 0 || index >= impl->count)
        return NULL;
    return &impl->items[index];
}

int64_t rt_objectlayer2d_get_x(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->x : 0;
}

int64_t rt_objectlayer2d_get_y(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->y : 0;
}

int64_t rt_objectlayer2d_get_width(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->width : 0;
}

int64_t rt_objectlayer2d_get_height(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->height : 0;
}

int64_t rt_objectlayer2d_get_type(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->type : 0;
}

void *rt_autotile2d_new(void) {
    return rt_obj_new_i64(0, (int64_t)sizeof(rt_autotile2d_impl));
}

void rt_autotile2d_set_variant(void *autotile, int64_t mask, int64_t tile) {
    if (!autotile)
        return;
    ((rt_autotile2d_impl *)autotile)->variants[mask & 15] = tile;
}

int64_t rt_autotile2d_resolve(void *autotile, int64_t mask) {
    return autotile ? ((rt_autotile2d_impl *)autotile)->variants[mask & 15] : 0;
}

void rt_autotile2d_apply(void *autotile, void *layer, int64_t x, int64_t y, int64_t mask) {
    if (!autotile || !layer)
        return;
    rt_tilelayer2d_set(layer, x, y, rt_autotile2d_resolve(autotile, mask));
}

static void path2d_finalize(void *obj) {
    rt_path2d_impl *path = (rt_path2d_impl *)obj;
    free(path->points);
}

void *rt_path2d_new(int64_t capacity) {
    rt_path2d_impl *path = (rt_path2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_path2d_impl));
    if (!path)
        return NULL;
    path->capacity = initial_capacity(capacity);
    path->points = (rt_path2d_point *)calloc((size_t)path->capacity, sizeof(*path->points));
    if (!path->points) {
        if (rt_obj_release_check0(path))
            rt_obj_free(path);
        return NULL;
    }
    rt_obj_set_finalizer(path, path2d_finalize);
    return path;
}

static int32_t path2d_reserve(rt_path2d_impl *path, int64_t needed) {
    if (!path || needed <= path->capacity)
        return 1;
    int64_t cap = path->capacity > 0 ? path->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_path2d_point))
        return 0;
    rt_path2d_point *points =
        (rt_path2d_point *)realloc(path->points, (size_t)cap * sizeof(*points));
    if (!points)
        return 0;
    memset(points + path->capacity, 0, (size_t)(cap - path->capacity) * sizeof(*points));
    path->points = points;
    path->capacity = cap;
    return 1;
}

static void path2d_add(void *path, int64_t x, int64_t y, int32_t move) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl)
        return;
    if (!path2d_reserve(impl, impl->count + 1)) {
        rt_trap("Path2D: capacity overflow");
        return;
    }
    rt_path2d_point *point = &impl->points[impl->count++];
    point->x = x;
    point->y = y;
    point->move = move;
}

void rt_path2d_clear(void *path) {
    if (path)
        ((rt_path2d_impl *)path)->count = 0;
}

void rt_path2d_move_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 1);
}

void rt_path2d_line_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 0);
}

int64_t rt_path2d_count(void *path) {
    return path ? ((rt_path2d_impl *)path)->count : 0;
}

int64_t rt_path2d_get_x(void *path, int64_t index) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].x;
}

int64_t rt_path2d_get_y(void *path, int64_t index) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].y;
}

void rt_path2d_draw_to_pixels(void *path, void *pixels, int64_t rgba) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || !pixels || impl->count < 2)
        return;
    int64_t color = draw_rgb(rgba);
    for (int64_t i = 1; i < impl->count; i++) {
        if (impl->points[i].move)
            continue;
        rt_pixels_draw_line(pixels,
                            impl->points[i - 1].x,
                            impl->points[i - 1].y,
                            impl->points[i].x,
                            impl->points[i].y,
                            color);
    }
}

void *rt_shaperenderer2d_new(void) {
    rt_shaperenderer2d_impl *renderer =
        (rt_shaperenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_shaperenderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->stroke = 0x00FFFFFF;
    renderer->fill = -1;
    return renderer;
}

void rt_shaperenderer2d_set_stroke(void *renderer, int64_t rgba) {
    if (renderer)
        ((rt_shaperenderer2d_impl *)renderer)->stroke = rgba;
}

void rt_shaperenderer2d_set_fill(void *renderer, int64_t rgba) {
    if (renderer)
        ((rt_shaperenderer2d_impl *)renderer)->fill = rgba;
}

void rt_shaperenderer2d_line(
    void *renderer, void *pixels, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (!renderer || !pixels)
        return;
    rt_pixels_draw_line(
        pixels, x0, y0, x1, y1, draw_rgb(((rt_shaperenderer2d_impl *)renderer)->stroke));
}

void rt_shaperenderer2d_rect(
    void *renderer, void *pixels, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!renderer || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_box(pixels, x, y, width, height, draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_frame(pixels, x, y, width, height, draw_rgb(impl->stroke));
}

void rt_shaperenderer2d_circle(void *renderer, void *pixels, int64_t x, int64_t y, int64_t radius) {
    if (!renderer || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_disc(pixels, x, y, radius, draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_ring(pixels, x, y, radius, draw_rgb(impl->stroke));
}

void rt_shaperenderer2d_path(void *renderer, void *pixels, void *path) {
    if (!renderer)
        return;
    rt_path2d_draw_to_pixels(path, pixels, ((rt_shaperenderer2d_impl *)renderer)->stroke);
}

static void textrenderer2d_finalize(void *obj) {
    rt_textrenderer2d_impl *renderer = (rt_textrenderer2d_impl *)obj;
    release_ref_slot(&renderer->font);
}

void *rt_textrenderer2d_new(void) {
    rt_textrenderer2d_impl *renderer =
        (rt_textrenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_textrenderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->scale = 1;
    renderer->color = 0x00FFFFFF;
    rt_obj_set_finalizer(renderer, textrenderer2d_finalize);
    return renderer;
}

void rt_textrenderer2d_set_font(void *renderer, void *font) {
    if (!renderer)
        return;
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    retain_ref(font);
    release_ref_slot(&impl->font);
    impl->font = font;
}

void rt_textrenderer2d_set_scale(void *renderer, int64_t scale) {
    if (renderer)
        ((rt_textrenderer2d_impl *)renderer)->scale = clamp_i64(scale, 1, 64);
}

void rt_textrenderer2d_set_color(void *renderer, int64_t rgb) {
    if (renderer)
        ((rt_textrenderer2d_impl *)renderer)->color = rgb & 0x00FFFFFF;
}

int64_t rt_textrenderer2d_measure_width(void *renderer, rt_string text) {
    if (!renderer)
        return rt_canvas_text_width(text);
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t width =
        impl->font ? rt_bitmapfont_text_width(impl->font, text) : rt_canvas_text_width(text);
    return width * impl->scale;
}

int64_t rt_textrenderer2d_measure_height(void *renderer, rt_string text) {
    (void)text;
    if (!renderer)
        return rt_canvas_text_height();
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t height = impl->font ? rt_bitmapfont_text_height(impl->font) : rt_canvas_text_height();
    return height * impl->scale;
}

void rt_textrenderer2d_draw(void *renderer, void *canvas, int64_t x, int64_t y, rt_string text) {
    if (!renderer || !canvas)
        return;
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    if (impl->font)
        rt_canvas_text_font_scaled(canvas, x, y, text, impl->font, impl->scale, impl->color);
    else
        rt_canvas_text_scaled(canvas, x, y, text, impl->scale, impl->color);
}

static void sdffont_finalize(void *obj) {
    rt_sdffont_impl *font = (rt_sdffont_impl *)obj;
    release_ref_slot(&font->bitmap_font);
}

void *rt_sdffont_new(void *bitmap_font, int64_t spread) {
    rt_sdffont_impl *font = (rt_sdffont_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sdffont_impl));
    if (!font)
        return NULL;
    font->bitmap_font = bitmap_font;
    font->spread = clamp_i64(spread, 1, 64);
    retain_ref(bitmap_font);
    rt_obj_set_finalizer(font, sdffont_finalize);
    return font;
}

void *rt_sdffont_get_bitmap_font(void *font) {
    return font ? ((rt_sdffont_impl *)font)->bitmap_font : NULL;
}

int64_t rt_sdffont_get_spread(void *font) {
    return font ? ((rt_sdffont_impl *)font)->spread : 0;
}

static void nineslice2d_finalize(void *obj) {
    rt_nineslice2d_impl *slice = (rt_nineslice2d_impl *)obj;
    release_ref_slot(&slice->pixels);
}

void *rt_nineslice2d_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom) {
    if (!pixels)
        return NULL;
    rt_nineslice2d_impl *slice =
        (rt_nineslice2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_nineslice2d_impl));
    if (!slice)
        return NULL;
    slice->pixels = pixels;
    slice->left = clamp_i64(left, 0, rt_pixels_width(pixels));
    slice->top = clamp_i64(top, 0, rt_pixels_height(pixels));
    slice->right = clamp_i64(right, 0, rt_pixels_width(pixels));
    slice->bottom = clamp_i64(bottom, 0, rt_pixels_height(pixels));
    retain_ref(pixels);
    rt_obj_set_finalizer(slice, nineslice2d_finalize);
    return slice;
}

static void nineslice_copy_scaled(void *target,
                                  int64_t dx,
                                  int64_t dy,
                                  int64_t dw,
                                  int64_t dh,
                                  void *source,
                                  int64_t sx,
                                  int64_t sy,
                                  int64_t sw,
                                  int64_t sh) {
    if (!target || !source || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
    if (dw == sw && dh == sh) {
        blit_pixels(target, dx, dy, source, sx, sy, sw, sh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
        return;
    }
    void *region = copy_region_pixels(source, sx, sy, sw, sh);
    if (!region)
        return;
    void *scaled = rt_pixels_scale(region, dw, dh);
    if (scaled)
        blit_pixels(target, dx, dy, scaled, 0, 0, dw, dh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
    release_ref_slot(&scaled);
    release_ref_slot(&region);
}

void rt_nineslice2d_draw_to_pixels(
    void *slice, void *target, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!slice || !target || width <= 0 || height <= 0)
        return;
    rt_nineslice2d_impl *impl = (rt_nineslice2d_impl *)slice;
    int64_t source_width = rt_pixels_width(impl->pixels);
    int64_t source_height = rt_pixels_height(impl->pixels);
    int64_t sl = clamp_i64(impl->left, 0, source_width);
    int64_t sr = clamp_i64(impl->right, 0, source_width - sl);
    int64_t st = clamp_i64(impl->top, 0, source_height);
    int64_t sb = clamp_i64(impl->bottom, 0, source_height - st);
    int64_t dl = clamp_i64(sl, 0, width);
    int64_t dr = clamp_i64(sr, 0, width - dl);
    int64_t dt = clamp_i64(st, 0, height);
    int64_t db = clamp_i64(sb, 0, height - dt);
    int64_t scw = source_width - sl - sr;
    int64_t sch = source_height - st - sb;
    int64_t dcw = width - dl - dr;
    int64_t dch = height - dt - db;

    nineslice_copy_scaled(target, x, y, dl, dt, impl->pixels, 0, 0, sl, st);
    nineslice_copy_scaled(target, x + dl, y, dcw, dt, impl->pixels, sl, 0, scw, st);
    nineslice_copy_scaled(target, x + dl + dcw, y, dr, dt, impl->pixels, sl + scw, 0, sr, st);

    nineslice_copy_scaled(target, x, y + dt, dl, dch, impl->pixels, 0, st, sl, sch);
    nineslice_copy_scaled(target, x + dl, y + dt, dcw, dch, impl->pixels, sl, st, scw, sch);
    nineslice_copy_scaled(
        target, x + dl + dcw, y + dt, dr, dch, impl->pixels, sl + scw, st, sr, sch);

    nineslice_copy_scaled(target, x, y + dt + dch, dl, db, impl->pixels, 0, st + sch, sl, sb);
    nineslice_copy_scaled(
        target, x + dl, y + dt + dch, dcw, db, impl->pixels, sl, st + sch, scw, sb);
    nineslice_copy_scaled(
        target, x + dl + dcw, y + dt + dch, dr, db, impl->pixels, sl + scw, st + sch, sr, sb);
}

static void debugdraw2d_finalize(void *obj) {
    rt_debugdraw2d_impl *debug_draw = (rt_debugdraw2d_impl *)obj;
    free(debug_draw->cmds);
}

void *rt_debugdraw2d_new(int64_t capacity) {
    rt_debugdraw2d_impl *debug_draw =
        (rt_debugdraw2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_debugdraw2d_impl));
    if (!debug_draw)
        return NULL;
    debug_draw->capacity = initial_capacity(capacity);
    debug_draw->cmds =
        (rt_debugdraw2d_cmd *)calloc((size_t)debug_draw->capacity, sizeof(*debug_draw->cmds));
    if (!debug_draw->cmds) {
        if (rt_obj_release_check0(debug_draw))
            rt_obj_free(debug_draw);
        return NULL;
    }
    rt_obj_set_finalizer(debug_draw, debugdraw2d_finalize);
    return debug_draw;
}

void rt_debugdraw2d_clear(void *debug_draw) {
    if (debug_draw)
        ((rt_debugdraw2d_impl *)debug_draw)->count = 0;
}

int64_t rt_debugdraw2d_count(void *debug_draw) {
    return debug_draw ? ((rt_debugdraw2d_impl *)debug_draw)->count : 0;
}

static int32_t debugdraw2d_reserve(rt_debugdraw2d_impl *debug_draw, int64_t needed) {
    if (!debug_draw || needed <= debug_draw->capacity)
        return 1;
    int64_t cap = debug_draw->capacity > 0 ? debug_draw->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_debugdraw2d_cmd))
        return 0;
    rt_debugdraw2d_cmd *cmds =
        (rt_debugdraw2d_cmd *)realloc(debug_draw->cmds, (size_t)cap * sizeof(*cmds));
    if (!cmds)
        return 0;
    memset(cmds + debug_draw->capacity, 0, (size_t)(cap - debug_draw->capacity) * sizeof(*cmds));
    debug_draw->cmds = cmds;
    debug_draw->capacity = cap;
    return 1;
}

static void debugdraw2d_add(rt_debugdraw2d_impl *debug_draw,
                            int32_t type,
                            int64_t x0,
                            int64_t y0,
                            int64_t x1,
                            int64_t y1,
                            int64_t value,
                            int64_t rgba) {
    if (!debug_draw)
        return;
    if (!debugdraw2d_reserve(debug_draw, debug_draw->count + 1)) {
        rt_trap("DebugDraw2D: capacity overflow");
        return;
    }
    rt_debugdraw2d_cmd *cmd = &debug_draw->cmds[debug_draw->count++];
    cmd->type = type;
    cmd->x0 = x0;
    cmd->y0 = y0;
    cmd->x1 = x1;
    cmd->y1 = y1;
    cmd->value = value;
    cmd->color = rgba;
}

void rt_debugdraw2d_line(
    void *debug_draw, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t rgba) {
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 1, x0, y0, x1, y1, 0, rgba);
}

void rt_debugdraw2d_rect(
    void *debug_draw, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgba) {
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 2, x, y, width, height, 0, rgba);
}

void rt_debugdraw2d_circle(void *debug_draw, int64_t x, int64_t y, int64_t radius, int64_t rgba) {
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 3, x, y, 0, 0, radius, rgba);
}

void rt_debugdraw2d_draw_to_pixels(void *debug_draw, void *pixels) {
    rt_debugdraw2d_impl *impl = (rt_debugdraw2d_impl *)debug_draw;
    if (!impl || !pixels)
        return;
    for (int64_t i = 0; i < impl->count; i++) {
        rt_debugdraw2d_cmd *cmd = &impl->cmds[i];
        int64_t color = draw_rgb(cmd->color);
        if (cmd->type == 1)
            rt_pixels_draw_line(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 2)
            rt_pixels_draw_frame(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 3)
            rt_pixels_draw_ring(pixels, cmd->x0, cmd->y0, cmd->value, color);
    }
}
