//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_skinning.h
// Purpose: CPU-side vertex skinning — transforms vertices by a bone palette.
//   Each vertex has up to 4 bone indices + weights (from vgfx3d_vertex_t).
//
// Key invariants:
//   - Bone palette is bone_count * 16 floats (row-major 4x4 per bone).
//   - Weights are NOT required to sum to 1.0 (implicit normalization).
//   - Skinned normals are re-normalized after blending.
//   - src and dst may alias (in-place skinning).
//
// Ownership/Lifetime:
//   - Source, destination, and palette storage remain caller-owned.
//   - Optional scratch storage is caller-owned and reused across draws.
//
// Links: rt_canvas3d_internal.h (vgfx3d_vertex_t), plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "vgfx3d_skinning_scratch.h"
#include <stdint.h>

/// @brief Apply skeletal skinning on the CPU.
/// Transforms position and normal of each vertex by the weighted bone matrices.
/// @brief CPU-skin @p vertex_count vertices applying both the vertex record's 4
///   influences and the optional per-vertex influences 5-8 side stream.
void vgfx3d_skin_vertices_extra(const vgfx3d_vertex_t *src,
                                vgfx3d_vertex_t *dst,
                                uint32_t vertex_count,
                                const float *palette,
                                int32_t bone_count,
                                const vgfx3d_extra_influences_t *extra,
                                vgfx3d_skinning_scratch_t *scratch);

void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src,
                          vgfx3d_vertex_t *dst,
                          uint32_t vertex_count,
                          const float *bone_palette,
                          int32_t bone_count,
                          vgfx3d_skinning_scratch_t *scratch);

#endif /* VIPER_ENABLE_GRAPHICS */
