//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_canvas3d_overlay.c
// Purpose: Canvas3D screen-space overlay, screenshot, and debug-draw helpers.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"

#include <math.h>
#include <stdlib.h>

extern rt_string rt_const_cstr(const char *s);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_pixels_new(int64_t width, int64_t height);

/* Helper: project 3D point to screen using the active or most recent scene VP. */
static int world_to_screen(
    const rt_canvas3d *c, const float *wp, float *sx, float *sy, int32_t fb_w, int32_t fb_h) {
    const float *vp = canvas3d_active_scene_vp(c);
    float pos4[4] = {wp[0], wp[1], wp[2], 1.0f};
    float clip[4];
    if (!vp)
        return 0;
    clip[0] = vp[0] * pos4[0] + vp[1] * pos4[1] + vp[2] * pos4[2] + vp[3] * pos4[3];
    clip[1] = vp[4] * pos4[0] + vp[5] * pos4[1] + vp[6] * pos4[2] + vp[7] * pos4[3];
    clip[2] = vp[8] * pos4[0] + vp[9] * pos4[1] + vp[10] * pos4[2] + vp[11] * pos4[3];
    clip[3] = vp[12] * pos4[0] + vp[13] * pos4[1] + vp[14] * pos4[2] + vp[15] * pos4[3];
    if (clip[3] <= 0.0f)
        return 0;
    {
        const float iw = 1.0f / clip[3];
        *sx = (clip[0] * iw + 1.0f) * 0.5f * (float)fb_w;
        *sy = (1.0f - clip[1] * iw) * 0.5f * (float)fb_h;
    }
    return 1;
}

