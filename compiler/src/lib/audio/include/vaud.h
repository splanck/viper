//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Public API
//
// Provides a cross-platform audio library for sound effect playback and music
// streaming. The library implements a simple immediate-mode API with a software
// mixer that combines multiple audio sources into a single output stream.
//
// Key design principles:
// - Zero external dependencies (uses only OS-level audio APIs)
// - Software mixing for predictable, portable audio output
// - Thread-safe playback (audio runs on dedicated thread)
// - Simple resource management (load/play/free)
// - WAV file format support (16-bit PCM)
//
// Supported platforms:
// - macOS (Core Audio / AudioQueue backend)
// - Linux (ALSA backend)
// - Windows (WASAPI backend)
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Cross-platform audio library public API.
/// @details Exposes audio context management, sound effect loading and playback,
///          music streaming, and volume control. All functions are thread-safe
///          and can be called from any thread.

#pragma once

#include "vaud_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //===----------------------------------------------------------------------===//
    // Library Version
    //===----------------------------------------------------------------------===//

#define VAUD_VERSION_MAJOR 1
#define VAUD_VERSION_MINOR 0
#define VAUD_VERSION_PATCH 0

    /// @brief Query the runtime library version as a packed integer.
    /// @details Encodes the version as (major << 16) | (minor << 8) | patch.
    /// @return Packed version number (e.g., 0x010000 for version 1.0.0).
    uint32_t vaud_version(void);

    /// @brief Get the library version as a human-readable string.
    /// @return Version string (e.g., "1.0.0"), never NULL.
    const char *vaud_version_string(void);

    //===----------------------------------------------------------------------===//
    // Core Data Types
    //===----------------------------------------------------------------------===//

    /// @brief Opaque handle to an audio context.
    /// @details Manages the audio output device, mixer, and all loaded resources.
    ///          Created via vaud_create() and destroyed via vaud_destroy().
    ///          A single context is typically sufficient for an application.
    typedef struct vaud_context *vaud_context_t;

    /// @brief Opaque handle to a loaded sound effect.
    /// @details Represents a short audio clip loaded entirely into memory.
    ///          Optimized for low-latency playback of effects like gunshots,
    ///          explosions, or UI sounds. Created via vaud_load_sound().
    typedef struct vaud_sound *vaud_sound_t;

    /// @brief Opaque handle to a music stream.
    /// @details Represents a long audio file streamed from disk. Optimized for
    ///          memory efficiency when playing background music or long audio.
    ///          Created via vaud_load_music().
    typedef struct vaud_music *vaud_music_t;

    /// @brief Voice identifier for active playback instances.
    /// @details Returned by vaud_play() to allow control of individual playing
    ///          sounds. Valid until the sound finishes or is stopped.
    typedef int32_t vaud_voice_id;

    /// @brief Invalid voice ID constant.
