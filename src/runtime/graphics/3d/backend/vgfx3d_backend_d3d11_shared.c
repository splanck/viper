//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shared.c
// Purpose: D3D11 backend helpers shared with other backends — constant-buffer
//   packing (HLSL float4 alignment), instance/bone palette upload prep,
//   per-frame TAA history, mip count math, capacity growth, and policy
//   choices for render-target / blend / color-format selection.
//
// Key invariants:
//   - HLSL constant buffers require float4 alignment, so scalar arrays must be
//     packed into vec4 slots (one scalar per .x, padding zeros in .yzw).
//   - Bone palette is a fixed VGFX3D_D3D11_MAX_BONES × mat4 (16 floats); the
//     upload is zero-padded and clamped to that supported cap to keep the
//     cbuffer size constant.
//   - Frame history tracks scene VP and previous-frame VP separately from
//     overlay/draw VP so motion vectors stay correct across overlay passes.
//
// Links: vgfx3d_backend_d3d11_shared.h, vgfx3d_backend_d3d11.c
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_d3d11_shared.h"

#include "rt_textureasset3d.h"
#include "vgfx3d_backend_utils.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/// @brief Pack a flat scalar array into HLSL-aligned float4 slots, four scalars per vector.
/// Remaining vector lanes are zeroed. Truncates if @p src exceeds @p dst capacity.
void vgfx3d_d3d11_pack_scalar_array4(float (*dst)[4],
                                     int32_t dst_vec_count,
                                     const float *src,
                                     int32_t src_scalar_count) {
    int32_t scalar_capacity;

    if (!dst || dst_vec_count <= 0)
        return;

    memset(dst, 0, (size_t)dst_vec_count * sizeof(dst[0]));
    if (!src || src_scalar_count <= 0)
        return;

    scalar_capacity = dst_vec_count > INT_MAX / 4 ? INT_MAX : dst_vec_count * 4;
    if (src_scalar_count > scalar_capacity)
        src_scalar_count = scalar_capacity;
    for (int32_t i = 0; i < src_scalar_count; i++)
        dst[i / 4][i % 4] = isfinite(src[i]) ? src[i] : 0.0f;
}

/// @brief Store a row-major identity matrix into one fixed bone-palette slot.
/// @details D3D11 identity-pads unused bones instead of zero-padding them so
///   malformed skinning indices that reference an unused palette entry leave
///   vertices in bind pose instead of collapsing them to the origin.
static void vgfx3d_d3d11_store_identity4x4(float *dst) {
    memset(dst, 0, sizeof(float) * 16u);
    dst[0] = 1.0f;
    dst[5] = 1.0f;
    dst[10] = 1.0f;
    dst[15] = 1.0f;
}

/// @brief Return non-zero only when every float in @p values is finite.
int vgfx3d_d3d11_float_array_is_finite(const float *values, size_t count) {
    if (!values && count > 0)
        return 0;
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(values[i]))
            return 0;
    }
    return 1;
}

/// @brief Copy float constants while replacing NaN/Inf lanes with @p fallback.
void vgfx3d_d3d11_copy_float_array_finite_or(float *dst,
                                             const float *src,
                                             size_t count,
                                             float fallback) {
    float safe_fallback = isfinite(fallback) ? fallback : 0.0f;

    if (!dst || count == 0)
        return;
    if (!src) {
        for (size_t i = 0; i < count; i++)
            dst[i] = safe_fallback;
        return;
    }
    for (size_t i = 0; i < count; i++)
        dst[i] = isfinite(src[i]) ? src[i] : safe_fallback;
}

/// @brief Validate a finite direction vector before CPU constants or HLSL normalize.
int vgfx3d_d3d11_vec3_direction_is_usable(const float *values) {
    double len2;

    if (!values || !vgfx3d_d3d11_float_array_is_finite(values, 3u))
        return 0;
    len2 = (double)values[0] * (double)values[0] + (double)values[1] * (double)values[1] +
           (double)values[2] * (double)values[2];
    return len2 > 1.0e-12 && len2 < 1.0e20 ? 1 : 0;
}

/// @brief Copy a direction vector with a stable default for zero/non-finite/huge sources.
void vgfx3d_d3d11_copy_vec3_direction_or(float *dst,
                                         const float *src,
                                         const float fallback[3]) {
    static const float default_forward[3] = {0.0f, 0.0f, -1.0f};
    const float *chosen;

    if (!dst)
        return;
    if (vgfx3d_d3d11_vec3_direction_is_usable(src))
        chosen = src;
    else if (vgfx3d_d3d11_vec3_direction_is_usable(fallback))
        chosen = fallback;
    else
        chosen = default_forward;
    dst[0] = chosen[0];
    dst[1] = chosen[1];
    dst[2] = chosen[2];
}

/// @brief Copy a matrix when finite, otherwise write identity.
void vgfx3d_d3d11_copy_mat4_finite_or_identity(float *dst, const float *src) {
    if (!dst)
        return;
    if (src && vgfx3d_d3d11_float_array_is_finite(src, 16u)) {
        memcpy(dst, src, sizeof(float) * 16u);
        return;
    }
    vgfx3d_d3d11_store_identity4x4(dst);
}

/// @brief Copy a matrix when finite, otherwise copy a finite fallback or identity.
void vgfx3d_d3d11_copy_mat4_finite_or(float *dst, const float *src, const float *fallback) {
    if (!dst)
        return;
    if (src && vgfx3d_d3d11_float_array_is_finite(src, 16u)) {
        memcpy(dst, src, sizeof(float) * 16u);
        return;
    }
    if (fallback && vgfx3d_d3d11_float_array_is_finite(fallback, 16u)) {
        memcpy(dst, fallback, sizeof(float) * 16u);
        return;
    }
    vgfx3d_d3d11_store_identity4x4(dst);
}

/// @brief Copy a bone palette (mat4 per bone) into a fixed-size cbuffer slot.
/// Fills unused slots with identity so out-of-range indices do not collapse vertices.
/// If @p bone_count exceeds `VGFX3D_D3D11_MAX_BONES`, the upload is clamped to
/// the largest supported palette size for this backend.
void vgfx3d_d3d11_pack_bone_palette(float *dst, const float *src, int32_t bone_count) {
    int32_t first_unused = 0;

    if (!dst)
        return;

    if (src && bone_count > 0) {
        if (bone_count > VGFX3D_D3D11_MAX_BONES)
            bone_count = VGFX3D_D3D11_MAX_BONES;
        for (int32_t i = 0; i < bone_count; i++)
            vgfx3d_d3d11_copy_mat4_finite_or_identity(&dst[(size_t)i * 16u], &src[(size_t)i * 16u]);
        first_unused = bone_count;
    }
    for (int32_t i = first_unused; i < VGFX3D_D3D11_MAX_BONES; i++)
        vgfx3d_d3d11_store_identity4x4(&dst[(size_t)i * 16u]);
}

