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
//   and platform-specific, with software fallback always available.
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
#include "rt_action.h"
#include "rt_graphics_internal.h"
#include "rt_input.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_string.h"
#include "rt_time.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief True when the active backend can apply GPU post-FX during present.
///
/// Requires the backend to expose `present_postfx` AND the canvas
/// not be in RTT mode (RTT outputs are read back directly without
/// post-processing).
static int canvas3d_backend_uses_gpu_postfx(const rt_canvas3d *c) {
    return c && c->backend && c->backend->present_postfx && c->render_target == NULL;
}

/// @brief True when the canvas's RTT is owned by a hardware backend.
///
/// Software backends fall back to a CPU-side copy for RTT; hardware
/// backends (D3D11/OpenGL/Metal) keep the texture on the GPU and
/// only read it back when the user requests `Pixels()`.
static int canvas3d_backend_owns_gpu_rtt(const rt_canvas3d *c) {
    return c && c->render_target && c->backend && c->backend != &vgfx3d_software_backend;
}

/// @brief Push the current frame's post-FX enable flag + snapshot to the backend.
///
/// Called by `latch_gpu_postfx_state` after capturing a snapshot.
/// Backends that don't implement these hooks silently no-op.
static void canvas3d_apply_gpu_postfx_state(rt_canvas3d *c) {
    const vgfx3d_postfx_snapshot_t *snapshot = NULL;

    if (!c || !c->backend)
        return;
    if (c->frame_gpu_postfx_enabled)
        snapshot = &c->frame_postfx_snapshot;
    if (c->backend->set_gpu_postfx_enabled)
        c->backend->set_gpu_postfx_enabled(c->backend_ctx, c->frame_gpu_postfx_enabled);
    if (c->backend->set_gpu_postfx_snapshot)
        c->backend->set_gpu_postfx_snapshot(c->backend_ctx, snapshot);
}

/// @brief Capture the current post-FX state into a per-frame snapshot.
///
/// Snapshotting once per frame ensures the post-FX parameters used
/// by the present-time composite match what was active at frame start
/// (avoids tearing if the user toggles a post-FX setting mid-frame).
/// Skips snapshot capture for RTT canvases or non-postfx backends.
static void canvas3d_latch_gpu_postfx_state(rt_canvas3d *c) {
    if (!c)
        return;
    memset(&c->frame_postfx_snapshot, 0, sizeof(c->frame_postfx_snapshot));
    c->frame_gpu_postfx_enabled = 0;
    c->frame_postfx_state_latched = 1;
    if (canvas3d_backend_uses_gpu_postfx(c) &&
        vgfx3d_postfx_get_snapshot(c->postfx, &c->frame_postfx_snapshot)) {
        c->frame_gpu_postfx_enabled = 1;
    }
    canvas3d_apply_gpu_postfx_state(c);
}

/// @brief Apply a window-size change to the canvas + active backend.
///
/// Idempotent on identical size. Updates the canvas's cached width
/// and height, then forwards to the backend's `resize` op so it can
/// rebuild offscreen targets.
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

/// @brief Window-system resize callback — `userdata` is the canvas pointer.
///
/// Hooked into the underlying `vgfx_window_t`'s resize event so the
/// canvas state stays in sync without requiring per-frame polling.
static void rt_canvas3d_on_resize(void *userdata, int32_t w, int32_t h) {
    rt_canvas3d_apply_resize((rt_canvas3d *)userdata, w, h);
}

/// @brief Drop a GC-managed reference held in a `**slot` and zero the slot.
///
/// Idempotent — safe to call on already-NULL slots. Used in the
/// canvas finalizer to release every owned sub-object cleanly.
static void canvas3d_release_owned_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Tell keyboard + mouse subsystems to forget this window.
///
/// Called when the canvas is destroyed. Without this, focus
/// queries would still return the dead window pointer until the
/// next focus event arrived.
static void rt_canvas3d_detach_input(vgfx_window_t gfx_win) {
    if (!gfx_win)
        return;
    rt_keyboard_clear_canvas_if_matches(gfx_win);
    rt_mouse_clear_canvas_if_matches(gfx_win);
}

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

/// @brief Grow the deferred-draw command buffer to hold `needed` entries.
///
/// Geometric growth (cap doubles, starting at 32). Used by the
/// transparency-sort path to buffer commands until end-of-frame.
/// Returns 0 on allocation failure (caller falls back to immediate
/// dispatch in that case).
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

