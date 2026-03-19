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
 * Deferred draw command (for transparency sorting)
 *=========================================================================*/

typedef struct
{
    vgfx3d_draw_cmd_t cmd;
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count;
    float ambient[3];
    int8_t wireframe;
    int8_t backface_cull;
    float sort_key; /* squared distance from camera (for transparent sorting) */
} deferred_draw_t;

/* Comparison for qsort: back-to-front (descending sort_key) */
static int cmp_back_to_front(const void *a, const void *b)
{
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka > kb)
        return -1;
    if (ka < kb)
        return 1;
    return 0;
}

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
    /* Destroy the backend context */
    if (c->backend && c->backend_ctx)
    {
        c->backend->destroy_ctx(c->backend_ctx);
        c->backend_ctx = NULL;
    }
    /* Free deferred draw command buffer */
    free(c->draw_cmds);
    c->draw_cmds = NULL;
    c->draw_count = c->draw_capacity = 0;
    /* Free any leftover temp buffers (e.g., from skinned draws) */
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    free(c->temp_buffers);
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;

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

    /* Select and initialize backend (GPU first, software fallback) */
    c->backend = vgfx3d_select_backend();
    c->backend_ctx = c->backend->create_ctx(c->gfx_win, (int32_t)w, (int32_t)h);
    if (!c->backend_ctx)
    {
        /* GPU backend failed — fall back to software */
        c->backend = &vgfx3d_software_backend;
        c->backend_ctx = c->backend->create_ctx(c->gfx_win, (int32_t)w, (int32_t)h);
        if (!c->backend_ctx)
        {
            rt_trap("Canvas3D.New: backend initialization failed");
            return NULL;
        }
    }

    c->ambient[0] = 0.1f;
    c->ambient[1] = 0.1f;
    c->ambient[2] = 0.1f;
    c->backface_cull = 0; /* disabled by default — extreme perspective can reverse
                           * screen-space winding, causing false culling. Users can
                           * enable with SetBackfaceCull(canvas, true) if needed. */
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;

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

    /* Also clear the software framebuffer so skybox/vgfx_update has
     * correct background content regardless of active backend. */
    if (c->backend != &vgfx3d_software_backend && !c->render_target)
    {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(c->gfx_win, &fb))
        {
            uint8_t cr = (uint8_t)((float)r * 255.0f);
            uint8_t cg = (uint8_t)((float)g * 255.0f);
            uint8_t cb = (uint8_t)((float)b * 255.0f);
            for (int32_t y = 0; y < fb.height; y++)
                for (int32_t x = 0; x < fb.width; x++)
                {
                    uint8_t *px = &fb.pixels[y * fb.stride + x * 4];
                    px[0] = cr; px[1] = cg; px[2] = cb; px[3] = 0xFF;
                }
        }
    }
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

    /* Cache camera position for transparency sort key computation */
    c->cached_cam_pos[0] = params.position[0];
    c->cached_cam_pos[1] = params.position[1];
    c->cached_cam_pos[2] = params.position[2];

    /* Reset draw command queue for this frame */
    c->draw_count = 0;

    /* Cache VP matrix for debug drawing (backend-agnostic) */
    {
        float vf[16], pf[16];
        mat4_d2f(cam->view, vf);
        mat4_d2f(cam->projection, pf);
        /* VP = P * V (row-major) */
        for (int r = 0; r < 4; r++)
            for (int col = 0; col < 4; col++)
                c->cached_vp[r * 4 + col] =
                    pf[r * 4 + 0] * vf[0 * 4 + col] + pf[r * 4 + 1] * vf[1 * 4 + col] +
                    pf[r * 4 + 2] * vf[2 * 4 + col] + pf[r * 4 + 3] * vf[3 * 4 + col];
    }

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

    /* Ensure draw command buffer has space */
    if (c->draw_count >= c->draw_capacity)
    {
        int32_t new_cap = c->draw_capacity == 0 ? 32 : c->draw_capacity * 2;
        deferred_draw_t *new_buf = (deferred_draw_t *)realloc(
            c->draw_cmds, (size_t)new_cap * sizeof(deferred_draw_t));
        if (!new_buf) return;
        c->draw_cmds = new_buf;
        c->draw_capacity = new_cap;
    }

    deferred_draw_t *dd = &((deferred_draw_t *)c->draw_cmds)[c->draw_count++];

    /* Build draw command */
    dd->cmd.vertices = mesh->vertices;
    dd->cmd.vertex_count = mesh->vertex_count;
    dd->cmd.indices = mesh->indices;
    dd->cmd.index_count = mesh->index_count;
    mat4_d2f(model_d->m, dd->cmd.model_matrix);
    dd->cmd.diffuse_color[0] = (float)mat->diffuse[0];
    dd->cmd.diffuse_color[1] = (float)mat->diffuse[1];
    dd->cmd.diffuse_color[2] = (float)mat->diffuse[2];
    dd->cmd.diffuse_color[3] = (float)mat->diffuse[3];
    dd->cmd.specular[0] = (float)mat->specular[0];
    dd->cmd.specular[1] = (float)mat->specular[1];
    dd->cmd.specular[2] = (float)mat->specular[2];
    dd->cmd.shininess = (float)mat->shininess;
    dd->cmd.alpha = (float)mat->alpha;
    dd->cmd.unlit = mat->unlit;
    dd->cmd.texture = mat->texture;
    dd->cmd.normal_map = mat->normal_map;
    dd->cmd.specular_map = mat->specular_map;
    dd->cmd.emissive_map = mat->emissive_map;
    dd->cmd.emissive_color[0] = (float)mat->emissive[0];
    dd->cmd.emissive_color[1] = (float)mat->emissive[1];
    dd->cmd.emissive_color[2] = (float)mat->emissive[2];

    /* Build light params */
    dd->light_count = build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS);
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->wireframe = c->wireframe;
    dd->backface_cull = c->backface_cull;

    /* Compute sort key: squared distance from camera to mesh centroid.
     * Uses model matrix translation (column 3 in row-major) as centroid proxy. */
    {
        const float *m = dd->cmd.model_matrix;
        float cx = m[3], cy = m[7], cz = m[11]; /* row-major column 3 = translation */
        float dx = cx - c->cached_cam_pos[0];
        float dy = cy - c->cached_cam_pos[1];
        float dz = cz - c->cached_cam_pos[2];
        dd->sort_key = dx * dx + dy * dy + dz * dz;
    }
}

