#pragma once

#include "vgfx3d_backend.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGFX3D_METAL_MAX_BONES 128

typedef enum {
    VGFX3D_METAL_BLEND_OPAQUE = 0,
    VGFX3D_METAL_BLEND_ALPHA = 1,
} vgfx3d_metal_blend_mode_t;

typedef enum {
    VGFX3D_METAL_TARGET_SWAPCHAIN = 0,
    VGFX3D_METAL_TARGET_SCENE = 1,
    VGFX3D_METAL_TARGET_RTT = 2,
    VGFX3D_METAL_TARGET_OVERLAY = 3,
} vgfx3d_metal_target_kind_t;

typedef enum {
    VGFX3D_METAL_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_METAL_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_metal_color_format_t;

typedef enum {
    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY = 0,
    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_AND_MOTION = 1,
} vgfx3d_metal_motion_attachment_mode_t;

typedef enum {
    VGFX3D_METAL_READBACK_BACKBUFFER = 0,
    VGFX3D_METAL_READBACK_POSTFX_COMPOSITE = 1,
} vgfx3d_metal_readback_kind_t;

typedef struct {
    float scene_vp[16];
    float scene_prev_vp[16];
    float scene_inv_vp[16];
    float draw_prev_vp[16];
    float scene_cam_pos[3];
    int8_t scene_history_valid;
    int8_t overlay_used_this_frame;
} vgfx3d_metal_frame_history_t;

typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} vgfx3d_metal_instance_data_t;

/// @brief Copy bone palette into a fixed-size MTLBuffer slot (zero-pads unused bones).
void vgfx3d_metal_pack_bone_palette(float *dst, const float *src, int32_t bone_count);
/// @brief Build per-instance Metal buffer entries with column-major transpose for MSL.
void vgfx3d_metal_fill_instance_data(vgfx3d_metal_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices);
/// @brief Roll per-frame VP/inv-VP/cam-pos history forward by one frame.
void vgfx3d_metal_update_frame_history(vgfx3d_metal_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target);
/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_metal_compute_mip_count(int32_t width, int32_t height);
/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_metal_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity);
/// @brief Pick the right render-target classification for the Metal backend.
vgfx3d_metal_target_kind_t vgfx3d_metal_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color);
/// @brief Decide whether the next pass should preserve existing color contents.
int8_t vgfx3d_metal_should_load_existing_color(vgfx3d_metal_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame);
/// @brief Pick the color format — HDR16F for the scene pass, UNORM8 elsewhere.
vgfx3d_metal_color_format_t
vgfx3d_metal_choose_color_format(vgfx3d_metal_target_kind_t target_kind);
/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_metal_blend_mode_t
vgfx3d_metal_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether to attach a motion-vector buffer (only for opaque scene draws).
vgfx3d_metal_motion_attachment_mode_t
vgfx3d_metal_choose_motion_attachment_mode(vgfx3d_metal_target_kind_t target_kind,
                                           const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether canvas readback should source the backbuffer or postfx target.
vgfx3d_metal_readback_kind_t vgfx3d_metal_choose_readback_kind(int8_t gpu_postfx_enabled);
/// @brief Decide whether to reuse a cached morph-target Metal buffer (key + revision + counts match).
int vgfx3d_metal_should_reuse_morph_cache(const void *cached_key,
                                          uint64_t cached_revision,
                                          int32_t cached_shape_count,
                                          uint32_t cached_vertex_count,
                                          int8_t cached_has_normal_deltas,
                                          const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether a per-mesh GPU cache entry should be evicted (unused for > max_age frames).
int vgfx3d_metal_should_prune_cache_entry(uint64_t current_frame,
                                          uint64_t last_used_frame,
                                          uint64_t max_age);

#ifdef __cplusplus
}
#endif
