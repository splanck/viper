//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_shared.c
// Purpose: OpenGL backend helpers shared with sister backends — frame history,
//   mip math, capacity growth, and policy choices for render-target,
//   readback, and morph-cache reuse.
//
// Links: vgfx3d_backend_opengl_shared.h, vgfx3d_backend_opengl.c
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_opengl_shared.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/// @brief Roll the OpenGL backend's per-frame VP/inv-VP/cam-pos history forward by one frame.
/// Mirrors the D3D11 helper; see vgfx3d_d3d11_update_frame_history for semantics.
void vgfx3d_opengl_update_frame_history(vgfx3d_opengl_frame_history_t *history,
                                        const float *vp,
                                        const float *inv_vp,
                                        const float *cam_pos,
                                        int8_t is_overlay_pass) {
    if (!history || !vp || !inv_vp)
        return;

    if (!is_overlay_pass) {
        if (history->scene_history_valid) {
            memcpy(history->scene_prev_vp, history->scene_vp, sizeof(history->scene_prev_vp));
        } else {
            memcpy(history->scene_prev_vp, vp, sizeof(history->scene_prev_vp));
            history->scene_history_valid = 1;
        }
        memcpy(history->scene_vp, vp, sizeof(history->scene_vp));
        memcpy(history->scene_inv_vp, inv_vp, sizeof(history->scene_inv_vp));
        memcpy(history->draw_prev_vp, history->scene_prev_vp, sizeof(history->draw_prev_vp));
        if (cam_pos)
            memcpy(history->scene_cam_pos, cam_pos, sizeof(history->scene_cam_pos));
        return;
    }

    memcpy(history->draw_prev_vp, vp, sizeof(history->draw_prev_vp));
}