void rt_canvas3d_end(void *obj)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame) return; /* End() without Begin() — nothing to do */
    if (!c->backend)
    {
        c->in_frame = 0;
        return;
    }

    /* Skybox pass: render cube map as background BEFORE any scene geometry.
     * Renders regardless of draw_count (skybox-only scenes are valid). */
    if (c->skybox)
    {
        extern void rt_cubemap_sample(const rt_cubemap3d *cm, float dx, float dy, float dz,
                                       float *out_r, float *out_g, float *out_b);

        /* Determine output buffer: render target or window framebuffer */
        uint8_t *out_pixels = NULL;
        int32_t out_w = 0, out_h = 0, out_stride = 0;

        if (c->render_target)
        {
            out_pixels = c->render_target->color_buf;
            out_w = c->render_target->width;
            out_h = c->render_target->height;
            out_stride = c->render_target->stride;
        }
        else
        {
            vgfx_framebuffer_t fb;
            if (c->gfx_win && vgfx_get_framebuffer(c->gfx_win, &fb))
            {
                out_pixels = fb.pixels;
                out_w = fb.width;
                out_h = fb.height;
                out_stride = fb.stride;
            }
        }

        if (out_pixels)
        {
            float vp_rot[16];
            memcpy(vp_rot, c->cached_vp, sizeof(float) * 16);
            vp_rot[3] = 0.0f;
            vp_rot[7] = 0.0f;
            vp_rot[11] = 0.0f;

            for (int32_t y = 0; y < out_h; y++)
            {
                float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) / (float)out_h;
                for (int32_t x = 0; x < out_w; x++)
                {
                    float ndc_x = 2.0f * ((float)x + 0.5f) / (float)out_w - 1.0f;

                    float dx = vp_rot[0] * ndc_x + vp_rot[4] * ndc_y + vp_rot[8];
                    float dy = vp_rot[1] * ndc_x + vp_rot[5] * ndc_y + vp_rot[9];
                    float dz = vp_rot[2] * ndc_x + vp_rot[6] * ndc_y + vp_rot[10];

                    float r, g, b;
                    rt_cubemap_sample(c->skybox, dx, dy, dz, &r, &g, &b);

                    uint8_t *dst = &out_pixels[y * out_stride + x * 4];
                    dst[0] = (uint8_t)(r * 255.0f);
                    dst[1] = (uint8_t)(g * 255.0f);
                    dst[2] = (uint8_t)(b * 255.0f);
                    dst[3] = 0xFF;
                }
            }
        }
    }

    /* Early exit if no geometry to draw (skybox already rendered above) */
    if (c->draw_count == 0)
    {
        c->backend->end_frame(c->backend_ctx);
        c->in_frame = 0;
        return;
    }

    deferred_draw_t *cmds = (deferred_draw_t *)c->draw_cmds;

    /* Pass 1: submit all opaque draws (alpha >= 1.0) immediately */
    for (int32_t i = 0; i < c->draw_count; i++)
    {
        if (cmds[i].cmd.alpha >= 1.0f)
        {
            c->backend->submit_draw(c->backend_ctx, c->gfx_win, &cmds[i].cmd,
                                     cmds[i].lights, cmds[i].light_count,
                                     cmds[i].ambient, cmds[i].wireframe,
                                     cmds[i].backface_cull);
        }
    }

    /* Pass 2: collect transparent draws, sort back-to-front, submit */
    {
        int32_t trans_count = 0;
        for (int32_t i = 0; i < c->draw_count; i++)
            if (cmds[i].cmd.alpha < 1.0f)
                trans_count++;

        if (trans_count > 0)
        {
            /* Build index array of transparent draws */
            deferred_draw_t *trans = (deferred_draw_t *)malloc(
                (size_t)trans_count * sizeof(deferred_draw_t));
            if (trans)
            {
                int32_t ti = 0;
                for (int32_t i = 0; i < c->draw_count; i++)
                    if (cmds[i].cmd.alpha < 1.0f)
                        trans[ti++] = cmds[i];

                /* Sort back-to-front (farthest first) */
                qsort(trans, (size_t)trans_count, sizeof(deferred_draw_t),
                      cmp_back_to_front);

                /* Submit sorted transparent draws */
                for (int32_t i = 0; i < trans_count; i++)
                {
                    c->backend->submit_draw(c->backend_ctx, c->gfx_win,
                                             &trans[i].cmd, trans[i].lights,
                                             trans[i].light_count, trans[i].ambient,
                                             trans[i].wireframe, trans[i].backface_cull);
                }

                free(trans);
            }
        }
    }

    c->backend->end_frame(c->backend_ctx);
    c->in_frame = 0;
    c->draw_count = 0;

    /* Free per-frame temporary buffers (e.g., skinned vertex data) */
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
}