/// @brief Grow the canvas's text-rendering vertex + index scratch buffers.
///
/// Two-pair geometric growth — vertices and indices have separate
/// caps because the ratio depends on the glyph layout. Starting
/// capacities: 256 vertices, 384 indices.
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

/// @brief `qsort` comparator: back-to-front (largest sort_key first).
///
/// Used to sort transparent draws so they composite correctly (back
/// objects drawn first so front objects blend over them).
static int cmp_back_to_front(const void *a, const void *b) {
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka > kb)
        return -1;
    if (ka < kb)
        return 1;
    return 0;
}

/// @brief `qsort` comparator: front-to-back (smallest sort_key first).
///
/// Used for opaque draws — front-to-back order maximizes early-Z
/// rejection, dropping pixel-shader work for occluded fragments.
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

/// @brief Convert a 16-element double-precision matrix to single precision.
///
/// Zia stores matrices as `Mat4` with double components; the GPU
/// uniform path takes float — straightforward narrowing conversion.
static void mat4_d2f(const double *src, float *dst) {
    for (int i = 0; i < 16; i++)
        dst[i] = (float)src[i];
}

/// @brief Whether a draw command needs alpha blending (transparency).
///
/// Two cases trigger blending:
///   1. Material alpha < 1.0 (legacy / Phong workflow).
///   2. PBR workflow with explicit `BLEND` alpha mode.
/// Used to route the command into the deferred transparency-sorted
/// pass instead of the immediate opaque pass.
static int canvas3d_cmd_requires_blend(const vgfx3d_draw_cmd_t *cmd) {
    if (!cmd)
        return 0;
    if (cmd->alpha < 0.999f)
        return 1;
    return (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR &&
            cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND);
}

/// @brief Resolve the effective backface-cull flag for a material draw.
///
/// AND of canvas-level backface cull AND material isn't `double_sided`.
/// Materials marked double-sided (e.g., leaves, fabric) override the
/// canvas setting to disable culling per-draw.
static int8_t canvas3d_material_backface_cull(const rt_canvas3d *c, const rt_material3d *mat) {
    if (!c)
        return 0;
    return (int8_t)(c->backface_cull && !(mat && mat->double_sided));
}

/// @brief Translate every material field into the corresponding draw-command field.
///
/// Per-vertex draw commands are float-typed (matches GPU expectations),
/// materials are double-typed (matches Zia's number type). This helper
/// performs the narrowing conversion plus the material→command name
/// remapping (e.g., `mat->shininess` → `cmd->shininess`).
static void canvas3d_fill_material_cmd(const rt_material3d *mat, vgfx3d_draw_cmd_t *cmd) {
    if (!mat || !cmd)
        return;

    cmd->diffuse_color[0] = (float)mat->diffuse[0];
    cmd->diffuse_color[1] = (float)mat->diffuse[1];
    cmd->diffuse_color[2] = (float)mat->diffuse[2];
    cmd->diffuse_color[3] = (float)mat->diffuse[3];
    cmd->specular[0] = (float)mat->specular[0];
    cmd->specular[1] = (float)mat->specular[1];
    cmd->specular[2] = (float)mat->specular[2];
    cmd->shininess = (float)mat->shininess;
    cmd->alpha = (float)mat->alpha;
    cmd->unlit = (int8_t)(mat->unlit || mat->shading_model == 3);
    cmd->texture = mat->texture;
    cmd->normal_map = mat->normal_map;
    cmd->specular_map = mat->specular_map;
    cmd->emissive_map = mat->emissive_map;
    cmd->metallic_roughness_map = mat->metallic_roughness_map;
    cmd->ao_map = mat->ao_map;
    cmd->emissive_color[0] = (float)mat->emissive[0];
    cmd->emissive_color[1] = (float)mat->emissive[1];
    cmd->emissive_color[2] = (float)mat->emissive[2];
    cmd->metallic = (float)mat->metallic;
    cmd->roughness = (float)mat->roughness;
    cmd->ao = (float)mat->ao;
    cmd->emissive_intensity = (float)mat->emissive_intensity;
    cmd->normal_scale = (float)mat->normal_scale;
    cmd->workflow = mat->workflow;
    cmd->alpha_mode = mat->alpha_mode;
    cmd->alpha_cutoff = (float)mat->alpha_cutoff;
    cmd->double_sided = mat->double_sided ? 1 : 0;
    cmd->env_map = mat->env_map;
    cmd->reflectivity = (float)mat->reflectivity;
    cmd->shading_model = (mat->shading_model == 3) ? 0 : mat->shading_model;
    for (int pi = 0; pi < 8; pi++)
        cmd->custom_params[pi] = (float)mat->custom_params[pi];
}

