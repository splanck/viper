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
#include "rt_string.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int canvas3d_backend_uses_gpu_postfx(const rt_canvas3d *c) {
    return c && c->backend && c->backend->present_postfx && c->render_target == NULL;
}

static int canvas3d_backend_owns_gpu_rtt(const rt_canvas3d *c) {
    return c && c->render_target && c->backend && c->backend != &vgfx3d_software_backend;
}

static void rt_canvas3d_apply_resize(rt_canvas3d *c, int32_t w, int32_t h) {
    if (!c || w <= 0 || h <= 0)
        return;
    if (c->width == w && c->height == h)
        return;
    c->width = w;
    c->height = h;
    if (c->backend && c->backend->resize)
        c->backend->resize(c->backend_ctx, w, h);
}

static void rt_canvas3d_on_resize(void *userdata, int32_t w, int32_t h) {
    rt_canvas3d_apply_resize((rt_canvas3d *)userdata, w, h);
}

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
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
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_pixels_new(int64_t width, int64_t height);
extern void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);

/*==========================================================================
 * Deferred draw command (for transparency sorting)
 *=========================================================================*/

typedef enum {
    DEFERRED_DRAW_MESH = 0,
    DEFERRED_DRAW_INSTANCED = 1,
} deferred_draw_kind_t;

typedef enum {
    DEFERRED_PASS_MAIN = 0,
    DEFERRED_PASS_SCREEN_OVERLAY = 1,
} deferred_pass_t;

typedef struct {
    deferred_draw_kind_t kind;
    deferred_pass_t pass_kind;
    vgfx3d_draw_cmd_t cmd;
    const float *instance_matrices; /* row-major float[instance_count * 16] */
    int32_t instance_count;
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count;
    float ambient[3];
    int8_t wireframe;
    int8_t backface_cull;
    int8_t has_local_bounds;
    float local_bounds_min[3];
    float local_bounds_max[3];
    float sort_key; /* squared distance from camera (for transparent sorting) */
} deferred_draw_t;

typedef struct {
    const void *key;
    float current_model[16];
    float prev_model[16];
    int64_t last_frame_seen;
    int8_t has_current;
    int8_t has_prev;
} canvas_motion_history_t;

static int ensure_deferred_capacity(void **buf, int32_t *capacity, int32_t needed) {
    if (!buf || !capacity || needed <= 0)
        return 0;
    if (*capacity >= needed)
        return 1;

    int32_t new_cap = *capacity > 0 ? *capacity : 32;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            new_cap = needed;
        else
            new_cap *= 2;
    }

    deferred_draw_t *new_buf =
        (deferred_draw_t *)realloc(*buf, (size_t)new_cap * sizeof(deferred_draw_t));
    if (!new_buf)
        return 0;
    *buf = new_buf;
    *capacity = new_cap;
    return 1;
}

static int ensure_text_capacity(rt_canvas3d *c, int32_t vertex_count, int32_t index_count) {
    if (!c || vertex_count < 0 || index_count < 0)
        return 0;

    if (vertex_count > c->text_vertex_capacity) {
        int32_t new_cap = c->text_vertex_capacity > 0 ? c->text_vertex_capacity : 256;
        while (new_cap < vertex_count) {
            if (new_cap > INT32_MAX / 2)
                new_cap = vertex_count;
            else
                new_cap *= 2;
        }
        vgfx3d_vertex_t *new_verts =
            (vgfx3d_vertex_t *)realloc(c->text_vertices, (size_t)new_cap * sizeof(vgfx3d_vertex_t));
        if (!new_verts)
            return 0;
        c->text_vertices = new_verts;
        c->text_vertex_capacity = new_cap;
    }

    if (index_count > c->text_index_capacity) {
        int32_t new_cap = c->text_index_capacity > 0 ? c->text_index_capacity : 384;
        while (new_cap < index_count) {
            if (new_cap > INT32_MAX / 2)
                new_cap = index_count;
            else
                new_cap *= 2;
        }
        uint32_t *new_indices =
            (uint32_t *)realloc(c->text_indices, (size_t)new_cap * sizeof(uint32_t));
        if (!new_indices)
            return 0;
        c->text_indices = new_indices;
        c->text_index_capacity = new_cap;
    }

    return 1;
}

/* Comparison for qsort: back-to-front (descending sort_key) */
static int cmp_back_to_front(const void *a, const void *b) {
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka > kb)
        return -1;
    if (ka < kb)
        return 1;
    return 0;
}

static int cmp_front_to_back(const void *a, const void *b) {
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

static void mat4_d2f(const double *src, float *dst) {
    for (int i = 0; i < 16; i++)
        dst[i] = (float)src[i];
}

static int ensure_motion_history_capacity(rt_canvas3d *c, int32_t needed) {
    if (!c || needed <= 0)
        return 0;
    if (c->motion_history_capacity >= needed)
        return 1;

    int32_t new_cap = c->motion_history_capacity > 0 ? c->motion_history_capacity : 32;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            new_cap = needed;
        else
            new_cap *= 2;
    }

    canvas_motion_history_t *new_hist = (canvas_motion_history_t *)realloc(
        c->motion_history, (size_t)new_cap * sizeof(canvas_motion_history_t));
    if (!new_hist)
        return 0;
    c->motion_history = new_hist;
    c->motion_history_capacity = new_cap;
    return 1;
}

static void canvas3d_prune_motion_history(rt_canvas3d *c) {
    if (!c || c->motion_history_count <= 0)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t dst = 0;
    for (int32_t i = 0; i < c->motion_history_count; i++) {
        if (c->frame_serial - hist[i].last_frame_seen > 1)
            continue;
        if (dst != i)
            hist[dst] = hist[i];
        dst++;
    }
    c->motion_history_count = dst;
}

static void canvas3d_resolve_previous_model(rt_canvas3d *c,
                                            const void *motion_key,
                                            const float *current_model,
                                            float *out_prev_model,
                                            int8_t *out_has_prev) {
    if (out_has_prev)
        *out_has_prev = 0;
    if (out_prev_model)
        memset(out_prev_model, 0, sizeof(float) * 16);
    if (!c || !motion_key || !current_model || !out_prev_model || !out_has_prev)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    for (int32_t i = 0; i < c->motion_history_count; i++) {
        if (hist[i].key != motion_key)
            continue;

        if (hist[i].last_frame_seen != c->frame_serial) {
            if (hist[i].has_current) {
                memcpy(hist[i].prev_model, hist[i].current_model, sizeof(hist[i].prev_model));
                hist[i].has_prev = 1;
            }
            memcpy(hist[i].current_model, current_model, sizeof(hist[i].current_model));
            hist[i].has_current = 1;
            hist[i].last_frame_seen = c->frame_serial;
        }

        if (hist[i].has_prev) {
            memcpy(out_prev_model, hist[i].prev_model, sizeof(hist[i].prev_model));
            *out_has_prev = 1;
        }
        return;
    }

    if (!ensure_motion_history_capacity(c, c->motion_history_count + 1))
        return;

    hist = (canvas_motion_history_t *)c->motion_history;
    canvas_motion_history_t *entry = &hist[c->motion_history_count++];
    memset(entry, 0, sizeof(*entry));
    entry->key = motion_key;
    memcpy(entry->current_model, current_model, sizeof(entry->current_model));
    entry->has_current = 1;
    entry->last_frame_seen = c->frame_serial;
}

/* Build light params array from canvas light pointers */
static int32_t build_light_params(const rt_canvas3d *c, vgfx3d_light_params_t *out, int32_t max) {
    int32_t count = 0;
    for (int i = 0; i < VGFX3D_MAX_LIGHTS && count < max; i++) {
        const rt_light3d *l = c->lights[i];
        if (!l)
            continue;
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
        out[count].inner_cos = (float)l->inner_cos;
        out[count].outer_cos = (float)l->outer_cos;
        count++;
    }
    return count;
}

static int canvas3d_track_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    if (c->temp_buf_count >= c->temp_buf_capacity) {
        int32_t new_cap = c->temp_buf_capacity == 0 ? 8 : c->temp_buf_capacity * 2;
        void **nb = (void **)realloc(c->temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb) {
            free(buffer);
            return 0;
        }
        c->temp_buffers = nb;
        c->temp_buf_capacity = new_cap;
    }
    c->temp_buffers[c->temp_buf_count++] = buffer;
    return 1;
}

static int canvas3d_track_temp_object(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return 0;
    if (c->temp_obj_count >= c->temp_obj_capacity) {
        int32_t new_cap = c->temp_obj_capacity == 0 ? 8 : c->temp_obj_capacity * 2;
        void **nb = (void **)realloc(c->temp_objects, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->temp_objects = nb;
        c->temp_obj_capacity = new_cap;
    }
    rt_obj_retain_maybe(obj);
    c->temp_objects[c->temp_obj_count++] = obj;
    return 1;
}

static void canvas3d_clear_temp_buffers(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
}

static void canvas3d_clear_temp_objects(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; i++) {
        if (c->temp_objects[i] && rt_obj_release_check0(c->temp_objects[i]))
            rt_obj_free(c->temp_objects[i]);
    }
    c->temp_obj_count = 0;
}

static float canvas3d_compute_sort_key(const rt_canvas3d *c, const float *model_matrix) {
    float cx;
    float cy;
    float cz;
    float dx;
    float dy;
    float dz;

    if (!c || !model_matrix)
        return 0.0f;
    cx = model_matrix[3];
    cy = model_matrix[7];
    cz = model_matrix[11];
    dx = cx - c->cached_cam_pos[0];
    dy = cy - c->cached_cam_pos[1];
    dz = cz - c->cached_cam_pos[2];
    return dx * dx + dy * dy + dz * dz;
}

static void canvas3d_build_ortho_camera(const rt_canvas3d *c, vgfx3d_camera_params_t *params) {
    float w;
    float h;

    if (!c || !params)
        return;
    memset(params, 0, sizeof(*params));
    w = (float)c->width + 2.0f;
    h = (float)c->height + 2.0f;
    params->projection[0] = 2.0f / w;
    params->projection[5] = -2.0f / h;
    params->projection[10] = -1.0f;
    params->projection[3] = -1.0f + 2.0f / w;
    params->projection[7] = 1.0f - 2.0f / h;
    params->projection[15] = 1.0f;
    params->view[0] = params->view[5] = params->view[10] = params->view[15] = 1.0f;
    params->position[2] = 1.0f;
    params->fog_enabled = 0;
}

