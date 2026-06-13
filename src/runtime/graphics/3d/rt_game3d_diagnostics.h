//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_diagnostics.h
// Purpose: Process-wide degradation diagnostics for Game3D and Graphics3D
//   subsystems that may fall back to slower or lower-fidelity behavior.
//
// Key invariants:
//   - Counters are plain signed 64-bit totals and never allocate on record.
//   - Summary output is stable, newline-delimited, and empty when all counters
//     are zero.
//
// Ownership/Lifetime:
//   - Counter storage is process-global and owned by the runtime.
//   - Summary returns a caller-owned runtime string.
//
// Links: src/runtime/graphics/3d/rt_game3d_diagnostics.c,
//   src/il/runtime/runtime.def, docs/viperlib/graphics/game3d.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t rt_game3d_diagnostics_get_broadphase_fallback_count(void);
int64_t rt_game3d_diagnostics_get_ccd_clamped_frames(void);
int64_t rt_game3d_diagnostics_get_ccd_clamped_bodies(void);
int64_t rt_game3d_diagnostics_get_anim_events_dropped(void);
int64_t rt_game3d_diagnostics_get_audio_voices_evicted(void);
int64_t rt_game3d_diagnostics_get_nav_grid_fallbacks(void);
int64_t rt_game3d_diagnostics_get_stale_entity_calls(void);
void rt_game3d_diagnostics_reset(void);
rt_string rt_game3d_diagnostics_summary(void);

void rt_game3d_diag_record_broadphase_fallback(void);
void rt_game3d_diag_record_ccd_clamp(int64_t affected_bodies);
void rt_game3d_diag_record_anim_events_dropped(int64_t count);
void rt_game3d_diag_record_audio_voice_evicted(void);
void rt_game3d_diag_record_nav_grid_fallback(void);
void rt_game3d_diag_record_stale_entity_call(void);

#ifdef __cplusplus
}
#endif