/// @brief Grow the motion-history table to hold `needed` entries.
///
/// Motion history is keyed by mesh-identity pointer and stores the
/// previous-frame model matrix for motion-blur / TAA. Geometric
/// growth starting at 32 entries.
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

/// @brief Drop motion-history entries that haven't been touched in over a frame.
///
/// In-place compaction. Anything not seen in the current or previous
/// frame is considered stale (the mesh has stopped being drawn or
/// has been destroyed). Bounded eviction prevents the table from
/// growing without bound.
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

/// @brief Look up (and update) the previous-frame model matrix for a mesh.
///
/// Three cases:
///   1. Existing entry, first lookup this frame → roll current→previous,
///      update current, return previous.
///   2. Existing entry, repeat lookup this frame → just return the
///      previous (don't roll twice).
///   3. New entry → register, return "no previous yet".
/// Returns through `out_has_prev` whether the previous frame was
/// available — first-frame draws fall back to current=previous.
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

/// @brief Compact the canvas's slotted light array into a dense param array.
///
/// Canvas lights live in a fixed array with NULL-able slots (so removal
/// doesn't shift indices). This packs the non-NULL slots into a dense
/// array the backend draw path consumes, returning the count.
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

/// @brief Track a malloc'd buffer for end-of-frame cleanup.
///
/// Used when the deferred path needs to allocate a transient instance-
/// matrix buffer that outlives the calling Zia frame. Geometric
/// growth (cap doubles, starting at 8). On growth failure, frees
/// the buffer to avoid a leak.
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

/// @brief Track a GC-managed object for end-of-frame release.
///
/// Retains `obj` immediately so it survives at least until the
/// frame ends, then releases at end-of-frame via `clear_temp_objects`.
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

/// @brief Free every tracked transient buffer (called at end of frame).
static void canvas3d_clear_temp_buffers(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
}

/// @brief Release every tracked transient GC object (called at end of frame).
static void canvas3d_clear_temp_objects(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; i++) {
        if (c->temp_objects[i] && rt_obj_release_check0(c->temp_objects[i]))
            rt_obj_free(c->temp_objects[i]);
    }
    c->temp_obj_count = 0;
}

/// @brief Compute a draw's sort key — squared distance from camera to mesh origin.
///
/// Squared distance avoids a `sqrtf` per draw — order is preserved
/// since distance is non-negative. The mesh origin (model_matrix
/// translation) is the cheapest proxy for "centroid" since exact
/// centroids would need the bounding box per draw.
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

/// @brief Build a 2D-overlay camera (orthographic projection in pixels).
///
/// Used by `BeginOverlayFrame` so the 2D HUD layer can draw with
/// pixel coordinates (top-left origin, Y-down). The +2 padding on
/// each axis avoids edge-clipping at half-pixel coordinates.
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

/// @brief Internal: begin a 2D overlay pass on top of the 3D scene.
///
/// Used by `Canvas3D` to draw HUD elements (text, sprites) on top of
/// the rendered scene. Switches to an orthographic projection,
/// preserves the existing color buffer (so the 3D scene stays
/// visible), and bypasses depth testing. Returns 0 if the canvas is
/// already in a frame or has no backend window.
int canvas3d_begin_overlay_frame(rt_canvas3d *c, int8_t preserve_existing_color) {
    vgfx3d_camera_params_t params;

    if (!c || !c->backend || !c->gfx_win || c->in_frame)
        return 0;
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);
    canvas3d_build_ortho_camera(c, &params);
    params.load_existing_color = preserve_existing_color ? 1 : 0;
    params.load_existing_depth = 0;
    if (!c->frame_postfx_state_latched)
        canvas3d_latch_gpu_postfx_state(c);
    else
        canvas3d_apply_gpu_postfx_state(c);
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