static int canvas3d_begin_overlay_frame(rt_canvas3d *c, int8_t preserve_existing_color) {
    vgfx3d_camera_params_t params;

    if (!c || !c->backend || !c->gfx_win || c->in_frame)
        return 0;
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);
    canvas3d_build_ortho_camera(c, &params);
    params.load_existing_color = preserve_existing_color ? 1 : 0;
    params.load_existing_depth = 0;
    c->cached_cam_pos[0] = 0.0f;
    c->cached_cam_pos[1] = 0.0f;
    c->cached_cam_pos[2] = 1.0f;
    c->draw_count = 0;
    c->frame_is_2d = 1;
    memcpy(c->cached_vp, params.projection, sizeof(c->cached_vp));
    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
    return 1;
}

static const float *canvas3d_active_scene_vp(const rt_canvas3d *c) {
    if (!c)
        return NULL;
    if (c->in_frame && !c->frame_is_2d)
        return c->cached_vp;
    if (c->has_last_scene_vp)
        return c->last_scene_vp;
    if (c->in_frame)
        return c->cached_vp;
    return NULL;
}

static void canvas3d_submit_mesh(rt_canvas3d *c,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 const vgfx3d_light_params_t *lights,
                                 int32_t light_count,
                                 const float *ambient,
                                 int8_t wireframe,
                                 int8_t backface_cull) {
    if (!c || !c->backend || !cmd)
        return;
    c->backend->submit_draw(
        c->backend_ctx, c->gfx_win, cmd, lights, light_count, ambient, wireframe, backface_cull);
}

static void canvas3d_submit_instanced_as_meshes(rt_canvas3d *c,
                                                const deferred_draw_t *dd,
                                                int shadow_only) {
    if (!c || !dd || !dd->instance_matrices || dd->instance_count <= 0)
        return;
    for (int32_t i = 0; i < dd->instance_count; i++) {
        vgfx3d_draw_cmd_t per_instance = dd->cmd;
        memcpy(per_instance.model_matrix,
               &dd->instance_matrices[(size_t)i * 16u],
               sizeof(per_instance.model_matrix));
        if (dd->cmd.has_prev_instance_matrices && dd->cmd.prev_instance_matrices) {
            memcpy(per_instance.prev_model_matrix,
                   &dd->cmd.prev_instance_matrices[(size_t)i * 16u],
                   sizeof(per_instance.prev_model_matrix));
            per_instance.has_prev_model_matrix = 1;
        } else {
            memcpy(per_instance.prev_model_matrix,
                   per_instance.model_matrix,
                   sizeof(per_instance.prev_model_matrix));
            per_instance.has_prev_model_matrix = 0;
        }
        if (shadow_only) {
            if (c->backend->shadow_draw)
                c->backend->shadow_draw(c->backend_ctx, &per_instance);
        } else {
            canvas3d_submit_mesh(c,
                                 &per_instance,
                                 dd->lights,
                                 dd->light_count,
                                 dd->ambient,
                                 dd->wireframe,
                                 dd->backface_cull);
        }
    }
}

static int canvas3d_enqueue_draw(rt_canvas3d *c,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 deferred_draw_kind_t kind,
                                 deferred_pass_t pass_kind,
                                 const float *instance_matrices,
                                 int32_t instance_count,
                                 int include_lights,
                                 int8_t wireframe,
                                 int8_t backface_cull,
                                 float sort_key,
                                 const float *local_bounds_min,
                                 const float *local_bounds_max) {
    deferred_draw_t *dd;

    if (!c || !cmd)
        return 0;
    if (!ensure_deferred_capacity(&c->draw_cmds, &c->draw_capacity, c->draw_count + 1))
        return 0;

    dd = &((deferred_draw_t *)c->draw_cmds)[c->draw_count++];
    memset(dd, 0, sizeof(*dd));
    dd->kind = kind;
    dd->pass_kind = pass_kind;
    dd->cmd = *cmd;
    dd->instance_matrices = instance_matrices;
    dd->instance_count = instance_count;
    dd->sort_key = sort_key;
    dd->wireframe = wireframe;
    dd->backface_cull = backface_cull;
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->light_count = include_lights ? build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS) : 0;
    if (local_bounds_min && local_bounds_max) {
        dd->has_local_bounds = 1;
        memcpy(dd->local_bounds_min, local_bounds_min, sizeof(dd->local_bounds_min));
        memcpy(dd->local_bounds_max, local_bounds_max, sizeof(dd->local_bounds_max));
    }
    return 1;
}

static void canvas3d_submit_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        if (c->backend->submit_draw_instanced && dd->instance_count > 0) {
            c->backend->submit_draw_instanced(c->backend_ctx,
                                              c->gfx_win,
                                              &dd->cmd,
                                              dd->instance_matrices,
                                              dd->instance_count,
                                              dd->lights,
                                              dd->light_count,
                                              dd->ambient,
                                              dd->wireframe,
                                              dd->backface_cull);
            return;
        }
        canvas3d_submit_instanced_as_meshes(c, dd, 0);
        return;
    }
    canvas3d_submit_mesh(
        c, &dd->cmd, dd->lights, dd->light_count, dd->ambient, dd->wireframe, dd->backface_cull);
}

static void canvas3d_shadow_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd || !c->backend || !c->backend->shadow_draw)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        canvas3d_submit_instanced_as_meshes(c, dd, 1);
        return;
    }
    c->backend->shadow_draw(c->backend_ctx, &dd->cmd);
}

static void canvas3d_expand_bounds(float *io_min, float *io_max, const float *mn, const float *mx) {
    if (!io_min || !io_max || !mn || !mx)
        return;
    for (int i = 0; i < 3; i++) {
        if (mn[i] < io_min[i])
            io_min[i] = mn[i];
        if (mx[i] > io_max[i])
            io_max[i] = mx[i];
    }
}

