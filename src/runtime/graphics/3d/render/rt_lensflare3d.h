//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_lensflare3d.h
// Purpose: LensFlare3D — occlusion-aware ghost-chain lens flares bound to a
//   Light3D and drawn in overlay space along the light->screen-center axis.
// Key invariants:
//   - Elements hold pre-tinted procedural ghost sprites (radial-falloff discs).
//   - Occlusion probes the CPU depth buffer around the light's screen position;
//     backends without CPU depth draw unoccluded (documented divergence).
// Ownership/Lifetime:
//   - LensFlare3D is GC-managed; finalizer releases the bound light and every
//     element's ghost Pixels.
// Links: rt_canvas3d.h, rt_light3d.c, misc/plans/fps/07-visual-polish.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a lens flare bound to @p light (retained).
void *rt_lensflare3d_new(void *light);
/// @brief Add a ghost element (axisOffset -1..2, size px at 1080p-normalized
///        scale, packed 0xRRGGBB tint, rotation accepted for API stability).
void rt_lensflare3d_add_element(
    void *obj, double axis_offset, double size, int64_t color_rgb, double rotation);
/// @brief Draw the flare into the canvas overlay (call after End, before Flip).
void rt_canvas3d_draw_lens_flare(void *canvas, void *flare);

#ifdef __cplusplus
}
#endif
