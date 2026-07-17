//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/audio/rt_audio.h
// Purpose: Runtime bridge for the ZannaAUD audio library, exposing sound effect and music playback
// controls (play, pause, stop, volume, loop) over opaque audio handles.
//
// Key invariants:
//   - All sound and music pointers are opaque handles returned by rt_sound_new/rt_music_new.
//   - Volume is in the range [0, 100]; values are clamped to this range.
//   - Looping and channel multiplexing are managed by the underlying ZannaAUD backend.
//
// Ownership/Lifetime:
//   - Sounds must be freed via rt_sound_destroy; music via rt_music_destroy.
//   - Caller owns all handles returned by factory functions.
//
// Links: src/lib/audio/include/vaud.h (underlying audio backend), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Audio System Management
//=========================================================================

/// @brief Report whether audio support is compiled into this runtime.
/// @return 1 when the audio backend is available, 0 otherwise.
int8_t rt_audio_is_available(void);

/// @brief Initialize the audio system.
/// @details Creates the global audio context. Called automatically on first use.
/// @return 1 on success, 0 on failure.
int64_t rt_audio_init(void);

/// @brief Shutdown the audio system.
/// @details Stops all playback and releases all resources. Called at program exit.
void rt_audio_shutdown(void);

/// @brief Set the master volume for all audio output.
/// @param volume Master volume (0-100).
void rt_audio_set_master_volume(int64_t volume);

/// @brief Get the current master volume.
/// @return Master volume (0-100).
int64_t rt_audio_get_master_volume(void);

/// @brief Pause all audio playback.
void rt_audio_pause_all(void);

/// @brief Resume all audio playback.
void rt_audio_resume_all(void);

/// @brief Advance time-based audio state such as music crossfades.
/// @details Call once per frame when using crossfades outside Playlist.Update().
void rt_audio_update(void);

/// @brief Stop all playing sounds (but not music).
void rt_audio_stop_all_sounds(void);

/// @brief Mixer render callbacks attempted by the active audio context.
int64_t rt_audio_get_render_calls(void);
/// @brief Mixer callbacks that emitted silence because the state lock was busy.
int64_t rt_audio_get_mixer_lock_misses(void);
/// @brief Platform backend write calls issued by the active audio context.
int64_t rt_audio_get_backend_write_calls(void);
/// @brief Platform backend writes that accepted fewer frames than requested.
int64_t rt_audio_get_backend_partial_writes(void);
/// @brief Platform backend waits used to avoid busy retry loops.
int64_t rt_audio_get_backend_waits(void);
/// @brief Platform backend underrun or suspend recoveries observed.
int64_t rt_audio_get_backend_xruns(void);
/// @brief Platform backend recovery attempts.
int64_t rt_audio_get_backend_recoveries(void);
/// @brief Platform backend writes that failed after recovery attempts.
int64_t rt_audio_get_backend_write_failures(void);

//=========================================================================
// Sound Effects
//=========================================================================

/// @brief Load a sound effect from a WAV, OGG, or MP3 file.
/// @param path Path to the WAV file (runtime string).
/// @return Opaque sound handle, or NULL on failure.
void *rt_sound_load(rt_string path);

/// @brief Load a sound effect through the runtime asset manager.
/// @param name Mounted/embedded asset name, asset:// URI, or dev filesystem path.
/// @return Opaque sound handle, or NULL on failure.
void *rt_sound_load_asset(rt_string name);

/// @brief Load a sound effect from in-memory WAV, OGG, or MP3 data.
/// @param data Pointer to file data in memory.
/// @param size Size of the data in bytes.
/// @return Opaque sound handle, or NULL on failure.
void *rt_sound_load_mem(const void *data, int64_t size);

/// @brief Free a loaded sound effect.
/// @param sound Sound handle from rt_sound_load.
void rt_sound_destroy(void *sound);

/// @brief Play a sound effect with default settings.
/// @param sound Sound handle.
/// @return Voice ID for controlling playback, or -1 on failure.
int64_t rt_sound_play(void *sound);

/// @brief Play a sound effect with volume and pan control.
/// @param sound Sound handle.
/// @param volume Playback volume (0-100).
/// @param pan Stereo pan (-100 = left, 0 = center, 100 = right).
/// @return Voice ID for controlling playback, or -1 on failure.
int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan);

/// @brief Play a sound effect with looping.
/// @param sound Sound handle.
/// @param volume Playback volume (0-100).
/// @param pan Stereo pan (-100 = left, 0 = center, 100 = right).
/// @return Voice ID for controlling playback, or -1 on failure.
int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan);

/// @brief Stop a playing voice.
/// @param voice_id Voice ID returned by rt_sound_play*.
void rt_voice_stop(int64_t voice_id);

/// @brief Set the volume of a playing voice.
/// @param voice_id Voice ID.
/// @param volume New volume (0-100).
void rt_voice_set_volume(int64_t voice_id, int64_t volume);

/// @brief Set the pan of a playing voice.
/// @param voice_id Voice ID.
/// @param pan New pan (-100 = left, 0 = center, 100 = right).
void rt_voice_set_pan(int64_t voice_id, int64_t pan);

/// @brief Check if a voice is still playing.
/// @param voice_id Voice ID.
/// @return 1 if playing, 0 if stopped or invalid.
int64_t rt_voice_is_playing(int64_t voice_id);

