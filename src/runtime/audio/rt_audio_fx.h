//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_fx.h
// Purpose: Runtime-owned insert effects for integer audio mix groups.
// Key invariants:
//   - Effect ids are per-group handles returned by the add functions; -1
//     signals failure (invalid group or effect table full).
//   - Public numeric parameters are sanitized/clamped so a bad Float cannot
//     poison filter state (frequencies/Q to valid ranges, peaking gain to
//     +/-40 dB, mix levels to [0, 1]).
// Ownership/Lifetime:
//   - Effect state is owned by the process-wide FX tables; Clear*/Remove
//     release it. No caller-visible allocations.
// Links: src/runtime/audio/rt_audio_fx.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Append a low-pass biquad insert to @p group. @return fx id, or -1.
int64_t rt_audio_fx_add_lowpass(int64_t group, double cutoff_hz, double q);

/// @brief Append a high-pass biquad insert to @p group. @return fx id, or -1.
int64_t rt_audio_fx_add_highpass(int64_t group, double cutoff_hz, double q);

/// @brief Append a peaking EQ insert (gain clamped to +/-40 dB). @return fx id, or -1.
int64_t rt_audio_fx_add_peaking(int64_t group, double freq_hz, double q, double gain_db);

/// @brief Append a feedback-delay insert (delay/feedback/wet clamped). @return fx id, or -1.
int64_t rt_audio_fx_add_delay(int64_t group, double delay_ms, double feedback, double wet);

/// @brief Append a reverb insert (room/damping/wet clamped). @return fx id, or -1.
int64_t rt_audio_fx_add_reverb(int64_t group, double room_size, double damping, double wet);

/// @brief Enable/disable an insert without removing it (unknown ids are no-ops).
void rt_audio_fx_set_bypass(int64_t group, int64_t fx_id, int8_t bypass);

/// @brief Update a reverb insert's room/damping/wet in place (no reallocation).
void rt_audio_fx_set_reverb_params(
    int64_t group, int64_t fx_id, double room_size, double damping, double wet);
/// @brief Remove one insert from @p group (unknown ids are no-ops).
void rt_audio_fx_remove(int64_t group, int64_t fx_id);

/// @brief Remove every insert from @p group.
void rt_audio_fx_clear_group(int64_t group);

/// @brief Remove every insert from every group.
void rt_audio_fx_clear_all(void);

/// @brief 1 when @p group has at least one active (non-bypassed) insert.
int rt_audio_fx_group_has_effects(int64_t group);

/// @brief Run @p group's insert chain over interleaved float samples in place.
/// @details Called from the mixer callback; non-finite outputs are sanitized
///          to silence per sample.
void rt_audio_fx_process_group(
    int64_t group, float *samples, int32_t frames, int32_t channels, int32_t sample_rate);

#ifdef __cplusplus
}
#endif