static void canvas3d_mul_mat4(const float *a, const float *b, float *out) {
    if (!a || !b || !out)
        return;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

static void canvas3d_accumulate_deferred_world_bounds(const deferred_draw_t *dd,
                                                      float *io_min,
                                                      float *io_max,
                                                      int8_t *io_has_bounds) {
    if (!dd || !io_min || !io_max || !io_has_bounds || !dd->has_local_bounds)
        return;

    if (dd->kind == DEFERRED_DRAW_INSTANCED && dd->instance_matrices && dd->instance_count > 0) {
        for (int32_t i = 0; i < dd->instance_count; i++) {
            double world_matrix[16];
            float world_min[3];
            float world_max[3];
            for (int j = 0; j < 16; j++)
                world_matrix[j] = (double)dd->instance_matrices[(size_t)i * 16u + (size_t)j];
            vgfx3d_transform_aabb(
                dd->local_bounds_min, dd->local_bounds_max, world_matrix, world_min, world_max);
            if (!*io_has_bounds) {
                memcpy(io_min, world_min, sizeof(float) * 3);
                memcpy(io_max, world_max, sizeof(float) * 3);
                *io_has_bounds = 1;
            } else {
                canvas3d_expand_bounds(io_min, io_max, world_min, world_max);
            }
        }
        return;
    }

    {
        double world_matrix[16];
        float world_min[3];
        float world_max[3];
        for (int j = 0; j < 16; j++)
            world_matrix[j] = (double)dd->cmd.model_matrix[j];
        vgfx3d_transform_aabb(
            dd->local_bounds_min, dd->local_bounds_max, world_matrix, world_min, world_max);
        if (!*io_has_bounds) {
            memcpy(io_min, world_min, sizeof(float) * 3);
            memcpy(io_max, world_max, sizeof(float) * 3);
            *io_has_bounds = 1;
        } else {
            canvas3d_expand_bounds(io_min, io_max, world_min, world_max);
        }
    }
}

static int canvas3d_build_shadow_light_vp(const deferred_draw_t *cmds,
                                          int32_t count,
                                          const vgfx3d_light_params_t *dir_light,
                                          float *out_light_vp) {
    float world_min[3] = {0.0f, 0.0f, 0.0f};
    float world_max[3] = {0.0f, 0.0f, 0.0f};
    int8_t has_bounds = 0;
    float center[3];
    float ldir[3];
    float eye[3];
    float fwd[3];
    float up[3] = {0.0f, 1.0f, 0.0f};
    float view[16];
    float proj[16];
    float corners[8][3];
    float ls_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float ls_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    if (!cmds || count <= 0 || !dir_light || !out_light_vp)
        return 0;

    for (int32_t i = 0; i < count; i++) {
        if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || cmds[i].cmd.alpha < 1.0f)
            continue;
        canvas3d_accumulate_deferred_world_bounds(&cmds[i], world_min, world_max, &has_bounds);
    }
    if (!has_bounds)
        return 0;

    center[0] = 0.5f * (world_min[0] + world_max[0]);
    center[1] = 0.5f * (world_min[1] + world_max[1]);
    center[2] = 0.5f * (world_min[2] + world_max[2]);

    ldir[0] = dir_light->direction[0];
    ldir[1] = dir_light->direction[1];
    ldir[2] = dir_light->direction[2];
    {
        float ll = sqrtf(ldir[0] * ldir[0] + ldir[1] * ldir[1] + ldir[2] * ldir[2]);
        if (ll > 1e-7f) {
            ldir[0] /= ll;
            ldir[1] /= ll;
            ldir[2] /= ll;
        } else {
            ldir[0] = 0.0f;
            ldir[1] = -1.0f;
            ldir[2] = 0.0f;
        }
    }

    {
        float dx = world_max[0] - world_min[0];
        float dy = world_max[1] - world_min[1];
        float dz = world_max[2] - world_min[2];
        float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
        if (radius < 1.0f)
            radius = 1.0f;
        eye[0] = center[0] - ldir[0] * (radius * 2.0f + 4.0f);
        eye[1] = center[1] - ldir[1] * (radius * 2.0f + 4.0f);
        eye[2] = center[2] - ldir[2] * (radius * 2.0f + 4.0f);
    }

    fwd[0] = center[0] - eye[0];
    fwd[1] = center[1] - eye[1];
    fwd[2] = center[2] - eye[2];
    {
        float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        float rx;
        float ry;
        float rz;
        float rl;
        float ux;
        float uy;
        float uz;

        if (fl > 1e-7f) {
            fwd[0] /= fl;
            fwd[1] /= fl;
            fwd[2] /= fl;
        } else {
            fwd[0] = 0.0f;
            fwd[1] = 0.0f;
            fwd[2] = -1.0f;
        }
        if (fabsf(fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2]) > 0.99f) {
            up[0] = 0.0f;
            up[1] = 0.0f;
            up[2] = 1.0f;
        }

        rx = fwd[1] * up[2] - fwd[2] * up[1];
        ry = fwd[2] * up[0] - fwd[0] * up[2];
        rz = fwd[0] * up[1] - fwd[1] * up[0];
        rl = sqrtf(rx * rx + ry * ry + rz * rz);
        if (rl > 1e-7f) {
            rx /= rl;
            ry /= rl;
            rz /= rl;
        } else {
            rx = 1.0f;
            ry = rz = 0.0f;
        }

        ux = ry * fwd[2] - rz * fwd[1];
        uy = rz * fwd[0] - rx * fwd[2];
        uz = rx * fwd[1] - ry * fwd[0];

        view[0] = rx;
        view[1] = ry;
        view[2] = rz;
        view[3] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
        view[4] = ux;
        view[5] = uy;
        view[6] = uz;
        view[7] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
        view[8] = fwd[0];
        view[9] = fwd[1];
        view[10] = fwd[2];
        view[11] = -(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
        view[12] = 0.0f;
        view[13] = 0.0f;
        view[14] = 0.0f;
        view[15] = 1.0f;
    }

    corners[0][0] = world_min[0];
    corners[0][1] = world_min[1];
    corners[0][2] = world_min[2];
    corners[1][0] = world_max[0];
    corners[1][1] = world_min[1];
    corners[1][2] = world_min[2];
    corners[2][0] = world_min[0];
    corners[2][1] = world_max[1];
    corners[2][2] = world_min[2];
    corners[3][0] = world_max[0];
    corners[3][1] = world_max[1];
    corners[3][2] = world_min[2];
    corners[4][0] = world_min[0];
    corners[4][1] = world_min[1];
    corners[4][2] = world_max[2];
    corners[5][0] = world_max[0];
    corners[5][1] = world_min[1];
    corners[5][2] = world_max[2];
    corners[6][0] = world_min[0];
    corners[6][1] = world_max[1];
    corners[6][2] = world_max[2];
    corners[7][0] = world_max[0];
    corners[7][1] = world_max[1];
    corners[7][2] = world_max[2];

    for (int i = 0; i < 8; i++) {
        float x = corners[i][0];
        float y = corners[i][1];
        float z = corners[i][2];
        float lx = view[0] * x + view[1] * y + view[2] * z + view[3];
        float ly = view[4] * x + view[5] * y + view[6] * z + view[7];
        float lz = view[8] * x + view[9] * y + view[10] * z + view[11];
        if (lx < ls_min[0])
            ls_min[0] = lx;
        if (ly < ls_min[1])
            ls_min[1] = ly;
        if (lz < ls_min[2])
            ls_min[2] = lz;
        if (lx > ls_max[0])
            ls_max[0] = lx;
        if (ly > ls_max[1])
            ls_max[1] = ly;
        if (lz > ls_max[2])
            ls_max[2] = lz;
    }

    {
        float pad_x = (ls_max[0] - ls_min[0]) * 0.05f + 1.0f;
        float pad_y = (ls_max[1] - ls_min[1]) * 0.05f + 1.0f;
        float pad_z = (ls_max[2] - ls_min[2]) * 0.10f + 2.0f;
        float left = ls_min[0] - pad_x;
        float right = ls_max[0] + pad_x;
        float bottom = ls_min[1] - pad_y;
        float top = ls_max[1] + pad_y;
        float near_z = ls_min[2] - pad_z;
        float far_z = ls_max[2] + pad_z;

        if (right - left < 1e-4f || top - bottom < 1e-4f || far_z - near_z < 1e-4f)
            return 0;

        memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (right - left);
        proj[3] = -(right + left) / (right - left);
        proj[5] = 2.0f / (top - bottom);
        proj[7] = -(top + bottom) / (top - bottom);
        proj[10] = -2.0f / (far_z - near_z);
        proj[11] = -(far_z + near_z) / (far_z - near_z);
        proj[15] = 1.0f;
    }

    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/*==========================================================================
 * Canvas3D lifecycle
 *=========================================================================*/

static void rt_canvas3d_finalize(void *obj) {
    rt_canvas3d *c = (rt_canvas3d *)obj;
    /* Destroy the backend context */
    if (c->backend && c->backend_ctx) {
        c->backend->destroy_ctx(c->backend_ctx);
        c->backend_ctx = NULL;
    }
    /* Free deferred draw command buffer */
    free(c->draw_cmds);
    c->draw_cmds = NULL;
    c->draw_count = c->draw_capacity = 0;
    free(c->trans_cmds);
    c->trans_cmds = NULL;
    c->trans_capacity = 0;
    free(c->motion_history);
    c->motion_history = NULL;
    c->motion_history_count = c->motion_history_capacity = 0;
    /* Free any leftover temp buffers (e.g., from skinned draws) */
    canvas3d_clear_temp_buffers(c);
    free(c->temp_buffers);
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;
    canvas3d_clear_temp_objects(c);
    free(c->temp_objects);
    c->temp_objects = NULL;
    c->temp_obj_count = c->temp_obj_capacity = 0;
    free(c->text_vertices);
    c->text_vertices = NULL;
    c->text_vertex_capacity = 0;
    free(c->text_indices);
    c->text_indices = NULL;
    c->text_index_capacity = 0;

    /* Free shadow render target if allocated */
    if (c->shadow_rt) {
        free(c->shadow_rt->color_buf);
        free(c->shadow_rt->depth_buf);
        free(c->shadow_rt);
        c->shadow_rt = NULL;
    }

    if (c->skybox) {
        if (rt_obj_release_check0(c->skybox))
            rt_obj_free(c->skybox);
        c->skybox = NULL;
    }

    if (c->gfx_win) {
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
    }
}

/// @brief Create a new 3D rendering canvas (window + backend context).
/// @details Opens a platform window, selects the best available rendering backend
///          (Metal > OpenGL > D3D11 > software), and initializes the framebuffer,
///          depth buffer, deferred draw queue, and motion blur history. The canvas
///          is the main entry point for 3D rendering — call Begin/DrawMesh/End/Flip
///          each frame. GC finalizer destroys the backend context and window.
/// @param title Window title (runtime string).
/// @param w     Window width in pixels (1–8192).
/// @param h     Window height in pixels (1–8192).
/// @return Opaque canvas handle, or NULL on failure.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        rt_trap("Canvas3D.New: dimensions must be 1-8192");
        return NULL;
    }

    rt_canvas3d *c = (rt_canvas3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_canvas3d));
    if (!c) {
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
    if (!c->gfx_win) {
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
    if (!c->backend_ctx) {
        /* GPU backend failed — fall back to software */
        c->backend = &vgfx3d_software_backend;
        c->backend_ctx = c->backend->create_ctx(c->gfx_win, (int32_t)w, (int32_t)h);
        if (!c->backend_ctx) {
            rt_trap("Canvas3D.New: backend initialization failed");
            return NULL;
        }
    }

    vgfx_set_resize_callback(c->gfx_win, rt_canvas3d_on_resize, c);

    c->ambient[0] = 0.1f;
    c->ambient[1] = 0.1f;
    c->ambient[2] = 0.1f;
    c->backface_cull = 0; /* disabled by default — extreme perspective can reverse
                           * screen-space winding, causing false culling. Users can
                           * enable with SetBackfaceCull(canvas, true) if needed. */
    c->postfx = NULL;
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;
    c->temp_objects = NULL;
    c->temp_obj_count = c->temp_obj_capacity = 0;
    c->fog_enabled = 0;
    c->fog_near = 10.0f;
    c->fog_far = 50.0f;
    c->fog_color[0] = c->fog_color[1] = c->fog_color[2] = 0.5f;
    c->shadows_enabled = 0;
    c->shadow_resolution = 1024;
    c->shadow_bias = 0.005f;
    c->shadow_rt = NULL;
    c->frame_serial = 0;
    c->motion_history = NULL;
    c->motion_history_count = 0;
    c->motion_history_capacity = 0;
    c->frame_is_2d = 0;
    c->has_last_scene_vp = 0;

    rt_keyboard_set_canvas(c->gfx_win);
    rt_mouse_set_canvas(c->gfx_win);
    rt_pad_init();

    return c;
}

/*==========================================================================
 * Rendering — dispatches through backend vtable
 *=========================================================================*/

/// @brief Clear the framebuffer and depth buffer with the given background color.
/// @details Must be called at the start of each frame before Begin. Also resets
///          fog state and ambient light to defaults for the new frame.
void rt_canvas3d_clear(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win || !c->backend)
        return;
    c->backend->clear(c->backend_ctx, c->gfx_win, (float)r, (float)g, (float)b);

    /* Also clear the software framebuffer so 2D overlay functions
     * (DrawText2D, DrawRect2D, DrawCrosshair, Screenshot) have correct
     * background content regardless of active backend. Uses memset for
     * stride-aligned rows instead of per-pixel loop (4x faster at 1080p). */
    if (c->backend != &vgfx3d_software_backend && !c->render_target) {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(c->gfx_win, &fb)) {
            uint32_t rgba = ((uint32_t)(uint8_t)((float)r * 255.0f)) |
                            ((uint32_t)(uint8_t)((float)g * 255.0f) << 8) |
                            ((uint32_t)(uint8_t)((float)b * 255.0f) << 16) |
                            0xFF000000u;
            uint32_t *row = (uint32_t *)fb.pixels;
            int32_t row_stride = fb.stride / 4;
            for (int32_t y = 0; y < fb.height; y++) {
                for (int32_t x = 0; x < fb.width; x++)
                    row[x] = rgba;
                row += row_stride;
            }
        }
    }
}

static deferred_pass_t canvas3d_screen_pass_kind(const rt_canvas3d *c) {
    return (c && c->frame_is_2d) ? DEFERRED_PASS_MAIN : DEFERRED_PASS_SCREEN_OVERLAY;
}

