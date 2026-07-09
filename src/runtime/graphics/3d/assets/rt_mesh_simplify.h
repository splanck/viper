//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_mesh_simplify.h
// Purpose: Quadric-error-metric mesh simplification (Mesh3D.Simplify) and the
//   one-call LOD-chain generator (SceneNode.GenerateLODs).
// Key invariants:
//   - Simplify returns a NEW mesh; attributes are never interpolated (subset
//     placement), so skinned meshes decimate safely and UV seams survive.
//   - Deterministic for identical inputs.
// Ownership/Lifetime: returned meshes are GC-managed Mesh3D objects.
// Links: rt_mesh_simplify.c, misc/plans/fps/09-asset-pipeline-upgrades.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Decimate @p mesh to approximately @p target_triangles (new mesh).
void *rt_mesh3d_simplify(void *mesh, int64_t target_triangles);
/// @brief Build 1..4 LOD levels (ratio^k triangles) and enable auto selection.
void rt_scene_node3d_generate_lods(void *node, int64_t levels, double ratio);

#ifdef __cplusplus
}
#endif
