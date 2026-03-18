//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_canvas3d.c
// Purpose: Viper.Graphics3D.Canvas3D — 3D rendering surface that dispatches
//   through the vgfx3d_backend_t vtable. Backend selection is automatic
//   (software fallback always available).
//
// Key invariants:
//   - Begin/End must bracket DrawMesh calls (no nesting)
//   - All rendering dispatches through backend->submit_draw
//   - Canvas3D owns the backend context (created in New, freed in finalizer)
//
// Ownership/Lifetime:
//   - Canvas3D is GC-managed; finalizer destroys backend ctx + window
//
// Links: vgfx3d_backend.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"
#include "rt_string.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void rt_trap(const char *msg);
extern int64_t rt_clock_ticks_us(void);
extern void rt_keyboard_set_canvas(vgfx_window_t win);
extern void rt_keyboard_begin_frame(void);
extern void rt_mouse_set_canvas(vgfx_window_t win);
extern void rt_mouse_begin_frame(void);
extern void rt_pad_init(void);
extern void rt_pad_begin_frame(void);
extern void rt_pad_poll(void);
extern rt_string rt_const_cstr(const char *s);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_pixels_new(int64_t width, int64_t height);

/*==========================================================================
 * Helpers
 *=========================================================================*/

static void mat4_d2f(const double *src, float *dst)
{
    for (int i = 0; i < 16; i++)
        dst[i] = (float)src[i];
}

/* Build light params array from canvas light pointers */
static int32_t build_light_params(const rt_canvas3d *c,
                                   vgfx3d_light_params_t *out, int32_t max)
{
    int32_t count = 0;
    for (int i = 0; i < VGFX3D_MAX_LIGHTS && count < max; i++)
    {
        const rt_light3d *l = c->lights[i];
        if (!l) continue;
        out[count].type = l->type;
        out[count].direction[0] = (float)l->direction[0];
        out[count].direction[1] = (float)l->direction[1];
        out[count].direction[2] = (float)l->direction[2];
        out[count].position[0] = (float)l->position[0];
        out[count].position[1] = (float)l->position[1];
        out[count].position[2] = (float)l->position[2];
        out[count].color[0] = (float)l->color[0];
        out[count].color[1] = (float)l->color[1];
        out[count].color[2] = (float)l->color[2];
        out[count].intensity = (float)l->intensity;
        out[count].attenuation = (float)l->attenuation;
        count++;
    }
    return count;
}

/*==========================================================================
 * Canvas3D lifecycle
 *=========================================================================*/

static void rt_canvas3d_finalize(void *obj)
{
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (c->backend && c->backend_ctx)
    {
        c->backend->destroy_ctx(c->backend_ctx);
        c->backend_ctx = NULL;
    }
    if (c->gfx_win)
    {
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
    }
}

void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h)
{
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
    {
        rt_trap("Canvas3D.New: dimensions must be 1-8192");
        return NULL;
    }

    rt_canvas3d *c = (rt_canvas3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_canvas3d));
    if (!c)
    {
        rt_trap("Canvas3D.New: memory allocation failed");
        return NULL;
    }
    memset(c, 0, sizeof(rt_canvas3d));
    rt_obj_set_finalizer(c, rt_canvas3d_finalize);

    /* Create window */
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)w;
    params.height = (int32_t)h;
    if (title)
        params.title = rt_string_cstr(title);

    c->gfx_win = vgfx_create_window(&params);
    if (!c->gfx_win)
    {
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        rt_trap("Canvas3D.New: failed to create window (display server unavailable?)");
        return NULL;
    }

    c->width = (int32_t)w;
    c->height = (int32_t)h;

    /* Select and initialize backend */
    c->backend = vgfx3d_select_backend();
    c->backend_ctx = c->backend->create_ctx(c->gfx_win, (int32_t)w, (int32_t)h);
    if (!c->backend_ctx)
    {
        rt_trap("Canvas3D.New: backend initialization failed");
        return NULL;
    }

    c->ambient[0] = 0.1f;
    c->ambient[1] = 0.1f;
    c->ambient[2] = 0.1f;
    c->backface_cull = 1;

    rt_keyboard_set_canvas(c->gfx_win);
    rt_mouse_set_canvas(c->gfx_win);
    rt_pad_init();

    return c;
}

/*==========================================================================
 * Rendering — dispatches through backend vtable
 *=========================================================================*/

void rt_canvas3d_clear(void *obj, double r, double g, double b)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win || !c->backend) return;
    c->backend->clear(c->backend_ctx, c->gfx_win, (float)r, (float)g, (float)b);
}

