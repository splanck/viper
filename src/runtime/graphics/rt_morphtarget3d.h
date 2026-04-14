//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_morphtarget3d.h
// Purpose: MorphTarget3D — per-vertex position/normal deltas blended by
//   weight. Enables facial animation, muscle flex, and shape-based deformation.
//
// Key invariants:
//   - Max 32 shapes per MorphTarget3D (VGFX3D_MAX_MORPH_SHAPES).
//   - vertex_count must match the mesh's vertex count at draw time.
//   - CPU-applied: morphed vertices submitted through normal draw pipeline.
//   - Normal deltas are optional per shape (NULL = normals unchanged).
//
// Links: plans/3d/16-morph-targets.md, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a morph-target container for blendshape animation (max 32 shapes).
void *rt_morphtarget3d_new(int64_t vertex_count);
/// @brief Register a named blendshape and allocate its delta arrays. Returns the new shape index.
int64_t rt_morphtarget3d_add_shape(void *mt, rt_string name);
/// @brief Set position delta for one vertex of one shape.
void rt_morphtarget3d_set_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
/// @brief Set normal delta for one vertex of one shape (lazy-allocates the per-shape normal array).
void rt_morphtarget3d_set_normal_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
/// @brief Set the blend weight for a shape by index (0.0 = off, 1.0 = full).
void rt_morphtarget3d_set_weight(void *mt, int64_t shape, double weight);
/// @brief Get the blend weight of the @p shape-th shape.
double rt_morphtarget3d_get_weight(void *mt, int64_t shape);
/// @brief Set the blend weight for a shape looked up by its name.
void rt_morphtarget3d_set_weight_by_name(void *mt, rt_string name, double weight);
/// @brief Number of registered shapes.
int64_t rt_morphtarget3d_get_shape_count(void *mt);
/// @brief Borrow the packed position-delta array (shape-major) for GPU upload.
const float *rt_morphtarget3d_get_packed_deltas(void *mt);
/// @brief Borrow the packed normal-delta array, or NULL if no shape has normal deltas.
const float *rt_morphtarget3d_get_packed_normal_deltas(void *mt);
/// @brief Monotonic generation counter; bumps whenever any delta changes.
uint64_t rt_morphtarget3d_get_payload_generation(void *mt);
/// @brief Bind a MorphTarget3D to a Mesh3D (vertex counts must match exactly).
void rt_mesh3d_set_morph_targets(void *mesh, void *morph_targets);
/// @brief Draw a mesh with morph targets applied (GPU path on Metal/OGL/D3D11, CPU otherwise).
void rt_canvas3d_draw_mesh_morphed(
    void *canvas, void *mesh, void *transform, void *material, void *morph_targets);

#ifdef __cplusplus
}
#endif