/// @brief Build per-instance cbuffer entries (model + normal + prev_model) for instanced draws.
/// When previous-frame matrices are missing, prev_model is filled with the current model so
/// motion-vector shaders compute zero displacement (no false motion).
void vgfx3d_d3d11_fill_instance_data(vgfx3d_d3d11_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices) {
    if (!dst || instance_count <= 0 || !instance_matrices)
        return;

    for (int32_t i = 0; i < instance_count; i++) {
        const float *model = &instance_matrices[(size_t)i * 16u];
        vgfx3d_d3d11_copy_mat4_finite_or_identity(dst[i].model, model);
        vgfx3d_compute_normal_matrix4(dst[i].model, dst[i].normal);
        if (has_prev_instance_matrices && prev_instance_matrices) {
            vgfx3d_d3d11_copy_mat4_finite_or(
                dst[i].prev_model, &prev_instance_matrices[(size_t)i * 16u], dst[i].model);
        } else {
            memcpy(dst[i].prev_model, dst[i].model, sizeof(dst[i].prev_model));
        }
    }
}

/// @brief Decide whether instanced motion-history attributes are actually available.
int vgfx3d_d3d11_should_use_previous_instance_matrices(const float *prev_instance_matrices,
                                                       int8_t has_prev_instance_matrices) {
    return has_prev_instance_matrices && prev_instance_matrices ? 1 : 0;
}

/// @brief Roll the per-frame VP/inv-VP/cam-pos history forward by one frame.
/// Scene VP and overlay VP are tracked separately because overlay passes use
/// the current VP for both "current" and "previous" (no temporal coherence).
void vgfx3d_d3d11_update_frame_history(vgfx3d_d3d11_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target) {
    float safe_vp[16];
    float safe_inv_vp[16];

    if (!history || !vp || !inv_vp)
        return;
    vgfx3d_d3d11_copy_mat4_finite_or_identity(safe_vp, vp);
    vgfx3d_d3d11_copy_mat4_finite_or_identity(safe_inv_vp, inv_vp);

    if (!is_overlay_pass) {
        if (history->scene_history_valid) {
            memcpy(history->scene_prev_vp, history->scene_vp, sizeof(history->scene_prev_vp));
        } else {
            memcpy(history->scene_prev_vp, safe_vp, sizeof(history->scene_prev_vp));
            history->scene_history_valid = 1;
        }
        memcpy(history->scene_vp, safe_vp, sizeof(history->scene_vp));
        memcpy(history->scene_inv_vp, safe_inv_vp, sizeof(history->scene_inv_vp));
        memcpy(history->draw_prev_vp, history->scene_prev_vp, sizeof(history->draw_prev_vp));
        if (cam_pos)
            vgfx3d_d3d11_copy_float_array_finite_or(history->scene_cam_pos, cam_pos, 3u, 0.0f);
        history->overlay_used_this_frame = 0;
        return;
    }

    memcpy(history->draw_prev_vp, safe_vp, sizeof(history->draw_prev_vp));
    history->overlay_used_this_frame = uses_separate_overlay_target ? 1 : 0;
}

/// @brief Reconcile bone-buffer upload outcomes into the per-object draw flags.
/// If the current upload failed, both skinning flags are cleared so the shader
/// falls back to the unskinned path. If only the prev-frame upload failed, motion
/// vectors degrade gracefully to "no skinning history".
void vgfx3d_d3d11_resolve_bone_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                             int current_upload_ok,
                                             int prev_upload_ok) {
    if (!object_data)
        return;
    if (!current_upload_ok) {
        object_data->has_skinning = 0;
        object_data->has_prev_skinning = 0;
        return;
    }
    if (!prev_upload_ok)
        object_data->has_prev_skinning = 0;
}

/// @brief Decide if shader skinning can run; palette uploads clamp to shader capacity.
int vgfx3d_d3d11_should_enable_skinning(const float *bone_palette, int32_t bone_count) {
    return (bone_palette && bone_count > 0) ? 1 : 0;
}

/// @brief Reconcile morph-target upload outcomes (positions and normals) into draw flags.
/// On failure the shape count drops to 0 (mesh renders un-morphed). If only normal
/// deltas fail, the position morph still applies but normals will be re-derived from
/// the morphed positions.
void vgfx3d_d3d11_resolve_morph_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                              int morph_upload_ok,
                                              int morph_normal_upload_ok) {
    if (!object_data)
        return;
    if (!morph_upload_ok) {
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return;
    }
    if (!morph_normal_upload_ok)
        object_data->has_morph_normal_deltas = 0;
}

/// @brief Compute the number of mipmap levels required to reach 1×1 from (width × height).
/// Returns 1 for invalid dimensions. Used when creating textures with full mip chains.
int32_t vgfx3d_d3d11_compute_mip_count(int32_t width, int32_t height) {
    int32_t mip_count = 1;

    if (width <= 0 || height <= 0)
        return 1;
    while (width > 1 || height > 1) {
        if (width > 1)
            width >>= 1;
        if (height > 1)
            height >>= 1;
        mip_count++;
    }
    return mip_count;
}

/// @brief Clamp material sampler anisotropy into the backend cacheable [1,16] range.
int32_t vgfx3d_d3d11_sanitize_anisotropy(int32_t requested) {
    if (requested < 1)
        return 1;
    if (requested > VGFX3D_D3D11_MAX_TEXTURE_ANISOTROPY)
        return VGFX3D_D3D11_MAX_TEXTURE_ANISOTROPY;
    return requested;
}

/// @brief Convert sanitized anisotropy to a compact cache index [0,15].
int32_t vgfx3d_d3d11_sampler_anisotropy_index(int32_t requested) {
    return vgfx3d_d3d11_sanitize_anisotropy(requested) - 1;
}

/// @brief Normalize texture UV-set selectors to the shader-visible uv0/uv1 range.
int32_t vgfx3d_d3d11_sanitize_texture_uv_set(int32_t requested) {
    return requested > 0 ? 1 : 0;
}