/// @brief Internal: retrieve the active scene view-projection matrix.
///
/// Three-way fallback: in a 3D frame → current frame's VP; otherwise
/// in an overlay frame → last 3D frame's VP (so overlays project
/// using the camera that drew the scene); otherwise → 2D ortho VP.
/// Used by 3D-aware overlay drawables (e.g., world-space tooltips).
const float *canvas3d_active_scene_vp(const rt_canvas3d *c) {
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

/// @brief Forward a draw command to the active backend's `submit_draw` op.
///
/// Thin wrapper that exists mostly so call sites read a single noun
/// ("submit a mesh") instead of a 7-arg backend method call.
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

/// @brief Decompose an instanced draw into N individual mesh draws.
///
/// Backends without `submit_draw_instanced` (e.g., software fallback)
/// can still render instanced data this way — it's slower but
/// preserves correctness. Per-instance matrices are unpacked into
/// individual `model_matrix`/`prev_model_matrix` pairs, with
/// `has_prev_instance_matrices` translated into per-mesh
/// `has_prev_model_matrix` flags.
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

/// @brief Append a draw to the deferred-draw queue (transparency / sort path).
///
/// The queue is dispatched at end-of-frame in sorted order. Captures
/// every parameter the backend needs so the snapshot survives even if
/// caller-side state (lights, ambient) changes between enqueue and
/// flush. Returns 0 on capacity-grow failure.
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

/// @brief Dispatch a single deferred draw to the backend.
///
/// Routes mesh kind directly to `submit_draw`. Routes instanced kind
/// to `submit_draw_instanced` if the backend supports it, otherwise
/// falls back to `canvas3d_submit_instanced_as_meshes`.
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

/// @brief Dispatch a deferred draw to the shadow-pass path.
///
/// Same dispatch shape as `submit_deferred` but routes through the
/// backend's depth-only shadow draw entry. Instanced shadow draws
/// always decompose to per-mesh draws (most backends don't have an
/// instanced shadow path).
static void canvas3d_shadow_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd || !c->backend || !c->backend->shadow_draw)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        canvas3d_submit_instanced_as_meshes(c, dd, 1);
        return;
    }
    c->backend->shadow_draw(c->backend_ctx, &dd->cmd);
}

/// @brief In-place AABB union: `[io_min, io_max] ⊇ [mn, mx]`.
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

/// @brief Row-major 4×4 matrix multiply: `out = a * b`.
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

/// @brief Compute world-space AABB for a deferred draw, unioning into `[io_min, io_max]`.
///
/// Iterates per-instance for instanced draws (so the shadow VP fits
/// every instance) or just transforms the single mesh AABB for normal
/// draws. `io_has_bounds` flips to true on first contribution.
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

/// @brief Build a tight orthographic shadow-map VP that bounds every opaque draw.
///
/// Algorithm:
///   1. Compute world-space AABB of all opaque draws.
///   2. Pick a light-space view position behind the AABB along the
///      light direction, looking at the AABB center.
///   3. Build the view matrix (right-handed, custom up vector).
///   4. Transform the 8 AABB corners into light space to find the
///      tight orthographic bounds.
///   5. Build the orthographic projection from those bounds.
/// Returns 0 if there are no opaque draws (nothing to shadow).
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
        if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || canvas3d_cmd_requires_blend(&cmds[i].cmd))
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
        proj[10] = 2.0f / (far_z - near_z);
        proj[11] = -(far_z + near_z) / (far_z - near_z);
        proj[15] = 1.0f;
    }

    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/*==========================================================================
 * Canvas3D lifecycle
 *=========================================================================*/

/// @brief GC finalizer — release the backend context and every owned scratch buffer.
///
/// Walks the deferred-command, temp-buffer, temp-object, motion
/// history, and text-vertex arrays, freeing each. Backend contexts
/// (D3D11/OpenGL/Software) destroy themselves through their
/// virtual `destroy_ctx`. Idempotent: nulled pointers prevent
/// double-free if the GC sweeps the canvas twice during shutdown.
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

    canvas3d_release_owned_ref(&c->postfx);
    canvas3d_release_owned_ref((void **)&c->render_target_owner);
    c->render_target = NULL;

    if (c->gfx_win) {
        rt_canvas3d_detach_input(c->gfx_win);
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
    }
}