/*==========================================================================
 * Window lifecycle — same as before, no backend involvement
 *=========================================================================*/

void rt_canvas3d_flip(void *obj)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;

    /* Apply post-processing effects to the software framebuffer */
    extern void rt_postfx3d_apply_to_canvas(void *canvas);
    rt_postfx3d_apply_to_canvas(obj);

    /* Present the GPU drawable / swap the back buffer. For GPU backends
     * (Metal, D3D11, OpenGL) this presents only the LAST Begin/End pair's
     * content, avoiding flicker with multi-pass rendering and RTT. */
    if (c->backend && c->backend->present)
        c->backend->present(c->backend_ctx);

    /* Always call vgfx_update to keep the window alive and process display
     * refresh on macOS. For GPU backends, the CAMetalLayer renders on top
     * of the software framebuffer. */
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
    extern void rt_keyboard_on_key_down(int64_t key);
    extern void rt_keyboard_on_key_up(int64_t key);
    extern void rt_mouse_update_pos(int64_t x, int64_t y);
    extern void rt_mouse_force_delta(int64_t dx, int64_t dy);
    extern void rt_mouse_button_down(int64_t button);
    extern void rt_mouse_button_up(int64_t button);
    extern int8_t rt_mouse_is_captured(void);

    int8_t captured = rt_mouse_is_captured();

    /* Read current platform mouse position */
    int32_t mx, my;
    vgfx_mouse_pos(c->gfx_win, &mx, &my);

    /* Begin frame (resets per-frame state for keyboard/mouse/pad) */
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    rt_pad_poll();

    /* For captured (FPS) mode: compute delta as offset from window center.
     * This avoids issues with warp timing, stale events, and OS mouse tracking. */
    if (captured)
    {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        int32_t cx = cw / 2, cy = ch / 2;
        int64_t dx = (int64_t)mx - (int64_t)cx;
        int64_t dy = (int64_t)my - (int64_t)cy;
        rt_mouse_force_delta(dx, dy);
    }
    else
    {
        rt_mouse_update_pos((int64_t)mx, (int64_t)my);
    }

    /* Process events (keyboard + mouse buttons only — mouse moves handled above) */
    vgfx_event_t evt;
    while (vgfx_poll_event(c->gfx_win, &evt))
    {
        if (evt.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)evt.data.key.key);
        else if (evt.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)evt.data.key.key);
        else if (!captured && evt.type == VGFX_EVENT_MOUSE_MOVE)
        {
            float cs = vgfx_window_get_scale(c->gfx_win);
            rt_mouse_update_pos((int64_t)(evt.data.mouse_move.x / cs),
                                (int64_t)(evt.data.mouse_move.y / cs));
        }
        else if (evt.type == VGFX_EVENT_MOUSE_DOWN)
            rt_mouse_button_down((int64_t)evt.data.mouse_button.button);
        else if (evt.type == VGFX_EVENT_MOUSE_UP)
            rt_mouse_button_up((int64_t)evt.data.mouse_button.button);
    }

    /* Warp cursor to center for next frame (only when captured) */
    if (captured)
    {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        vgfx_warp_cursor(c->gfx_win, cw / 2, ch / 2);
    }

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