/// @brief Clamp an integer cbuffer parameter, tolerating inverted caller bounds.
int32_t vgfx3d_d3d11_clamp_int_param(int32_t requested, int32_t min_value, int32_t max_value) {
    int32_t tmp;

    if (min_value > max_value) {
        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (requested < min_value)
        return min_value;
    if (requested > max_value)
        return max_value;
    return requested;
}

/// @brief Replace NaN/Inf float parameters before D3D11 cbuffer/state upload.
float vgfx3d_d3d11_finite_or(float requested, float fallback) {
    if (isfinite(requested))
        return requested;
    return isfinite(fallback) ? fallback : 0.0f;
}

/// @brief Clamp a finite float parameter, tolerating inverted caller bounds.
float vgfx3d_d3d11_clamp_float_param(float requested,
                                     float min_value,
                                     float max_value,
                                     float fallback) {
    float safe_fallback = isfinite(fallback) ? fallback : 0.0f;
    float tmp;

    if (!isfinite(requested))
        requested = safe_fallback;
    if (!isfinite(min_value) && !isfinite(max_value)) {
        min_value = safe_fallback;
        max_value = safe_fallback;
    } else if (!isfinite(min_value)) {
        min_value = max_value;
    } else if (!isfinite(max_value)) {
        max_value = min_value;
    }
    if (min_value > max_value) {
        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (requested < min_value)
        return min_value;
    if (requested > max_value)
        return max_value;
    return requested;
}

/// @brief Normalize arbitrary integer flags to shader-facing 0/1 values.
int32_t vgfx3d_d3d11_sanitize_bool_flag(int32_t requested) {
    return requested ? 1 : 0;
}

/// @brief Clamp a clustered-light global prefix to the uploaded light-array range.
int32_t vgfx3d_d3d11_sanitize_cluster_global_count(int32_t requested, int32_t light_count) {
    int32_t max_count;

    if (requested < 0)
        return -1;
    max_count = vgfx3d_d3d11_clamp_int_param(light_count, 0, VGFX3D_MAX_LIGHTS);
    return requested > max_count ? max_count : requested;
}

/// @brief Sanitize the clustered-light logarithmic Z range before shader upload.
void vgfx3d_d3d11_sanitize_cluster_depth_range(float requested_near,
                                               float requested_far,
                                               float *out_near,
                                               float *out_far) {
    float znear;
    float zfar;
    float min_far;
    float fallback_far;

    znear = vgfx3d_d3d11_clamp_float_param(requested_near,
                                           VGFX3D_D3D11_CLUSTER_ZNEAR_MIN,
                                           VGFX3D_D3D11_CLUSTER_ZFAR_MAX * 0.5f,
                                           VGFX3D_D3D11_CLUSTER_ZNEAR_FALLBACK);
    min_far = znear * (1.0f + 1.0e-3f);
    fallback_far = znear * VGFX3D_D3D11_CLUSTER_ZFAR_FALLBACK;
    if (!isfinite(fallback_far) || fallback_far <= min_far)
        fallback_far = znear + VGFX3D_D3D11_CLUSTER_ZFAR_FALLBACK;
    if (fallback_far > VGFX3D_D3D11_CLUSTER_ZFAR_MAX)
        fallback_far = VGFX3D_D3D11_CLUSTER_ZFAR_MAX;

    if (!isfinite(requested_far) || requested_far <= min_far)
        zfar = fallback_far;
    else if (requested_far > VGFX3D_D3D11_CLUSTER_ZFAR_MAX)
        zfar = VGFX3D_D3D11_CLUSTER_ZFAR_MAX;
    else
        zfar = requested_far;

    if (zfar <= min_far) {
        znear = VGFX3D_D3D11_CLUSTER_ZNEAR_FALLBACK;
        zfar = VGFX3D_D3D11_CLUSTER_ZFAR_FALLBACK;
    }
    if (out_near)
        *out_near = znear;
    if (out_far)
        *out_far = zfar;
}

/// @brief Sanitize slope-scaled rasterizer bias before D3D11 state creation/cache keys.
float vgfx3d_d3d11_sanitize_slope_scaled_depth_bias(float requested) {
    return vgfx3d_d3d11_clamp_float_param(requested,
                                          -VGFX3D_D3D11_MAX_SLOPE_SCALED_DEPTH_BIAS,
                                          VGFX3D_D3D11_MAX_SLOPE_SCALED_DEPTH_BIAS,
                                          0.0f);
}

/// @brief Normalize material workflow constants before the shader branches on them.
int32_t vgfx3d_d3d11_sanitize_material_workflow(int32_t requested) {
    return requested == RT_MATERIAL3D_WORKFLOW_PBR ? RT_MATERIAL3D_WORKFLOW_PBR
                                                   : RT_MATERIAL3D_WORKFLOW_LEGACY;
}

/// @brief Normalize alpha-mode constants before draw-state and shader upload.
int32_t vgfx3d_d3d11_sanitize_alpha_mode(int32_t requested) {
    if (requested < RT_MATERIAL3D_ALPHA_MODE_OPAQUE || requested > RT_MATERIAL3D_ALPHA_MODE_BLEND)
        return RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    return requested;
}

/// @brief Normalize Game3D shading-model constants before shader upload.
int32_t vgfx3d_d3d11_sanitize_shading_model(int32_t requested) {
    if (requested < 0 || requested > VGFX3D_D3D11_SHADING_MODEL_MAX)
        return 0;
    return requested;
}

/// @brief Normalize tonemap mode constants before shader upload.
int32_t vgfx3d_d3d11_sanitize_tonemap_mode(int32_t requested) {
    if (requested < 0 || requested > VGFX3D_D3D11_TONEMAP_MODE_MAX)
        return 0;
    return requested;
}

/// @brief Sanitize fog near/far distances before scene constant upload.
void vgfx3d_d3d11_sanitize_fog_range(float requested_near,
                                      float requested_far,
                                      float *out_near,
                                      float *out_far) {
    float fog_near = vgfx3d_d3d11_clamp_float_param(
        requested_near, 0.0f, VGFX3D_D3D11_FOG_DISTANCE_MAX, 10.0f);
    float min_far = fog_near + 1.0f;
    float fog_far;

    if (min_far > VGFX3D_D3D11_FOG_DISTANCE_MAX)
        min_far = VGFX3D_D3D11_FOG_DISTANCE_MAX;
    fog_far = vgfx3d_d3d11_clamp_float_param(
        requested_far, min_far, VGFX3D_D3D11_FOG_DISTANCE_MAX, 50.0f);
    if (fog_far <= fog_near) {
        fog_near = 10.0f;
        fog_far = 50.0f;
    }
    if (out_near)
        *out_near = fog_near;
    if (out_far)
        *out_far = fog_far;
}

/// @brief Sanitize D3D11 shader-facing shadow depth bias.
float vgfx3d_d3d11_sanitize_shadow_bias(float requested) {
    return vgfx3d_d3d11_clamp_float_param(
        requested, -VGFX3D_D3D11_SHADOW_BIAS_MAX, VGFX3D_D3D11_SHADOW_BIAS_MAX, 0.0f);
}

/// @brief Validate a backend-facing post-FX chain before indexed iteration.
int vgfx3d_d3d11_postfx_chain_is_usable(const vgfx3d_postfx_chain_t *chain) {
    if (!chain || !chain->enabled || !chain->effects || chain->effect_count <= 0 ||
        chain->effect_capacity < chain->effect_count)
        return 0;
    return 1;
}

/// @brief Return non-zero when one PostFX effect descriptor actually changes rendering.
int vgfx3d_d3d11_postfx_effect_is_active(const vgfx3d_postfx_effect_desc_t *effect) {
    const vgfx3d_postfx_snapshot_t *snapshot;

    if (!effect)
        return 0;
    snapshot = &effect->snapshot;
    switch (effect->type) {
    case VGFX3D_POSTFX_EFFECT_BLOOM:
        return snapshot->bloom_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_TONEMAP:
        return snapshot->tonemap_explicit ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_FXAA:
        return snapshot->fxaa_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_COLOR_GRADE:
        return snapshot->color_grade_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_VIGNETTE:
        return snapshot->vignette_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_SSAO:
        return snapshot->ssao_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_DOF:
        return snapshot->dof_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_MOTION_BLUR:
        return snapshot->motion_blur_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_TAA:
        return snapshot->taa_enabled ? 1 : 0;
    case VGFX3D_POSTFX_EFFECT_SSR:
        return snapshot->ssr_enabled ? 1 : 0;
    default:
        return 0;
    }
}

/// @brief Return non-zero when a usable chain contains an active effect of @p type_value.
int vgfx3d_d3d11_postfx_chain_has_active_effect(const vgfx3d_postfx_chain_t *chain,
                                                int32_t type_value) {
    if (!vgfx3d_d3d11_postfx_chain_is_usable(chain))
        return 0;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        if (chain->effects[i].type == type_value &&
            vgfx3d_d3d11_postfx_effect_is_active(&chain->effects[i]))
            return 1;
    }
    return 0;
}

/// @brief Return non-zero when a usable chain contains any active effect.
int vgfx3d_d3d11_postfx_chain_has_active_effects(const vgfx3d_postfx_chain_t *chain) {
    if (!vgfx3d_d3d11_postfx_chain_is_usable(chain))
        return 0;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        if (vgfx3d_d3d11_postfx_effect_is_active(&chain->effects[i]))
            return 1;
    }
    return 0;
}

