//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_postfx3d.h
// Purpose: PostFX3D — full-screen post-processing effect chain (bloom, tone
//   mapping, FXAA, color grading, vignette). Applied to the rendered
//   framebuffer after scene drawing completes.
//
// Key invariants:
//   - Effects are applied in chain order (first added = first applied).
//   - Max 8 effects per chain.
//   - Software path: per-pixel operations on the CPU framebuffer.
//   - SetPostFX on Canvas3D enables automatic application in Flip().
//
// Links: plans/3d/18-post-processing.md, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_postfx3d_new(void);
    void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes);
    void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure);
    void rt_postfx3d_add_fxaa(void *obj);
    void rt_postfx3d_add_color_grade(void *obj,
                                     double brightness,
                                     double contrast,
                                     double saturation);
    void rt_postfx3d_add_vignette(void *obj, double radius, double softness);
    void rt_postfx3d_set_enabled(void *obj, int8_t enabled);
    int8_t rt_postfx3d_get_enabled(void *obj);
    void rt_postfx3d_clear(void *obj);
    int64_t rt_postfx3d_get_effect_count(void *obj);
    void rt_canvas3d_set_post_fx(void *canvas, void *postfx);

    /* Phase F additions */
    void rt_postfx3d_add_ssao(void *obj, double radius, double intensity, int64_t samples);
    void rt_postfx3d_add_dof(void *obj, double focus_distance, double aperture, double max_blur);
    void rt_postfx3d_add_motion_blur(void *obj, double intensity, int64_t samples);

#ifdef __cplusplus
}
#endif
