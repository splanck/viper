//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio.c
// Purpose: Implements the runtime bridge between the Viper audio API and the
//          ViperAUD (vaud) library. Provides Init/Shutdown, LoadSound,
//          LoadMusic, Play/Stop/Pause/Resume for sounds and music, volume
//          control, and IsPlaying queries. When audio is disabled at compile
//          time, all functions are no-ops that return safe defaults.
//
// Key invariants:
//   - All functions guard against NULL handles and return silently if passed one.
//   - The audio context is a module-level global; Init must be called once
//     before any other audio function.
//   - Shutdown releases all loaded sounds/music and destroys the context.
//   - Sounds use ref-counting; the caller owns the reference from LoadSound.
//   - Music is loaded as a single stream; only one music track plays at a time.
//   - The VIPER_ENABLE_AUDIO compile flag controls whether real or stub impls
//     are compiled; stubs are always safe no-ops.
//
// Ownership/Lifetime:
//   - Sound objects are ref-counted via the runtime heap; callers must release.
//   - The global audio context is owned by this module and freed on Shutdown.
//
// Links: src/runtime/audio/rt_audio.h (public API),
//        src/lib/audio/include/vaud.h (ViperAUD low-level audio library)
//
//===----------------------------------------------------------------------===//

#include "rt_audio.h"
#include "rt_mixgroup.h"
#include "rt_mp3.h"
#include "rt_object.h"
#include "rt_ogg.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_vorbis.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Mix Group State (shared between real and stub implementations)
//===----------------------------------------------------------------------===//

/// @brief Per-group volume (0-100). Defaults to 100.
static int64_t g_group_volume[RT_MIXGROUP_COUNT] = {100, 100};

/// @brief Crossfade state.
static struct {
    void *fade_out;   ///< Music being faded out (NULL when not crossfading).
    void *fade_in;    ///< Music being faded in (NULL when not crossfading).
    int64_t elapsed;  ///< Milliseconds elapsed in crossfade.
    int64_t duration; ///< Total crossfade duration in ms.
    int64_t vol_out;  ///< Starting volume of fade-out track.
    int8_t active;    ///< 1 if crossfade in progress.
} g_crossfade = {NULL, NULL, 0, 0, 100, 0};

#ifdef VIPER_ENABLE_AUDIO

#include "vaud.h"

//===----------------------------------------------------------------------===//
// Global Audio Context
//===----------------------------------------------------------------------===//

/// @brief Global audio context (singleton).
static vaud_context_t g_audio_ctx = NULL;

/// @brief Initialization state: 0 = not init, 1 = initialized, -1 = init failed.
static volatile int g_audio_initialized = 0;

/// @brief Spinlock for thread-safe initialization (RACE-009 fix).
/// CONC-008: kept as spinlock (pthread_once doesn't support retry-on-failure);
/// yield hint added to reduce CPU waste under contention.
static volatile int g_audio_init_lock = 0;

#if !defined(_WIN32)
#include <sched.h>
#endif

//===----------------------------------------------------------------------===//
// Sound Wrapper Structure
//===----------------------------------------------------------------------===//

/// @brief Internal sound wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vaud sound handle.
typedef struct {
    void *vptr;         ///< VTable pointer (reserved for future use)
    vaud_sound_t sound; ///< ViperAUD sound handle
} rt_sound;

/// @brief Finalizer for sound objects.
static void rt_sound_finalize(void *obj) {
    if (!obj)
        return;

    rt_sound *snd = (rt_sound *)obj;
    if (snd->sound) {
        vaud_free_sound(snd->sound);
        snd->sound = NULL;
    }
}

//===----------------------------------------------------------------------===//
// Music Wrapper Structure
//===----------------------------------------------------------------------===//

/// @brief Internal music wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vaud music handle.
typedef struct {
    void *vptr;         ///< VTable pointer (reserved for future use)
    vaud_music_t music; ///< ViperAUD music handle
} rt_music;

/// @brief Finalizer for music objects.
static void rt_music_finalize(void *obj) {
    if (!obj)
        return;

    rt_music *mus = (rt_music *)obj;
    if (mus->music) {
        vaud_free_music(mus->music);
        mus->music = NULL;
    }
}

//===----------------------------------------------------------------------===//
// Audio System Management
//===----------------------------------------------------------------------===//