/// @brief Decide whether a draw needs current/previous bone cbuffer uploads.
int vgfx3d_d3d11_should_upload_bone_palette(int has_skinning, int has_prev_skinning) {
    return has_skinning || has_prev_skinning ? 1 : 0;
}

/// @brief Add two uint64_t counters with saturation instead of wraparound.
uint64_t vgfx3d_d3d11_saturating_add_u64(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

/// @brief Capacity-doubling growth helper: returns the next power-of-2 capacity >= @p needed.
/// Saturates at @p needed when doubling would overflow INT_MAX.
int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity) {
    int32_t next_capacity;

    if (needed <= 0) {
        if (current_capacity > 0)
            return current_capacity;
        return minimum_capacity > 0 ? minimum_capacity : 1;
    }
    next_capacity = current_capacity > 0 ? current_capacity : minimum_capacity;
    if (next_capacity < 1)
        next_capacity = 1;
    while (next_capacity < needed) {
        if (next_capacity > INT_MAX / 2)
            return needed;
        next_capacity *= 2;
    }
    return next_capacity;
}

/// @brief Overflow-checked size_t multiplication used by byte-span helpers.
/// @details The destination is cleared before any validation so callers never
///   observe a stale byte count after a rejected span.
static int vgfx3d_d3d11_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

/// @brief Compute tightly packed row bytes for a positive-width pixel row.
/// @details Used before upload/readback copies so all callers share the same
///   overflow behavior and reject non-positive dimensions before casting to
///   unsigned D3D11 pitches.
int vgfx3d_d3d11_compute_row_bytes(int32_t width, int32_t bytes_per_pixel, size_t *out_bytes) {
    if (out_bytes)
        *out_bytes = 0;
    if (!out_bytes || width <= 0 || bytes_per_pixel <= 0)
        return 0;
    return vgfx3d_d3d11_checked_mul_size((size_t)width, (size_t)bytes_per_pixel, out_bytes);
}

/// @brief Compute a valid D3D11 buffer ByteWidth from a size_t byte count.
int vgfx3d_d3d11_compute_buffer_byte_width(size_t size, uint32_t *out_width) {
    if (out_width)
        *out_width = 0;
    if (!out_width || size == 0 || size > UINT_MAX)
        return 0;
    *out_width = (uint32_t)size;
    return 1;
}

/// @brief Compute a valid 16-byte-aligned D3D11 constant-buffer ByteWidth.
int vgfx3d_d3d11_compute_constant_buffer_byte_width(size_t size, uint32_t *out_width) {
    size_t aligned_size;

    if (out_width)
        *out_width = 0;
    if (!out_width || size == 0 || size > VGFX3D_D3D11_MAX_CONSTANT_BUFFER_BYTES ||
        size > SIZE_MAX - 15u)
        return 0;
    aligned_size = (size + 15u) & ~(size_t)15u;
    if (aligned_size == 0 || aligned_size > VGFX3D_D3D11_MAX_CONSTANT_BUFFER_BYTES ||
        aligned_size > UINT_MAX)
        return 0;
    *out_width = (uint32_t)aligned_size;
    return 1;
}

/// @brief Compute a D3D11 row pitch for a tightly packed RGBA8 upload.
int vgfx3d_d3d11_compute_rgba8_upload_pitch(int32_t width, uint32_t *out_pitch) {
    size_t row_bytes;

    if (out_pitch)
        *out_pitch = 0;
    if (!out_pitch || !vgfx3d_d3d11_compute_row_bytes(width, 4, &row_bytes) || row_bytes > UINT_MAX)
        return 0;
    *out_pitch = (uint32_t)row_bytes;
    return 1;
}

/// @brief Compute one mip level extent for a square D3D11 texture chain.
int vgfx3d_d3d11_expected_square_mip_extent(int32_t base_extent,
                                            int32_t mip_level,
                                            int32_t *out_extent) {
    int32_t extent;
    int32_t mip_count;

    if (out_extent)
        *out_extent = 0;
    if (!out_extent || !vgfx3d_d3d11_is_valid_cubemap_extent(base_extent) || mip_level < 0)
        return 0;
    mip_count = vgfx3d_d3d11_compute_mip_count(base_extent, base_extent);
    if (mip_level >= mip_count)
        return 0;
    extent = base_extent;
    for (int32_t i = 0; i < mip_level; i++) {
        if (extent > 1)
            extent >>= 1;
    }
    *out_extent = extent > 0 ? extent : 1;
    return 1;
}

