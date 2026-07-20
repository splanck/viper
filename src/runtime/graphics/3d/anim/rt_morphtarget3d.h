//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_morphtarget3d.h
// Purpose: MorphTarget3D — per-vertex position/normal/tangent deltas blended by
//   weight. Enables facial animation, muscle flex, and shape-based deformation.
//
// Key invariants:
//   - Shape storage grows on demand; GPU backends may still fall back to CPU
//     morphing when the active shape count exceeds backend shader limits.
//   - vertex_count must match the mesh's vertex count at draw time.
//   - Morphed vertices are applied on GPU when supported, otherwise on CPU.
//   - Normal/tangent deltas are optional per shape (NULL = channel unchanged).
//
// Ownership/Lifetime:
//   - MorphTarget3D objects are GC-managed and own every shape name/delta array.
//   - Mesh bindings retain the container; packed GPU views remain borrowed.
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

/// @brief Create a morph-target container for blendshape animation.
void *rt_morphtarget3d_new(int64_t vertex_count);
/// @brief Deep-copy a morph-target container, including shapes, deltas, and weights.
void *rt_morphtarget3d_clone(void *mt);
/**
 * @brief Deep-copy a morph-target container through a simplified vertex map.
 *
 * Each output vertex `i` receives the position, normal, and tangent deltas from
 * source vertex `new_to_old[i]` for every shape. Shape names, current/previous
 * weights, motion snapshots, and motion-history state are preserved. The result
 * owns independent arrays and has exactly @p new_vertex_count vertices.
 *
 * @param mt Source MorphTarget3D handle.
 * @param new_to_old Borrowed output-to-source vertex index map.
 * @param new_vertex_count Number of output vertices and entries in @p new_to_old.
 * @return A new MorphTarget3D, or `NULL` for an invalid map/handle or allocation
 * failure. The source remains unchanged.
 */
void *rt_morphtarget3d_clone_remapped(void *mt,
                                      const uint32_t *new_to_old,
                                      uint32_t new_vertex_count);
/// @brief Register a named blendshape and allocate its delta arrays. Returns the new shape index.
int64_t rt_morphtarget3d_add_shape(void *mt, rt_string name);
/// @brief Set position delta for one vertex of one shape.
void rt_morphtarget3d_set_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
/// @brief Set normal delta for one vertex of one shape (lazy-allocates the per-shape normal array).
void rt_morphtarget3d_set_normal_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
/// @brief Set tangent delta for one vertex of one shape (lazy-allocates tangent storage).
void rt_morphtarget3d_set_tangent_delta(
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
/// @brief Largest position-delta vector length across every shape and vertex.
double rt_morphtarget3d_get_max_position_delta(void *mt);
/// @brief Borrow the packed normal-delta array, or NULL if no shape has normal deltas.
const float *rt_morphtarget3d_get_packed_normal_deltas(void *mt);
/// @brief True when any shape carries tangent deltas that require CPU morphing.
int64_t rt_morphtarget3d_has_tangent_deltas(void *mt);
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
