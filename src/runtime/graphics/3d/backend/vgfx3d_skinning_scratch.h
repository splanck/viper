//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_skinning_scratch.h
// Purpose: Grow-only CPU skinning scratch storage shared by canvas-frame draw paths.
// Key invariants:
//   - Scratch buffers are caller-owned and never static/global.
//   - Capacity only grows; callers decide when to release storage.
// Ownership/Lifetime:
//   - The owner initializes the struct to zero and frees it with
//     vgfx3d_skinning_scratch_free().
//   - Pointers are invalidated only by scratch growth or explicit free.
// Links: vgfx3d_skinning.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float *normal_palette;
    int32_t normal_palette_capacity;
    uint64_t normal_palette_grow_count;
    /* Per-bone finiteness flags, computed once per palette. Matrix validity
     * is a bone property, so hoisting it out of the vertex loop removes up
     * to ~128 isfinite() calls per vertex from the CPU-skinning inner loop. */
    uint8_t *bone_valid;
    int32_t bone_valid_capacity;
} vgfx3d_skinning_scratch_t;

/// @brief Release all owned CPU skinning scratch buffers and reset counters.
void vgfx3d_skinning_scratch_free(vgfx3d_skinning_scratch_t *scratch);

#ifdef __cplusplus
}
#endif