/// @brief Compute a bloom mip extent using the backend's bounded half-res policy.
int vgfx3d_d3d11_compute_bloom_mip_extent(int32_t width,
                                          int32_t height,
                                          int32_t mip_level,
                                          int32_t *out_width,
                                          int32_t *out_height) {
    int32_t mip_width;
    int32_t mip_height;

    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;
    if (!out_width || !out_height || mip_level < 0 ||
        mip_level >= VGFX3D_D3D11_BLOOM_MIP_COUNT_MAX ||
        !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return 0;

    mip_width = width > 1 ? width / 2 : 1;
    mip_height = height > 1 ? height / 2 : 1;
    for (int32_t i = 0; i < mip_level; i++) {
        if (mip_width / 2 < VGFX3D_D3D11_BLOOM_MIN_DOWNSAMPLE_EXTENT ||
            mip_height / 2 < VGFX3D_D3D11_BLOOM_MIN_DOWNSAMPLE_EXTENT)
            return 0;
        mip_width = mip_width > 1 ? mip_width / 2 : 1;
        mip_height = mip_height > 1 ? mip_height / 2 : 1;
    }

    *out_width = mip_width > 0 ? mip_width : 1;
    *out_height = mip_height > 0 ? mip_height : 1;
    return 1;
}

/// @brief Validate an IBL mip payload extent against the destination cubemap mip.
int vgfx3d_d3d11_validate_ibl_mip_extent(int32_t face_size,
                                         int32_t mip_level,
                                         int32_t width,
                                         int32_t height) {
    int32_t expected_extent;

    if (!vgfx3d_d3d11_expected_square_mip_extent(face_size, mip_level, &expected_extent))
        return 0;
    return width == expected_extent && height == expected_extent;
}

/// @brief Compute a checked per-instance vertex-buffer upload size.
/// @details The D3D11 `ByteWidth` field is a UINT, so the helper rejects both
///   size_t multiplication overflow and byte counts that cannot be represented
///   by the D3D11 buffer descriptor. The output is cleared on failure.
int vgfx3d_d3d11_compute_instance_upload_bytes(int32_t instance_count,
                                               size_t instance_stride,
                                               size_t *out_bytes) {
    size_t bytes;

    if (out_bytes)
        *out_bytes = 0;
    if (!out_bytes || instance_count <= 0 || instance_stride == 0)
        return 0;
    if (!vgfx3d_d3d11_checked_mul_size((size_t)instance_count, instance_stride, &bytes))
        return 0;
    if (bytes > UINT_MAX)
        return 0;
    *out_bytes = bytes;
    return 1;
}

/// @brief Compute the exact byte range for updating live float SRV elements.
/// @details The backing buffer can be larger than the live morph payload after
///   capacity growth; this helper keeps UpdateSubresource boxed to the live
///   elements and rejects stale counts that exceed the allocation.
int vgfx3d_d3d11_compute_float_srv_update_bytes(size_t element_count,
                                                size_t capacity,
                                                size_t *out_bytes) {
    if (out_bytes)
        *out_bytes = 0;
    if (!out_bytes || element_count == 0 || element_count > capacity)
        return 0;
    if (element_count > (size_t)(UINT_MAX / sizeof(float)))
        return 0;
    return vgfx3d_d3d11_checked_mul_size(element_count, sizeof(float), out_bytes);
}

/// @brief Validate an RGBA8 destination rectangle and optionally return its byte span.
/// @details The total stride * height span is checked even when @p out_bytes is
///   NULL. That keeps callers that only need a boolean answer from accepting an
///   impossible destination size on narrower hosts.
int vgfx3d_d3d11_validate_rgba8_destination(int32_t width,
                                            int32_t height,
                                            int32_t stride,
                                            size_t *out_bytes) {
    size_t min_stride;
    size_t total_bytes;

    if (out_bytes)
        *out_bytes = 0;
    if (width <= 0 || height <= 0 || stride <= 0)
        return 0;
    if (!vgfx3d_d3d11_compute_row_bytes(width, 4, &min_stride))
        return 0;
    if ((size_t)stride < min_stride)
        return 0;
    if (!vgfx3d_d3d11_checked_mul_size((size_t)stride, (size_t)height, &total_bytes))
        return 0;
    if (out_bytes)
        *out_bytes = total_bytes;
    return 1;
}

/// @brief Validate a row span before converting it into unsigned D3D11 box bounds.
int vgfx3d_d3d11_validate_row_span(int32_t extent, int32_t start, int32_t count) {
    if (extent <= 0 || start < 0 || count <= 0 || start >= extent)
        return 0;
    return count <= extent - start;
}

/// @brief Check 2D texture dimensions against D3D11 feature-level 11 limits.
int vgfx3d_d3d11_is_valid_texture2d_extent(int32_t width, int32_t height) {
    return width > 0 && height > 0 && width <= VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION &&
           height <= VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION;
}

/// @brief Check a square cubemap face dimension against D3D11 limits.
int vgfx3d_d3d11_is_valid_cubemap_extent(int32_t face_size) {
    return face_size > 0 && face_size <= VGFX3D_D3D11_MAX_CUBEMAP_DIMENSION;
}

/// @brief Validate source/destination row spans for a mapped texture readback copy.
int vgfx3d_d3d11_validate_mapped_texture_copy(int32_t width,
                                              int32_t dst_stride,
                                              uint32_t src_row_pitch,
                                              int32_t src_bytes_per_pixel,
                                              size_t *out_src_row_bytes,
                                              size_t *out_dst_row_bytes) {
    size_t src_row_bytes;
    size_t dst_row_bytes;

    if (out_src_row_bytes)
        *out_src_row_bytes = 0;
    if (out_dst_row_bytes)
        *out_dst_row_bytes = 0;
    if (width <= 0 || dst_stride <= 0 || src_row_pitch == 0 || src_bytes_per_pixel <= 0)
        return 0;
    if (!vgfx3d_d3d11_compute_row_bytes(width, src_bytes_per_pixel, &src_row_bytes) ||
        !vgfx3d_d3d11_compute_row_bytes(width, 4, &dst_row_bytes))
        return 0;
    if ((size_t)src_row_pitch < src_row_bytes || (size_t)dst_stride < dst_row_bytes)
        return 0;
    if (out_src_row_bytes)
        *out_src_row_bytes = src_row_bytes;
    if (out_dst_row_bytes)
        *out_dst_row_bytes = dst_row_bytes;
    return 1;
}

/// @brief Bytes per compressed/native block row for D3D11 texture updates.
uint64_t vgfx3d_d3d11_native_mip_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
    uint64_t cols;

    if (!mip || mip->width <= 0 || mip->block_width <= 0 || mip->block_bytes <= 0)
        return 0;
    cols = ((uint64_t)(uint32_t)mip->width + (uint64_t)(uint32_t)mip->block_width - 1u) /
           (uint64_t)(uint32_t)mip->block_width;
    if (cols > UINT64_MAX / (uint64_t)(uint32_t)mip->block_bytes)
        return 0;
    return cols * (uint64_t)(uint32_t)mip->block_bytes;
}

/// @brief Number of compressed/native block rows needed to cover a mip height.
uint64_t vgfx3d_d3d11_native_mip_block_rows(const vgfx3d_native_texture_mip_t *mip) {
    if (!mip || mip->height <= 0 || mip->block_height <= 0)
        return 0;
    return ((uint64_t)(uint32_t)mip->height + (uint64_t)(uint32_t)mip->block_height - 1u) /
           (uint64_t)(uint32_t)mip->block_height;
}

/// @brief Minimum payload bytes required by a complete compressed/native mip.
uint64_t vgfx3d_d3d11_native_mip_required_bytes(const vgfx3d_native_texture_mip_t *mip) {
    uint64_t row_bytes;
    uint64_t block_rows;

    row_bytes = vgfx3d_d3d11_native_mip_row_bytes(mip);
    block_rows = vgfx3d_d3d11_native_mip_block_rows(mip);
    if (row_bytes == 0 || block_rows == 0 || block_rows > UINT64_MAX / row_bytes)
        return 0;
    return row_bytes * block_rows;
}

/// @brief Return the block footprint D3D11 expects for one native compressed format.
int vgfx3d_d3d11_native_format_block_layout(int32_t format_id,
                                            int32_t *out_block_width,
                                            int32_t *out_block_height,
                                            int32_t *out_block_bytes) {
    int32_t block_bytes = 0;

    if (out_block_width)
        *out_block_width = 0;
    if (out_block_height)
        *out_block_height = 0;
    if (out_block_bytes)
        *out_block_bytes = 0;
    if (!out_block_width || !out_block_height || !out_block_bytes)
        return 0;

    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC1 ||
        format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC4) {
        block_bytes = 8;
    } else if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3 ||
               format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC5 ||
               format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7) {
        block_bytes = 16;
    } else {
        return 0;
    }

    *out_block_width = 4;
    *out_block_height = 4;
    *out_block_bytes = block_bytes;
    return 1;
}