/// @brief Ensure the audio system is initialized.
/// @details Uses double-checked locking with a spinlock to ensure thread-safe
///          initialization. Only one thread will perform the actual initialization;
///          other threads will wait and then use the result (RACE-009 fix).
/// @return 1 if initialized, 0 on failure.
static int ensure_audio_init(void) {
    // Fast path: already initialized (use acquire to sync with the release below)
#if RT_COMPILER_MSVC
    int state = rt_atomic_load_i32(&g_audio_initialized, __ATOMIC_ACQUIRE);
#else
    int state = __atomic_load_n(&g_audio_initialized, __ATOMIC_ACQUIRE);
#endif
    if (state != 0)
        return state > 0;

    // Slow path: acquire spinlock and double-check
#if RT_COMPILER_MSVC
    while (_InterlockedExchange8((volatile char *)&g_audio_init_lock, 1))
#else
    while (__atomic_test_and_set(&g_audio_init_lock, __ATOMIC_ACQUIRE))
#endif
    {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
    }

    // Double-check under lock
#if RT_COMPILER_MSVC
    state = rt_atomic_load_i32(&g_audio_initialized, __ATOMIC_RELAXED);
#else
    state = __atomic_load_n(&g_audio_initialized, __ATOMIC_RELAXED);
#endif
    if (state != 0) {
        // Another thread already initialized - release lock and return
#if RT_COMPILER_MSVC
        _InterlockedExchange8((volatile char *)&g_audio_init_lock, 0);
#else
        __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
#endif
        return state > 0;
    }

    // We are the initializing thread
    g_audio_ctx = vaud_create();

    // Set initialization state (use release to ensure context is visible)
#if RT_COMPILER_MSVC
    rt_atomic_store_i32(&g_audio_initialized, g_audio_ctx ? 1 : -1, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&g_audio_initialized, g_audio_ctx ? 1 : -1, __ATOMIC_RELEASE);
#endif

#if RT_COMPILER_MSVC
    _InterlockedExchange8((volatile char *)&g_audio_init_lock, 0);
#else
    __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
#endif
    return g_audio_ctx != NULL;
}

/// @brief Explicitly initialize the audio system. Returns 1 on success, 0 on failure.
/// @details Normally called lazily on first sound/music operation. This function
///          allows eager initialization to detect audio hardware issues early.
int64_t rt_audio_init(void) {
    return ensure_audio_init() ? 1 : 0;
}

