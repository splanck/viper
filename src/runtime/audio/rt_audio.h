//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio.h
// Purpose: Runtime bridge for the ViperAUD audio library, exposing sound effect and music playback
// controls (play, pause, stop, volume, loop) over opaque audio handles.
//
// Key invariants:
//   - All sound and music pointers are opaque handles returned by rt_sound_new/rt_music_new.
//   - Volume is in the range [0, 100]; values are clamped to this range.
//   - Looping and channel multiplexing are managed by the underlying ViperAUD backend.
//
// Ownership/Lifetime:
//   - Sounds must be freed via rt_sound_free; music via rt_music_free.
//   - Caller owns all handles returned by factory functions.
//
// Links: src/lib/audio/include/vaud.h (underlying audio backend), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Audio System Management
    //=========================================================================

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

    /// @brief Stop all playing sounds (but not music).
    void rt_audio_stop_all_sounds(void);

    //=========================================================================
    // Sound Effects
    //=========================================================================

    /// @brief Load a sound effect from a WAV file.
    /// @param path Path to the WAV file (runtime string).
    /// @return Opaque sound handle, or NULL on failure.
    void *rt_sound_load(rt_string path);

    /// @brief Free a loaded sound effect.
    /// @param sound Sound handle from rt_sound_load.
    void rt_sound_free(void *sound);

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

    //=========================================================================
    // Music Streaming
    //=========================================================================

    /// @brief Load music from a WAV file for streaming playback.
    /// @param path Path to the WAV file (runtime string).
    /// @return Opaque music handle, or NULL on failure.
    void *rt_music_load(rt_string path);

    /// @brief Free a loaded music stream.
    /// @param music Music handle from rt_music_load.
    void rt_music_free(void *music);

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
    void rt_music_seek(void *music, int64_t position_ms);

    /// @brief Get the current playback position.
    /// @param music Music handle.
    /// @return Current position in milliseconds, or 0 if NULL.
    int64_t rt_music_get_position(void *music);

    /// @brief Get the total duration of the music.
    /// @param music Music handle.
    /// @return Duration in milliseconds, or 0 if NULL.
    int64_t rt_music_get_duration(void *music);

#ifdef __cplusplus
}
#endif
