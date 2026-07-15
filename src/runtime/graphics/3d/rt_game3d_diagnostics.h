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
int64_t rt_game3d_diagnostics_get_stale_async_loads_dropped(void);

/// @brief Process-wide count of streaming cell/tile staging failures (missing/corrupt payloads).
int64_t rt_game3d_diagnostics_get_stream_staging_errors(void);

/// @brief Process-wide count of worker-staged streaming payloads dropped as stale/undesired.
int64_t rt_game3d_diagnostics_get_stream_stale_stages_dropped(void);

/// @brief Process-wide count of EPA polytope-cap fallbacks (0-depth contacts emitted).
int64_t rt_game3d_diagnostics_get_epa_fallbacks(void);

/// @brief Process-wide count of shadow slots reused from their previous-frame depth.
int64_t rt_game3d_diagnostics_get_shadow_slots_reused(void);

/// @brief Process-wide count of opaque draws folded into auto-instanced batches.
int64_t rt_game3d_diagnostics_get_auto_instanced_draws(void);
void rt_game3d_diagnostics_reset(void);
rt_string rt_game3d_diagnostics_summary(void);

void rt_game3d_diag_record_broadphase_fallback(void);
void rt_game3d_diag_record_ccd_clamp(int64_t affected_bodies);
void rt_game3d_diag_record_anim_events_dropped(int64_t count);
void rt_game3d_diag_record_audio_voice_evicted(void);
void rt_game3d_diag_record_nav_grid_fallback(void);
void rt_game3d_diag_record_stale_entity_call(void);
void rt_game3d_diag_record_stale_async_load_dropped(void);

/// @brief Record a streaming staging failure (worker could not read/parse a payload).
void rt_game3d_diag_record_stream_staging_error(void);

/// @brief Record a worker-staged streaming payload dropped without being committed.
void rt_game3d_diag_record_stream_stale_stage_dropped(void);

/// @brief Record an EPA polytope-cap fallback (overlap reported with 0 depth).
void rt_game3d_diag_record_epa_fallback(void);

/// @brief Record a shadow slot satisfied from its previous-frame depth contents.
void rt_game3d_diag_record_shadow_slot_reused(void);

/// @brief Record @p count opaque draws folded into one auto-instanced batch.
void rt_game3d_diag_record_auto_instanced_draws(int64_t count);

#ifdef __cplusplus
}
#endif