/// @brief Create a new 3D rendering canvas (window + backend context).
/// @details Opens a platform window, selects the platform-default rendering backend
///          with software fallback if initialization fails, and initializes the framebuffer,
///          depth buffer, deferred draw queue, and motion blur history. The canvas
///          is the main entry point for 3D rendering — call Begin/DrawMesh/End/Flip
///          each frame. GC finalizer destroys the backend context and window.
/// @param title Window title (runtime string).
/// @param w     Window width in pixels (1–8192).
/// @param h     Window height in pixels (1–8192).
/// @return Opaque canvas handle, or NULL on failure.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h) {
    vgfx_framebuffer_t fb;
    int32_t initial_width = (int32_t)w;
    int32_t initial_height = (int32_t)h;

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

    vgfx_set_coord_scale(c->gfx_win, vgfx_window_get_scale(c->gfx_win));
    if (vgfx_get_framebuffer(c->gfx_win, &fb) && fb.width > 0 && fb.height > 0) {
        initial_width = fb.width;
        initial_height = fb.height;
    }

    c->width = initial_width;
    c->height = initial_height;

    /* Select and initialize the platform-default backend, with software fallback. */
    c->backend = vgfx3d_select_backend();
    c->backend_ctx = c->backend->create_ctx(c->gfx_win, initial_width, initial_height);
    if (!c->backend_ctx) {
        /* Selected backend failed — fall back to software. */
        c->backend = &vgfx3d_software_backend;
        c->backend_ctx = c->backend->create_ctx(c->gfx_win, initial_width, initial_height);
        if (!c->backend_ctx) {
            rt_trap("Canvas3D.New: backend initialization failed");
            return NULL;
        }
    }
    vgfx_set_gpu_present(c->gfx_win, c->backend != &vgfx3d_software_backend);

    vgfx_set_resize_callback(c->gfx_win, rt_canvas3d_on_resize, c);

    c->ambient[0] = 0.1f;
    c->ambient[1] = 0.1f;
    c->ambient[2] = 0.1f;
    c->backface_cull = 0; /* disabled by default — extreme perspective can reverse
                           * screen-space winding, causing false culling. Users can
                           * enable with SetBackfaceCull(canvas, true) if needed. */
    c->render_target = NULL;
    c->render_target_owner = NULL;
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
                            ((uint32_t)(uint8_t)((float)b * 255.0f) << 16) | 0xFF000000u;
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

/// @brief Decide which deferred pass owns 2D screen-space draws based on canvas mode.
static deferred_pass_t canvas3d_screen_pass_kind(const rt_canvas3d *c) {
    return (c && c->frame_is_2d) ? DEFERRED_PASS_MAIN : DEFERRED_PASS_SCREEN_OVERLAY;
}

/// @brief Append a 2D triangle list (positions + UVs + color) to the deferred queue.
///
/// Used as the underlying primitive for `canvas3d_queue_screen_rect`
/// and `canvas3d_queue_screen_line`. Manages the auto-grow of the
/// command buffer and decides whether the geometry lands in the
/// pre-3D, post-3D, or HUD pass via `canvas3d_screen_pass_kind`.
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

