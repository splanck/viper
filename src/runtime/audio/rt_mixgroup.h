//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_mixgroup.h
// Purpose: Audio mix groups (MUSIC / SFX) with independent volume control,
//   music crossfading, and group-aware sound playback.
//
// Key invariants:
//   - Two built-in groups: RT_MIXGROUP_MUSIC (0) and RT_MIXGROUP_SFX (1).
//   - Group volumes default to 100 (full) and are clamped to [0, 100].
//   - Effective volume = voice_volume × group_volume × master_volume / 10000.
//   - Crossfades are tracked per active music pair; unrelated playlist transitions do not collide.
//
// Ownership/Lifetime:
//   - Mix group state is global (module-level); no allocation needed.
//   - Crossfade holds references to two music objects during the fade.
//
// Links: src/runtime/audio/rt_audio.c (implementation),
//        src/runtime/audio/rt_playlist.h (playlist crossfade integration)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Mix group constants.
#define RT_MIXGROUP_MUSIC 0
#define RT_MIXGROUP_SFX 1
#define RT_MIXGROUP_COUNT 2
#define RT_MIXGROUP_NAMED_BASE 100
#define RT_MIXGROUP_MAX_GROUPS 256

//=========================================================================
// Mix Group Volume
//=========================================================================

/// @brief Set the volume for a mix group (0-100, clamped).
void rt_audio_set_group_volume(int64_t group, int64_t volume);

/// @brief Get the volume for a mix group.
/// @return Volume 0-100, or 100 if invalid group.
int64_t rt_audio_get_group_volume(int64_t group);

/// @brief Register a named mix group. Built-ins "music" and "sfx" return 0 and 1.
int64_t rt_audio_register_group(rt_string group_name);

/// @brief Find a registered named mix group, or -1 when missing.
int64_t rt_audio_find_group(rt_string group_name);

/// @brief Find a registered named mix group as an Option id.
/// @details Returns `SomeI64(id)` for built-in or registered groups and `None`
///          when the name is missing, avoiding the legacy `-1` sentinel.
/// @param group_name Group name.
/// @return Opaque Viper.Option containing the group id, or None.
void *rt_audio_find_group_option(rt_string group_name);

/// @brief Set a named group's volume (0-100, clamped). Missing groups are registered.
void rt_audio_set_group_volume_named(rt_string group_name, int64_t volume);

/// @brief Get a named group's volume, or 100 when missing.
int64_t rt_audio_get_group_volume_named(rt_string group_name);

/// @brief Return a group's registered name, or an empty string when invalid.
rt_string rt_audio_group_name(int64_t group_id);

//=========================================================================
// Mix Group Effects
//=========================================================================

/// @brief Add a low-pass biquad to a mix group. Returns an effect id or -1.
int64_t rt_snd_group_add_lowpass(int64_t group, double cutoff_hz, double q);

/// @brief Add a high-pass biquad to a mix group. Returns an effect id or -1.
int64_t rt_snd_group_add_highpass(int64_t group, double cutoff_hz, double q);

/// @brief Add a peaking EQ biquad to a mix group. Returns an effect id or -1.
int64_t rt_snd_group_add_peaking(int64_t group, double freq_hz, double q, double gain_db);

/// @brief Add a delay insert to a mix group. Returns an effect id or -1.
int64_t rt_snd_group_add_delay(int64_t group, double delay_ms, double feedback, double wet);

/// @brief Add a small Freeverb-style reverb insert to a mix group.
int64_t rt_snd_group_add_reverb(int64_t group, double room_size, double damping, double wet);

/// @brief Bypass or enable one effect by id.
void rt_snd_group_fx_bypass(int64_t group, int64_t fx_id, int8_t bypass);

/// @brief Remove one effect from a group.
void rt_snd_group_remove_fx(int64_t group, int64_t fx_id);

/// @brief Remove every effect from a group.
void rt_snd_group_clear_fx(int64_t group);

//=========================================================================
// Music Crossfade
//=========================================================================

/// @brief Crossfade from current music to new music over duration_ms.
/// @param current_music Currently playing music (will fade out). May be NULL.
/// @param new_music Music to fade in. If NULL, just fades out current.
/// @param duration_ms Crossfade duration. ≤ 0 means immediate switch.
void rt_music_crossfade_to(void *current_music, void *new_music, int64_t duration_ms);

/// @brief Check if a crossfade is in progress.
int8_t rt_music_is_crossfading(void);

/// @brief Update crossfade state. Called internally per frame.
void rt_music_crossfade_update(int64_t dt_ms);

//=========================================================================
// Playlist Crossfade
//=========================================================================

/// @brief Set the crossfade duration for playlist auto-advance (0 = disabled).
void rt_playlist_set_crossfade(void *playlist, int64_t duration_ms);

/// @brief Get the crossfade duration.
int64_t rt_playlist_get_crossfade(void *playlist);

//=========================================================================
// Group-Aware Sound Playback
//=========================================================================

/// @brief Play a sound in a specific mix group.
int64_t rt_sound_play_in_group(void *sound, int64_t group);

/// @brief Play a sound with volume/pan in a specific mix group.
int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan, int64_t group);

/// @brief Play a looping sound with volume/pan in a specific mix group.
int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan, int64_t group);

#ifdef __cplusplus
}
#endif