static int canvas3d_queue_screen_geometry(rt_canvas3d *c,
                                          const vgfx3d_vertex_t *vertices,
                                          int32_t vertex_count,
                                          const uint32_t *indices,
                                          int32_t index_count,
                                          float r,
                                          float g,
                                          float b,
                                          float a) {
    size_t vertex_bytes;
    size_t index_bytes;
    uint8_t *block;
    vgfx3d_vertex_t *verts_copy;
    uint32_t *indices_copy;
    vgfx3d_draw_cmd_t cmd;

    if (!c || !c->in_frame || !vertices || vertex_count <= 0 || !indices || index_count <= 0)
        return 0;
    vertex_bytes = (size_t)vertex_count * sizeof(vgfx3d_vertex_t);
    index_bytes = (size_t)index_count * sizeof(uint32_t);
    block = (uint8_t *)malloc(vertex_bytes + index_bytes);
    if (!block)
        return 0;
    verts_copy = (vgfx3d_vertex_t *)block;
    indices_copy = (uint32_t *)(block + vertex_bytes);
    memcpy(verts_copy, vertices, vertex_bytes);
    memcpy(indices_copy, indices, index_bytes);
    if (!canvas3d_track_temp_buffer(c, block))
        return 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.vertices = verts_copy;
    cmd.vertex_count = (uint32_t)vertex_count;
    cmd.indices = indices_copy;
    cmd.index_count = (uint32_t)index_count;
    cmd.model_matrix[0] = cmd.model_matrix[5] = cmd.model_matrix[10] = cmd.model_matrix[15] = 1.0f;
    cmd.diffuse_color[0] = r;
    cmd.diffuse_color[1] = g;
    cmd.diffuse_color[2] = b;
    cmd.diffuse_color[3] = a;
    cmd.alpha = a;
    cmd.unlit = 1;

    return canvas3d_enqueue_draw(c,
                                 &cmd,
                                 DEFERRED_DRAW_MESH,
                                 canvas3d_screen_pass_kind(c),
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0.0f,
                                 NULL,
                                 NULL);
}

static int canvas3d_queue_screen_rect(rt_canvas3d *c,
                                      float x,
                                      float y,
                                      float w,
                                      float h,
                                      float r,
                                      float g,
                                      float b,
                                      float a) {
    vgfx3d_vertex_t verts[4];
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

    memset(verts, 0, sizeof(verts));
    verts[0].pos[0] = x;
    verts[0].pos[1] = y;
    verts[1].pos[0] = x + w;
    verts[1].pos[1] = y;
    verts[2].pos[0] = x + w;
    verts[2].pos[1] = y + h;
    verts[3].pos[0] = x;
    verts[3].pos[1] = y + h;
    for (int i = 0; i < 4; i++) {
        verts[i].normal[2] = 1.0f;
        verts[i].color[0] = r;
        verts[i].color[1] = g;
        verts[i].color[2] = b;
        verts[i].color[3] = a;
    }
    return canvas3d_queue_screen_geometry(c, verts, 4, indices, 6, r, g, b, a);
}

static int canvas3d_queue_screen_line(rt_canvas3d *c,
                                      float x0,
                                      float y0,
                                      float x1,
                                      float y1,
                                      float thickness,
                                      float r,
                                      float g,
                                      float b,
                                      float a) {
    float dx;
    float dy;
    float len;
    float px;
    float py;
    float half;
    vgfx3d_vertex_t verts[4];
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

    dx = x1 - x0;
    dy = y1 - y0;
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f)
        return canvas3d_queue_screen_rect(c, x0 - thickness * 0.5f, y0 - thickness * 0.5f,
                                          thickness, thickness, r, g, b, a);
    px = -dy / len;
    py = dx / len;
    half = thickness * 0.5f;
    memset(verts, 0, sizeof(verts));
    verts[0].pos[0] = x0 - px * half;
    verts[0].pos[1] = y0 - py * half;
    verts[1].pos[0] = x0 + px * half;
    verts[1].pos[1] = y0 + py * half;
    verts[2].pos[0] = x1 + px * half;
    verts[2].pos[1] = y1 + py * half;
    verts[3].pos[0] = x1 - px * half;
    verts[3].pos[1] = y1 - py * half;
    for (int i = 0; i < 4; i++) {
        verts[i].normal[2] = 1.0f;
        verts[i].color[0] = r;
        verts[i].color[1] = g;
        verts[i].color[2] = b;
        verts[i].color[3] = a;
    }
    return canvas3d_queue_screen_geometry(c, verts, 4, indices, 6, r, g, b, a);
}

void rt_canvas3d_begin_2d(void *obj) {
    vgfx3d_camera_params_t params;

    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->backend)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.Begin2D: Begin/End must not nest");
        return;
    }
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);

    canvas3d_build_ortho_camera(c, &params);

    c->cached_cam_pos[0] = 0.0f;
    c->cached_cam_pos[1] = 0.0f;
    c->cached_cam_pos[2] = 1.0f;
    c->draw_count = 0;
    c->frame_is_2d = 1;
    memcpy(c->cached_vp, params.projection, sizeof(c->cached_vp));

    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

/// @brief Draw a filled rectangle through the 3D pipeline (screen-space coords).
/// Must be called between Begin2D/End or Begin/End.
void rt_canvas3d_draw_rect_3d(
    void *obj, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    float r;
    float g;
    float b;

    if (!obj)
        return;
    if (w <= 0 || h <= 0)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame || !c->backend)
        return;
    r = (float)((color >> 16) & 0xFF) / 255.0f;
    g = (float)((color >> 8) & 0xFF) / 255.0f;
    b = (float)(color & 0xFF) / 255.0f;
    (void)canvas3d_queue_screen_rect(c, (float)x, (float)y, (float)w, (float)h, r, g, b, 1.0f);
}

/// @brief Draw text through the 3D pipeline using the 5×7 bitmap font.
/// Each character's "on" pixels are rendered as 2×2 quads batched into one mesh.
void rt_canvas3d_draw_text_3d(void *obj, int64_t x, int64_t y, rt_string text, int64_t color) {
    if (!obj || !text)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame || !c->backend)
        return;

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    float r = (float)((color >> 16) & 0xFF) / 255.0f;
    float g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)(color & 0xFF) / 255.0f;

    /* Reference the font data from draw_text2d (defined later in this file).
     * We duplicate the font table reference here for self-containment. */
    static const uint8_t font5x7[95][7] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00},
        {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00, 0x00},
        {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x19, 0x1A, 0x04, 0x0B, 0x13, 0x00, 0x00},
        {0x08, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00}, {0x08, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00},
        {0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00}, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}, {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x0E, 0x00}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00},
        {0x0E, 0x11, 0x01, 0x06, 0x08, 0x1F, 0x00}, {0x0E, 0x11, 0x02, 0x01, 0x11, 0x0E, 0x00},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x00}, {0x1F, 0x10, 0x1E, 0x01, 0x11, 0x0E, 0x00},
        {0x06, 0x08, 0x1E, 0x11, 0x11, 0x0E, 0x00}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00},
        {0x0E, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, {0x0E, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00},
        {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}, {0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08},
        {0x02, 0x04, 0x08, 0x04, 0x02, 0x00, 0x00}, {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
        {0x08, 0x04, 0x02, 0x04, 0x08, 0x00, 0x00}, {0x0E, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00},
        {0x0E, 0x11, 0x17, 0x17, 0x16, 0x10, 0x0E}, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00},
        {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, {0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x00},
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00}, {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00},
        {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x10, 0x13, 0x11, 0x0E, 0x00},
        {0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},
        {0x01, 0x01, 0x01, 0x01, 0x11, 0x0E, 0x00}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x00},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x1E, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00},
        {0x1E, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00}, {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x1E, 0x00},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00},
        {0x11, 0x0A, 0x04, 0x04, 0x0A, 0x11, 0x00}, {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00},
        {0x1F, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00}, {0x0E, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00},
        {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00}, {0x0E, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00},
        {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00},
        {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00},
        {0x10, 0x10, 0x1E, 0x11, 0x11, 0x1E, 0x00}, {0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00},
        {0x01, 0x01, 0x0F, 0x11, 0x11, 0x0F, 0x00}, {0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00},
        {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x00}, {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E, 0x00},
        {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x00}, {0x04, 0x00, 0x0C, 0x04, 0x04, 0x0E, 0x00},
        {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, {0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00},
        {0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, {0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00},
        {0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, {0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10, 0x00}, {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01, 0x00},
        {0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00}, {0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00},
        {0x08, 0x1E, 0x08, 0x08, 0x0A, 0x04, 0x00}, {0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00},
        {0x00, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00}, {0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00},
        {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, {0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00},
        {0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00}, {0x02, 0x04, 0x0C, 0x04, 0x04, 0x02, 0x00},
        {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x08, 0x04, 0x06, 0x04, 0x04, 0x08, 0x00},
        {0x00, 0x00, 0x0D, 0x12, 0x00, 0x00, 0x00},
    };

    /* Count "on" pixels to size the reusable scratch mesh exactly. */
    int32_t quad_count = 0;
    for (const char *p = str; *p; p++) {
        int ch = *p;
        if (ch < 32 || ch > 126)
            ch = 32;
        const uint8_t *glyph = font5x7[ch - 32];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (glyph[row] & (1 << (4 - col)))
                    quad_count++;
    }

    if (quad_count <= 0)
        return;

    int32_t vertex_count = quad_count * 4;
    int32_t index_count = quad_count * 6;
    if (!ensure_text_capacity(c, vertex_count, index_count))
        return;

    float scale = 2.0f; /* pixel size for each font dot */
    float cx = (float)x;
    int32_t quad_idx = 0;

    for (const char *p = str; *p; p++) {
        int ch = *p;
        if (ch < 32 || ch > 126)
            ch = 32;
        const uint8_t *glyph = font5x7[ch - 32];

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1 << (4 - col))) {
                    float px = cx + col * scale;
                    float py = (float)y + row * scale;
                    int32_t vi = quad_idx * 4;
                    int32_t ii = quad_idx * 6;

                    /* 4 vertices for this pixel quad */
                    for (int v = 0; v < 4; v++) {
                        memset(&c->text_vertices[vi + v], 0, sizeof(vgfx3d_vertex_t));
                        c->text_vertices[vi + v].normal[2] = 1.0f;
                        c->text_vertices[vi + v].color[0] = r;
                        c->text_vertices[vi + v].color[1] = g;
                        c->text_vertices[vi + v].color[2] = b;
                        c->text_vertices[vi + v].color[3] = 1.0f;
                    }
                    c->text_vertices[vi + 0].pos[0] = px;
                    c->text_vertices[vi + 0].pos[1] = py;
                    c->text_vertices[vi + 1].pos[0] = px + scale;
                    c->text_vertices[vi + 1].pos[1] = py;
                    c->text_vertices[vi + 2].pos[0] = px + scale;
                    c->text_vertices[vi + 2].pos[1] = py + scale;
                    c->text_vertices[vi + 3].pos[0] = px;
                    c->text_vertices[vi + 3].pos[1] = py + scale;

                    c->text_indices[ii + 0] = (uint32_t)vi;
                    c->text_indices[ii + 1] = (uint32_t)(vi + 1);
                    c->text_indices[ii + 2] = (uint32_t)(vi + 2);
                    c->text_indices[ii + 3] = (uint32_t)vi;
                    c->text_indices[ii + 4] = (uint32_t)(vi + 2);
                    c->text_indices[ii + 5] = (uint32_t)(vi + 3);
                    quad_idx++;
                }
            }
        }
        cx += 6.0f * scale; /* char width + 1px spacing */
    }

    (void)canvas3d_queue_screen_geometry(
        c, c->text_vertices, vertex_count, c->text_indices, index_count, r, g, b, 1.0f);
}