/// @brief Check one native compressed mip against D3D11 chain and block invariants.
int vgfx3d_d3d11_validate_native_mip_desc(const vgfx3d_native_texture_mip_t *mip,
                                          const vgfx3d_native_texture_mip_t *previous_mip,
                                          int32_t expected_format_id,
                                          int32_t expected_block_width,
                                          int32_t expected_block_height,
                                          int32_t expected_block_bytes) {
    uint64_t required_bytes;
    int32_t format_block_width;
    int32_t format_block_height;
    int32_t format_block_bytes;

    if (!mip || !mip->data || mip->bytes == 0 || expected_format_id <= 0)
        return 0;
    if (mip->bytes > UINT_MAX)
        return 0;
    if (!vgfx3d_d3d11_native_format_block_layout(
            expected_format_id, &format_block_width, &format_block_height, &format_block_bytes))
        return 0;
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(mip->width, mip->height))
        return 0;
    if (mip->format_id != expected_format_id)
        return 0;
    if (mip->block_width <= 0 || mip->block_height <= 0 || mip->block_bytes <= 0)
        return 0;
    if (mip->block_width != format_block_width || mip->block_height != format_block_height ||
        mip->block_bytes != format_block_bytes)
        return 0;
    if (expected_block_width > 0 && mip->block_width != expected_block_width)
        return 0;
    if (expected_block_height > 0 && mip->block_height != expected_block_height)
        return 0;
    if (expected_block_bytes > 0 && mip->block_bytes != expected_block_bytes)
        return 0;
    if (previous_mip) {
        int32_t expected_width = previous_mip->width > 1 ? previous_mip->width >> 1 : 1;
        int32_t expected_height = previous_mip->height > 1 ? previous_mip->height >> 1 : 1;
        if (!vgfx3d_d3d11_is_valid_texture2d_extent(previous_mip->width, previous_mip->height))
            return 0;
        if (previous_mip->block_width <= 0 || previous_mip->block_height <= 0 ||
            previous_mip->block_bytes <= 0)
            return 0;
        if (mip->width != expected_width || mip->height != expected_height)
            return 0;
    }
    required_bytes = vgfx3d_d3d11_native_mip_required_bytes(mip);
    if (required_bytes == 0 || mip->bytes < required_bytes)
        return 0;
    return 1;
}

/// @brief Check that a native mip count can fit in D3D11's MipLevels field and chain length.
int vgfx3d_d3d11_is_valid_native_mip_count(int32_t base_width,
                                           int32_t base_height,
                                           int64_t mip_count) {
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(base_width, base_height) || mip_count <= 0 ||
        mip_count > UINT_MAX)
        return 0;
    return mip_count <= vgfx3d_d3d11_compute_mip_count(base_width, base_height);
}

/// @brief Clamp morph shape count to shader and index-range limits.
/// @details HLSL buffer indexing is signed-int based in the shader source, so
///   the largest accepted shape count is also bounded by
///   `(shape * vertex_count + vertex_id) * 3 + component <= INT_MAX`.
int32_t vgfx3d_d3d11_clamp_morph_shape_count(uint32_t vertex_count, int32_t requested_shape_count) {
    int32_t shape_count;
    uint32_t max_indexed_vertices;
    uint32_t max_shapes_by_index;

    if (vertex_count == 0 || requested_shape_count <= 0)
        return 0;
    shape_count = requested_shape_count;
    if (shape_count > VGFX3D_D3D11_MAX_MORPH_SHAPES)
        shape_count = VGFX3D_D3D11_MAX_MORPH_SHAPES;
    max_indexed_vertices = (uint32_t)((INT_MAX - 2) / 3);
    max_shapes_by_index = max_indexed_vertices / vertex_count;
    if (max_shapes_by_index == 0)
        return 0;
    if ((uint32_t)shape_count > max_shapes_by_index)
        shape_count = (int32_t)max_shapes_by_index;
    return shape_count;
}

/// @brief Compute the float element count for a D3D11 morph-delta SRV upload.
int vgfx3d_d3d11_compute_morph_float_count(uint32_t vertex_count,
                                           int32_t requested_shape_count,
                                           size_t *out_elements) {
    size_t shaped_vertices;
    size_t elements;
    size_t bytes;
    int32_t shape_count;

    if (out_elements)
        *out_elements = 0;
    if (!out_elements)
        return 0;
    shape_count = vgfx3d_d3d11_clamp_morph_shape_count(vertex_count, requested_shape_count);
    if (shape_count <= 0)
        return 0;
    if (!vgfx3d_d3d11_checked_mul_size(
            (size_t)shape_count, (size_t)vertex_count, &shaped_vertices) ||
        !vgfx3d_d3d11_checked_mul_size(shaped_vertices, 3u, &elements) ||
        !vgfx3d_d3d11_checked_mul_size(elements, sizeof(float), &bytes) || bytes > UINT_MAX)
        return 0;
    *out_elements = elements;
    return 1;
}

/// @brief Decide whether a compacting cache sweep can drop one aged entry.
/// @details The predicate keeps enough unvisited entries to preserve the
///   resident floor. `kept_count` is the number already copied to the compacted
///   prefix, and `scan_index` is the current entry in the original array.
int vgfx3d_d3d11_should_prune_cache_entry(int32_t total_count,
                                          int32_t kept_count,
                                          int32_t scan_index,
                                          uint64_t age,
                                          int32_t max_resident,
                                          uint64_t prune_age) {
    int32_t remaining_after_current;

    if (total_count <= 0 || kept_count < 0 || scan_index < 0 || scan_index >= total_count)
        return 0;
    if (kept_count > total_count)
        return 0;
    if (max_resident < 0)
        max_resident = 0;
    if (total_count <= max_resident || age <= prune_age)
        return 0;
    if (kept_count >= max_resident)
        return 1;
    remaining_after_current = total_count - scan_index - 1;
    return remaining_after_current >= max_resident - kept_count;
}

/// @brief Pick the right render-target classification for the current draw context.
/// Order of priority: explicit RTT > swapchain (no postfx) > overlay (loading existing
/// color) > scene (HDR intermediate that postfx will tonemap).
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color) {
    if (rtt_active)
        return VGFX3D_D3D11_TARGET_RTT;
    if (!gpu_postfx_enabled)
        return VGFX3D_D3D11_TARGET_SWAPCHAIN;
    if (load_existing_color)
        return VGFX3D_D3D11_TARGET_OVERLAY;
    return VGFX3D_D3D11_TARGET_SCENE;
}

