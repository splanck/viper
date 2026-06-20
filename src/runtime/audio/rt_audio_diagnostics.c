//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_diagnostics.c
// Purpose: Audio-domain degradation diagnostics counter storage.
//
// Key invariants:
//   - Recording paths are bounded integer increments and allocate no memory.
//   - Counters saturate at INT64_MAX instead of overflowing.
//
// Ownership/Lifetime:
//   - Counter storage is process-global and owned by the runtime.
//
// Links: src/runtime/audio/rt_audio_diagnostics.h,
//        src/runtime/audio/rt_sound3d.c
//
//===----------------------------------------------------------------------===//

#include "rt_audio_diagnostics.h"

#include <stdint.h>

static int64_t g_spatial_voice_evictions = 0;

int64_t rt_audio_diagnostics_get_spatial_voice_evictions(void) {
    return g_spatial_voice_evictions > 0 ? g_spatial_voice_evictions : 0;
}

void rt_audio_diagnostics_reset_spatial_voice_evictions(void) {
    g_spatial_voice_evictions = 0;
}

void rt_audio_diag_record_spatial_voice_evicted(void) {
    if (g_spatial_voice_evictions < INT64_MAX)
        g_spatial_voice_evictions++;
}