/// @brief Shut down the audio system, releasing all device resources.
/// @details Thread-safe via spinlock. Resets the initialization state so the
///          system can be re-initialized later if needed.
void rt_audio_shutdown(void) {
    // Acquire lock to ensure exclusive access during shutdown
#if RT_COMPILER_MSVC
    while (_InterlockedExchange8((volatile char *)&g_audio_init_lock, 1))
#else
    while (__atomic_test_and_set(&g_audio_init_lock, __ATOMIC_ACQUIRE))
#endif
    {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
    }

    if (g_audio_ctx) {
        vaud_destroy(g_audio_ctx);
        g_audio_ctx = NULL;
    }

    // Reset state to allow re-initialization
#if RT_COMPILER_MSVC
    rt_atomic_store_i32(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#endif

#if RT_COMPILER_MSVC
    _InterlockedExchange8((volatile char *)&g_audio_init_lock, 0);
#else
    __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
#endif
}

/// @brief Set the global master volume (0 = mute, 100 = full volume).
void rt_audio_set_master_volume(int64_t volume) {
    if (!ensure_audio_init())
        return;

    /* Clamp to 0-100 range and convert to 0.0-1.0 */
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;

    vaud_set_master_volume(g_audio_ctx, (float)volume / 100.0f);
}

/// @brief Get the current master volume as an integer (0–100).
int64_t rt_audio_get_master_volume(void) {
    if (!g_audio_ctx)
        return 0;

    float vol = vaud_get_master_volume(g_audio_ctx);
    return (int64_t)(vol * 100.0f + 0.5f);
}

/// @brief Pause all currently playing sounds and music.
void rt_audio_pause_all(void) {
    if (g_audio_ctx)
        vaud_pause_all(g_audio_ctx);
}

/// @brief Resume all paused sounds and music.
void rt_audio_resume_all(void) {
    if (g_audio_ctx)
        vaud_resume_all(g_audio_ctx);
}

/// @brief Stop all currently playing sound effects (music is unaffected).
void rt_audio_stop_all_sounds(void) {
    if (g_audio_ctx)
        vaud_stop_all_sounds(g_audio_ctx);
}

//===----------------------------------------------------------------------===//
// Sound Effects
//===----------------------------------------------------------------------===//

/// @brief Detect audio file format from magic bytes.
/// @return 1=WAV/RIFF, 2=OGG, 0=unknown
static int detect_audio_format(const char *filepath) {
    FILE *af = fopen(filepath, "rb");
    if (!af)
        return 0;
    uint8_t hdr[4];
    size_t n = fread(hdr, 1, 4, af);
    fclose(af);
    if (n < 4)
        return 0;
    if (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F')
        return 1; // WAV
    if (hdr[0] == 'O' && hdr[1] == 'g' && hdr[2] == 'g' && hdr[3] == 'S')
        return 2; // OGG
    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
        return 3; // MP3 with ID3v2 tag
    if (hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0)
        return 3; // MP3 frame sync
    return 0;
}

/// @brief Decode an entire OGG Vorbis file to a WAV-format memory buffer.
/// @details Creates a valid RIFF/WAV header followed by raw PCM data.
/// @param filepath Path to the .ogg file.
/// @param out_data Receives malloc'd WAV data (caller frees).
/// @param out_len Receives total WAV data length.
/// @return 0 on success, -1 on failure.
static int ogg_decode_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len) {
    ogg_reader_t *reader = ogg_reader_open_file(filepath);
    if (!reader)
        return -1;

    vorbis_decoder_t *dec = vorbis_decoder_new();
    if (!dec) {
        ogg_reader_free(reader);
        return -1;
    }

    // Read and decode the 3 header packets
    for (int i = 0; i < 3; i++) {
        const uint8_t *pkt_data;
        size_t pkt_len;
        if (!ogg_reader_next_packet(reader, &pkt_data, &pkt_len)) {
            vorbis_decoder_free(dec);
            ogg_reader_free(reader);
            return -1;
        }
        if (vorbis_decode_header(dec, pkt_data, pkt_len, i) != 0) {
            vorbis_decoder_free(dec);
            ogg_reader_free(reader);
            return -1;
        }
    }

    int channels = vorbis_get_channels(dec);
    int sample_rate = vorbis_get_sample_rate(dec);
    if (channels <= 0 || sample_rate <= 0) {
        vorbis_decoder_free(dec);
        ogg_reader_free(reader);
        return -1;
    }

    // Decode all audio packets into a growing PCM buffer
    int16_t *pcm_buf = NULL;
    size_t pcm_frames = 0;
    size_t pcm_cap = 0;

    const uint8_t *pkt_data;
    size_t pkt_len;
    while (ogg_reader_next_packet(reader, &pkt_data, &pkt_len)) {
        int16_t *frame_pcm = NULL;
        int frame_samples = 0;
        if (vorbis_decode_packet(dec, pkt_data, pkt_len, &frame_pcm, &frame_samples) != 0)
            break;
        if (frame_samples > 0 && frame_pcm) {
            size_t needed = pcm_frames + (size_t)frame_samples;
            if (needed > pcm_cap) {
                size_t new_cap = pcm_cap ? pcm_cap * 2 : 65536;
                if (new_cap < needed)
                    new_cap = needed;
                int16_t *new_buf =
                    (int16_t *)realloc(pcm_buf, new_cap * (size_t)channels * sizeof(int16_t));
                if (!new_buf) {
                    free(pcm_buf);
                    vorbis_decoder_free(dec);
                    ogg_reader_free(reader);
                    return -1;
                }
                pcm_buf = new_buf;
                pcm_cap = new_cap;
            }
            memcpy(pcm_buf + pcm_frames * channels, frame_pcm,
                   (size_t)frame_samples * (size_t)channels * sizeof(int16_t));
            pcm_frames += (size_t)frame_samples;
        }
    }

    vorbis_decoder_free(dec);
    ogg_reader_free(reader);

    if (pcm_frames == 0) {
        free(pcm_buf);
        return -1;
    }

    // Build a WAV file in memory: 44-byte header + PCM data
    size_t data_size = pcm_frames * (size_t)channels * sizeof(int16_t);
    size_t wav_size = 44 + data_size;
    uint8_t *wav = (uint8_t *)malloc(wav_size);
    if (!wav) {
        free(pcm_buf);
        return -1;
    }

    // RIFF header
    memcpy(wav, "RIFF", 4);
    uint32_t riff_size = (uint32_t)(wav_size - 8);
    wav[4] = (uint8_t)(riff_size);
    wav[5] = (uint8_t)(riff_size >> 8);
    wav[6] = (uint8_t)(riff_size >> 16);
    wav[7] = (uint8_t)(riff_size >> 24);
    memcpy(wav + 8, "WAVE", 4);

    // fmt chunk
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16;
    wav[17] = wav[18] = wav[19] = 0; // chunk size = 16
    wav[20] = 1;
    wav[21] = 0; // PCM format
    wav[22] = (uint8_t)channels;
    wav[23] = 0;
    wav[24] = (uint8_t)(sample_rate);
    wav[25] = (uint8_t)(sample_rate >> 8);
    wav[26] = (uint8_t)(sample_rate >> 16);
    wav[27] = (uint8_t)(sample_rate >> 24);
    uint32_t byte_rate = (uint32_t)(sample_rate * channels * 2);
    wav[28] = (uint8_t)(byte_rate);
    wav[29] = (uint8_t)(byte_rate >> 8);
    wav[30] = (uint8_t)(byte_rate >> 16);
    wav[31] = (uint8_t)(byte_rate >> 24);
    wav[32] = (uint8_t)(channels * 2); // block align
    wav[33] = 0;
    wav[34] = 16; // bits per sample
    wav[35] = 0;

    // data chunk
    memcpy(wav + 36, "data", 4);
    wav[40] = (uint8_t)(data_size);
    wav[41] = (uint8_t)(data_size >> 8);
    wav[42] = (uint8_t)(data_size >> 16);
    wav[43] = (uint8_t)(data_size >> 24);
    memcpy(wav + 44, pcm_buf, data_size);

    free(pcm_buf);
    *out_data = wav;
    *out_len = wav_size;
    return 0;
}

/// @brief Decode an MP3 file to a WAV-format memory buffer.
static int mp3_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len) {
    FILE *mf = fopen(filepath, "rb");
    if (!mf)
        return -1;
    fseek(mf, 0, SEEK_END);
    long mf_len = ftell(mf);
    fseek(mf, 0, SEEK_SET);
    if (mf_len <= 0 || mf_len > 256 * 1024 * 1024) {
        fclose(mf);
        return -1;
    }
    uint8_t *mf_data = (uint8_t *)malloc((size_t)mf_len);
    if (!mf_data) {
        fclose(mf);
        return -1;
    }
    if (fread(mf_data, 1, (size_t)mf_len, mf) != (size_t)mf_len) {
        free(mf_data);
        fclose(mf);
        return -1;
    }
    fclose(mf);

    mp3_decoder_t *dec = mp3_decoder_new();
    if (!dec) {
        free(mf_data);
        return -1;
    }

    int16_t *pcm = NULL;
    int samples = 0, channels = 0, sample_rate = 0;
    int rc = mp3_decode_file(dec, mf_data, (size_t)mf_len,
                             &pcm, &samples, &channels, &sample_rate);
    mp3_decoder_free(dec);
    free(mf_data);

    if (rc != 0 || !pcm || samples == 0)
        return -1;

    // Build WAV header
    size_t data_size = (size_t)samples * (size_t)channels * sizeof(int16_t);
    size_t wav_size = 44 + data_size;
    uint8_t *wav = (uint8_t *)malloc(wav_size);
    if (!wav) {
        free(pcm);
        return -1;
    }

    memcpy(wav, "RIFF", 4);
    uint32_t riff_sz = (uint32_t)(wav_size - 8);
    wav[4] = (uint8_t)(riff_sz); wav[5] = (uint8_t)(riff_sz >> 8);
    wav[6] = (uint8_t)(riff_sz >> 16); wav[7] = (uint8_t)(riff_sz >> 24);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16; wav[17] = wav[18] = wav[19] = 0;
    wav[20] = 1; wav[21] = 0;
    wav[22] = (uint8_t)channels; wav[23] = 0;
    wav[24] = (uint8_t)(sample_rate); wav[25] = (uint8_t)(sample_rate >> 8);
    wav[26] = (uint8_t)(sample_rate >> 16); wav[27] = (uint8_t)(sample_rate >> 24);
    uint32_t brate = (uint32_t)(sample_rate * channels * 2);
    wav[28] = (uint8_t)(brate); wav[29] = (uint8_t)(brate >> 8);
    wav[30] = (uint8_t)(brate >> 16); wav[31] = (uint8_t)(brate >> 24);
    wav[32] = (uint8_t)(channels * 2); wav[33] = 0;
    wav[34] = 16; wav[35] = 0;
    memcpy(wav + 36, "data", 4);
    wav[40] = (uint8_t)(data_size); wav[41] = (uint8_t)(data_size >> 8);
    wav[42] = (uint8_t)(data_size >> 16); wav[43] = (uint8_t)(data_size >> 24);
    memcpy(wav + 44, pcm, data_size);
    free(pcm);

    *out_data = wav;
    *out_len = wav_size;
    return 0;
}

