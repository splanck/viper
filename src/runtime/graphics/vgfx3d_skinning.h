//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_skinning.h
// Purpose: CPU-side vertex skinning — transforms vertices by a bone palette.
//   Each vertex has up to 4 bone indices + weights (from vgfx3d_vertex_t).
//
// Key invariants:
//   - Bone palette is bone_count * 16 floats (row-major 4x4 per bone).
//   - Weights are NOT required to sum to 1.0 (implicit normalization).
//   - Skinned normals are re-normalized after blending.
//   - src and dst may alias (in-place skinning).
//
// Links: rt_canvas3d_internal.h (vgfx3d_vertex_t), plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include <stdint.h>

/// @brief Apply skeletal skinning on the CPU.
/// Transforms position and normal of each vertex by the weighted bone matrices.
void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src,
                          vgfx3d_vertex_t *dst,
                          uint32_t vertex_count,
                          const float *bone_palette,
                          int32_t bone_count);

#endif /* VIPER_ENABLE_GRAPHICS */