/// @brief Play a sound with volume (0-100), pan (-100..100), and pitch.
/// @param pitch Playback-rate multiplier (clamped 0.25–4.0; 1.0 = native).
/// @return Voice ID for controlling playback, or -1 on failure.
int64_t rt_sound_play_ex2(void *sound, int64_t volume, int64_t pan, double pitch);

/// @brief Set a voice's playback-rate (pitch) multiplier (0.25–4.0).
void rt_voice_set_pitch(int64_t voice_id, double pitch);

/// @brief Get a voice's playback-rate (pitch) multiplier (1.0 default).
double rt_voice_get_pitch(int64_t voice_id);

/// @brief Set a direct per-voice lowpass cutoff in Hz (<= 0 disables).
void rt_voice_set_lowpass(int64_t voice_id, double cutoff_hz);

/// @brief Set a voice's occlusion amount (0 open .. 1 occluded, smoothed).
void rt_voice_set_occlusion(int64_t voice_id, double amount);
/// @brief Enable/disable per-voice RMS metering (lip-sync level tap).
void rt_voice_enable_metering(int64_t voice_id, int8_t enabled);
/// @brief RMS source level (pre-gain) of the last mixed block; 0 when unmetered.
double rt_voice_get_level(int64_t voice_id);

/// @brief Register/replace/remove a sidechain ducking rule between mix groups.
/// @details amount <= 0 removes the (trigger, target) rule.
void rt_audio_set_group_ducking(rt_string trigger_group,
                                rt_string target_group,
                                double amount,
                                double attack_sec,
                                double release_sec);

/// @brief Return non-zero when @p sound is a live runtime Sound wrapper.
/// @details This is intentionally a runtime-handle check, not a playback-capability
///          check. A detached Sound after Audio.Shutdown() is still a Sound object,
///          but playback will fail until a new handle is loaded.
int64_t rt_sound_is_handle(void *sound);

/// @brief True when the wrapper's backend Sound is attached to the live audio
///        context (stricter than rt_sound_is_handle; false after Shutdown).
int64_t rt_sound_is_playable(void *sound);

//=========================================================================
// Music Streaming
//=========================================================================

/// @brief Load music from a WAV, OGG, or MP3 file for playback.
/// @param path Path to the audio file (runtime string).
/// @return Opaque music handle, or NULL on failure.
void *rt_music_load(rt_string path);

/// @brief Free a loaded music stream.
/// @param music Music handle from rt_music_load.
void rt_music_destroy(void *music);

/// @brief Start music playback.
/// @param music Music handle.
/// @param loop Non-zero for looped playback, zero for one-shot.
void rt_music_play(void *music, int64_t loop);

/// @brief Stop music playback.
/// @param music Music handle.
void rt_music_stop(void *music);

/// @brief Pause music playback.
/// @param music Music handle.
void rt_music_pause(void *music);

/// @brief Resume paused music playback.
/// @param music Music handle.
void rt_music_resume(void *music);

/// @brief Update the loop flag for a music stream without changing play state.
/// @param music Music handle.
/// @param loop Non-zero for looped playback, zero for one-shot.
void rt_music_set_loop(void *music, int64_t loop);

/// @brief Set music playback volume.
/// @param music Music handle.
/// @param volume Volume (0-100).
void rt_music_set_volume(void *music, int64_t volume);

/// @brief Get music playback volume.
/// @param music Music handle.
/// @return Current volume (0-100), or 0 if music is NULL.
int64_t rt_music_get_volume(void *music);

/// @brief Check if music is currently playing.
/// @param music Music handle.
/// @return 1 if playing, 0 if stopped/paused or NULL.
int64_t rt_music_is_playing(void *music);

/// @brief Seek to a position in the music.
/// @param music Music handle.
/// @param position_ms Time offset in milliseconds from the beginning.
/// @details Repositions only this stream; does not stop unrelated music.
void rt_music_seek(void *music, int64_t position_ms);

/// @brief Get the current playback position.
/// @param music Music handle.
/// @return Current position in milliseconds, or 0 if NULL.
int64_t rt_music_get_position(void *music);

/// @brief Get the total duration of the music.
/// @param music Music handle.
/// @return Duration in milliseconds, or 0 if NULL.
int64_t rt_music_get_duration(void *music);

/// @brief Return non-zero when @p music is a live runtime Music wrapper.
int64_t rt_music_is_handle(void *music);

/// @brief Pause a music stream and any active crossfade companion tied to it.
/// @details Pauses the companion streams and freezes the shared crossfade clock.
/// @param music Music handle.
void rt_music_pause_related(void *music);

/// @brief Resume a music stream and any active crossfade companion tied to it.
/// @details Restores playback and foreground ownership for the related stream(s).
/// @param music Music handle.
void rt_music_resume_related(void *music);

/// @brief Stop a music stream and any active crossfade companion tied to it.
/// @param music Music handle.
void rt_music_stop_related(void *music);

/// @brief Apply one logical volume to both sides of an active crossfade containing @p music.
/// @param music Music handle.
/// @param volume Volume (0-100).
void rt_music_set_crossfade_pair_volume(void *music, int64_t volume);

#ifdef __cplusplus
}
#endif