/// @brief Load a sound effect from a file (WAV, OGG, or MP3 auto-detected from magic bytes).
/// @details OGG and MP3 files are decoded to WAV in memory before loading into the
///          audio engine. The returned handle can be played multiple times concurrently.
void *rt_sound_load(rt_string path) {
    if (!path)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
        return NULL;

    vaud_sound_t snd = NULL;

    // Detect format and dispatch
    int fmt = detect_audio_format(path_str);
    if (fmt == 2) {
        // OGG Vorbis
        uint8_t *wav_data = NULL;
        size_t wav_len = 0;
        if (ogg_decode_to_wav(path_str, &wav_data, &wav_len) != 0)
            return NULL;
        snd = vaud_load_sound_mem(g_audio_ctx, wav_data, wav_len);
        free(wav_data);
    } else if (fmt == 3) {
        // MP3
        uint8_t *wav_data = NULL;
        size_t wav_len = 0;
        if (mp3_file_to_wav(path_str, &wav_data, &wav_len) != 0)
            return NULL;
        snd = vaud_load_sound_mem(g_audio_ctx, wav_data, wav_len);
        free(wav_data);
    } else {
        /* WAV path */
        snd = vaud_load_sound(g_audio_ctx, path_str);
    }
    if (!snd)
        return NULL;

    /* Allocate wrapper object */
    rt_sound *wrapper = (rt_sound *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sound));
    if (!wrapper) {
        vaud_free_sound(snd);
        return NULL;
    }

    wrapper->vptr = NULL;
    wrapper->sound = snd;
    rt_obj_set_finalizer(wrapper, rt_sound_finalize);

    return wrapper;
}