/// @brief Begin a 3D rendering frame with the given camera.
/// @details Must be called after Clear and before any DrawMesh calls. Captures
///          the camera's view/projection matrices, resets the deferred draw queue,
///          and updates per-frame timing state. Begin/End must not be nested.
/// @param obj    Canvas handle.
/// @param camera Camera3D handle providing view and projection matrices.
void rt_canvas3d_begin(void *obj, void *camera) {
    vgfx3d_camera_params_t params;

    if (!obj || !camera)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    rt_camera3d *cam = (rt_camera3d *)camera;
    if (!c->backend)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.Begin: Begin/End must not nest");
        return;
    }

    /* Show GPU layer for 3D rendering (in case it was hidden for 2D menu) */
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);

    mat4_d2f(cam->view, params.view);
    mat4_d2f(cam->projection, params.projection);
    params.position[0] = (float)cam->eye[0];
    params.position[1] = (float)cam->eye[1];
    params.position[2] = (float)cam->eye[2];
    params.fog_enabled = c->fog_enabled;
    params.fog_near = c->fog_near;
    params.fog_far = c->fog_far;
    params.fog_color[0] = c->fog_color[0];
    params.fog_color[1] = c->fog_color[1];
    params.fog_color[2] = c->fog_color[2];
    params.load_existing_color = 0;
    params.load_existing_depth = 0;

    /* Cache camera position for transparency sort key computation */
    c->cached_cam_pos[0] = params.position[0];
    c->cached_cam_pos[1] = params.position[1];
    c->cached_cam_pos[2] = params.position[2];

    /* Reset draw command queue for this frame */
    c->frame_serial++;
    canvas3d_prune_motion_history(c);
    c->draw_count = 0;
    c->frame_is_2d = 0;

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
    memcpy(c->last_scene_vp, c->cached_vp, sizeof(c->last_scene_vp));
    memcpy(c->last_scene_cam_pos, c->cached_cam_pos, sizeof(c->last_scene_cam_pos));
    c->has_last_scene_vp = 1;

    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

int64_t rt_canvas3d_get_frame_serial(void *obj) {
    return obj ? ((rt_canvas3d *)obj)->frame_serial : 0;
}

void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                        void *mesh_obj,
                                        const double *model_matrix,
                                        void *material_obj,
                                        const void *motion_key,
                                        const float *prev_bone_palette,
                                        const float *prev_morph_weights) {
    if (!obj || !mesh_obj || !model_matrix || !material_obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame || !c->gfx_win || !c->backend)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    rt_material3d *mat = (rt_material3d *)material_obj;

    if (mesh->morph_targets_ref && mesh->morph_deltas == NULL && mesh->morph_weights == NULL &&
        mesh->morph_shape_count == 0) {
        extern void rt_canvas3d_draw_mesh_matrix_morphed(void *canvas,
                                                         void *mesh,
                                                         const double *model_matrix,
                                                         void *material,
                                                         const void *motion_key,
                                                         void *morph_targets);
        rt_canvas3d_draw_mesh_matrix_morphed(
            obj, mesh_obj, model_matrix, material_obj, motion_key, mesh->morph_targets_ref);
        return;
    }

    if (mesh->vertex_count == 0 || mesh->index_count == 0)
        return;
    rt_mesh3d_refresh_bounds(mesh);

    /* Ensure draw command buffer has space */
    if (!ensure_deferred_capacity(&c->draw_cmds, &c->draw_capacity, c->draw_count + 1))
        return;

    deferred_draw_t *dd = &((deferred_draw_t *)c->draw_cmds)[c->draw_count++];
    memset(dd, 0, sizeof(*dd));
    dd->kind = DEFERRED_DRAW_MESH;
    dd->pass_kind = DEFERRED_PASS_MAIN;

    /* Build draw command */
    dd->cmd.vertices = mesh->vertices;
    dd->cmd.vertex_count = mesh->vertex_count;
    dd->cmd.indices = mesh->indices;
    dd->cmd.index_count = mesh->index_count;
    dd->cmd.geometry_key = mesh;
    dd->cmd.geometry_revision = mesh->geometry_revision;
    mat4_d2f(model_matrix, dd->cmd.model_matrix);
    canvas3d_resolve_previous_model(
        c, motion_key, dd->cmd.model_matrix, dd->cmd.prev_model_matrix, &dd->cmd.has_prev_model_matrix);
    dd->cmd.diffuse_color[0] = (float)mat->diffuse[0];
    dd->cmd.diffuse_color[1] = (float)mat->diffuse[1];
    dd->cmd.diffuse_color[2] = (float)mat->diffuse[2];
    dd->cmd.diffuse_color[3] = (float)mat->diffuse[3];
    dd->cmd.specular[0] = (float)mat->specular[0];
    dd->cmd.specular[1] = (float)mat->specular[1];
    dd->cmd.specular[2] = (float)mat->specular[2];
    dd->cmd.shininess = (float)mat->shininess;
    dd->cmd.alpha = (float)mat->alpha;
    dd->cmd.unlit = (int8_t)(mat->unlit || mat->shading_model == 3);
    dd->cmd.texture = mat->texture;
    dd->cmd.normal_map = mat->normal_map;
    dd->cmd.specular_map = mat->specular_map;
    dd->cmd.emissive_map = mat->emissive_map;
    dd->cmd.emissive_color[0] = (float)mat->emissive[0];
    dd->cmd.emissive_color[1] = (float)mat->emissive[1];
    dd->cmd.emissive_color[2] = (float)mat->emissive[2];
    dd->cmd.env_map = mat->env_map;
    dd->cmd.reflectivity = (float)mat->reflectivity;
    dd->cmd.shading_model = (mat->shading_model == 3) ? 0 : mat->shading_model;
    for (int pi = 0; pi < 8; pi++)
        dd->cmd.custom_params[pi] = (float)mat->custom_params[pi];

    /* Consume pending terrain splat data (if set by terrain draw path) */
    dd->cmd.has_splat = c->pending_has_splat;
    dd->cmd.splat_map = c->pending_splat_map;
    for (int i = 0; i < 4; i++) {
        dd->cmd.splat_layers[i] = c->pending_splat_layers[i];
        dd->cmd.splat_layer_scales[i] = c->pending_splat_layer_scales[i];
    }
    /* Clear pending splat state (one-shot consumption) */
    c->pending_has_splat = 0;
    c->pending_splat_map = NULL;
    for (int i = 0; i < 4; i++) {
        c->pending_splat_layers[i] = NULL;
        c->pending_splat_layer_scales[i] = 0.0f;
    }

    /* Pass through bone palette for GPU skinning (MTL-09) */
    dd->cmd.bone_palette = mesh->bone_palette;
    dd->cmd.prev_bone_palette = prev_bone_palette ? prev_bone_palette : mesh->prev_bone_palette;
    dd->cmd.bone_count = mesh->bone_count;

    /* GPU morph payloads are supplied by DrawMeshMorphed via transient mesh fields.
     * CPU morph paths leave these null. */
    dd->cmd.morph_deltas = mesh->morph_deltas;
    dd->cmd.morph_normal_deltas = mesh->morph_normal_deltas;
    dd->cmd.morph_weights = mesh->morph_weights;
    dd->cmd.prev_morph_weights = prev_morph_weights ? prev_morph_weights : mesh->prev_morph_weights;
    dd->cmd.morph_shape_count = mesh->morph_shape_count;

    /* Build light params */
    dd->light_count = build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS);
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->wireframe = c->wireframe;
    dd->backface_cull = c->backface_cull;
    dd->has_local_bounds = 1;
    memcpy(dd->local_bounds_min, mesh->aabb_min, sizeof(dd->local_bounds_min));
    memcpy(dd->local_bounds_max, mesh->aabb_max, sizeof(dd->local_bounds_max));

    /* Compute sort key: squared distance from camera to mesh centroid.
     * Uses model matrix translation (column 3 in row-major) as centroid proxy. */
    {
        dd->sort_key = canvas3d_compute_sort_key(c, dd->cmd.model_matrix);
    }
}

void rt_canvas3d_draw_mesh_matrix(void *obj,
                                  void *mesh_obj,
                                  const double *model_matrix,
                                  void *material_obj) {
    rt_canvas3d_draw_mesh_matrix_keyed(obj, mesh_obj, model_matrix, material_obj, NULL, NULL, NULL);
}

/// @brief Submit a mesh for drawing with the given transform and material.
/// @details Defers the draw into the per-frame queue. Actual rendering happens
///          in End(), which sorts opaque draws front-to-back and transparent draws
///          back-to-front for correct alpha blending. The mesh, transform, and
///          material pointers are borrowed (not retained).
void rt_canvas3d_draw_mesh(void *obj, void *mesh_obj, void *transform_obj, void *material_obj) {
    if (!transform_obj)
        return;
    rt_canvas3d_draw_mesh_matrix_keyed(
        obj, mesh_obj, ((mat4_impl *)transform_obj)->m, material_obj, transform_obj, NULL, NULL);
}

