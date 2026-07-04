//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_lighting.c
// Purpose: Canvas3D light flattening — pack the canvas's slotted light array
//   (plus scene lights) into the dense vgfx3d_light_params_t array the backend
//   draw path consumes, applying camera-relative rebasing and value sanitizing.
//   Split out of rt_canvas3d.c; the light slots live on rt_canvas3d.
// Key invariants:
//   - Light slots are sparse (NULL-able) so removal keeps stable indices; the
//     packed output is dense and bounded by the active light limit.
// Links: rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_string.h"
#include "vgfx3d_backend.h"

#include <string.h>

/// @brief Clamp a Light3D type id to a backend-supported value.
static int32_t canvas3d_sanitize_light_type(int32_t type) {
    return (type >= 0 && type <= 3) ? type : 0;
}

/// @brief Compact one canvas light into a backend param struct (camera-relative rebased,
///        value-sanitized). NULL inputs are ignored.
static void canvas3d_copy_light_params(const rt_canvas3d *c,
                                       const rt_light3d *l,
                                       vgfx3d_light_params_t *out) {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
    if (!l || !out)
        return;
    if (canvas3d_uses_camera_relative_upload(c)) {
        origin_x = c->camera_relative_origin[0];
        origin_y = c->camera_relative_origin[1];
        origin_z = c->camera_relative_origin[2];
    }
    memset(out, 0, sizeof(*out));
    out->type = canvas3d_sanitize_light_type(l->type);
    out->shadow_index = -1;
    out->shadow_cascade_count = 1;
    out->shadow_projection_type = VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC;
    out->casts_shadows = l->casts_shadows ? 1 : 0;
    out->identity = (uintptr_t)l;
    out->direction[0] = canvas3d_sanitize_f64_to_float(l->direction[0], 0.0f);
    out->direction[1] = canvas3d_sanitize_f64_to_float(l->direction[1], -1.0f);
    out->direction[2] = canvas3d_sanitize_f64_to_float(l->direction[2], 0.0f);
    out->position[0] = canvas3d_sanitize_f64_to_float(l->position[0] - origin_x, 0.0f);
    out->position[1] = canvas3d_sanitize_f64_to_float(l->position[1] - origin_y, 0.0f);
    out->position[2] = canvas3d_sanitize_f64_to_float(l->position[2] - origin_z, 0.0f);
    out->color[0] = canvas3d_clamp01_f64(l->color[0]);
    out->color[1] = canvas3d_clamp01_f64(l->color[1]);
    out->color[2] = canvas3d_clamp01_f64(l->color[2]);
    out->intensity = canvas3d_sanitize_nonnegative_f64(l->intensity, 1.0f);
    out->attenuation = canvas3d_sanitize_nonnegative_f64(l->attenuation, 1.0f);
    out->inner_cos = canvas3d_clamp_f64_to_float(l->inner_cos, -1.0, 1.0, 1.0f);
    out->outer_cos = canvas3d_clamp_f64_to_float(l->outer_cos, -1.0, 1.0, 0.0f);
}

/// @brief Return the active light payload limit for the selected lighting path.
int32_t canvas3d_active_light_limit(rt_canvas3d *c) {
    if (c && c->clustered_lighting &&
        rt_canvas3d_backend_supports(c, rt_const_cstr("clustered-lighting")))
        return VGFX3D_MAX_LIGHTS;
    return VGFX3D_FORWARD_LIGHT_LIMIT;
}

