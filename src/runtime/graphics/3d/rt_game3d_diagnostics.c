//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_diagnostics.c
// Purpose: Process-wide Game3D degradation diagnostics and summary formatting.
//
// Key invariants:
//   - Recording paths perform only bounded integer increments.
//   - Summary lines are emitted in a stable order and omitted for zero counters.
//
// Ownership/Lifetime:
//   - Counter storage is process-global for the lifetime of the process.
//   - Summary allocates and returns a runtime string owned by the caller.
//
// Links: src/runtime/graphics/3d/rt_game3d_diagnostics.h,
//   src/il/runtime/runtime.def, docs/viperlib/graphics/game3d.md
//
//===----------------------------------------------------------------------===//

#include "rt_game3d_diagnostics.h"

#include "rt_audio_diagnostics.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int64_t broadphase_fallback_count;
    int64_t ccd_clamped_frames;
    int64_t ccd_clamped_bodies;
    int64_t anim_events_dropped;
    int64_t nav_grid_fallbacks;
    int64_t stale_entity_calls;
    int64_t stale_async_loads_dropped;
    int64_t stream_staging_errors;
    int64_t stream_stale_stages_dropped;
} rt_game3d_diagnostics_state;

static rt_game3d_diagnostics_state g_game3d_diagnostics;

static int64_t diag_nonnegative(int64_t value) {
    return value > 0 ? value : 0;
}

static void diag_increment(int64_t *counter, int64_t amount) {
    if (!counter || amount <= 0)
        return;
    if (*counter > INT64_MAX - amount) {
        *counter = INT64_MAX;
        return;
    }
    *counter += amount;
}

static void diag_append_line(
    char *buffer, size_t capacity, size_t *offset, const char *name, int64_t value) {
    char line[96];
    int written;
    size_t available;
    if (!buffer || capacity == 0 || !offset || !name || value <= 0 || *offset >= capacity)
        return;
    written = snprintf(line, sizeof(line), "%s=%lld\n", name, (long long)value);
    if (written <= 0)
        return;
    available = capacity - *offset;
    if ((size_t)written >= available)
        written = (int)available - 1;
    if (written <= 0)
        return;
    memcpy(buffer + *offset, line, (size_t)written);
    *offset += (size_t)written;
    buffer[*offset] = '\0';
}

int64_t rt_game3d_diagnostics_get_broadphase_fallback_count(void) {
    return diag_nonnegative(g_game3d_diagnostics.broadphase_fallback_count);
}

int64_t rt_game3d_diagnostics_get_ccd_clamped_frames(void) {
    return diag_nonnegative(g_game3d_diagnostics.ccd_clamped_frames);
}

int64_t rt_game3d_diagnostics_get_ccd_clamped_bodies(void) {
    return diag_nonnegative(g_game3d_diagnostics.ccd_clamped_bodies);
}

int64_t rt_game3d_diagnostics_get_anim_events_dropped(void) {
    return diag_nonnegative(g_game3d_diagnostics.anim_events_dropped);
}

int64_t rt_game3d_diagnostics_get_audio_voices_evicted(void) {
    return rt_audio_diagnostics_get_spatial_voice_evictions();
}

int64_t rt_game3d_diagnostics_get_nav_grid_fallbacks(void) {
    return diag_nonnegative(g_game3d_diagnostics.nav_grid_fallbacks);
}

int64_t rt_game3d_diagnostics_get_stale_entity_calls(void) {
    return diag_nonnegative(g_game3d_diagnostics.stale_entity_calls);
}

int64_t rt_game3d_diagnostics_get_stale_async_loads_dropped(void) {
    return diag_nonnegative(g_game3d_diagnostics.stale_async_loads_dropped);
}

int64_t rt_game3d_diagnostics_get_stream_staging_errors(void) {
    return diag_nonnegative(g_game3d_diagnostics.stream_staging_errors);
}

int64_t rt_game3d_diagnostics_get_stream_stale_stages_dropped(void) {
    return diag_nonnegative(g_game3d_diagnostics.stream_stale_stages_dropped);
}

void rt_game3d_diagnostics_reset(void) {
    memset(&g_game3d_diagnostics, 0, sizeof(g_game3d_diagnostics));
    rt_audio_diagnostics_reset_spatial_voice_evictions();
}

rt_string rt_game3d_diagnostics_summary(void) {
    char buffer[512];
    size_t offset = 0;
    buffer[0] = '\0';
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "BroadphaseFallbackCount",
                     g_game3d_diagnostics.broadphase_fallback_count);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "CcdClampedFrames",
                     g_game3d_diagnostics.ccd_clamped_frames);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "CcdClampedBodies",
                     g_game3d_diagnostics.ccd_clamped_bodies);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "AnimEventsDropped",
                     g_game3d_diagnostics.anim_events_dropped);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "AudioVoicesEvicted",
                     rt_audio_diagnostics_get_spatial_voice_evictions());
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "NavGridFallbacks",
                     g_game3d_diagnostics.nav_grid_fallbacks);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "StaleEntityCalls",
                     g_game3d_diagnostics.stale_entity_calls);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "StaleAsyncLoadsDropped",
                     g_game3d_diagnostics.stale_async_loads_dropped);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "StreamStagingErrors",
                     g_game3d_diagnostics.stream_staging_errors);
    diag_append_line(buffer,
                     sizeof(buffer),
                     &offset,
                     "StreamStaleStagesDropped",
                     g_game3d_diagnostics.stream_stale_stages_dropped);
    if (offset == 0)
        return rt_str_empty();
    return rt_string_from_bytes(buffer, offset);
}

void rt_game3d_diag_record_broadphase_fallback(void) {
    diag_increment(&g_game3d_diagnostics.broadphase_fallback_count, 1);
}

void rt_game3d_diag_record_ccd_clamp(int64_t affected_bodies) {
    diag_increment(&g_game3d_diagnostics.ccd_clamped_frames, 1);
    diag_increment(&g_game3d_diagnostics.ccd_clamped_bodies, affected_bodies);
}

void rt_game3d_diag_record_anim_events_dropped(int64_t count) {
    diag_increment(&g_game3d_diagnostics.anim_events_dropped, count);
}

void rt_game3d_diag_record_audio_voice_evicted(void) {
    rt_audio_diag_record_spatial_voice_evicted();
}

void rt_game3d_diag_record_nav_grid_fallback(void) {
    diag_increment(&g_game3d_diagnostics.nav_grid_fallbacks, 1);
}

void rt_game3d_diag_record_stale_entity_call(void) {
    diag_increment(&g_game3d_diagnostics.stale_entity_calls, 1);
}

void rt_game3d_diag_record_stale_async_load_dropped(void) {
    diag_increment(&g_game3d_diagnostics.stale_async_loads_dropped, 1);
}

void rt_game3d_diag_record_stream_staging_error(void) {
    diag_increment(&g_game3d_diagnostics.stream_staging_errors, 1);
}

void rt_game3d_diag_record_stream_stale_stage_dropped(void) {
    diag_increment(&g_game3d_diagnostics.stream_stale_stages_dropped, 1);
}
