//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_instbatch3d.h
// Purpose: Instanced rendering — draw N copies of one mesh with different
//   transforms in a single draw call. Software fallback loops individual draws.
//
// Key invariants:
//   - Mesh and material are retained by the batch while it is alive.
//   - Transforms stored as contiguous float[16*N] array (row-major Mat4).
//   - Per-instance frustum culling at Canvas3D level before submission.
//
// Links: rt_canvas3d.h, vgfx3d_backend.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create an instanced-rendering batch bound to one mesh + material.
void *rt_instbatch3d_new(void *mesh, void *material);
/// @brief Append an instance with the given Mat4 transform to the batch.
void rt_instbatch3d_add(void *batch, void *transform);
/// @brief Remove the instance at @p index via swap-remove (O(1), order not preserved).
void rt_instbatch3d_remove(void *batch, int64_t index);
/// @brief Replace the transform of the @p index-th instance.
void rt_instbatch3d_set(void *batch, int64_t index, void *transform);
/// @brief Drop every instance from the batch.
void rt_instbatch3d_clear(void *batch);
/// @brief Number of instances currently in the batch.
int64_t rt_instbatch3d_count(void *batch);
/// @brief Submit the batch as a single GPU-instanced draw call.
void rt_canvas3d_draw_instanced(void *canvas, void *batch);
/// @brief Submit the batch with one AnimPlayer3D/AnimController3D palette per instance (R18).
void rt_canvas3d_draw_instanced_skinned(void *canvas, void *batch, void *players);
/// @brief Internal bridge: borrow the batch's retained mesh (NULL for invalid handles).
void *rt_instbatch3d_borrow_mesh(void *batch);
/// @brief Internal bridge: borrow the batch's retained material (NULL for invalid handles).
void *rt_instbatch3d_borrow_material(void *batch);
/// @brief Internal bridge: borrow the batch's sanitized float transform array (N * 16 values).
const float *rt_instbatch3d_borrow_transforms(void *batch, int32_t *out_count);

#ifdef __cplusplus
}
#endif