/// @brief Convenience wrapper: queue a screen-space rectangle as two triangles.
int canvas3d_queue_screen_rect(
    rt_canvas3d *c, float x, float y, float w, float h, float r, float g, float b, float a) {
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

/// @brief Queue a screen-space line as a thin quad (tessellated triangles).
///
/// Width is in screen pixels. The endpoints define the centerline;
/// the quad is built by extruding by `width/2` perpendicular to
/// the segment direction. Properly aligned for sub-pixel positions.
int canvas3d_queue_screen_line(rt_canvas3d *c,
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
        return canvas3d_queue_screen_rect(
            c, x0 - thickness * 0.5f, y0 - thickness * 0.5f, thickness, thickness, r, g, b, a);
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

/// @brief Switch the canvas into 2D-overlay mode for the next batch of draw calls.
///
/// All subsequent screen-space draws (rects, lines, sprites, text)
/// queue into the post-3D HUD pass, which renders after the 3D
/// scene, postFX, and tonemapping. Pair with `rt_canvas3d_end_2d`
/// to return to 3D drawing.
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

    canvas3d_latch_gpu_postfx_state(c);
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

    canvas3d_latch_gpu_postfx_state(c);
    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

/// @brief Monotonically-increasing per-frame serial; used for cache invalidation.
///
/// Resources keyed off `(canvas, serial)` know they're stale when
/// their cached serial is older than the canvas's current value.
int64_t rt_canvas3d_get_frame_serial(void *obj) {
    return obj ? ((rt_canvas3d *)obj)->frame_serial : 0;
}

/// @brief Queue a 3D mesh draw with a model matrix and a sort key for transparency ordering.
///
/// `sort_key` is used by the deferred renderer to depth-sort
/// translucent draws back-to-front before flushing. Opaque
/// objects ignore it (the depth buffer handles their order).
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
    canvas3d_resolve_previous_model(c,
                                    motion_key,
                                    dd->cmd.model_matrix,
                                    dd->cmd.prev_model_matrix,
                                    &dd->cmd.has_prev_model_matrix);
    canvas3d_fill_material_cmd(mat, &dd->cmd);

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
    dd->cmd.morph_key = mesh->morph_targets_ref;
    dd->cmd.morph_revision = mesh->morph_targets_ref
                                 ? rt_morphtarget3d_get_payload_generation(mesh->morph_targets_ref)
                                 : 0;

    /* Build light params */
    dd->light_count = build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS);
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->wireframe = c->wireframe;
    dd->backface_cull = canvas3d_material_backface_cull(c, mat);
    dd->has_local_bounds = 1;
    memcpy(dd->local_bounds_min, mesh->aabb_min, sizeof(dd->local_bounds_min));
    memcpy(dd->local_bounds_max, mesh->aabb_max, sizeof(dd->local_bounds_max));

    /* Compute sort key: squared distance from camera to mesh centroid.
     * Uses model matrix translation (column 3 in row-major) as centroid proxy. */
    {
        dd->sort_key = canvas3d_compute_sort_key(c, dd->cmd.model_matrix);
    }
}

/// @brief Convenience: queue a mesh draw without an explicit sort key (uses default).
/// @see rt_canvas3d_draw_mesh_matrix_keyed
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