/// @brief Load a sound effect from an in-memory buffer (WAV format expected).
void *rt_sound_load_mem(const void *data, int64_t size) {
    if (!data || size <= 0)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    /* Load the sound from memory via ViperAUD */
    vaud_sound_t snd = vaud_load_sound_mem(g_audio_ctx, data, (size_t)size);
    if (!snd)
        return NULL;

    /* Allocate wrapper object */
    rt_sound *wrapper = (rt_sound *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sound));
    if (!wrapper) {
        vaud_free_sound(snd);
        return NULL;
    }

    wrapper->vptr = NULL;
    wrapper->sound = snd;
    rt_obj_set_finalizer(wrapper, rt_sound_finalize);

    return wrapper;
}

/// @brief Destroy a sound handle and release the underlying audio buffer.
void rt_sound_destroy(void *sound) {
    if (!sound)
        return;

    if (rt_obj_release_check0(sound))
        rt_obj_free(sound);
}

/// @brief Play a sound effect at default volume and center pan. Returns a voice ID.
int64_t rt_sound_play(void *sound) {
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    vaud_voice_id voice = vaud_play(snd->sound);
    return (int64_t)voice;
}

/// @brief Play a sound with explicit volume (0–100) and stereo pan (-100 to 100).
int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan) {
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    /* Convert from 0-100 to 0.0-1.0 */
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    float vol = (float)volume / 100.0f;

    /* Convert from -100 to 100 to -1.0 to 1.0 */
    if (pan < -100)
        pan = -100;
    if (pan > 100)
        pan = 100;
    float p = (float)pan / 100.0f;

    vaud_voice_id voice = vaud_play_ex(snd->sound, vol, p);
    return (int64_t)voice;
}

/// @brief Play a sound in a continuous loop with explicit volume and pan. Returns a voice ID.
int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan) {
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    /* Convert from 0-100 to 0.0-1.0 */
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    float vol = (float)volume / 100.0f;

    /* Convert from -100 to 100 to -1.0 to 1.0 */
    if (pan < -100)
        pan = -100;
    if (pan > 100)
        pan = 100;
    float p = (float)pan / 100.0f;

    vaud_voice_id voice = vaud_play_loop(snd->sound, vol, p);
    return (int64_t)voice;
}

/// @brief Stop a playing voice immediately by its voice ID.
void rt_voice_stop(int64_t voice_id) {
    if (!g_audio_ctx || voice_id < 0)
        return;

    vaud_stop_voice(g_audio_ctx, (vaud_voice_id)voice_id);
}

/// @brief Change the volume of a playing voice (0–100).
void rt_voice_set_volume(int64_t voice_id, int64_t volume) {
    if (!g_audio_ctx || voice_id < 0)
        return;

    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    float vol = (float)volume / 100.0f;

    vaud_set_voice_volume(g_audio_ctx, (vaud_voice_id)voice_id, vol);
}