void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color) {
    int8_t started_temp_frame = 0;

    if (!obj || !from || !to)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb))
        return;

    {
        float p0[3] = {(float)rt_vec3_x(from), (float)rt_vec3_y(from), (float)rt_vec3_z(from)};
        float p1[3] = {(float)rt_vec3_x(to), (float)rt_vec3_y(to), (float)rt_vec3_z(to)};
        float sx0;
        float sy0;
        float sx1;
        float sy1;
        if (!world_to_screen(c, p0, &sx0, &sy0, fb.width, fb.height))
            return;
        if (!world_to_screen(c, p1, &sx1, &sy1, fb.width, fb.height))
            return;
        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        (void)canvas3d_queue_screen_line(c,
                                         sx0,
                                         sy0,
                                         sx1,
                                         sy1,
                                         1.0f,
                                         (float)((color >> 16) & 0xFF) / 255.0f,
                                         (float)((color >> 8) & 0xFF) / 255.0f,
                                         (float)(color & 0xFF) / 255.0f,
                                         1.0f);
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size) {
    int8_t started_temp_frame = 0;

    if (!obj || !pos)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb))
        return;

    {
        float p[3] = {(float)rt_vec3_x(pos), (float)rt_vec3_y(pos), (float)rt_vec3_z(pos)};
        float sx;
        float sy;
        if (!world_to_screen(c, p, &sx, &sy, fb.width, fb.height))
            return;
        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        {
            const float side = size > 0 ? (float)size : 1.0f;
            const float half = side * 0.5f;
            (void)canvas3d_queue_screen_rect(c,
                                             sx - half,
                                             sy - half,
                                             side,
                                             side,
                                             (float)((color >> 16) & 0xFF) / 255.0f,
                                             (float)((color >> 8) & 0xFF) / 255.0f,
                                             (float)(color & 0xFF) / 255.0f,
                                             1.0f);
        }
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

void rt_canvas3d_draw_rect2d(void *obj, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    int8_t started_temp_frame = 0;

    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (w <= 0 || h <= 0)
        return;
    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    rt_canvas3d_draw_rect_3d(c, x, y, w, h, color);
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

void rt_canvas3d_draw_crosshair(void *obj, int64_t color, int64_t size) {
    int8_t started_temp_frame = 0;

    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb))
        return;

    {
        const int32_t cx = fb.width / 2;
        const int32_t cy = fb.height / 2;
        const int32_t half = (int32_t)(size / 2);
        const float r = (float)((color >> 16) & 0xFF) / 255.0f;
        const float g = (float)((color >> 8) & 0xFF) / 255.0f;
        const float b = (float)(color & 0xFF) / 255.0f;

        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        (void)canvas3d_queue_screen_line(
            c, (float)(cx - half), (float)cy, (float)(cx + half), (float)cy, 1.0f, r, g, b, 1.0f);
        (void)canvas3d_queue_screen_line(
            c, (float)cx, (float)(cy - half), (float)cx, (float)(cy + half), 1.0f, r, g, b, 1.0f);
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

void rt_canvas3d_draw_text2d(void *obj, int64_t x, int64_t y, rt_string text, int64_t color) {
    int8_t started_temp_frame = 0;

    if (!obj || !text)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    rt_canvas3d_draw_text_3d(c, x, y, text, color);
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

rt_string rt_canvas3d_get_backend(void *obj) {
    if (!obj)
        return rt_const_cstr("unknown");
    rt_canvas3d *c = (rt_canvas3d *)obj;
    return rt_const_cstr(c->backend ? c->backend->name : "unknown");
}

void *rt_canvas3d_screenshot(void *obj) {
    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    if (!obj)
        return NULL;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return NULL;

    const int32_t shot_w = c->render_target ? c->render_target->width : c->width;
    const int32_t shot_h = c->render_target ? c->render_target->height : c->height;
    if (shot_w <= 0 || shot_h <= 0)
        return NULL;

    void *pixels = rt_pixels_new((int64_t)shot_w, (int64_t)shot_h);
    if (!pixels)
        return NULL;
    px_view *pv = (px_view *)pixels;

    if (c->render_target && c->render_target->color_buf) {
        if (!vgfx3d_rendertarget_sync_color_if_needed(c->render_target))
            return pixels;
        for (int32_t y = 0; y < shot_h; y++)
            for (int32_t x = 0; x < shot_w; x++) {
                const uint8_t *src =
                    &c->render_target->color_buf[y * c->render_target->stride + x * 4];
                pv->data[y * pv->w + x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                                          ((uint32_t)src[2] << 8) | (uint32_t)src[3];
            }
        return pixels;
    }

    if (c->backend && c->backend != &vgfx3d_software_backend && c->backend->readback_rgba) {
        const size_t row_bytes = (size_t)shot_w * 4u;
        uint8_t *rgba = (uint8_t *)malloc((size_t)shot_h * row_bytes);
        if (rgba &&
            c->backend->readback_rgba(c->backend_ctx, rgba, shot_w, shot_h, (int32_t)row_bytes)) {
            for (int32_t y = 0; y < shot_h; y++)
                for (int32_t x = 0; x < shot_w; x++) {
                    const uint8_t *src = &rgba[(size_t)y * row_bytes + (size_t)x * 4u];
                    pv->data[y * pv->w + x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                                              ((uint32_t)src[2] << 8) | (uint32_t)src[3];
                }
            free(rgba);
            return pixels;
        }
        free(rgba);
    }

    {
        vgfx_framebuffer_t fb;
        if (!vgfx_get_framebuffer(c->gfx_win, &fb))
            return pixels;
        for (int32_t y = 0; y < fb.height && y < shot_h; y++)
            for (int32_t x = 0; x < fb.width && x < shot_w; x++) {
                const uint8_t *src = &fb.pixels[y * fb.stride + x * 4];
                pv->data[y * pv->w + x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                                          ((uint32_t)src[2] << 8) | (uint32_t)src[3];
            }
    }
    return pixels;
}

void rt_canvas3d_draw_aabb_wire(void *obj, void *min_v, void *max_v, int64_t color) {
    if (!obj || !min_v || !max_v)
        return;
    double mn[3] = {rt_vec3_x(min_v), rt_vec3_y(min_v), rt_vec3_z(min_v)};
    double mx[3] = {rt_vec3_x(max_v), rt_vec3_y(max_v), rt_vec3_z(max_v)};
    void *corners[8];
    for (int i = 0; i < 8; i++) {
        corners[i] =
            rt_vec3_new((i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2]);
    }

    static const int edges[12][2] = {{0, 1},
                                     {1, 3},
                                     {3, 2},
                                     {2, 0},
                                     {4, 5},
                                     {5, 7},
                                     {7, 6},
                                     {6, 4},
                                     {0, 4},
                                     {1, 5},
                                     {2, 6},
                                     {3, 7}};
    for (int e = 0; e < 12; e++)
        rt_canvas3d_draw_line3d(obj, corners[edges[e][0]], corners[edges[e][1]], color);
}

void rt_canvas3d_draw_sphere_wire(void *obj, void *center, double radius, int64_t color) {
    if (!obj || !center)
        return;
    const double cx = rt_vec3_x(center);
    const double cy = rt_vec3_y(center);
    const double cz = rt_vec3_z(center);
    const int segs = 24;
    const double step = 2.0 * 3.14159265358979323846 / segs;

    for (int i = 0; i < segs; i++) {
        const double a0 = i * step;
        const double a1 = (i + 1) * step;
        const double c0 = cos(a0);
        const double s0 = sin(a0);
        const double c1 = cos(a1);
        const double s1 = sin(a1);

        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx + c0 * radius, cy + s0 * radius, cz),
                                rt_vec3_new(cx + c1 * radius, cy + s1 * radius, cz),
                                color);
        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx + c0 * radius, cy, cz + s0 * radius),
                                rt_vec3_new(cx + c1 * radius, cy, cz + s1 * radius),
                                color);
        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx, cy + c0 * radius, cz + s0 * radius),
                                rt_vec3_new(cx, cy + c1 * radius, cz + s1 * radius),
                                color);
    }
}

void rt_canvas3d_draw_debug_ray(void *obj, void *origin, void *dir, double length, int64_t color) {
    if (!obj || !origin || !dir)
        return;
    rt_canvas3d_draw_line3d(obj,
                            origin,
                            rt_vec3_new(rt_vec3_x(origin) + rt_vec3_x(dir) * length,
                                        rt_vec3_y(origin) + rt_vec3_y(dir) * length,
                                        rt_vec3_z(origin) + rt_vec3_z(dir) * length),
                            color);
}

void rt_canvas3d_draw_axis(void *obj, void *origin, double scale) {
    if (!obj || !origin)
        return;
    const double ox = rt_vec3_x(origin);
    const double oy = rt_vec3_y(origin);
    const double oz = rt_vec3_z(origin);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox + scale, oy, oz), 0xFF0000);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy + scale, oz), 0x00FF00);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy, oz + scale), 0x0000FF);
}

#else
typedef int rt_canvas3d_overlay_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
