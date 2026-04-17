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

//=========================================================================
// Mix Group Volume
//=========================================================================

/// @brief Set the volume for a mix group (0-100, clamped).
void rt_audio_set_group_volume(int64_t group, int64_t volume);

/// @brief Get the volume for a mix group.
/// @return Volume 0-100, or 100 if invalid group.
int64_t rt_audio_get_group_volume(int64_t group);

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
