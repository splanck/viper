//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_fx.h
// Purpose: Runtime-owned insert effects for integer audio mix groups.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t rt_audio_fx_add_lowpass(int64_t group, double cutoff_hz, double q);
int64_t rt_audio_fx_add_highpass(int64_t group, double cutoff_hz, double q);
int64_t rt_audio_fx_add_peaking(int64_t group, double freq_hz, double q, double gain_db);
int64_t rt_audio_fx_add_delay(int64_t group, double delay_ms, double feedback, double wet);
int64_t rt_audio_fx_add_reverb(int64_t group, double room_size, double damping, double wet);

void rt_audio_fx_set_bypass(int64_t group, int64_t fx_id, int8_t bypass);

/// @brief Update a reverb insert's room/damping/wet in place (no reallocation).
void rt_audio_fx_set_reverb_params(
    int64_t group, int64_t fx_id, double room_size, double damping, double wet);
void rt_audio_fx_remove(int64_t group, int64_t fx_id);
void rt_audio_fx_clear_group(int64_t group);
void rt_audio_fx_clear_all(void);

int rt_audio_fx_group_has_effects(int64_t group);
void rt_audio_fx_process_group(
    int64_t group, float *samples, int32_t frames, int32_t channels, int32_t sample_rate);

#ifdef __cplusplus
}
#endif