/// @brief Resolve a requested target to one with complete backing resources.
/// @details Invalid enum values are treated as swapchain requests rather than
///   propagated to the backend state machine. Overlay falls back to scene first
///   because that preserves the already-rendered 3D color when a separate HUD
///   target allocation failed.
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_resolve_available_target(
    vgfx3d_d3d11_target_kind_t requested,
    int scene_available,
    int overlay_available,
    int rtt_available) {
    if (requested == VGFX3D_D3D11_TARGET_RTT)
        return rtt_available ? VGFX3D_D3D11_TARGET_RTT : VGFX3D_D3D11_TARGET_SWAPCHAIN;
    if (requested == VGFX3D_D3D11_TARGET_OVERLAY) {
        if (overlay_available)
            return VGFX3D_D3D11_TARGET_OVERLAY;
        return scene_available ? VGFX3D_D3D11_TARGET_SCENE : VGFX3D_D3D11_TARGET_SWAPCHAIN;
    }
    if (requested == VGFX3D_D3D11_TARGET_SCENE && !scene_available)
        return VGFX3D_D3D11_TARGET_SWAPCHAIN;
    if (requested == VGFX3D_D3D11_TARGET_SCENE || requested == VGFX3D_D3D11_TARGET_SWAPCHAIN)
        return requested;
    return VGFX3D_D3D11_TARGET_SWAPCHAIN;
}

/// @brief Decide whether a pass should preserve existing color contents.
/// @details Overlay targets only load after this frame has already rendered
///   into the separate overlay target; the first overlay pass clears stale HUD
///   contents from prior frames.
int8_t vgfx3d_d3d11_should_load_existing_color(vgfx3d_d3d11_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame) {
    if (!requested_load_existing_color)
        return 0;
    if (target_kind != VGFX3D_D3D11_TARGET_OVERLAY)
        return 1;
    return overlay_used_this_frame ? 1 : 0;
}

/// @brief Decide whether a cached morph SRV payload can be reused for a draw.
/// @details The key includes the normal-delta presence bit in addition to the
///   stable morph identity, revision, clamped shape count, and vertex count.
///   Without that bit, a payload uploaded for position-only morphing could be
///   reused for a later draw that requires normal deltas but did not change the
///   content revision.
int vgfx3d_d3d11_should_reuse_morph_cache(const void *cached_key,
                                          uint64_t cached_revision,
                                          int32_t cached_shape_count,
                                          uint32_t cached_vertex_count,
                                          int8_t cached_has_normal_deltas,
                                          const vgfx3d_draw_cmd_t *cmd) {
    int32_t shape_count;
    int8_t has_normal_deltas;

    if (!cmd || !cmd->morph_key || cmd->morph_revision == 0 || !cmd->morph_deltas ||
        !cmd->morph_weights || cmd->morph_shape_count <= 0 || cmd->vertex_count == 0)
        return 0;

    shape_count = vgfx3d_d3d11_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    if (shape_count <= 0)
        return 0;
    has_normal_deltas = cmd->morph_normal_deltas ? 1 : 0;
    return cached_key == cmd->morph_key && cached_revision == cmd->morph_revision &&
           cached_shape_count == shape_count && cached_vertex_count == cmd->vertex_count &&
           (cached_has_normal_deltas ? 1 : 0) == has_normal_deltas;
}

/// @brief Count contiguous complete shadow slots starting at slot 0.
/// @details The HLSL shader receives only `shadowCount` plus two fixed SRV
///   bindings, so advertising a higher slot while an earlier slot is missing
///   would let a light sample an unbound shadow texture. Requiring a contiguous
///   prefix keeps `0 <= shadowIndex < shadowCount` equivalent to "SRV exists".
int32_t vgfx3d_d3d11_compute_shadow_count(int32_t slot_count, const int *slot_complete) {
    int32_t count = 0;
    int32_t max_slots;

    if (!slot_complete || slot_count <= 0)
        return 0;
    max_slots = slot_count > VGFX3D_MAX_SHADOW_LIGHTS ? VGFX3D_MAX_SHADOW_LIGHTS : slot_count;
    while (count < max_slots && slot_complete[count] > 0)
        count++;
    return count;
}

/// @brief Sanitize a light's requested shadow slot against the advertised range.
/// @details Invalid, negative, sparse, or out-of-range slots are converted to -1,
///   which the shader treats as unshadowed. This prevents stale scene data from
///   indexing a shadow SRV that was not allocated this frame.
int32_t vgfx3d_d3d11_sanitize_shadow_index(int32_t requested_shadow_index,
                                           int32_t advertised_shadow_count) {
    advertised_shadow_count = vgfx3d_d3d11_clamp_shadow_count(advertised_shadow_count);
    if (advertised_shadow_count <= 0 || requested_shadow_index < 0 ||
        requested_shadow_index >= advertised_shadow_count)
        return -1;
    return requested_shadow_index;
}

/// @brief Clamp a light's cascade count so it cannot address beyond advertised shadow slots.
int32_t vgfx3d_d3d11_sanitize_shadow_cascade_count(int32_t requested_cascade_count,
                                                   int32_t sanitized_shadow_index,
                                                   int32_t advertised_shadow_count) {
    int32_t remaining_slots;

    advertised_shadow_count = vgfx3d_d3d11_clamp_shadow_count(advertised_shadow_count);
    if (sanitized_shadow_index < 0 || sanitized_shadow_index >= advertised_shadow_count)
        return 1;
    remaining_slots = advertised_shadow_count - sanitized_shadow_index;
    if (requested_cascade_count < 1)
        return 1;
    return requested_cascade_count > remaining_slots ? remaining_slots : requested_cascade_count;
}

/// @brief Clamp a shadow-count value to the fixed HLSL shadow texture bindings.
int32_t vgfx3d_d3d11_clamp_shadow_count(int32_t advertised_shadow_count) {
    if (advertised_shadow_count <= 0)
        return 0;
    return advertised_shadow_count > VGFX3D_MAX_SHADOW_LIGHTS ? VGFX3D_MAX_SHADOW_LIGHTS
                                                              : advertised_shadow_count;
}

/// @brief Project a world-space point through a shadow VP matrix using HLSL sampling rules.
int vgfx3d_d3d11_project_shadow_coord(const float *shadow_vp,
                                      int32_t projection_type,
                                      const float world_pos[3],
                                      float out_uv_depth[3]) {
    float lx;
    float ly;
    float lz;
    float lw;
    float ndc_x;
    float ndc_y;
    float ndc_z;

    if (out_uv_depth) {
        out_uv_depth[0] = 0.0f;
        out_uv_depth[1] = 0.0f;
        out_uv_depth[2] = 0.0f;
    }
    if (!shadow_vp || !world_pos || !out_uv_depth)
        return 0;
    lx = world_pos[0] * shadow_vp[0] + world_pos[1] * shadow_vp[1] + world_pos[2] * shadow_vp[2] +
         shadow_vp[3];
    ly = world_pos[0] * shadow_vp[4] + world_pos[1] * shadow_vp[5] + world_pos[2] * shadow_vp[6] +
         shadow_vp[7];
    lz = world_pos[0] * shadow_vp[8] + world_pos[1] * shadow_vp[9] + world_pos[2] * shadow_vp[10] +
         shadow_vp[11];
    lw = world_pos[0] * shadow_vp[12] + world_pos[1] * shadow_vp[13] +
         world_pos[2] * shadow_vp[14] + shadow_vp[15];
    if (projection_type == VGFX3D_SHADOW_PROJECTION_PERSPECTIVE) {
        if (!isfinite(lw) || lw <= 0.0001f)
            return 0;
        ndc_x = lx / lw;
        ndc_y = ly / lw;
        ndc_z = lz / lw;
    } else {
        ndc_x = lx;
        ndc_y = ly;
        ndc_z = lz;
    }
    if (!isfinite(ndc_x) || !isfinite(ndc_y) || !isfinite(ndc_z))
        return 0;
    out_uv_depth[0] = ndc_x * 0.5f + 0.5f;
    out_uv_depth[1] = 0.5f - ndc_y * 0.5f;
    out_uv_depth[2] = ndc_z * 0.5f + 0.5f;
    return 1;
}