void rt_canvas3d_add_temp_buffer(void *obj, void *buffer)
{
    if (!obj || !buffer) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (c->temp_buf_count >= c->temp_buf_capacity)
    {
        int32_t new_cap = c->temp_buf_capacity == 0 ? 8 : c->temp_buf_capacity * 2;
        void **nb = (void **)realloc(c->temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb) { free(buffer); return; }
        c->temp_buffers = nb;
        c->temp_buf_capacity = new_cap;
    }
    c->temp_buffers[c->temp_buf_count++] = buffer;
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

/* Helper: project 3D point to screen using the cached VP matrix.
 * Uses c->cached_vp set in begin_frame (backend-agnostic). */
static int world_to_screen(const rt_canvas3d *c, const float *wp,
                            float *sx, float *sy, int32_t fb_w, int32_t fb_h)
{
    const float *vp = c->cached_vp;
    float pos4[4] = {wp[0], wp[1], wp[2], 1.0f};
    float clip[4];
    clip[0] = vp[0]*pos4[0] + vp[1]*pos4[1] + vp[2]*pos4[2] + vp[3]*pos4[3];
    clip[1] = vp[4]*pos4[0] + vp[5]*pos4[1] + vp[6]*pos4[2] + vp[7]*pos4[3];
    clip[2] = vp[8]*pos4[0] + vp[9]*pos4[1] + vp[10]*pos4[2] + vp[11]*pos4[3];
    clip[3] = vp[12]*pos4[0] + vp[13]*pos4[1] + vp[14]*pos4[2] + vp[15]*pos4[3];
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

/*==========================================================================
 * Screen-space HUD overlay (drawn directly to framebuffer, no 3D transform)
 *=========================================================================*/

void rt_canvas3d_draw_rect2d(void *obj, int64_t x, int64_t y,
                               int64_t w, int64_t h, int64_t color)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return;

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    for (int64_t py = y; py < y + h; py++)
        for (int64_t px = x; px < x + w; px++)
        {
            if (px >= 0 && px < fb.width && py >= 0 && py < fb.height)
            {
                uint8_t *dst = &fb.pixels[py * fb.stride + px * 4];
                dst[0] = cr; dst[1] = cg; dst[2] = cb; dst[3] = 0xFF;
            }
        }
}

void rt_canvas3d_draw_crosshair(void *obj, int64_t color, int64_t size)
{
    if (!obj) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return;

    int32_t cx = fb.width / 2, cy = fb.height / 2;
    int32_t half = (int32_t)(size / 2);
    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    /* Horizontal line */
    for (int32_t dx = -half; dx <= half; dx++)
    {
        int32_t px = cx + dx;
        if (px >= 0 && px < fb.width && cy >= 0 && cy < fb.height)
        {
            uint8_t *dst = &fb.pixels[cy * fb.stride + px * 4];
            dst[0] = cr; dst[1] = cg; dst[2] = cb; dst[3] = 0xFF;
        }
    }
    /* Vertical line */
    for (int32_t dy = -half; dy <= half; dy++)
    {
        int32_t py = cy + dy;
        if (cx >= 0 && cx < fb.width && py >= 0 && py < fb.height)
        {
            uint8_t *dst = &fb.pixels[py * fb.stride + cx * 4];
            dst[0] = cr; dst[1] = cg; dst[2] = cb; dst[3] = 0xFF;
        }
    }
}

void rt_canvas3d_draw_text2d(void *obj, int64_t x, int64_t y,
                               rt_string text, int64_t color)
{
    if (!obj || !text) return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win) return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb)) return;

    const char *str = rt_string_cstr(text);
    if (!str) return;

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    /* Minimal 5x7 bitmap font for ASCII 32-126 */
    static const uint8_t font5x7[95][7] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
        {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
        {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
        {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, /* # */
        {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
        {0x19,0x1A,0x04,0x0B,0x13,0x00,0x00}, /* % */
        {0x08,0x14,0x08,0x15,0x12,0x0D,0x00}, /* & */
        {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
        {0x02,0x04,0x04,0x04,0x04,0x02,0x00}, /* ( */
        {0x08,0x04,0x04,0x04,0x04,0x08,0x00}, /* ) */
        {0x04,0x15,0x0E,0x15,0x04,0x00,0x00}, /* * */
        {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
        {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, /* , */
        {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
        {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, /* . */
        {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, /* / */
        {0x0E,0x11,0x13,0x15,0x19,0x0E,0x00}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x04,0x0E,0x00}, /* 1 */
        {0x0E,0x11,0x01,0x06,0x08,0x1F,0x00}, /* 2 */
        {0x0E,0x11,0x02,0x01,0x11,0x0E,0x00}, /* 3 */
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x00}, /* 4 */
        {0x1F,0x10,0x1E,0x01,0x11,0x0E,0x00}, /* 5 */
        {0x06,0x08,0x1E,0x11,0x11,0x0E,0x00}, /* 6 */
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x00}, /* 7 */
        {0x0E,0x11,0x0E,0x11,0x11,0x0E,0x00}, /* 8 */
        {0x0E,0x11,0x0F,0x01,0x02,0x0C,0x00}, /* 9 */
        {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
        {0x00,0x04,0x00,0x00,0x04,0x04,0x08}, /* ; */
        {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, /* < */
        {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
        {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, /* > */
        {0x0E,0x11,0x02,0x04,0x00,0x04,0x00}, /* ? */
        {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x00}, /* A */
        {0x1E,0x11,0x1E,0x11,0x11,0x1E,0x00}, /* B */
        {0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, /* C */
        {0x1E,0x11,0x11,0x11,0x11,0x1E,0x00}, /* D */
        {0x1F,0x10,0x1E,0x10,0x10,0x1F,0x00}, /* E */
        {0x1F,0x10,0x1E,0x10,0x10,0x10,0x00}, /* F */
        {0x0E,0x11,0x10,0x17,0x11,0x0E,0x00}, /* G */
        {0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, /* H */
        {0x0E,0x04,0x04,0x04,0x04,0x0E,0x00}, /* I */
        {0x07,0x02,0x02,0x02,0x12,0x0C,0x00}, /* J */
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, /* L */
        {0x11,0x1B,0x15,0x11,0x11,0x11,0x00}, /* M */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x00}, /* N */
        {0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, /* O */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x00}, /* P */
        {0x0E,0x11,0x11,0x15,0x12,0x0D,0x00}, /* Q */
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x00}, /* R */
        {0x0E,0x10,0x0E,0x01,0x11,0x0E,0x00}, /* S */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x00}, /* T */
        {0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, /* U */
        {0x11,0x11,0x11,0x0A,0x0A,0x04,0x00}, /* V */
        {0x11,0x11,0x11,0x15,0x1B,0x11,0x00}, /* W */
        {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00}, /* X */
        {0x11,0x0A,0x04,0x04,0x04,0x04,0x00}, /* Y */
        {0x1F,0x02,0x04,0x08,0x10,0x1F,0x00}, /* Z */
        {0x0E,0x08,0x08,0x08,0x08,0x0E,0x00}, /* [ */
        {0x10,0x08,0x04,0x02,0x01,0x00,0x00}, /* \ */
        {0x0E,0x02,0x02,0x02,0x02,0x0E,0x00}, /* ] */
        {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
        {0x00,0x00,0x00,0x00,0x00,0x1F,0x00}, /* _ */
        {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
        {0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00}, /* a */
        {0x10,0x10,0x1E,0x11,0x11,0x1E,0x00}, /* b */
        {0x00,0x0E,0x10,0x10,0x10,0x0E,0x00}, /* c */
        {0x01,0x01,0x0F,0x11,0x11,0x0F,0x00}, /* d */
        {0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00}, /* e */
        {0x06,0x08,0x1C,0x08,0x08,0x08,0x00}, /* f */
        {0x00,0x0F,0x11,0x0F,0x01,0x0E,0x00}, /* g */
        {0x10,0x10,0x1E,0x11,0x11,0x11,0x00}, /* h */
        {0x04,0x00,0x0C,0x04,0x04,0x0E,0x00}, /* i */
        {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
        {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
        {0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}, /* l */
        {0x00,0x1A,0x15,0x15,0x15,0x15,0x00}, /* m */
        {0x00,0x1E,0x11,0x11,0x11,0x11,0x00}, /* n */
        {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}, /* o */
        {0x00,0x1E,0x11,0x1E,0x10,0x10,0x00}, /* p */
        {0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, /* q */
        {0x00,0x16,0x19,0x10,0x10,0x10,0x00}, /* r */
        {0x00,0x0F,0x10,0x0E,0x01,0x1E,0x00}, /* s */
        {0x08,0x1C,0x08,0x08,0x08,0x06,0x00}, /* t */
        {0x00,0x11,0x11,0x11,0x11,0x0F,0x00}, /* u */
        {0x00,0x11,0x11,0x0A,0x0A,0x04,0x00}, /* v */
        {0x00,0x11,0x11,0x15,0x15,0x0A,0x00}, /* w */
        {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, /* x */
        {0x00,0x11,0x11,0x0F,0x01,0x0E,0x00}, /* y */
        {0x00,0x1F,0x02,0x04,0x08,0x1F,0x00}, /* z */
        {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, /* { */
        {0x04,0x04,0x04,0x04,0x04,0x04,0x00}, /* | */
        {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, /* } */
        {0x00,0x00,0x0A,0x15,0x00,0x00,0x00}, /* ~ */
    };

    int64_t cx = x;
    for (const char *p = str; *p; p++)
    {
        int ch = (int)(unsigned char)*p;
        if (ch < 32 || ch > 126) { cx += 6; continue; }
        const uint8_t *glyph = font5x7[ch - 32];

        for (int row = 0; row < 7; row++)
        {
            for (int col = 0; col < 5; col++)
            {
                if (glyph[row] & (1 << (4 - col)))
                {
                    int64_t px = cx + col, py = y + row;
                    if (px >= 0 && px < fb.width && py >= 0 && py < fb.height)
                    {
                        uint8_t *dst = &fb.pixels[py * fb.stride + px * 4];
                        dst[0] = cr; dst[1] = cg; dst[2] = cb; dst[3] = 0xFF;
                    }
                }
            }
        }
        cx += 6; /* 5px glyph + 1px spacing */
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