void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                       void *mesh_obj,
                                       void *material_obj,
                                       const float *instance_matrices,
                                       int32_t instance_count,
                                       const float *prev_instance_matrices,
                                       int8_t has_prev_instance_matrices) {
    rt_canvas3d *c;
    rt_mesh3d *mesh;
    rt_material3d *mat;
    vgfx3d_draw_cmd_t base_cmd;

    if (!canvas_obj || !mesh_obj || !material_obj || !instance_matrices || instance_count <= 0)
        return;
    c = (rt_canvas3d *)canvas_obj;
    mesh = (rt_mesh3d *)mesh_obj;
    mat = (rt_material3d *)material_obj;
    if (!c->in_frame || !c->backend || mesh->vertex_count == 0 || mesh->index_count == 0)
        return;

    rt_mesh3d_refresh_bounds(mesh);
    memset(&base_cmd, 0, sizeof(base_cmd));
    base_cmd.vertices = mesh->vertices;
    base_cmd.vertex_count = mesh->vertex_count;
    base_cmd.indices = mesh->indices;
    base_cmd.index_count = mesh->index_count;
    base_cmd.geometry_key = mesh;
    base_cmd.geometry_revision = mesh->geometry_revision;
    base_cmd.model_matrix[0] = base_cmd.model_matrix[5] = base_cmd.model_matrix[10] =
        base_cmd.model_matrix[15] = 1.0f;
    base_cmd.diffuse_color[0] = (float)mat->diffuse[0];
    base_cmd.diffuse_color[1] = (float)mat->diffuse[1];
    base_cmd.diffuse_color[2] = (float)mat->diffuse[2];
    base_cmd.diffuse_color[3] = (float)mat->diffuse[3];
    base_cmd.specular[0] = (float)mat->specular[0];
    base_cmd.specular[1] = (float)mat->specular[1];
    base_cmd.specular[2] = (float)mat->specular[2];
    base_cmd.shininess = (float)mat->shininess;
    base_cmd.alpha = (float)mat->alpha;
    base_cmd.unlit = (int8_t)(mat->unlit || mat->shading_model == 3);
    base_cmd.texture = mat->texture;
    base_cmd.normal_map = mat->normal_map;
    base_cmd.specular_map = mat->specular_map;
    base_cmd.emissive_map = mat->emissive_map;
    base_cmd.emissive_color[0] = (float)mat->emissive[0];
    base_cmd.emissive_color[1] = (float)mat->emissive[1];
    base_cmd.emissive_color[2] = (float)mat->emissive[2];
    base_cmd.env_map = mat->env_map;
    base_cmd.reflectivity = (float)mat->reflectivity;
    base_cmd.shading_model = (mat->shading_model == 3) ? 0 : mat->shading_model;
    for (int pi = 0; pi < 8; pi++)
        base_cmd.custom_params[pi] = (float)mat->custom_params[pi];
    base_cmd.bone_palette = mesh->bone_palette;
    base_cmd.prev_bone_palette = mesh->prev_bone_palette;
    base_cmd.bone_count = mesh->bone_count;
    base_cmd.morph_deltas = mesh->morph_deltas;
    base_cmd.morph_normal_deltas = mesh->morph_normal_deltas;
    base_cmd.morph_weights = mesh->morph_weights;
    base_cmd.prev_morph_weights = mesh->prev_morph_weights;
    base_cmd.morph_shape_count = mesh->morph_shape_count;

    if (base_cmd.alpha < 1.0f || !c->backend->submit_draw_instanced) {
        for (int32_t i = 0; i < instance_count; i++) {
            vgfx3d_draw_cmd_t per_instance = base_cmd;
            memcpy(per_instance.model_matrix,
                   &instance_matrices[(size_t)i * 16u],
                   sizeof(per_instance.model_matrix));
            if (has_prev_instance_matrices && prev_instance_matrices) {
                memcpy(per_instance.prev_model_matrix,
                       &prev_instance_matrices[(size_t)i * 16u],
                       sizeof(per_instance.prev_model_matrix));
                per_instance.has_prev_model_matrix = 1;
            }
            per_instance.prev_instance_matrices = NULL;
            per_instance.has_prev_instance_matrices = 0;
            (void)canvas3d_enqueue_draw(c,
                                        &per_instance,
                                        DEFERRED_DRAW_MESH,
                                        DEFERRED_PASS_MAIN,
                                        NULL,
                                        0,
                                        1,
                                        c->wireframe,
                                        c->backface_cull,
                                        canvas3d_compute_sort_key(c, per_instance.model_matrix),
                                        mesh->aabb_min,
                                        mesh->aabb_max);
        }
        return;
    }

    base_cmd.prev_instance_matrices = prev_instance_matrices;
    base_cmd.has_prev_instance_matrices =
        (int8_t)(has_prev_instance_matrices && prev_instance_matrices != NULL);
    {
        float batch_sort_key = FLT_MAX;
        for (int32_t i = 0; i < instance_count; i++) {
            float key =
                canvas3d_compute_sort_key(c, &instance_matrices[(size_t)i * 16u]);
            if (key < batch_sort_key)
                batch_sort_key = key;
        }
        if (batch_sort_key == FLT_MAX)
            batch_sort_key = 0.0f;
        (void)canvas3d_enqueue_draw(c,
                                    &base_cmd,
                                    DEFERRED_DRAW_INSTANCED,
                                    DEFERRED_PASS_MAIN,
                                    instance_matrices,
                                    instance_count,
                                    1,
                                    c->wireframe,
                                    c->backface_cull,
                                    batch_sort_key,
                                    mesh->aabb_min,
                                    mesh->aabb_max);
    }
}

/// @brief Flush all deferred draws, performing depth sorting and backend dispatch.
/// @details Processes the deferred draw queue built during Begin/DrawMesh calls:
///          1. Frustum-culls draws against the camera's view frustum.
///          2. Sorts opaque draws front-to-back (Z-sort for early depth rejection).
///          3. Sorts transparent draws back-to-front (for correct alpha blending).
///          4. Dispatches each draw through the backend's submit_draw vtable.
///          5. Applies shadow mapping and post-processing if enabled.
///          Must be called after all DrawMesh calls and before Flip.
void rt_canvas3d_end(void *obj) {
    deferred_draw_t *cmds;
    int32_t main_count = 0;
    int32_t overlay_count = 0;

    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->in_frame)
        return;
    if (!c->backend) {
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    cmds = (deferred_draw_t *)c->draw_cmds;

    if (!c->frame_is_2d && c->skybox) {
        extern void rt_cubemap_sample(const rt_cubemap3d *cm,
                                      float dx,
                                      float dy,
                                      float dz,
                                      float *out_r,
                                      float *out_g,
                                      float *out_b);

        uint8_t *out_pixels = NULL;
        int32_t out_w = 0;
        int32_t out_h = 0;
        int32_t out_stride = 0;

        if (c->backend->draw_skybox) {
            c->backend->draw_skybox(c->backend_ctx, c->skybox);
        } else {
            if (c->render_target) {
                out_pixels = c->render_target->color_buf;
                out_w = c->render_target->width;
                out_h = c->render_target->height;
                out_stride = c->render_target->stride;
            } else {
                vgfx_framebuffer_t fb;
                if (c->gfx_win && vgfx_get_framebuffer(c->gfx_win, &fb)) {
                    out_pixels = fb.pixels;
                    out_w = fb.width;
                    out_h = fb.height;
                    out_stride = fb.stride;
                }
            }
        }

        if (!c->backend->draw_skybox && out_pixels && !canvas3d_backend_owns_gpu_rtt(c)) {
            float inv_vp[16];
            if (vgfx3d_invert_matrix4(c->cached_vp, inv_vp) == 0) {
                for (int32_t y = 0; y < out_h; y++) {
                    float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) / (float)out_h;
                    for (int32_t x = 0; x < out_w; x++) {
                        float ndc_x = 2.0f * ((float)x + 0.5f) / (float)out_w - 1.0f;
                        float clip[4] = {ndc_x, ndc_y, 1.0f, 1.0f};
                        float world[4];
                        float dx;
                        float dy;
                        float dz;
                        float dl;
                        float r;
                        float g;
                        float b;
                        uint8_t *dst;

                        world[0] = inv_vp[0] * clip[0] + inv_vp[1] * clip[1] +
                                   inv_vp[2] * clip[2] + inv_vp[3] * clip[3];
                        world[1] = inv_vp[4] * clip[0] + inv_vp[5] * clip[1] +
                                   inv_vp[6] * clip[2] + inv_vp[7] * clip[3];
                        world[2] = inv_vp[8] * clip[0] + inv_vp[9] * clip[1] +
                                   inv_vp[10] * clip[2] + inv_vp[11] * clip[3];
                        world[3] = inv_vp[12] * clip[0] + inv_vp[13] * clip[1] +
                                   inv_vp[14] * clip[2] + inv_vp[15] * clip[3];
                        if (fabsf(world[3]) > 1e-7f) {
                            world[0] /= world[3];
                            world[1] /= world[3];
                            world[2] /= world[3];
                        }
                        dx = world[0] - c->cached_cam_pos[0];
                        dy = world[1] - c->cached_cam_pos[1];
                        dz = world[2] - c->cached_cam_pos[2];
                        dl = sqrtf(dx * dx + dy * dy + dz * dz);
                        if (dl > 1e-7f) {
                            dx /= dl;
                            dy /= dl;
                            dz /= dl;
                        }
                        rt_cubemap_sample(c->skybox, dx, dy, dz, &r, &g, &b);
                        dst = &out_pixels[y * out_stride + x * 4];
                        dst[0] = (uint8_t)(r * 255.0f);
                        dst[1] = (uint8_t)(g * 255.0f);
                        dst[2] = (uint8_t)(b * 255.0f);
                        dst[3] = 0xFF;
                    }
                }
            }
        }
    }

    for (int32_t i = 0; i < c->draw_count; i++) {
        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
            main_count++;
        else if (cmds[i].pass_kind == DEFERRED_PASS_SCREEN_OVERLAY)
            overlay_count++;
    }

    if (main_count == 0 && overlay_count == 0) {
        c->backend->end_frame(c->backend_ctx);
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    if (!c->frame_is_2d && main_count > 0 && c->shadows_enabled && c->shadow_rt &&
        c->shadow_rt->depth_buf && c->backend->shadow_begin && c->backend->shadow_draw &&
        c->backend->shadow_end) {
        const vgfx3d_light_params_t *dir_light = NULL;
        for (int32_t i = 0; i < c->draw_count && !dir_light; i++) {
            if (cmds[i].pass_kind != DEFERRED_PASS_MAIN)
                continue;
            for (int32_t li = 0; li < cmds[i].light_count; li++) {
                if (cmds[i].lights[li].type == 0) {
                    dir_light = &cmds[i].lights[li];
                    break;
                }
            }
        }

        if (dir_light) {
            float light_vp[16];
            if (canvas3d_build_shadow_light_vp(cmds, c->draw_count, dir_light, light_vp)) {
                memcpy(c->shadow_light_vp, light_vp, sizeof(light_vp));
                c->backend->shadow_begin(c->backend_ctx,
                                         c->shadow_rt->depth_buf,
                                         c->shadow_rt->width,
                                         c->shadow_rt->height,
                                         light_vp);
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || cmds[i].cmd.alpha < 1.0f)
                        continue;
                    canvas3d_shadow_deferred(c, &cmds[i]);
                }
                c->backend->shadow_end(c->backend_ctx, c->shadow_bias);
            }
        }
    }

    if (main_count > 0) {
        if (c->occlusion_culling) {
            /* This mode is currently depth-friendly ordering only. It improves
             * early-Z efficiency for opaque draws but does not perform true
             * visibility rejection or GPU occlusion queries. */
            int32_t opaque_count = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN && cmds[i].cmd.alpha >= 1.0f)
                    opaque_count++;
            }
            if (opaque_count > 0 &&
                ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, opaque_count)) {
                deferred_draw_t *opaque = (deferred_draw_t *)c->trans_cmds;
                int32_t oi = 0;
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind == DEFERRED_PASS_MAIN && cmds[i].cmd.alpha >= 1.0f)
                        opaque[oi++] = cmds[i];
                }
                qsort(opaque, (size_t)opaque_count, sizeof(deferred_draw_t), cmp_front_to_back);
                for (int32_t i = 0; i < opaque_count; i++)
                    canvas3d_submit_deferred(c, &opaque[i]);
            }
        } else {
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN && cmds[i].cmd.alpha >= 1.0f)
                    canvas3d_submit_deferred(c, &cmds[i]);
            }
        }

        {
            int32_t trans_count = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN && cmds[i].cmd.alpha < 1.0f)
                    trans_count++;
            }
            if (trans_count > 0 &&
                ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, trans_count)) {
                deferred_draw_t *trans = (deferred_draw_t *)c->trans_cmds;
                int32_t ti = 0;
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind == DEFERRED_PASS_MAIN && cmds[i].cmd.alpha < 1.0f)
                        trans[ti++] = cmds[i];
                }
                qsort(trans, (size_t)trans_count, sizeof(deferred_draw_t), cmp_back_to_front);
                for (int32_t i = 0; i < trans_count; i++)
                    canvas3d_submit_deferred(c, &trans[i]);
            }
        }
    }

    c->backend->end_frame(c->backend_ctx);

    if (!c->frame_is_2d && overlay_count > 0) {
        if (canvas3d_begin_overlay_frame(c, 1)) {
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind != DEFERRED_PASS_SCREEN_OVERLAY)
                    continue;
                canvas3d_submit_deferred(c, &cmds[i]);
            }
            c->backend->end_frame(c->backend_ctx);
        }
    }

    c->in_frame = 0;
    c->frame_is_2d = 0;
    c->draw_count = 0;
    canvas3d_clear_temp_buffers(c);
    canvas3d_clear_temp_objects(c);
}

