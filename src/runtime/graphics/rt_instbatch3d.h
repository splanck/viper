//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_instbatch3d.h
// Purpose: Instanced rendering — draw N copies of one mesh with different
//   transforms in a single draw call. Software fallback loops individual draws.
//
// Key invariants:
//   - Mesh and material are borrowed references (not owned).
//   - Transforms stored as contiguous float[16*N] array (row-major Mat4).
//   - Per-instance frustum culling at Canvas3D level before submission.
//
// Links: rt_canvas3d.h, vgfx3d_backend.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_instbatch3d_new(void *mesh, void *material);
    void rt_instbatch3d_add(void *batch, void *transform);
    void rt_instbatch3d_remove(void *batch, int64_t index);
    void rt_instbatch3d_set(void *batch, int64_t index, void *transform);
    void rt_instbatch3d_clear(void *batch);
    int64_t rt_instbatch3d_count(void *batch);
    void rt_canvas3d_draw_instanced(void *canvas, void *batch);

#ifdef __cplusplus
}
#endif