/// @brief Change the stereo pan of a playing voice (-100 = full left, 100 = full right).
void rt_voice_set_pan(int64_t voice_id, int64_t pan) {
    if (!g_audio_ctx || voice_id < 0)
        return;

    if (pan < -100)
        pan = -100;
    if (pan > 100)
        pan = 100;
    float p = (float)pan / 100.0f;

    vaud_set_voice_pan(g_audio_ctx, (vaud_voice_id)voice_id, p);
}

/// @brief Check whether a voice is currently playing.
int64_t rt_voice_is_playing(int64_t voice_id) {
    if (!g_audio_ctx || voice_id < 0)
        return 0;

    return vaud_voice_is_playing(g_audio_ctx, (vaud_voice_id)voice_id) ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// Music Streaming
//===----------------------------------------------------------------------===//

/// @brief Load a music track for streaming playback (WAV, OGG, or MP3 auto-detected).
/// @details Unlike sounds, music streams from disk and is not fully decoded into memory.
///          Only one music track plays at a time (use crossfade for transitions).
void *rt_music_load(rt_string path) {
    if (!path)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
        return NULL;

    /* Detect format and load via appropriate ViperAUD function */
    int fmt = detect_audio_format(path_str);
    vaud_music_t mus = NULL;
    switch (fmt) {
        case 2:
            mus = vaud_load_music_ogg(g_audio_ctx, path_str);
            break;
        case 3:
            mus = vaud_load_music_mp3(g_audio_ctx, path_str);
            break;
        default:
            mus = vaud_load_music(g_audio_ctx, path_str);
            break;
    }
    if (!mus)
        return NULL;

    /* Allocate wrapper object */
    rt_music *wrapper = (rt_music *)rt_obj_new_i64(0, (int64_t)sizeof(rt_music));
    if (!wrapper) {
        vaud_free_music(mus);
        return NULL;
    }

    wrapper->vptr = NULL;
    wrapper->music = mus;
    rt_obj_set_finalizer(wrapper, rt_music_finalize);

    return wrapper;
}

/// @brief Destroy a music handle and release streaming resources.
void rt_music_destroy(void *music) {
    if (!music)
        return;

    if (rt_obj_release_check0(music))
        rt_obj_free(music);
}

/// @brief Start playing a music track (loop=1 for continuous looping, 0 for one-shot).
void rt_music_play(void *music, int64_t loop) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    vaud_music_play(mus->music, loop ? 1 : 0);
}

/// @brief Stop music playback and reset the position to the beginning.
void rt_music_stop(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_stop(mus->music);
}

/// @brief Pause music playback at the current position (can be resumed).
void rt_music_pause(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_pause(mus->music);
}

/// @brief Resume paused music playback from where it was paused.
void rt_music_resume(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_resume(mus->music);
}

/// @brief Set the music playback volume (0–100).
void rt_music_set_volume(void *music, int64_t volume) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    float vol = (float)volume / 100.0f;

    vaud_music_set_volume(mus->music, vol);
}

/// @brief Get the current music playback volume (0–100).
int64_t rt_music_get_volume(void *music) {
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float vol = vaud_music_get_volume(mus->music);
    return (int64_t)(vol * 100.0f + 0.5f);
}

/// @brief Check whether a music track is currently playing.
int64_t rt_music_is_playing(void *music) {
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    return vaud_music_is_playing(mus->music) ? 1 : 0;
}

/// @brief Seek to a position in the music track (in milliseconds from the start).
void rt_music_seek(void *music, int64_t position_ms) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    if (position_ms < 0)
        position_ms = 0;
    float seconds = (float)position_ms / 1000.0f;

    vaud_music_seek(mus->music, seconds);
}

/// @brief Get the current playback position in milliseconds.
int64_t rt_music_get_position(void *music) {
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float seconds = vaud_music_get_position(mus->music);
    return (int64_t)(seconds * 1000.0f + 0.5f);
}

/// @brief Get the total duration of a music track in milliseconds.
int64_t rt_music_get_duration(void *music) {
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float seconds = vaud_music_get_duration(mus->music);
    return (int64_t)(seconds * 1000.0f + 0.5f);
}

//===----------------------------------------------------------------------===//
// Mix Groups — real implementation
//===----------------------------------------------------------------------===//

/// @brief Set the volume for a mix group (0–100). Sounds in this group are scaled by this.
void rt_audio_set_group_volume(int64_t group, int64_t volume) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return;
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    g_group_volume[group] = volume;
}