/*==========================================================================
 * Window lifecycle — same as before, no backend involvement
 *=========================================================================*/

/// @brief Present the rendered frame to the window (swaps buffers).
/// @details Applies post-processing effects (if any), then presents the
///          framebuffer via the backend's present function. Updates the FPS
///          counter and delta-time calculation for the next frame.
void rt_canvas3d_flip(void *obj) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return;

    int gpu_postfx_presented = 0;
    if (canvas3d_backend_uses_gpu_postfx(c)) {
        vgfx3d_postfx_snapshot_t snapshot;
        if (vgfx3d_postfx_get_snapshot(c->postfx, &snapshot)) {
            c->backend->present_postfx(c->backend_ctx, &snapshot);
            gpu_postfx_presented = 1;
        }
    }

    if (!gpu_postfx_presented) {
        /* Apply post-processing effects to the software framebuffer */
        extern void rt_postfx3d_apply_to_canvas(void *canvas);
        rt_postfx3d_apply_to_canvas(obj);
    }

    /* Present the GPU drawable / swap the back buffer after all queued passes
     * for the frame have rendered into the backend's scene targets. */
    if (!gpu_postfx_presented && c->backend && c->backend->present)
        c->backend->present(c->backend_ctx);

    /* Always call vgfx_update to keep the window alive and process display
     * refresh. GPU backends own the final on-screen present path. */
    vgfx_update(c->gfx_win);

    int64_t now_us = rt_clock_ticks_us();
    if (c->last_flip_us > 0) {
        int64_t delta_us = now_us - c->last_flip_us;
        c->delta_time_ms = delta_us > 0 ? delta_us / 1000 : 0;
    } else
        c->delta_time_ms = 0;
    c->last_flip_us = now_us;

    if (vgfx_close_requested(c->gfx_win)) {
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
        c->should_close = 1;
    }
}

/// @brief Process all pending window events (keyboard, mouse, resize, close).
/// @details Polls the platform event queue and updates input state for
///          Keyboard/Mouse/Pad subsystems. Must be called once per frame.
/// @return 1 if a close event was received, 0 otherwise.
int64_t rt_canvas3d_poll(void *obj) {
    if (!obj)
        return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return 0;
    extern void rt_keyboard_on_key_down(int64_t key);
    extern void rt_keyboard_on_key_up(int64_t key);
    extern void rt_mouse_update_pos(int64_t x, int64_t y);
    extern void rt_mouse_force_delta(int64_t dx, int64_t dy);
    extern void rt_mouse_button_down(int64_t button);
    extern void rt_mouse_button_up(int64_t button);
    extern int8_t rt_mouse_is_captured(void);

    int8_t captured = rt_mouse_is_captured();

    /* Begin frame (resets per-frame state for keyboard/mouse/pad) */
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    rt_pad_poll();

    /* Read current platform mouse position */
    int32_t mx, my;
    vgfx_mouse_pos(c->gfx_win, &mx, &my);

    /* For captured (FPS) mode: compute delta as offset from window center.
     * This avoids issues with warp timing, stale events, and OS mouse tracking. */
    if (captured) {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        int32_t cx = cw / 2, cy = ch / 2;
        int64_t dx = (int64_t)mx - (int64_t)cx;
        int64_t dy = (int64_t)my - (int64_t)cy;
        rt_mouse_force_delta(dx, dy);
    } else {
        rt_mouse_update_pos((int64_t)mx, (int64_t)my);
    }

    /* Process events (keyboard + mouse buttons only — mouse moves handled above) */
    vgfx_event_t evt;
    while (vgfx_poll_event(c->gfx_win, &evt)) {
        if (evt.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)evt.data.key.key);
        else if (evt.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)evt.data.key.key);
        else if (!captured && evt.type == VGFX_EVENT_MOUSE_MOVE) {
            float cs = vgfx_window_get_scale(c->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            rt_mouse_update_pos((int64_t)(evt.data.mouse_move.x / cs),
                                (int64_t)(evt.data.mouse_move.y / cs));
        } else if (evt.type == VGFX_EVENT_MOUSE_DOWN) {
            float cs = vgfx_window_get_scale(c->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            rt_mouse_update_pos((int64_t)(evt.data.mouse_button.x / cs),
                                (int64_t)(evt.data.mouse_button.y / cs));
            rt_mouse_button_down((int64_t)evt.data.mouse_button.button);
        } else if (evt.type == VGFX_EVENT_MOUSE_UP) {
            float cs = vgfx_window_get_scale(c->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            rt_mouse_update_pos((int64_t)(evt.data.mouse_button.x / cs),
                                (int64_t)(evt.data.mouse_button.y / cs));
            rt_mouse_button_up((int64_t)evt.data.mouse_button.button);
        } else if (evt.type == VGFX_EVENT_RESIZE) {
            rt_canvas3d_apply_resize(c, evt.data.resize.width, evt.data.resize.height);
        }
    }

    if (!captured) {
        vgfx_mouse_pos(c->gfx_win, &mx, &my);
        rt_mouse_update_pos((int64_t)mx, (int64_t)my);
    }

    /* Warp cursor to center for next frame (only when captured) */
    if (captured) {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        vgfx_warp_cursor(c->gfx_win, cw / 2, ch / 2);
    }

    return 0;
}

/// @brief Check if the canvas window received a close request.
int8_t rt_canvas3d_should_close(void *obj) {
    return obj ? ((rt_canvas3d *)obj)->should_close : 0;
}

/// @brief Enable or disable wireframe rendering mode.
void rt_canvas3d_set_wireframe(void *obj, int8_t enabled) {
    if (obj)
        ((rt_canvas3d *)obj)->wireframe = enabled;
}

/// @brief Enable or disable backface culling (CCW winding = front face).
void rt_canvas3d_set_backface_cull(void *obj, int8_t enabled) {
    if (obj)
        ((rt_canvas3d *)obj)->backface_cull = enabled;
}

void rt_canvas3d_add_temp_buffer(void *obj, void *buffer) {
    if (!obj || !buffer)
        return;
    (void)canvas3d_track_temp_buffer((rt_canvas3d *)obj, buffer);
}

void rt_canvas3d_add_temp_object(void *obj, void *value) {
    if (!obj || !value)
        return;
    (void)canvas3d_track_temp_object((rt_canvas3d *)obj, value);
}

/// @brief Get the current canvas width in pixels (updates on window resize).
int64_t rt_canvas3d_get_width(void *obj) {
    return obj ? ((rt_canvas3d *)obj)->width : 0;
}

/// @brief Get the current canvas height in pixels (updates on window resize).
int64_t rt_canvas3d_get_height(void *obj) {
    return obj ? ((rt_canvas3d *)obj)->height : 0;
}

/// @brief Get the current frames-per-second (updated each Flip call).
int64_t rt_canvas3d_get_fps(void *obj) {
    if (!obj)
        return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    return c->delta_time_ms > 0 ? 1000 / c->delta_time_ms : 0;
}

/// @brief Get the time elapsed since the last frame in milliseconds.
/// @details Clamped to dt_max (default 100ms) to prevent physics explosions
///          after long pauses (e.g., window drag, breakpoint, alt-tab).
int64_t rt_canvas3d_get_delta_time(void *obj) {
    if (!obj)
        return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    int64_t dt = c->delta_time_ms;
    if (c->dt_max_ms > 0) {
        if (dt < 1)
            dt = 1;
        if (dt > c->dt_max_ms)
            dt = c->dt_max_ms;
    }
    return dt;
}

void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms) {
    if (obj)
        ((rt_canvas3d *)obj)->dt_max_ms = max_ms;
}

