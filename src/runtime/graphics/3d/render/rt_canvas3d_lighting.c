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
    out->casts_shadows = l->casts_shadows ? 1 : 0;
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
/// @return The number of lights actually copied into `out`.
int32_t build_light_params(const rt_canvas3d *c, vgfx3d_light_params_t *out, int32_t max) {
    int32_t count = 0;
    if (!c || !out || max <= 0)
        return 0;
    for (int i = 0; i < VGFX3D_MAX_LIGHTS && count < max; i++) {
        const rt_light3d *l = c->lights[i];
        if (!l || !l->enabled)
            continue;
        canvas3d_copy_light_params(c, l, &out[count]);
        count++;
    }
    for (int i = 0; i < c->scene_light_count && i < VGFX3D_MAX_LIGHTS && count < max; i++) {
        const rt_light3d *l = c->scene_lights[i];
        if (!l || !l->enabled)
            continue;
        canvas3d_copy_light_params(c, l, &out[count]);
        count++;
    }
    return count;
}

#endif /* VIPER_ENABLE_GRAPHICS */