/// @brief Queue an instanced draw — render `instance_count` copies of `mesh` with per-instance transforms.
///
/// One draw call instead of `instance_count` separate calls.
/// Used for foliage, debris, particle clouds, and any scene with
/// many copies of the same mesh. The matrix array is owned by the
/// caller for the duration of the frame; the canvas keeps a
/// reference until the deferred queue flushes.
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
    canvas3d_fill_material_cmd(mat, &base_cmd);
    base_cmd.bone_palette = mesh->bone_palette;
    base_cmd.prev_bone_palette = mesh->prev_bone_palette;
    base_cmd.bone_count = mesh->bone_count;
    base_cmd.morph_deltas = mesh->morph_deltas;
    base_cmd.morph_normal_deltas = mesh->morph_normal_deltas;
    base_cmd.morph_weights = mesh->morph_weights;
    base_cmd.prev_morph_weights = mesh->prev_morph_weights;
    base_cmd.morph_shape_count = mesh->morph_shape_count;
    base_cmd.morph_key = mesh->morph_targets_ref;
    base_cmd.morph_revision = mesh->morph_targets_ref
                                  ? rt_morphtarget3d_get_payload_generation(mesh->morph_targets_ref)
                                  : 0;

    if (canvas3d_cmd_requires_blend(&base_cmd) || !c->backend->submit_draw_instanced) {
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
                                        canvas3d_material_backface_cull(c, mat),
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
            float key = canvas3d_compute_sort_key(c, &instance_matrices[(size_t)i * 16u]);
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
                                    canvas3d_material_backface_cull(c, mat),
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
/// @brief End-of-frame: flush all deferred passes, run postFX, present, and bookkeep.
///
/// Pass order: pre-3D HUD → opaque mesh → translucent (sorted) →
/// postFX (bloom, tonemap, motion blur) → post-3D HUD → present.
/// Increments `frame_serial`, clears the deferred queues, and
/// releases per-frame temp objects.
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

                        world[0] = inv_vp[0] * clip[0] + inv_vp[1] * clip[1] + inv_vp[2] * clip[2] +
                                   inv_vp[3] * clip[3];
                        world[1] = inv_vp[4] * clip[0] + inv_vp[5] * clip[1] + inv_vp[6] * clip[2] +
                                   inv_vp[7] * clip[3];
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
                    if (cmds[i].pass_kind != DEFERRED_PASS_MAIN ||
                        canvas3d_cmd_requires_blend(&cmds[i].cmd))
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
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                    !canvas3d_cmd_requires_blend(&cmds[i].cmd))
                    opaque_count++;
            }
            if (opaque_count > 0 &&
                ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, opaque_count)) {
                deferred_draw_t *opaque = (deferred_draw_t *)c->trans_cmds;
                int32_t oi = 0;
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                        !canvas3d_cmd_requires_blend(&cmds[i].cmd))
                        opaque[oi++] = cmds[i];
                }
                qsort(opaque, (size_t)opaque_count, sizeof(deferred_draw_t), cmp_front_to_back);
                for (int32_t i = 0; i < opaque_count; i++)
                    canvas3d_submit_deferred(c, &opaque[i]);
            }
        } else {
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                    !canvas3d_cmd_requires_blend(&cmds[i].cmd))
                    canvas3d_submit_deferred(c, &cmds[i]);
            }
        }

        {
            int32_t trans_count = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                    canvas3d_cmd_requires_blend(&cmds[i].cmd))
                    trans_count++;
            }
            if (trans_count > 0 &&
                ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, trans_count)) {
                deferred_draw_t *trans = (deferred_draw_t *)c->trans_cmds;
                int32_t ti = 0;
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                        canvas3d_cmd_requires_blend(&cmds[i].cmd))
                        trans[ti++] = cmds[i];
                }
                qsort(trans, (size_t)trans_count, sizeof(deferred_draw_t), cmp_back_to_front);
                for (int32_t i = 0; i < trans_count; i++)
                    canvas3d_submit_deferred(c, &trans[i]);
            }
        }
    }

    c->backend->end_frame(c->backend_ctx);
    c->in_frame = 0;

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
        if (c->frame_gpu_postfx_enabled) {
            c->backend->present_postfx(c->backend_ctx, &c->frame_postfx_snapshot);
            gpu_postfx_presented = 1;
        }
    }

    if (!gpu_postfx_presented) {
        /* Apply post-processing effects to the software framebuffer */
        rt_postfx3d_apply_to_canvas(obj);
    }

    /* Present the GPU drawable / swap the back buffer after all queued passes
     * for the frame have rendered into the backend's scene targets. */
    if (!gpu_postfx_presented && c->backend && c->backend->present)
        c->backend->present(c->backend_ctx);

    /* Always call vgfx_update to keep the window alive and process display
     * refresh. GPU backends own the final on-screen present path. */
    vgfx_update(c->gfx_win);
    c->frame_postfx_state_latched = 0;
    c->frame_gpu_postfx_enabled = 0;
    memset(&c->frame_postfx_snapshot, 0, sizeof(c->frame_postfx_snapshot));

    int64_t now_us = rt_clock_ticks_us();
    if (c->last_flip_us > 0) {
        int64_t delta_us = now_us - c->last_flip_us;
        c->delta_time_ms = delta_us > 0 ? delta_us / 1000 : 0;
    } else
        c->delta_time_ms = 0;
    c->last_flip_us = now_us;

    if (vgfx_close_requested(c->gfx_win)) {
        rt_canvas3d_detach_input(c->gfx_win);
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
        c->should_close = 1;
    }
}

/// @brief Process all pending window events (keyboard, mouse, resize, close).
/// @details Polls the platform event queue and updates input state for
///          Keyboard/Mouse/Pad subsystems. Must be called once per frame.
/// @return Event type code of the last event processed, or 0 if no events.
/// @brief Translate physical pixel coordinates to logical (HiDPI-scaled) and notify the mouse subsystem.
static void rt_canvas3d_update_mouse_from_physical(vgfx_window_t gfx_win, int32_t x, int32_t y) {
    float scale = vgfx_window_get_scale(gfx_win);
    if (scale < 0.001f)
        scale = 1.0f;
    rt_mouse_update_pos((int64_t)((double)x / (double)scale),
                        (int64_t)((double)y / (double)scale));
}

