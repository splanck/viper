//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_morphtarget3d_internal.h
// Purpose: Narrow internal bulk-data boundary for MorphTarget3D persistence.
// Key invariants:
//   - Views expose borrowed, immutable shape storage only after handle validation.
//   - Bulk append requires an exact vertex-count match and finite float payloads.
// Ownership/Lifetime:
//   - Returned pointers remain owned by the MorphTarget3D and are invalidated by mutation.
//   - Append copies every supplied byte; the caller retains ownership of its buffers.
// Links: rt_morphtarget3d.c, rt_scene3d_vscn_save.c, rt_scene3d_vscn_load.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Borrowed data for one validated morph shape.
typedef struct rt_morphtarget3d_shape_view_internal {
    const char *name;
    const float *position_deltas;
    const float *normal_deltas;
    const float *tangent_deltas;
    double weight;
    int32_t vertex_count;
} rt_morphtarget3d_shape_view_internal;

/// @brief Borrow one shape's complete persistence payload.
/// @return One on success; zero for a null/wrong-class handle or invalid index.
int8_t rt_morphtarget3d_get_shape_view_internal(void *morph,
                                                int64_t shape_index,
                                                rt_morphtarget3d_shape_view_internal *out_view);

/// @brief Copy one complete shape into an existing morph container.
/// @details The destination vertex count must equal `view->vertex_count`; position deltas are
/// required, normal/tangent deltas are optional, and all values must be finite.
/// @return One after publication; zero without publishing a usable shape on validation/OOM.
int8_t rt_morphtarget3d_append_shape_internal(void *morph,
                                              const rt_morphtarget3d_shape_view_internal *view);

#ifdef __cplusplus
}
#endif
