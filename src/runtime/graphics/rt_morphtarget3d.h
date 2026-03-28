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

void *rt_morphtarget3d_new(int64_t vertex_count);
int64_t rt_morphtarget3d_add_shape(void *mt, rt_string name);
void rt_morphtarget3d_set_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
void rt_morphtarget3d_set_normal_delta(
    void *mt, int64_t shape, int64_t vertex, double dx, double dy, double dz);
void rt_morphtarget3d_set_weight(void *mt, int64_t shape, double weight);
double rt_morphtarget3d_get_weight(void *mt, int64_t shape);
void rt_morphtarget3d_set_weight_by_name(void *mt, rt_string name, double weight);
int64_t rt_morphtarget3d_get_shape_count(void *mt);
void rt_mesh3d_set_morph_targets(void *mesh, void *morph_targets);
void rt_canvas3d_draw_mesh_morphed(
    void *canvas, void *mesh, void *transform, void *material, void *morph_targets);

#ifdef __cplusplus
}
#endif