void rt_canvas3d_begin(void *obj, void *camera)
{
    if (!obj || !camera) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    rt_camera3d *cam = (rt_camera3d *)camera;
    if (!c->backend) return;

    vgfx3d_camera_params_t params;
    mat4_d2f(cam->view, params.view);
    mat4_d2f(cam->projection, params.projection);
    params.position[0] = (float)cam->eye[0];
    params.position[1] = (float)cam->eye[1];
    params.position[2] = (float)cam->eye[2];

    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

void rt_canvas3d_draw_mesh(void *obj, void *mesh_obj, void *transform_obj,
                           void *material_obj)
{
    if (!obj || !mesh_obj || !transform_obj || !material_obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame || !c->gfx_win || !c->backend) return;

    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    mat4_impl *model_d = (mat4_impl *)transform_obj;
    rt_material3d *mat = (rt_material3d *)material_obj;

    if (mesh->vertex_count == 0 || mesh->index_count == 0) return;

    /* Build draw command */
    vgfx3d_draw_cmd_t cmd;
    cmd.vertices = mesh->vertices;
    cmd.vertex_count = mesh->vertex_count;
    cmd.indices = mesh->indices;
    cmd.index_count = mesh->index_count;
    mat4_d2f(model_d->m, cmd.model_matrix);
    cmd.diffuse_color[0] = (float)mat->diffuse[0];
    cmd.diffuse_color[1] = (float)mat->diffuse[1];
    cmd.diffuse_color[2] = (float)mat->diffuse[2];
    cmd.diffuse_color[3] = (float)mat->diffuse[3];
    cmd.specular[0] = (float)mat->specular[0];
    cmd.specular[1] = (float)mat->specular[1];
    cmd.specular[2] = (float)mat->specular[2];
    cmd.shininess = (float)mat->shininess;
    cmd.unlit = mat->unlit;
    cmd.texture = mat->texture;

    /* Build light params */
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count = build_light_params(c, lights, VGFX3D_MAX_LIGHTS);

    c->backend->submit_draw(c->backend_ctx, c->gfx_win, &cmd,
                             lights, light_count, c->ambient,
                             c->wireframe, c->backface_cull);
}

void rt_canvas3d_end(void *obj)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (c->backend)
        c->backend->end_frame(c->backend_ctx);
    c->in_frame = 0;
}

/*==========================================================================
 * Window lifecycle — same as before, no backend involvement
 *=========================================================================*/

void rt_canvas3d_flip(void *obj)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;

    vgfx_update(c->gfx_win);

    int64_t now_us = rt_clock_ticks_us();
    if (c->last_flip_us > 0)
    {
        int64_t delta_us = now_us - c->last_flip_us;
        c->delta_time_ms = delta_us > 0 ? delta_us / 1000 : 0;
    }
    else
        c->delta_time_ms = 0;
    c->last_flip_us = now_us;

    if (vgfx_close_requested(c->gfx_win))
    {
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
        c->should_close = 1;
    }
}

int64_t rt_canvas3d_poll(void *obj)
{
    if (!obj) return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return 0;
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    rt_pad_poll();
    vgfx_event_t evt;
    while (vgfx_poll_event(c->gfx_win, &evt)) { }
    return 0;
}

int8_t rt_canvas3d_should_close(void *obj)
{
    return obj ? ((rt_canvas3d *)obj)->should_close : 0;
}

void rt_canvas3d_set_wireframe(void *obj, int8_t enabled)
{
    if (obj) ((rt_canvas3d *)obj)->wireframe = enabled;
}

void rt_canvas3d_set_backface_cull(void *obj, int8_t enabled)
{
    if (obj) ((rt_canvas3d *)obj)->backface_cull = enabled;
}

int64_t rt_canvas3d_get_width(void *obj)
{
    return obj ? ((rt_canvas3d *)obj)->width : 0;
}

int64_t rt_canvas3d_get_height(void *obj)
{
    return obj ? ((rt_canvas3d *)obj)->height : 0;
}

int64_t rt_canvas3d_get_fps(void *obj)
{
    if (!obj) return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    return c->delta_time_ms > 0 ? 1000 / c->delta_time_ms : 0;
}

int64_t rt_canvas3d_get_delta_time(void *obj)
{
    if (!obj) return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    int64_t dt = c->delta_time_ms;
    if (c->dt_max_ms > 0)
    {
        if (dt < 1) dt = 1;
        if (dt > c->dt_max_ms) dt = c->dt_max_ms;
    }
    return dt;
}

void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms)
{
    if (obj) ((rt_canvas3d *)obj)->dt_max_ms = max_ms;
}

void rt_canvas3d_set_light(void *obj, int64_t index, void *light)
{
    if (!obj || index < 0 || index >= VGFX3D_MAX_LIGHTS) return;
    ((rt_canvas3d *)obj)->lights[index] = (rt_light3d *)light;
}

void rt_canvas3d_set_ambient(void *obj, double r, double g, double b)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    c->ambient[0] = (float)r; c->ambient[1] = (float)g; c->ambient[2] = (float)b;
}

/*==========================================================================
 * Debug drawing — transform 3D points to screen via backend VP
 *=========================================================================*/