/// @brief Pump platform events, advance per-frame input state, and return whether the window is still open.
///
/// Called once per game-loop iteration. Drives keyboard/mouse/
/// gamepad/action input subsystems, updates the wall-clock dt
/// (capped at `dt_max` to prevent huge jumps after pauses), and
/// dispatches resize / focus / close events.
/// @return 1 if the window remains open, 0 if the user requested close.
int64_t rt_canvas3d_poll(void *obj) {
    if (!obj)
        return 0;
    rt_canvas3d *c = (rt_canvas3d *)obj;
    if (!c->gfx_win)
        return 0;

    int8_t captured = rt_mouse_is_captured();

    /* Begin frame (resets per-frame state for keyboard/mouse/pad) */
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    rt_pad_poll();

    if (!vgfx_pump_events(c->gfx_win))
        c->should_close = 1;

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
    int64_t last_event_type = VGFX_EVENT_NONE;
    while (vgfx_poll_event(c->gfx_win, &evt)) {
        last_event_type = (int64_t)evt.type;
        if (evt.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)evt.data.key.key);
        else if (evt.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)evt.data.key.key);
        else if (evt.type == VGFX_EVENT_TEXT_INPUT)
            rt_keyboard_text_input((int32_t)evt.data.text.codepoint);
        else if (evt.type == VGFX_EVENT_CLOSE)
            c->should_close = 1;
        else if (!captured && evt.type == VGFX_EVENT_MOUSE_MOVE) {
            rt_canvas3d_update_mouse_from_physical(
                c->gfx_win, evt.data.mouse_move.x, evt.data.mouse_move.y);
        } else if (evt.type == VGFX_EVENT_MOUSE_DOWN) {
            rt_canvas3d_update_mouse_from_physical(
                c->gfx_win, evt.data.mouse_button.x, evt.data.mouse_button.y);
            rt_mouse_button_down((int64_t)evt.data.mouse_button.button);
        } else if (evt.type == VGFX_EVENT_MOUSE_UP) {
            rt_canvas3d_update_mouse_from_physical(
                c->gfx_win, evt.data.mouse_button.x, evt.data.mouse_button.y);
            rt_mouse_button_up((int64_t)evt.data.mouse_button.button);
        } else if (evt.type == VGFX_EVENT_RESIZE) {
            rt_canvas3d_apply_resize(c, evt.data.resize.width, evt.data.resize.height);
        } else if (evt.type == VGFX_EVENT_SCROLL) {
            rt_canvas3d_update_mouse_from_physical(c->gfx_win, evt.data.scroll.x, evt.data.scroll.y);
            rt_mouse_update_wheel((double)evt.data.scroll.delta_x,
                                  (double)evt.data.scroll.delta_y);
        }
    }

    if (!captured) {
        vgfx_mouse_pos(c->gfx_win, &mx, &my);
        rt_mouse_update_pos((int64_t)mx, (int64_t)my);
    }

    /* Update action mapping state after input devices and event queues are
     * finalized so action queries observe this frame's input. */
    rt_action_update();

    /* Warp cursor to center for next frame (only when captured) */
    if (captured) {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        vgfx_warp_cursor(c->gfx_win, cw / 2, ch / 2);
    }

    return last_event_type;
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

/// @brief Park a `malloc`'d buffer for end-of-frame disposal.
///
/// Used by skinning / morph-target paths that allocate
/// per-draw vertex transforms — the canvas owns the lifetime
/// until after the GPU has consumed the data on `end()`.
void rt_canvas3d_add_temp_buffer(void *obj, void *buffer) {
    if (!obj || !buffer)
        return;
    (void)canvas3d_track_temp_buffer((rt_canvas3d *)obj, buffer);
}

/// @brief Park a GC-managed object reference for end-of-frame release.
///
/// Lets a draw call reference an object (mesh, material, pixels)
/// that might otherwise be collected before the deferred queue
/// flushes. The canvas drops the reference in `rt_canvas3d_end`.
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

/// @brief Cap the per-frame delta-time at `max_ms` milliseconds (prevents huge jumps after pauses).
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

/*==========================================================================
 * Fog — linear distance fog
 *=========================================================================*/

/// @brief Configure the global fog parameters (color, near/far, density).
///
/// Applied by the postFX stage as a depth-keyed blend toward
/// `fog_color`. Setting `fog_density` to 0 disables fog without
/// changing the queued draws.
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

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
