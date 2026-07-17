//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_untrusted_count.h
// Purpose: Shared validation for counts read from untrusted 3D asset files.
//
// Key invariants:
//   - Negative counts are always invalid.
//   - count * elem_min_bytes must fit in size_t and stay within the available byte budget.
//
// Ownership/Lifetime:
//   - Header-only helper; owns no memory and performs no allocation.
//
// Links: assets/rt_gltf.c, assets/rt_fbx_loader.c, render/rt_mesh3d.c,
//        scene/rt_scene3d_vscn_load.c, rt_game3d.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Validate an untrusted element count against the bytes available for its source data.
static inline int rt_untrusted_count_ok(int64_t count,
                                        size_t elem_min_bytes,
                                        size_t available_bytes) {
    uint64_t unsigned_count;

    if (count < 0)
        return 0;
    if (elem_min_bytes == 0u)
        return count == 0;
    unsigned_count = (uint64_t)count;
    if (unsigned_count > (uint64_t)(SIZE_MAX / elem_min_bytes))
        return 0;
    return (size_t)unsigned_count <= available_bytes / elem_min_bytes;
}

#ifdef __cplusplus
}
#endif