/// @brief Get the volume of a mix group (0–100).
int64_t rt_audio_get_group_volume(int64_t group) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return 100;
    return g_group_volume[group];
}

//===----------------------------------------------------------------------===//
// Music Crossfade — real implementation
//===----------------------------------------------------------------------===//

/// @brief Begin a crossfade transition between two music tracks over the given duration.
/// @details Fades out the current track while fading in the new track simultaneously.
///          Both tracks are retained for the duration of the crossfade. Call
///          rt_music_crossfade_update each frame to advance the fade.
void rt_music_crossfade_to(void *current_music, void *new_music, int64_t duration_ms) {
    // Cancel any existing crossfade and release retained music objects
    if (g_crossfade.active) {
        // Complete the previous crossfade immediately
        if (g_crossfade.fade_out) {
            rt_music_stop(g_crossfade.fade_out);
            if (rt_obj_release_check0(g_crossfade.fade_out))
                rt_obj_free(g_crossfade.fade_out);
        }
        if (g_crossfade.fade_in) {
            if (rt_obj_release_check0(g_crossfade.fade_in))
                rt_obj_free(g_crossfade.fade_in);
        }
        g_crossfade.fade_out = NULL;
        g_crossfade.fade_in = NULL;
        g_crossfade.active = 0;
    }

    // Nothing to crossfade if both are NULL
    if (!current_music && !new_music)
        return;

    if (duration_ms <= 0) {
        // Immediate switch: stop old, start new
        if (current_music)
            rt_music_stop(current_music);
        if (new_music)
            rt_music_play(new_music, 0);
        return;
    }

    // Start crossfade — retain both music objects for the duration
    if (current_music)
        rt_obj_retain_maybe(current_music);
    if (new_music)
        rt_obj_retain_maybe(new_music);
    g_crossfade.fade_out = current_music;
    g_crossfade.fade_in = new_music;
    g_crossfade.duration = duration_ms;
    g_crossfade.elapsed = 0;
    g_crossfade.active = 1;

    // Get current volume of fade-out track
    if (current_music)
        g_crossfade.vol_out = rt_music_get_volume(current_music);
    else
        g_crossfade.vol_out = 100;

    // Start the new track at volume 0
    if (new_music) {
        rt_music_set_volume(new_music, 0);
        rt_music_play(new_music, 0);
    }
}

/// @brief Check whether a music crossfade is currently in progress.
int8_t rt_music_is_crossfading(void) {
    return g_crossfade.active;
}

/// @brief Advance the crossfade by dt_ms milliseconds (call each frame during a crossfade).
void rt_music_crossfade_update(int64_t dt_ms) {
    if (!g_crossfade.active)
        return;

    g_crossfade.elapsed += dt_ms;

    if (g_crossfade.elapsed >= g_crossfade.duration) {
        // Crossfade complete
        if (g_crossfade.fade_out) {
            rt_music_stop(g_crossfade.fade_out);
            rt_music_set_volume(g_crossfade.fade_out, (int64_t)g_crossfade.vol_out);
        }
        if (g_crossfade.fade_in)
            rt_music_set_volume(g_crossfade.fade_in, (int64_t)g_crossfade.vol_out);

        // Release retained music objects
        if (g_crossfade.fade_out) {
            if (rt_obj_release_check0(g_crossfade.fade_out))
                rt_obj_free(g_crossfade.fade_out);
        }
        if (g_crossfade.fade_in) {
            if (rt_obj_release_check0(g_crossfade.fade_in))
                rt_obj_free(g_crossfade.fade_in);
        }
        g_crossfade.active = 0;
        g_crossfade.fade_out = NULL;
        g_crossfade.fade_in = NULL;
        return;
    }

    // Linear interpolation
    int64_t progress = (g_crossfade.elapsed * 1000) / g_crossfade.duration; // 0-1000

    if (g_crossfade.fade_out) {
        int64_t vol = g_crossfade.vol_out * (1000 - progress) / 1000;
        rt_music_set_volume(g_crossfade.fade_out, vol);
    }
    if (g_crossfade.fade_in) {
        int64_t vol = g_crossfade.vol_out * progress / 1000;
        rt_music_set_volume(g_crossfade.fade_in, vol);
    }
}

//===----------------------------------------------------------------------===//
// Group-Aware Sound Playback — real implementation
//===----------------------------------------------------------------------===//