/// @brief Flatten the canvas's sparse light array into a dense backend buffer.
/// @details The canvas stores lights in fixed slots (`lights[0..VGFX3D_MAX_LIGHTS]`)
///   so that dropped-and-readded lights keep stable slot identities, but the
///   GPU backends expect a packed array — this routine bridges the two. Stops
///   when either every slot has been visited or `max` entries have been
///   written, whichever comes first.
///   Plan 07: the output is ordered with directional/ambient lights first (the
///   "global" prefix the clustered shader loops flatly) followed by point/spot
///   lights (looped via per-cluster index lists). Shading is an order-independent
///   sum, so the flat path's output is unchanged by the reorder; within each
///   group the original slot order is preserved so revision stamps stay stable.
/// @return The number of lights actually copied into `out`.
int32_t build_light_params(const rt_canvas3d *c, vgfx3d_light_params_t *out, int32_t max) {
    int32_t count = 0;
    if (!c || !out || max <= 0)
        return 0;
    for (int pass = 0; pass < 2 && count < max; pass++) {
        const int want_global = (pass == 0);
        for (int i = 0; i < VGFX3D_MAX_LIGHTS && count < max; i++) {
            const rt_light3d *l = c->lights[i];
            int32_t type;
            if (!l || !l->enabled)
                continue;
            type = canvas3d_sanitize_light_type(l->type);
            if ((type == 0 || type == 2) != want_global)
                continue;
            canvas3d_copy_light_params(c, l, &out[count]);
            count++;
        }
        for (int i = 0; i < c->scene_light_count && i < VGFX3D_MAX_LIGHTS && count < max; i++) {
            const rt_light3d *l = c->scene_lights[i];
            int32_t type;
            if (!l || !l->enabled)
                continue;
            type = canvas3d_sanitize_light_type(l->type);
            if ((type == 0 || type == 2) != want_global)
                continue;
            canvas3d_copy_light_params(c, l, &out[count]);
            count++;
        }
    }
    return count;
}

#endif /* VIPER_ENABLE_GRAPHICS */

/// @brief Stamp the current light+ambient snapshot with a monotonic revision.
/// @details Compares @p lights (freshly built, memset-padded entries) and the
///          canvas ambient against the previous snapshot; the revision only
///          advances on a real change. Queued draws record the returned stamp
///          so backends can skip re-uploading scene/light constants across
///          runs of draws that share it. Never returns 0 (0 = "unknown,
///          always upload" in the draw command).
uint32_t canvas3d_stamp_light_snapshot(rt_canvas3d *c,
                                       const vgfx3d_light_params_t *lights,
                                       int32_t light_count) {
    vgfx3d_light_params_t *last;
    size_t bytes;

    if (!c)
        return 0;
    if (light_count < 0)
        light_count = 0;
    if (light_count > VGFX3D_MAX_LIGHTS)
        light_count = VGFX3D_MAX_LIGHTS;
    bytes = (size_t)light_count * sizeof(vgfx3d_light_params_t);
    last = (vgfx3d_light_params_t *)c->last_light_snapshot;
    if (!last) {
        last = (vgfx3d_light_params_t *)calloc(VGFX3D_MAX_LIGHTS, sizeof(*last));
        if (!last) {
            /* Snapshot cache unavailable: force per-draw uploads (correct, slower). */
            c->lights_revision++;
            if (c->lights_revision == 0)
                c->lights_revision = 1;
            return c->lights_revision;
        }
        c->last_light_snapshot = last;
        c->last_light_snapshot_valid = 0;
    }
    if (c->last_light_snapshot_valid && c->last_light_snapshot_count == light_count &&
        memcmp(c->last_light_snapshot_ambient,
               c->ambient,
               sizeof(c->last_light_snapshot_ambient)) == 0 &&
        (bytes == 0 || memcmp(last, lights, bytes) == 0)) {
        if (c->lights_revision == 0)
            c->lights_revision = 1;
        return c->lights_revision;
    }
    if (bytes)
        memcpy(last, lights, bytes);
    c->last_light_snapshot_count = light_count;
    memcpy(c->last_light_snapshot_ambient, c->ambient, sizeof(c->last_light_snapshot_ambient));
    c->last_light_snapshot_valid = 1;
    c->lights_revision++;
    if (c->lights_revision == 0)
        c->lights_revision = 1;
    return c->lights_revision;
}
