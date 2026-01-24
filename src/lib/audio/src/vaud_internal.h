//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Internal Definitions
//
// Internal structures and functions shared between the core library and
// platform backends. Not part of the public API.
//
// Key structures:
// - vaud_context: Main audio context with mixer, voice pool, platform data
// - vaud_sound: Loaded PCM audio data for sound effects
// - vaud_music: Streaming music state with file handle and buffers
// - vaud_voice: Individual playback instance state
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief ViperAUD internal definitions (not public API).

#pragma once

#include "vaud.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //===----------------------------------------------------------------------===//
    // Platform Threading Abstraction
    //===----------------------------------------------------------------------===//

#if defined(VAUD_PLATFORM_WINDOWS)
#include <windows.h>
    typedef CRITICAL_SECTION vaud_mutex_t;
    typedef HANDLE vaud_thread_t;
    typedef HANDLE vaud_event_t;
#else
#include <pthread.h>
typedef pthread_mutex_t vaud_mutex_t;
typedef pthread_t vaud_thread_t;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
} vaud_event_t;
#endif

    //===----------------------------------------------------------------------===//
    // Voice State
    //===----------------------------------------------------------------------===//

    /// @brief State of a playback voice.
    typedef enum
    {
        VAUD_VOICE_INACTIVE = 0, ///< Voice is available
        VAUD_VOICE_PLAYING,      ///< Voice is actively playing
        VAUD_VOICE_STOPPING      ///< Voice is fading out (future use)
    } vaud_voice_state;

    /// @brief Individual sound playback instance.
    /// @details Tracks position, volume, and pan for one playing sound.
    /// @invariant If state != INACTIVE, sound must be valid.
    typedef struct
    {
        vaud_voice_state state; ///< Current voice state
        vaud_sound_t sound;     ///< Source sound data (NULL if inactive)
        int64_t position;       ///< Current sample position (in frames)
        float volume;           ///< Voice volume (0.0 to 1.0)
        float pan;              ///< Stereo pan (-1.0 to 1.0)
        int loop;               ///< Loop flag (0 = one-shot, 1 = loop)
        int32_t id;             ///< Unique voice ID for external reference
        int64_t start_time;     ///< Frame count when voice started (for age-based stealing)
    } vaud_voice;

    //===----------------------------------------------------------------------===//
    // Sound Structure
    //===----------------------------------------------------------------------===//

    /// @brief Loaded sound effect data.
    /// @details Contains PCM audio data in the internal format (16-bit stereo, 44.1kHz).
    /// @invariant samples != NULL after successful load.
    /// @invariant frame_count > 0 after successful load.
    struct vaud_sound
    {
        vaud_context_t ctx;   ///< Owning context
        int16_t *samples;     ///< Interleaved stereo PCM data
        int64_t frame_count;  ///< Number of frames (samples / channels)
        int32_t sample_rate;  ///< Original sample rate (for reference)
        int32_t channels;     ///< Original channel count (for reference)
        float default_volume; ///< Default playback volume
    };

    //===----------------------------------------------------------------------===//
    // Music Structure
    //===----------------------------------------------------------------------===//

    /// @brief Music stream state.
    typedef enum
    {
        VAUD_MUSIC_STOPPED = 0, ///< Not playing
        VAUD_MUSIC_PLAYING,     ///< Actively playing
        VAUD_MUSIC_PAUSED       ///< Paused at current position
    } vaud_music_state;

    /// @brief Streaming music instance.
    /// @details Manages file I/O, buffering, and playback state for streamed audio.
    struct vaud_music
    {
        vaud_context_t ctx;      ///< Owning context
        void *file;              ///< FILE pointer for streaming
        int64_t data_offset;     ///< Offset to PCM data in file
        int64_t data_size;       ///< Total PCM data size in bytes
        int64_t frame_count;     ///< Total frames in file
        int32_t sample_rate;     ///< File sample rate
        int32_t channels;        ///< File channel count
        int32_t bits_per_sample; ///< Bits per sample in file

        vaud_music_state state; ///< Current playback state
        int64_t position;       ///< Current frame position
        int loop;               ///< Loop flag
        float volume;           ///< Playback volume

        // Streaming buffers
        int16_t *buffers[VAUD_MUSIC_BUFFER_COUNT];      ///< Decoded audio buffers
        int32_t buffer_frames[VAUD_MUSIC_BUFFER_COUNT]; ///< Frames in each buffer
        int32_t current_buffer;                         ///< Index of buffer being played
        int32_t buffer_position;                        ///< Frame position within current buffer
    };

    //===----------------------------------------------------------------------===//
    // Context Structure
    //===----------------------------------------------------------------------===//

    /// @brief Main audio context.
    /// @details Contains all audio state: mixer, voices, loaded resources, platform data.
    /// @invariant voices array is always valid.
    /// @invariant platform_data is valid after successful vaud_create().
    struct vaud_context
    {
        // Mixer state
        float master_volume;                ///< Master volume (0.0 to 1.0)
        vaud_voice voices[VAUD_MAX_VOICES]; ///< Voice pool
        int32_t next_voice_id;              ///< Counter for unique voice IDs
        int64_t frame_counter;              ///< Total frames rendered (for timing)

        // Music (single active music stream for simplicity)
        vaud_music_t active_music[VAUD_MAX_MUSIC]; ///< Active music streams
        int32_t music_count;                       ///< Number of active music streams

        // Thread synchronization
        vaud_mutex_t mutex; ///< Protects voice and music state
        int running;        ///< Audio thread running flag
        int paused;         ///< Global pause flag

        // Platform-specific data
        void *platform_data; ///< Platform backend state (AudioQueue, ALSA, WASAPI)
    };

    //===----------------------------------------------------------------------===//
    // Mixer Functions
    //===----------------------------------------------------------------------===//

    /// @brief Render mixed audio into output buffer.
    /// @details Called by platform backend to fill audio buffers. Mixes all active
    ///          voices and music into the output buffer. Thread-safe.
    /// @param ctx Audio context.
    /// @param output Output buffer (interleaved stereo 16-bit PCM).
    /// @param frames Number of frames to render.
    void vaud_mixer_render(vaud_context_t ctx, int16_t *output, int32_t frames);

    /// @brief Allocate a voice for playback.
    /// @details Finds an inactive voice or steals the oldest if none available.
    /// @param ctx Audio context.
    /// @return Pointer to allocated voice, or NULL if context is invalid.
    vaud_voice *vaud_alloc_voice(vaud_context_t ctx);

    /// @brief Find a voice by ID.
    /// @param ctx Audio context.
    /// @param id Voice ID to find.
    /// @return Pointer to voice, or NULL if not found.
    vaud_voice *vaud_find_voice(vaud_context_t ctx, vaud_voice_id id);

    //===----------------------------------------------------------------------===//
    // WAV Parser Functions
    //===----------------------------------------------------------------------===//

    /// @brief Parse WAV file from disk.
    /// @param path File path.
    /// @param out_samples Output: allocated sample buffer (caller must free).
    /// @param out_frames Output: number of frames.
    /// @param out_sample_rate Output: sample rate.
    /// @param out_channels Output: channel count.
    /// @return 1 on success, 0 on failure.
    int vaud_wav_load_file(const char *path,
                           int16_t **out_samples,
                           int64_t *out_frames,
                           int32_t *out_sample_rate,
                           int32_t *out_channels);

    /// @brief Parse WAV file from memory.
    /// @param data Pointer to WAV data.
    /// @param size Size of data in bytes.
    /// @param out_samples Output: allocated sample buffer (caller must free).
    /// @param out_frames Output: number of frames.
    /// @param out_sample_rate Output: sample rate.
    /// @param out_channels Output: channel count.
    /// @return 1 on success, 0 on failure.
    int vaud_wav_load_mem(const void *data,
                          size_t size,
                          int16_t **out_samples,
                          int64_t *out_frames,
                          int32_t *out_sample_rate,
                          int32_t *out_channels);

    /// @brief Open WAV file for streaming (music).
    /// @param path File path.
    /// @param out_file Output: file handle.
    /// @param out_data_offset Output: byte offset to PCM data.
    /// @param out_data_size Output: size of PCM data in bytes.
    /// @param out_frames Output: total frame count.
    /// @param out_sample_rate Output: sample rate.
    /// @param out_channels Output: channel count.
    /// @param out_bits Output: bits per sample.
    /// @return 1 on success, 0 on failure.
    int vaud_wav_open_stream(const char *path,
                             void **out_file,
                             int64_t *out_data_offset,
                             int64_t *out_data_size,
                             int64_t *out_frames,
                             int32_t *out_sample_rate,
                             int32_t *out_channels,
                             int32_t *out_bits);

    /// @brief Read frames from a streaming WAV file.
    /// @param file File handle from vaud_wav_open_stream.
    /// @param samples Output buffer (must hold frames * channels samples).
    /// @param frames Number of frames to read.
    /// @param channels Channel count.
    /// @param bits_per_sample Bits per sample.
    /// @return Number of frames actually read.
    int32_t vaud_wav_read_frames(
        void *file, int16_t *samples, int32_t frames, int32_t channels, int32_t bits_per_sample);

    //===----------------------------------------------------------------------===//
    // Resampling Functions
    //===----------------------------------------------------------------------===//

    /// @brief Resample audio to target sample rate.
    /// @details Uses linear interpolation for simplicity and low CPU usage.
    /// @param input Input samples (interleaved stereo).
    /// @param in_frames Number of input frames.
    /// @param in_rate Input sample rate.
    /// @param output Output buffer (must be pre-allocated).
    /// @param out_frames Expected output frame count.
    /// @param out_rate Output sample rate (VAUD_SAMPLE_RATE).
    /// @param channels Number of channels.
    void vaud_resample(const int16_t *input,
                       int64_t in_frames,
                       int32_t in_rate,
                       int16_t *output,
                       int64_t out_frames,
                       int32_t out_rate,
                       int32_t channels);

    /// @brief Calculate output frame count after resampling.
    /// @param in_frames Input frame count.
    /// @param in_rate Input sample rate.
    /// @param out_rate Output sample rate.
    /// @return Required output frame count.
    int64_t vaud_resample_output_frames(int64_t in_frames, int32_t in_rate, int32_t out_rate);

    //===----------------------------------------------------------------------===//
    // Platform Backend Interface
    //===----------------------------------------------------------------------===//

    /// @brief Initialize platform audio backend.
    /// @details Allocates platform_data, opens audio device, starts audio thread.
    /// @param ctx Audio context.
    /// @return 1 on success, 0 on failure.
    int vaud_platform_init(vaud_context_t ctx);

    /// @brief Shutdown platform audio backend.
    /// @details Stops audio thread, closes audio device, frees platform_data.
    /// @param ctx Audio context.
    void vaud_platform_shutdown(vaud_context_t ctx);

    /// @brief Pause platform audio output.
    /// @param ctx Audio context.
    void vaud_platform_pause(vaud_context_t ctx);

    /// @brief Resume platform audio output.
    /// @param ctx Audio context.
    void vaud_platform_resume(vaud_context_t ctx);

    /// @brief Get current time in milliseconds.
    /// @return Monotonic time in milliseconds.
    int64_t vaud_platform_now_ms(void);

    //===----------------------------------------------------------------------===//
    // Thread Utilities
    //===----------------------------------------------------------------------===//

    /// @brief Initialize a mutex.
    void vaud_mutex_init(vaud_mutex_t *mutex);

    /// @brief Destroy a mutex.
    void vaud_mutex_destroy(vaud_mutex_t *mutex);

    /// @brief Lock a mutex.
    void vaud_mutex_lock(vaud_mutex_t *mutex);

    /// @brief Unlock a mutex.
    void vaud_mutex_unlock(vaud_mutex_t *mutex);

    //===----------------------------------------------------------------------===//
    // Error Handling
    //===----------------------------------------------------------------------===//

    /// @brief Set the thread-local error state.
    /// @param code Error code.
    /// @param msg Error message.
    void vaud_set_error(vaud_error_t code, const char *msg);

#ifdef __cplusplus
}
#endif