/// @brief Assign a light to one of the 8 per-canvas light slots.
/// @details Slot index must be in [0, VGFX3D_MAX_LIGHTS). Pass NULL to clear a slot.
void rt_canvas3d_set_light(void *obj, int64_t index, void *light) {
    if (!obj || index < 0 || index >= VGFX3D_MAX_LIGHTS)
        return;
    ((rt_canvas3d *)obj)->lights[index] = (rt_light3d *)light;
}

/// @brief Set the global ambient light color for the canvas (applied to all surfaces).
void rt_canvas3d_set_ambient(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    c->ambient[0] = (float)r;
    c->ambient[1] = (float)g;
    c->ambient[2] = (float)b;
}

/*==========================================================================
 * Debug drawing — transform 3D points to screen via backend VP
 *=========================================================================*/

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
    float iw = 1.0f / clip[3];
    *sx = (clip[0] * iw + 1.0f) * 0.5f * (float)fb_w;
    *sy = (1.0f - clip[1] * iw) * 0.5f * (float)fb_h;
    return 1;
}

/// @brief Draw a debug line between two 3D points (rendered as a thin quad).
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

    float p0[3] = {(float)rt_vec3_x(from), (float)rt_vec3_y(from), (float)rt_vec3_z(from)};
    float p1[3] = {(float)rt_vec3_x(to), (float)rt_vec3_y(to), (float)rt_vec3_z(to)};
    float sx0, sy0, sx1, sy1;
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
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw a debug point at a 3D position (rendered as a small quad billboard).
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

    float p[3] = {(float)rt_vec3_x(pos), (float)rt_vec3_y(pos), (float)rt_vec3_z(pos)};
    float sx, sy;
    if (!world_to_screen(c, p, &sx, &sy, fb.width, fb.height))
        return;
    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    {
        float side = size > 0 ? (float)size : 1.0f;
        float half = side * 0.5f;
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
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/*==========================================================================
 * Screen-space HUD overlay (drawn directly to framebuffer, no 3D transform)
 *=========================================================================*/

/// @brief Draw a filled 2D rectangle on the screen (HUD overlay, screen-space).
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

/// @brief Draw a centered crosshair on the screen (two crossing lines).
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

    int32_t cx = fb.width / 2, cy = fb.height / 2;
    int32_t half = (int32_t)(size / 2);
    float r = (float)((color >> 16) & 0xFF) / 255.0f;
    float g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)(color & 0xFF) / 255.0f;

    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    (void)canvas3d_queue_screen_line(
        c, (float)(cx - half), (float)cy, (float)(cx + half), (float)cy, 1.0f, r, g, b, 1.0f);
    (void)canvas3d_queue_screen_line(
        c, (float)cx, (float)(cy - half), (float)cx, (float)(cy + half), 1.0f, r, g, b, 1.0f);
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw 2D text on the screen using the built-in 5x7 bitmap font.
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

/// @brief Get the name of the active rendering backend ("metal", "opengl", "d3d11", or "software").
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

    int32_t shot_w = c->render_target ? c->render_target->width : c->width;
    int32_t shot_h = c->render_target ? c->render_target->height : c->height;
    if (shot_w <= 0 || shot_h <= 0)
        return NULL;

    void *pixels = rt_pixels_new((int64_t)shot_w, (int64_t)shot_h);
    if (!pixels)
        return NULL;
    px_view *pv = (px_view *)pixels;

    if (c->render_target && c->render_target->color_buf) {
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
        size_t row_bytes = (size_t)shot_w * 4u;
        uint8_t *rgba = (uint8_t *)malloc((size_t)shot_h * row_bytes);
        if (rgba && c->backend->readback_rgba(c->backend_ctx, rgba, shot_w, shot_h,
                                              (int32_t)row_bytes)) {
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

/*==========================================================================
 * Debug Gizmos — wireframe AABB, sphere, ray, axis
 *=========================================================================*/

/// @brief Draw a wireframe axis-aligned bounding box (12 edges) for debugging.
void rt_canvas3d_draw_aabb_wire(void *obj, void *min_v, void *max_v, int64_t color) {
    if (!obj || !min_v || !max_v)
        return;
    double mn[3] = {rt_vec3_x(min_v), rt_vec3_y(min_v), rt_vec3_z(min_v)};
    double mx[3] = {rt_vec3_x(max_v), rt_vec3_y(max_v), rt_vec3_z(max_v)};

    /* 8 corners from min/max combinations */
    void *c[8];
    for (int i = 0; i < 8; i++)
        c[i] =
            rt_vec3_new((i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2]);

    /* 12 edges: bottom face (0-1,1-3,3-2,2-0), top face (4-5,5-7,7-6,6-4), verticals */
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
        rt_canvas3d_draw_line3d(obj, c[edges[e][0]], c[edges[e][1]], color);
}

/// @brief Draw a wireframe sphere approximation (3 circles on XY, XZ, YZ planes).
void rt_canvas3d_draw_sphere_wire(void *obj, void *center, double radius, int64_t color) {
    if (!obj || !center)
        return;
    double cx = rt_vec3_x(center), cy = rt_vec3_y(center), cz = rt_vec3_z(center);
    double r = radius;
    int segs = 24;
    double step = 2.0 * 3.14159265358979323846 / segs;

    for (int i = 0; i < segs; i++) {
        double a0 = i * step, a1 = (i + 1) * step;
        double c0 = cos(a0), s0 = sin(a0), c1 = cos(a1), s1 = sin(a1);

        /* XY circle */
        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx + c0 * r, cy + s0 * r, cz),
                                rt_vec3_new(cx + c1 * r, cy + s1 * r, cz),
                                color);
        /* XZ circle */
        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx + c0 * r, cy, cz + s0 * r),
                                rt_vec3_new(cx + c1 * r, cy, cz + s1 * r),
                                color);
        /* YZ circle */
        rt_canvas3d_draw_line3d(obj,
                                rt_vec3_new(cx, cy + c0 * r, cz + s0 * r),
                                rt_vec3_new(cx, cy + c1 * r, cz + s1 * r),
                                color);
    }
}

/// @brief Draw a debug ray from an origin along a direction for the given length.
void rt_canvas3d_draw_debug_ray(void *obj, void *origin, void *dir, double length, int64_t color) {
    if (!obj || !origin || !dir)
        return;
    double ex = rt_vec3_x(origin) + rt_vec3_x(dir) * length;
    double ey = rt_vec3_y(origin) + rt_vec3_y(dir) * length;
    double ez = rt_vec3_z(origin) + rt_vec3_z(dir) * length;
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ex, ey, ez), color);
}

/// @brief Draw an XYZ axis gizmo (red=X, green=Y, blue=Z) at the given origin.
void rt_canvas3d_draw_axis(void *obj, void *origin, double scale) {
    if (!obj || !origin)
        return;
    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox + scale, oy, oz), 0xFF0000);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy + scale, oz), 0x00FF00);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy, oz + scale), 0x0000FF);
}

/*==========================================================================
 * Fog — linear distance fog
 *=========================================================================*/

void rt_canvas3d_set_fog(
    void *obj, double near_dist, double far_dist, double r, double g, double b) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    c->fog_enabled = 1;
    c->fog_near = (float)near_dist;
    c->fog_far = (float)far_dist;
    c->fog_color[0] = (float)r;
    c->fog_color[1] = (float)g;
    c->fog_color[2] = (float)b;
}

/// @brief Disable distance fog on the canvas.
void rt_canvas3d_clear_fog(void *obj) {
    if (!obj)
        return;
    ((rt_canvas3d *)obj)->fog_enabled = 0;
}

/*==========================================================================
 * Shadow Mapping
 *=========================================================================*/

/// @brief Enable shadow mapping with the given shadow map resolution.
/// @details Creates a shadow depth buffer and configures directional light shadow
///          casting. The shadow map is rendered from the light's perspective and
///          sampled during the main render pass.
void rt_canvas3d_enable_shadows(void *obj, int64_t resolution) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    int32_t res = (int32_t)resolution;
    if (res < 64)
        res = 64;
    if (res > 4096)
        res = 4096;
    c->shadows_enabled = 1;
    c->shadow_resolution = res;

    /* Allocate shadow render target if needed */
    if (!c->shadow_rt || c->shadow_rt->width != res) {
        if (c->shadow_rt) {
            free(c->shadow_rt->color_buf);
            free(c->shadow_rt->depth_buf);
            free(c->shadow_rt);
        }
        c->shadow_rt = (vgfx3d_rendertarget_t *)calloc(1, sizeof(vgfx3d_rendertarget_t));
        if (c->shadow_rt) {
            c->shadow_rt->width = res;
            c->shadow_rt->height = res;
            c->shadow_rt->stride = res * 4;
            c->shadow_rt->color_buf = NULL; /* depth-only */
            c->shadow_rt->depth_buf = (float *)malloc((size_t)res * (size_t)res * sizeof(float));
        }
    }
}

/// @brief Disable shadow mapping and free the shadow depth buffer.
void rt_canvas3d_disable_shadows(void *obj) {
    if (!obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    c->shadows_enabled = 0;
    if (c->shadow_rt) {
        free(c->shadow_rt->color_buf);
        free(c->shadow_rt->depth_buf);
        free(c->shadow_rt);
        c->shadow_rt = NULL;
    }
}

/// @brief Set the shadow map depth bias to reduce shadow acne artifacts.
void rt_canvas3d_set_shadow_bias(void *obj, double bias) {
    if (!obj)
        return;
    ((rt_canvas3d *)obj)->shadow_bias = (float)bias;
}

/// @brief Enable or disable software occlusion culling for draw submission.
void rt_canvas3d_set_occlusion_culling(void *obj, int8_t enabled) {
    if (!obj)
        return;
    ((rt_canvas3d *)obj)->occlusion_culling = enabled;
}

#endif /* VIPER_ENABLE_GRAPHICS */