/* Helper: project 3D point to screen using the backend's current VP matrix.
 * We access the software backend's context directly here since debug drawing
 * is a Canvas3D feature, not a backend feature. For GPU backends, this would
 * need a backend-agnostic VP accessor. */
static int world_to_screen(const rt_canvas3d *c, const float *wp,
                            float *sx, float *sy, int32_t fb_w, int32_t fb_h)
{
    /* Access the VP matrix from the software backend context.
     * This is tightly coupled to the SW backend but acceptable for Phase 2. */
    typedef struct { float *zbuf; int32_t w, h; float vp[16]; float cp[3]; } sw_ctx_peek;
    const sw_ctx_peek *sw = (const sw_ctx_peek *)c->backend_ctx;
    float pos4[4] = {wp[0], wp[1], wp[2], 1.0f};
    float clip[4];
    clip[0] = sw->vp[0]*pos4[0] + sw->vp[1]*pos4[1] + sw->vp[2]*pos4[2] + sw->vp[3]*pos4[3];
    clip[1] = sw->vp[4]*pos4[0] + sw->vp[5]*pos4[1] + sw->vp[6]*pos4[2] + sw->vp[7]*pos4[3];
    clip[2] = sw->vp[8]*pos4[0] + sw->vp[9]*pos4[1] + sw->vp[10]*pos4[2] + sw->vp[11]*pos4[3];
    clip[3] = sw->vp[12]*pos4[0] + sw->vp[13]*pos4[1] + sw->vp[14]*pos4[2] + sw->vp[15]*pos4[3];
    if (clip[3] <= 0.0f) return 0;
    float iw = 1.0f / clip[3];
    *sx = (clip[0] * iw + 1.0f) * 0.5f * (float)fb_w;
    *sy = (1.0f - clip[1] * iw) * 0.5f * (float)fb_h;
    return 1;
}

static void draw_line_px(uint8_t *pixels, int32_t fb_w, int32_t fb_h, int32_t stride,
                          int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        if (x0 >= 0 && x0 < fb_w && y0 >= 0 && y0 < fb_h)
        {
            uint8_t *dst = &pixels[y0 * stride + x0 * 4];
            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 0xFF;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color)
{
    if (!obj || !from || !to) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return;

    float p0[3] = {(float)rt_vec3_x(from), (float)rt_vec3_y(from), (float)rt_vec3_z(from)};
    float p1[3] = {(float)rt_vec3_x(to), (float)rt_vec3_y(to), (float)rt_vec3_z(to)};
    float sx0, sy0, sx1, sy1;
    if (!world_to_screen(c, p0, &sx0, &sy0, fb.width, fb.height)) return;
    if (!world_to_screen(c, p1, &sx1, &sy1, fb.width, fb.height)) return;

    draw_line_px(fb.pixels, fb.width, fb.height, fb.stride,
                  (int)sx0, (int)sy0, (int)sx1, (int)sy1,
                  (uint8_t)((color >> 16) & 0xFF),
                  (uint8_t)((color >> 8) & 0xFF),
                  (uint8_t)(color & 0xFF));
}

void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size)
{
    if (!obj || !pos) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return;

    float p[3] = {(float)rt_vec3_x(pos), (float)rt_vec3_y(pos), (float)rt_vec3_z(pos)};
    float sx, sy;
    if (!world_to_screen(c, p, &sx, &sy, fb.width, fb.height)) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    int half = (int)(size / 2), cx = (int)sx, cy = (int)sy;
    for (int dy = -half; dy <= half; dy++)
        for (int dx = -half; dx <= half; dx++)
        {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < fb.width && py >= 0 && py < fb.height)
            {
                uint8_t *dst = &fb.pixels[py * fb.stride + px * 4];
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 0xFF;
            }
        }
}

rt_string rt_canvas3d_get_backend(void *obj)
{
    if (!obj) return rt_const_cstr("unknown");
    rt_canvas3d *c = (rt_canvas3d *)obj;
    return rt_const_cstr(c->backend ? c->backend->name : "unknown");
}

void *rt_canvas3d_screenshot(void *obj)
{
    if (!obj) return NULL;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return NULL;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return NULL;

    void *pixels = rt_pixels_new((int64_t)fb.width, (int64_t)fb.height);
    if (!pixels) return NULL;

    typedef struct { int64_t w; int64_t h; uint32_t *data; } px_view;
    px_view *pv = (px_view *)pixels;
    for (int32_t y = 0; y < fb.height; y++)
        for (int32_t x = 0; x < fb.width; x++)
        {
            const uint8_t *src = &fb.pixels[y * fb.stride + x * 4];
            pv->data[y * pv->w + x] = ((uint32_t)src[0] << 24) |
                                       ((uint32_t)src[1] << 16) |
                                       ((uint32_t)src[2] << 8) |
                                       (uint32_t)src[3];
        }
    return pixels;
}

#endif /* VIPER_ENABLE_GRAPHICS */
