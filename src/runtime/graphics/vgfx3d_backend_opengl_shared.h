#pragma once

#include "vgfx3d_backend.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VGFX3D_OPENGL_BLEND_OPAQUE = 0,
    VGFX3D_OPENGL_BLEND_ALPHA = 1,
} vgfx3d_opengl_blend_mode_t;

typedef enum {
    VGFX3D_OPENGL_TARGET_SWAPCHAIN = 0,
    VGFX3D_OPENGL_TARGET_SCENE = 1,
    VGFX3D_OPENGL_TARGET_RTT = 2,
} vgfx3d_opengl_target_kind_t;

typedef enum {
    VGFX3D_OPENGL_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_OPENGL_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_opengl_color_format_t;

typedef enum {
    VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY = 0,
    VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_AND_MOTION = 1,
} vgfx3d_opengl_motion_attachment_mode_t;

typedef enum {
    VGFX3D_OPENGL_READBACK_BACKBUFFER = 0,
    VGFX3D_OPENGL_READBACK_POSTFX_COMPOSITE = 1,
} vgfx3d_opengl_readback_kind_t;

typedef struct {
    float scene_vp[16];
    float scene_prev_vp[16];
    float scene_inv_vp[16];
    float draw_prev_vp[16];
    float scene_cam_pos[3];
    int8_t scene_history_valid;
} vgfx3d_opengl_frame_history_t;

void vgfx3d_opengl_update_frame_history(vgfx3d_opengl_frame_history_t *history,
                                        const float *vp,
                                        const float *inv_vp,
                                        const float *cam_pos,
                                        int8_t is_overlay_pass);
int32_t vgfx3d_opengl_compute_mip_count(int32_t width, int32_t height);
int32_t vgfx3d_opengl_next_capacity(int32_t current_capacity,
                                    int32_t needed,
                                    int32_t minimum_capacity);
vgfx3d_opengl_target_kind_t vgfx3d_opengl_choose_target_kind(int8_t rtt_active,
                                                             int8_t gpu_postfx_enabled);
vgfx3d_opengl_color_format_t
vgfx3d_opengl_choose_color_format(vgfx3d_opengl_target_kind_t target_kind);
vgfx3d_opengl_blend_mode_t
vgfx3d_opengl_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
vgfx3d_opengl_motion_attachment_mode_t
vgfx3d_opengl_choose_motion_attachment_mode(vgfx3d_opengl_target_kind_t target_kind,
                                            const vgfx3d_draw_cmd_t *cmd);
vgfx3d_opengl_readback_kind_t vgfx3d_opengl_choose_readback_kind(int8_t gpu_postfx_enabled);
int vgfx3d_opengl_should_reuse_morph_cache(const void *cached_key,
                                           uint64_t cached_revision,
                                           int32_t cached_shape_count,
                                           uint32_t cached_vertex_count,
                                           int8_t cached_has_normal_deltas,
                                           const vgfx3d_draw_cmd_t *cmd);
int vgfx3d_opengl_should_prune_cache_entry(uint64_t current_frame,
                                           uint64_t last_used_frame,
                                           uint64_t max_age);

#ifdef __cplusplus
}
#endif