/// @brief Decide whether an RTT can safely mark its CPU-side mirror dirty.
/// @details End-of-frame dirtying is meaningful only when the target handle and
///   every GPU resource needed for a later staging copy are present. This helper
///   keeps partial allocation failures from installing stale sync hooks.
int vgfx3d_d3d11_should_mark_rtt_dirty(int8_t rtt_active,
                                       int has_target,
                                       int has_color_tex,
                                       int has_color_rtv,
                                       int has_depth_tex,
                                       int has_depth_dsv,
                                       int has_staging) {
    return rtt_active && has_target && has_color_tex && has_color_rtv && has_depth_tex &&
           has_depth_dsv && has_staging;
}

/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_d3d11_blend_mode_t vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    if (cmd && cmd->additive_blend)
        return VGFX3D_D3D11_BLEND_ADDITIVE;
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_D3D11_BLEND_ALPHA
                                                 : VGFX3D_D3D11_BLEND_OPAQUE;
}

/// @brief Pick the color format for a render target — HDR16F for the scene pass
/// (so post-FX tonemapping has headroom), UNORM8 for everything else.
vgfx3d_d3d11_color_format_t vgfx3d_d3d11_choose_color_format(
    vgfx3d_d3d11_target_kind_t target_kind) {
    return target_kind == VGFX3D_D3D11_TARGET_SCENE ? VGFX3D_D3D11_COLOR_FORMAT_HDR16F
                                                    : VGFX3D_D3D11_COLOR_FORMAT_UNORM8;
}

/// @brief Decide whether the current pass should bind the motion-vector target.
/// @details Motion vectors are only meaningful for opaque scene draws. Alpha
///   and additive passes blend multiple histories into one pixel, so they draw
///   color only and leave motion at the clear "no object history" sentinel.
vgfx3d_d3d11_motion_attachment_mode_t vgfx3d_d3d11_choose_motion_attachment_mode(
    vgfx3d_d3d11_target_kind_t target_kind, const vgfx3d_draw_cmd_t *cmd) {
    if (target_kind != VGFX3D_D3D11_TARGET_SCENE)
        return VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY;
    if (!cmd || cmd->disable_depth_test)
        return VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY;
    return vgfx3d_d3d11_choose_blend_mode(cmd) == VGFX3D_D3D11_BLEND_OPAQUE
               ? VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_AND_MOTION
               : VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY;
}

/// @brief Decide whether terrain splatting has every required texture bound.
/// @details D3D11's shader samples the control map plus four layers as a unit;
///   partial binds are treated as no splat so missing layers do not sample NULL
///   resources or produce backend-specific black terrain.
int vgfx3d_d3d11_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3) {
    return cmd_has_splat && has_splat_map && has_layer0 && has_layer1 && has_layer2 && has_layer3;
}

/// @brief Decide whether the offscreen scene still needs a swapchain composite.
int vgfx3d_d3d11_should_composite_to_swapchain(int8_t rtt_active,
                                               int8_t gpu_postfx_enabled,
                                               int has_scene_targets,
                                               int8_t scene_composited_to_swapchain) {
    return !rtt_active && gpu_postfx_enabled && has_scene_targets && !scene_composited_to_swapchain;
}

/// @brief Decide whether a new begin-frame invalidates a prior swapchain composite.
int vgfx3d_d3d11_should_reset_composited_swapchain_for_frame(int8_t rtt_active,
                                                             int8_t load_existing_color) {
    return rtt_active || !load_existing_color;
}

/// @brief Decide whether a post-FX enable update invalidates a prior swapchain composite.
int vgfx3d_d3d11_should_reset_composited_swapchain_for_postfx_update(int8_t current_enabled,
                                                                     int8_t requested_enabled) {
    return (current_enabled ? 1 : 0) != (requested_enabled ? 1 : 0);
}

/// @brief Decide whether a begin-frame should preserve scene temporal history.
int vgfx3d_d3d11_should_treat_begin_frame_as_overlay(
    vgfx3d_d3d11_target_kind_t resolved_target_kind, int8_t requested_load_existing_color) {
    return resolved_target_kind == VGFX3D_D3D11_TARGET_OVERLAY ||
           (requested_load_existing_color && resolved_target_kind != VGFX3D_D3D11_TARGET_RTT);
}

/// @brief Decide whether overlay contents are in the separate overlay target.
int vgfx3d_d3d11_uses_separate_overlay_target(vgfx3d_d3d11_target_kind_t resolved_target_kind,
                                              int has_overlay_target) {
    return resolved_target_kind == VGFX3D_D3D11_TARGET_OVERLAY && has_overlay_target;
}

/// @brief Choose the readback source class without touching D3D11 resources.
vgfx3d_d3d11_readback_kind_t vgfx3d_d3d11_choose_readback_kind(
    int8_t presented_snapshot_valid,
    int presented_snapshot_has_texture,
    int8_t scene_composited_to_swapchain,
    int8_t gpu_postfx_enabled,
    int8_t postfx_chain_valid,
    int8_t postfx_chain_enabled,
    int32_t postfx_effect_count,
    int postfx_has_effects,
    int has_scene_targets,
    vgfx3d_d3d11_target_kind_t current_target_kind) {
    if (presented_snapshot_valid && presented_snapshot_has_texture)
        return VGFX3D_D3D11_READBACK_PRESENTED_SNAPSHOT;
    if (scene_composited_to_swapchain)
        return VGFX3D_D3D11_READBACK_BACKBUFFER;
    if (gpu_postfx_enabled && postfx_chain_valid && postfx_chain_enabled && has_scene_targets &&
        postfx_effect_count > 0 && postfx_has_effects)
        return VGFX3D_D3D11_READBACK_POSTFX_COMPOSITE;
    if (has_scene_targets && current_target_kind != VGFX3D_D3D11_TARGET_SWAPCHAIN)
        return VGFX3D_D3D11_READBACK_SCENE_COLOR;
    return VGFX3D_D3D11_READBACK_BACKBUFFER;
}

/// @brief Keep a pre-present snapshot only when both snapshot and Present succeeded.
int vgfx3d_d3d11_should_keep_presented_snapshot(int snapshot_ok, int present_ok) {
    return snapshot_ok && present_ok ? 1 : 0;
}