#define VAUD_INVALID_VOICE (-1)

    //===----------------------------------------------------------------------===//
    // Error Handling
    //===----------------------------------------------------------------------===//

    /// @brief Error code enumeration.
    typedef enum
    {
        VAUD_OK = 0,           ///< No error
        VAUD_ERR_ALLOC,        ///< Memory allocation failed
        VAUD_ERR_PLATFORM,     ///< Platform-specific audio error
        VAUD_ERR_FILE,         ///< File I/O error
        VAUD_ERR_FORMAT,       ///< Unsupported audio format
        VAUD_ERR_INVALID_PARAM ///< Invalid parameter
    } vaud_error_t;

    /// @brief Retrieve the last error message.
    /// @return Error message string, or NULL if no error.
    const char *vaud_get_last_error(void);

    /// @brief Clear the last error state.
    void vaud_clear_error(void);

    //===----------------------------------------------------------------------===//
    // Context Management
    //===----------------------------------------------------------------------===//

    /// @brief Create a new audio context.
    /// @details Initializes the platform audio backend, starts the audio thread,
    ///          and prepares the software mixer. Only one context is needed per
    ///          application. Returns NULL on failure.
    /// @return Audio context handle on success, NULL on failure.
    vaud_context_t vaud_create(void);

    /// @brief Destroy an audio context and free all resources.
    /// @details Stops all playback, frees all loaded sounds and music, shuts down
    ///          the audio thread, and releases platform resources. Safe to pass NULL.
    /// @param ctx Audio context to destroy (may be NULL).
    void vaud_destroy(vaud_context_t ctx);

    /// @brief Set the master volume for all audio output.
    /// @details Affects all sounds and music proportionally. Does not affect
    ///          individual sound/music volume settings, which are multiplied
    ///          with the master volume.
    /// @param ctx Audio context.
    /// @param volume Master volume (0.0 = silent, 1.0 = full volume).
    void vaud_set_master_volume(vaud_context_t ctx, float volume);

    /// @brief Get the current master volume.
    /// @param ctx Audio context.
    /// @return Current master volume (0.0 to 1.0), or 0.0 if ctx is NULL.
    float vaud_get_master_volume(vaud_context_t ctx);

    /// @brief Pause all audio playback.
    /// @details Suspends the audio thread. All playing sounds and music will
    ///          pause at their current positions. Use vaud_resume_all() to continue.
    /// @param ctx Audio context.
    void vaud_pause_all(vaud_context_t ctx);

    /// @brief Resume all audio playback.
    /// @details Resumes the audio thread after vaud_pause_all(). All sounds and
    ///          music continue from their paused positions.
    /// @param ctx Audio context.
    void vaud_resume_all(vaud_context_t ctx);

    //===----------------------------------------------------------------------===//
    // Sound Effects
    //===----------------------------------------------------------------------===//

    /// @brief Load a sound effect from a WAV file.
    /// @details Reads the entire file into memory and converts to internal format.
    ///          Supports 8-bit and 16-bit PCM WAV files, mono or stereo, any sample
    ///          rate (will be resampled to VAUD_SAMPLE_RATE).
    /// @param ctx Audio context.
    /// @param path Path to the WAV file.
    /// @return Sound handle on success, NULL on failure.
    vaud_sound_t vaud_load_sound(vaud_context_t ctx, const char *path);

    /// @brief Load a sound effect from memory.
    /// @details Parses WAV data from a memory buffer. Useful for embedded resources.
    /// @param ctx Audio context.
    /// @param data Pointer to WAV file data.
    /// @param size Size of the data in bytes.
    /// @return Sound handle on success, NULL on failure.
    vaud_sound_t vaud_load_sound_mem(vaud_context_t ctx, const void *data, size_t size);

    /// @brief Free a loaded sound effect.
    /// @details Releases the memory used by the sound. Any voices currently playing
    ///          this sound will be stopped. Safe to pass NULL.
    /// @param sound Sound to free (may be NULL).
    void vaud_free_sound(vaud_sound_t sound);

    /// @brief Play a sound effect.
    /// @details Starts playback immediately using the next available voice. If all
    ///          voices are in use, the oldest non-looping voice is stolen.
    /// @param sound Sound to play.
    /// @return Voice ID for controlling playback, or VAUD_INVALID_VOICE on failure.
    vaud_voice_id vaud_play(vaud_sound_t sound);

    /// @brief Play a sound effect with volume and pan control.
    /// @details Extended version of vaud_play() with per-instance settings.
    /// @param sound Sound to play.
    /// @param volume Playback volume (0.0 to 1.0).
    /// @param pan Stereo pan (-1.0 = left, 0.0 = center, 1.0 = right).
    /// @return Voice ID for controlling playback, or VAUD_INVALID_VOICE on failure.
    vaud_voice_id vaud_play_ex(vaud_sound_t sound, float volume, float pan);

    /// @brief Play a sound effect with looping.
    /// @details Starts looped playback that continues until explicitly stopped.
    /// @param sound Sound to play.
    /// @param volume Playback volume (0.0 to 1.0).
    /// @param pan Stereo pan (-1.0 = left, 0.0 = center, 1.0 = right).
    /// @return Voice ID for controlling playback, or VAUD_INVALID_VOICE on failure.
    vaud_voice_id vaud_play_loop(vaud_sound_t sound, float volume, float pan);

    /// @brief Stop a playing voice.
    /// @details Immediately stops playback of the specified voice.
    /// @param ctx Audio context.
    /// @param voice Voice ID returned by vaud_play().
    void vaud_stop_voice(vaud_context_t ctx, vaud_voice_id voice);

    /// @brief Set the volume of a playing voice.
    /// @param ctx Audio context.
    /// @param voice Voice ID.
    /// @param volume New volume (0.0 to 1.0).
    void vaud_set_voice_volume(vaud_context_t ctx, vaud_voice_id voice, float volume);

    /// @brief Set the pan of a playing voice.
    /// @param ctx Audio context.
    /// @param voice Voice ID.
    /// @param pan New pan (-1.0 = left, 0.0 = center, 1.0 = right).
    void vaud_set_voice_pan(vaud_context_t ctx, vaud_voice_id voice, float pan);

    /// @brief Check if a voice is still playing.
    /// @param ctx Audio context.
    /// @param voice Voice ID.
    /// @return 1 if playing, 0 if stopped or invalid.
    int vaud_voice_is_playing(vaud_context_t ctx, vaud_voice_id voice);

    //===----------------------------------------------------------------------===//
    // Music Streaming
    //===----------------------------------------------------------------------===//

    /// @brief Load music from a WAV file for streaming playback.
    /// @details Opens the file for streaming. Only a small buffer is loaded into
    ///          memory at a time, making this suitable for long audio files.
    /// @param ctx Audio context.
    /// @param path Path to the WAV file.
    /// @return Music handle on success, NULL on failure.
    vaud_music_t vaud_load_music(vaud_context_t ctx, const char *path);

    /// @brief Free a loaded music stream.
    /// @details Stops playback if playing, closes the file, and frees resources.
    /// @param music Music to free (may be NULL).
    void vaud_free_music(vaud_music_t music);

    /// @brief Start music playback.
    /// @details Begins streaming playback from the beginning of the file.
    /// @param music Music to play.
    /// @param loop Non-zero for looped playback, zero for one-shot.
    void vaud_music_play(vaud_music_t music, int loop);

    /// @brief Stop music playback.
    /// @details Stops playback and resets position to the beginning.
    /// @param music Music to stop.
    void vaud_music_stop(vaud_music_t music);

    /// @brief Pause music playback.
    /// @details Pauses at the current position. Use vaud_music_resume() to continue.
    /// @param music Music to pause.
    void vaud_music_pause(vaud_music_t music);

    /// @brief Resume paused music playback.
    /// @param music Music to resume.
    void vaud_music_resume(vaud_music_t music);

    /// @brief Set music playback volume.
    /// @param music Music handle.
    /// @param volume Volume (0.0 to 1.0).
    void vaud_music_set_volume(vaud_music_t music, float volume);

    /// @brief Get music playback volume.
    /// @param music Music handle.
    /// @return Current volume, or 0.0 if music is NULL.
    float vaud_music_get_volume(vaud_music_t music);

    /// @brief Check if music is currently playing.
    /// @param music Music handle.
    /// @return 1 if playing, 0 if stopped/paused or NULL.
    int vaud_music_is_playing(vaud_music_t music);

    /// @brief Seek to a position in the music.
    /// @details Seeks to the specified time offset. May cause a brief audio gap.
    /// @param music Music handle.
    /// @param seconds Time offset in seconds from the beginning.
    void vaud_music_seek(vaud_music_t music, float seconds);

    /// @brief Get the current playback position.
    /// @param music Music handle.
    /// @return Current position in seconds, or 0.0 if NULL.
    float vaud_music_get_position(vaud_music_t music);

    /// @brief Get the total duration of the music.
    /// @param music Music handle.
    /// @return Duration in seconds, or 0.0 if NULL.
    float vaud_music_get_duration(vaud_music_t music);

    //===----------------------------------------------------------------------===//
    // Utility Functions
    //===----------------------------------------------------------------------===//

    /// @brief Get the number of active voices.
    /// @param ctx Audio context.
    /// @return Number of voices currently playing.
    int32_t vaud_get_active_voice_count(vaud_context_t ctx);

    /// @brief Stop all playing sounds (but not music).
    /// @param ctx Audio context.
    void vaud_stop_all_sounds(vaud_context_t ctx);

    /// @brief Get the audio latency in milliseconds.
    /// @details Returns the approximate time between a play call and audible output.
    /// @param ctx Audio context.
    /// @return Latency in milliseconds.
    float vaud_get_latency_ms(vaud_context_t ctx);

#ifdef __cplusplus
}
#endif