/// @brief Apply group volume to a requested volume.
static int64_t apply_group_volume(int64_t volume, int64_t group) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        group = RT_MIXGROUP_SFX;
    return volume * g_group_volume[group] / 100;
}

/// @brief Play a sound at default volume, scaled by the given mix group's volume.
int64_t rt_sound_play_in_group(void *sound, int64_t group) {
    if (!sound)
        return -1;
    int64_t vol = apply_group_volume(100, group);
    return rt_sound_play_ex(sound, vol, 0);
}

/// @brief Play a sound with explicit volume/pan, scaled by the mix group's volume.
int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    if (!sound)
        return -1;
    int64_t vol = apply_group_volume(volume, group);
    return rt_sound_play_ex(sound, vol, pan);
}

/// @brief Play a looping sound with explicit volume/pan, scaled by the mix group's volume.
int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    if (!sound)
        return -1;
    int64_t vol = apply_group_volume(volume, group);
    return rt_sound_play_loop(sound, vol, pan);
}

#else /* !VIPER_ENABLE_AUDIO */

//===----------------------------------------------------------------------===//
// Stub implementations when audio library is not available
//===----------------------------------------------------------------------===//

int64_t rt_audio_init(void) {
    return 0;
}

void rt_audio_shutdown(void) {}

void rt_audio_set_master_volume(int64_t volume) {
    (void)volume;
}

int64_t rt_audio_get_master_volume(void) {
    return 0;
}

void rt_audio_pause_all(void) {}

void rt_audio_resume_all(void) {}

void rt_audio_stop_all_sounds(void) {}

void *rt_sound_load(rt_string path) {
    (void)path;
    return NULL;
}

void *rt_sound_load_mem(const void *data, int64_t size) {
    (void)data;
    (void)size;
    return NULL;
}

void rt_sound_destroy(void *sound) {
    (void)sound;
}

int64_t rt_sound_play(void *sound) {
    (void)sound;
    return -1;
}

int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan) {
    (void)sound;
    (void)volume;
    (void)pan;
    return -1;
}

int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan) {
    (void)sound;
    (void)volume;
    (void)pan;
    return -1;
}

void rt_voice_stop(int64_t voice_id) {
    (void)voice_id;
}

void rt_voice_set_volume(int64_t voice_id, int64_t volume) {
    (void)voice_id;
    (void)volume;
}

void rt_voice_set_pan(int64_t voice_id, int64_t pan) {
    (void)voice_id;
    (void)pan;
}

int64_t rt_voice_is_playing(int64_t voice_id) {
    (void)voice_id;
    return 0;
}

void *rt_music_load(rt_string path) {
    (void)path;
    return NULL;
}

void rt_music_destroy(void *music) {
    (void)music;
}

void rt_music_play(void *music, int64_t loop) {
    (void)music;
    (void)loop;
}

void rt_music_stop(void *music) {
    (void)music;
}

void rt_music_pause(void *music) {
    (void)music;
}

void rt_music_resume(void *music) {
    (void)music;
}

void rt_music_set_volume(void *music, int64_t volume) {
    (void)music;
    (void)volume;
}

int64_t rt_music_get_volume(void *music) {
    (void)music;
    return 0;
}

int64_t rt_music_is_playing(void *music) {
    (void)music;
    return 0;
}

void rt_music_seek(void *music, int64_t position_ms) {
    (void)music;
    (void)position_ms;
}

int64_t rt_music_get_position(void *music) {
    (void)music;
    return 0;
}

int64_t rt_music_get_duration(void *music) {
    (void)music;
    return 0;
}

// Mix group stubs — these work without audio (just store state)
void rt_audio_set_group_volume(int64_t group, int64_t volume) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return;
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    g_group_volume[group] = volume;
}

int64_t rt_audio_get_group_volume(int64_t group) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return 100;
    return g_group_volume[group];
}

void rt_music_crossfade_to(void *current_music, void *new_music, int64_t duration_ms) {
    (void)current_music;
    (void)new_music;
    (void)duration_ms;
}

int8_t rt_music_is_crossfading(void) {
    return 0;
}

void rt_music_crossfade_update(int64_t dt_ms) {
    (void)dt_ms;
}

int64_t rt_sound_play_in_group(void *sound, int64_t group) {
    (void)sound;
    (void)group;
    return -1;
}

int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    (void)sound;
    (void)volume;
    (void)pan;
    (void)group;
    return -1;
}

int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    (void)sound;
    (void)volume;
    (void)pan;
    (void)group;
    return -1;
}

#endif /* VIPER_ENABLE_AUDIO */