/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_opengl_compute_mip_count(int32_t width, int32_t height) {
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
int32_t vgfx3d_opengl_sanitize_anisotropy(int32_t requested) {
    if (requested < 1)
        return 1;
    if (requested > VGFX3D_OPENGL_MAX_TEXTURE_ANISOTROPY)
        return VGFX3D_OPENGL_MAX_TEXTURE_ANISOTROPY;
    return requested;
}

/// @brief Convert sanitized anisotropy to a compact cache index [0,15].
int32_t vgfx3d_opengl_sampler_anisotropy_index(int32_t requested) {
    return vgfx3d_opengl_sanitize_anisotropy(requested) - 1;
}

/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_opengl_next_capacity(int32_t current_capacity,
                                    int32_t needed,
                                    int32_t minimum_capacity) {
    int32_t next_capacity;

    if (needed <= 0)
        return current_capacity > 0 ? current_capacity : minimum_capacity;
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

/// @brief Multiply two size_t values with overflow checking; writes the product to @p out
///   and returns 1, or zeroes @p out and returns 0 on overflow or a NULL out pointer.
static int vgfx3d_opengl_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

/// @brief Overflow-safe size_t capacity growth helper for GL buffer uploads.
int vgfx3d_opengl_compute_buffer_capacity(size_t current_capacity,
                                          size_t needed,
                                          size_t minimum_capacity,
                                          size_t *out_capacity) {
    size_t next_capacity;

    if (out_capacity)
        *out_capacity = 0;
    if (!out_capacity)
        return 0;
    if (needed == 0) {
        *out_capacity = current_capacity > 0 ? current_capacity : minimum_capacity;
        return 1;
    }
    next_capacity = current_capacity > 0 ? current_capacity : minimum_capacity;
    if (next_capacity < 1)
        next_capacity = 1;
    while (next_capacity < needed) {
        if (next_capacity > SIZE_MAX / 2) {
            next_capacity = needed;
            break;
        }
        next_capacity *= 2;
    }
    *out_capacity = next_capacity;
    return 1;
}

/// @brief Validate an RGBA8 destination rectangle and optionally return its byte span.
int vgfx3d_opengl_validate_rgba8_destination(int32_t width,
                                             int32_t height,
                                             int32_t stride,
                                             size_t *out_bytes) {
    size_t min_stride;
    size_t total_bytes;

    if (out_bytes)
        *out_bytes = 0;
    if (width <= 0 || height <= 0 || stride <= 0)
        return 0;
    if (!vgfx3d_opengl_checked_mul_size((size_t)width, 4u, &min_stride))
        return 0;
    if ((size_t)stride < min_stride)
        return 0;
    if (!vgfx3d_opengl_checked_mul_size((size_t)stride, (size_t)height, &total_bytes))
        return 0;
    if (out_bytes)
        *out_bytes = total_bytes;
    return 1;
}

/// @brief Clamp morph shape count to shader and index-range limits.
int32_t vgfx3d_opengl_clamp_morph_shape_count(uint32_t vertex_count,
                                              int32_t requested_shape_count) {
    int32_t shape_count;
    uint32_t max_indexed_vertices;
    uint32_t max_shapes_by_index;

    if (vertex_count == 0 || requested_shape_count <= 0)
        return 0;
    shape_count = requested_shape_count;
    if (shape_count > VGFX3D_OPENGL_MAX_MORPH_SHAPES)
        shape_count = VGFX3D_OPENGL_MAX_MORPH_SHAPES;
    max_indexed_vertices = (uint32_t)((INT_MAX - 2) / 3);
    max_shapes_by_index = max_indexed_vertices / vertex_count;
    if (max_shapes_by_index == 0)
        return 0;
    if ((uint32_t)shape_count > max_shapes_by_index)
        shape_count = (int32_t)max_shapes_by_index;
    return shape_count;
}

/// @brief Validate an R32F texture-buffer payload against a GL texel limit.
/// @details Morph-target buffers are bound as `samplerBuffer` with `R32F`, so
///   one texture-buffer texel stores exactly one float. A non-positive limit
///   means the backend could not query a useful `GL_MAX_TEXTURE_BUFFER_SIZE`,
///   in which case the caller should fall back to overflow-only validation.
int vgfx3d_opengl_texture_buffer_accepts_r32f_payload(size_t payload_bytes,
                                                      int32_t max_texture_buffer_texels) {
    size_t texels;

    if (payload_bytes == 0 || (payload_bytes % sizeof(float)) != 0)
        return 0;
    if (max_texture_buffer_texels <= 0)
        return 1;
    texels = payload_bytes / sizeof(float);
    return texels <= (size_t)max_texture_buffer_texels;
}

/// @brief Pick the right render-target classification for the OpenGL backend.
/// See vgfx3d_d3d11_choose_target_kind for the policy semantics.
vgfx3d_opengl_target_kind_t vgfx3d_opengl_choose_target_kind(int8_t rtt_active,
                                                             int8_t gpu_postfx_enabled) {
    if (rtt_active)
        return VGFX3D_OPENGL_TARGET_RTT;
    return gpu_postfx_enabled ? VGFX3D_OPENGL_TARGET_SCENE : VGFX3D_OPENGL_TARGET_SWAPCHAIN;
}

/// @brief Pick the GL internal color format for a given render-target classification.
/// @details Scene targets go through tonemap/postfx, so they allocate HDR (half-float)
///   to preserve intensity beyond [0, 1] prior to the final exposure curve. Swapchain
///   and RTT targets stay in UNORM8 since those are the final consumer surfaces and
///   extra precision is wasted on them.
vgfx3d_opengl_color_format_t vgfx3d_opengl_choose_color_format(
    vgfx3d_opengl_target_kind_t target_kind) {
    return target_kind == VGFX3D_OPENGL_TARGET_SCENE ? VGFX3D_OPENGL_COLOR_FORMAT_HDR16F
                                                     : VGFX3D_OPENGL_COLOR_FORMAT_UNORM8;
}

/// @brief Select storage precision for encoded motion/history/mask data.
/// @details Even though velocity is encoded into [0,1], eight-bit storage visibly quantizes
/// sub-pixel motion used by TAA. Float-capable contexts therefore use RGBA16F; RGBA8 remains a
/// compatibility fallback for drivers that cannot render to half-float attachments.
vgfx3d_opengl_color_format_t vgfx3d_opengl_choose_motion_format(int8_t supports_float_target) {
    return supports_float_target ? VGFX3D_OPENGL_COLOR_FORMAT_HDR16F
                                 : VGFX3D_OPENGL_COLOR_FORMAT_UNORM8;
}

/// @brief Select the GL blend mode for a draw command.
/// @details Explicit `additive_blend` wins outright (used by particles, decals of fire,
///   etc.). Otherwise the command's material alpha and vertex-color-alpha determine
///   whether alpha blending is needed, via `vgfx3d_draw_cmd_uses_alpha_blend`. Opaque
///   is the default — it skips the blend unit entirely and enables early-Z in the GPU.
vgfx3d_opengl_blend_mode_t vgfx3d_opengl_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    if (cmd && cmd->additive_blend)
        return VGFX3D_OPENGL_BLEND_ADDITIVE;
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_OPENGL_BLEND_ALPHA
                                                 : VGFX3D_OPENGL_BLEND_OPAQUE;
}

/// @brief Decide whether terrain splatting has every required texture bound.
int vgfx3d_opengl_has_complete_splat(int8_t cmd_has_splat,
                                     int has_splat_map,
                                     int has_layer0,
                                     int has_layer1,
                                     int has_layer2,
                                     int has_layer3) {
    return cmd_has_splat && has_splat_map && has_layer0 && has_layer1 && has_layer2 && has_layer3;
}

/// @brief Decide whether a draw contributes to the motion-vector buffer.
/// @details Motion vectors drive TAA / motion-blur postfx, both of which only run when
///   the scene target is bound. Transparent draws are excluded because blended fragments
///   don't have a single authoritative "this pixel came from there" source — their
///   motion vectors would corrupt the reconstruction. Opaque scene draws write both
///   color and motion; every other combination writes color only.
vgfx3d_opengl_motion_attachment_mode_t vgfx3d_opengl_choose_motion_attachment_mode(
    vgfx3d_opengl_target_kind_t target_kind, const vgfx3d_draw_cmd_t *cmd) {
    if (target_kind != VGFX3D_OPENGL_TARGET_SCENE)
        return VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY;
    return vgfx3d_opengl_choose_blend_mode(cmd) == VGFX3D_OPENGL_BLEND_OPAQUE
               ? VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_AND_MOTION
               : VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY;
}

/// @brief Decide whether canvas readback should source the swapchain or a postfx target.
vgfx3d_opengl_readback_kind_t vgfx3d_opengl_choose_readback_kind(int8_t gpu_postfx_enabled) {
    return gpu_postfx_enabled ? VGFX3D_OPENGL_READBACK_POSTFX_COMPOSITE
                              : VGFX3D_OPENGL_READBACK_BACKBUFFER;
}

/// @brief Clamp light shadow indices to completed contiguous shadow-map slots.
int32_t vgfx3d_opengl_sanitize_shadow_index(int32_t shadow_index, int32_t shadow_count) {
    if (shadow_count <= 0 || shadow_count > VGFX3D_MAX_SHADOW_LIGHTS)
        return -1;
    return (shadow_index >= 0 && shadow_index < shadow_count) ? shadow_index : -1;
}

/// @brief Project a world-space point through a shadow VP matrix using GLSL sampling rules.
int vgfx3d_opengl_project_shadow_coord(const float *shadow_vp,
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
    out_uv_depth[1] = ndc_y * 0.5f + 0.5f;
    out_uv_depth[2] = ndc_z * 0.5f + 0.5f;
    return 1;
}

/// @brief Decide whether to reuse a cached morph-target GPU buffer.
/// Returns 1 if the cached payload is still valid (same key + matching shape/vertex count),
/// 0 if the buffer must be re-uploaded.
int vgfx3d_opengl_should_reuse_morph_cache(const void *cached_key,
                                           uint64_t cached_revision,
                                           int32_t cached_shape_count,
                                           uint32_t cached_vertex_count,
                                           int8_t cached_has_normal_deltas,
                                           const vgfx3d_draw_cmd_t *cmd) {
    int32_t shape_count;
    int8_t has_normal_deltas;

    if (!cmd || !cmd->morph_key || cmd->morph_revision == 0 || !cmd->morph_deltas ||
        !cmd->morph_weights || cmd->morph_shape_count <= 0 || cmd->vertex_count == 0) {
        return 0;
    }

    shape_count = vgfx3d_opengl_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    if (shape_count <= 0)
        return 0;
    has_normal_deltas = cmd->morph_normal_deltas ? 1 : 0;
    return cached_key == cmd->morph_key && cached_revision == cmd->morph_revision &&
           cached_shape_count == shape_count && cached_vertex_count == cmd->vertex_count &&
           cached_has_normal_deltas == has_normal_deltas;
}

/// @brief Match a static mesh upload without trusting allocator addresses.
int vgfx3d_opengl_mesh_cache_matches(uint32_t cached_identity,
                                     uint32_t cached_revision,
                                     uint32_t cached_vertex_count,
                                     uint32_t cached_index_count,
                                     int8_t cached_compact,
                                     const vgfx3d_draw_cmd_t *cmd) {
    return cmd && cmd->geometry_key && cmd->geometry_identity != 0 &&
           cached_identity == cmd->geometry_identity && cached_revision == cmd->geometry_revision &&
           cached_vertex_count == cmd->vertex_count && cached_index_count == cmd->index_count &&
           (cached_compact ? 1 : 0) == (cmd->compact_vertex_stream ? 1 : 0);
}

/// @brief Decide whether a per-mesh GPU cache entry should be evicted this frame.
/// Returns 1 when the entry has been unused for more than @p ttl_frames frames.
int vgfx3d_opengl_should_prune_cache_entry(uint64_t current_frame,
                                           uint64_t last_used_frame,
                                           uint64_t max_age) {
    if (max_age == 0 || current_frame <= last_used_frame)
        return 0;
    return (current_frame - last_used_frame) > max_age;
}
