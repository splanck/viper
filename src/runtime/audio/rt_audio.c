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
//          time, capability probes remain available and public playback/load
//          operations fail with deterministic InvalidOperation traps.
//
// Key invariants:
//   - All functions guard against NULL handles and return silently if passed one.
//   - The audio context is a module-level global; Init must be called once
//     before any other audio function.
//   - Shutdown releases all loaded sounds/music and destroys the context.
//   - Sounds use ref-counting; the caller owns the reference from LoadSound.
//   - Music is loaded as a single stream; only one music track plays at a time.
//   - The VIPER_ENABLE_AUDIO compile flag controls whether real or stub impls
//     are compiled; stub playback/load entry points fail loudly.
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
#include "rt_error.h"
#include "rt_mixgroup.h"
#include "rt_mp3.h"
#include "rt_object.h"
#include "rt_ogg.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_time.h"
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

#ifdef VIPER_ENABLE_AUDIO
#include "vaud.h"

typedef struct {
    void *fade_out;      ///< Music being faded out (NULL when not crossfading).
    void *fade_in;       ///< Music being faded in (NULL when not crossfading).
    int64_t elapsed;     ///< Milliseconds elapsed in crossfade.
    int64_t duration;    ///< Total crossfade duration in ms.
    int64_t vol_out;     ///< Target logical volume of the fade-out track.
    int64_t vol_in;      ///< Target logical volume of the fade-in track.
    int64_t last_tick_ms; ///< Last monotonic tick used to advance the fade.
    int8_t active;       ///< 1 if crossfade in progress.
} rt_music_crossfade_state;

static rt_music_crossfade_state g_crossfades[VAUD_MAX_MUSIC];

//===----------------------------------------------------------------------===//
// Global Audio Context
//===----------------------------------------------------------------------===//

/// @brief Global audio context (singleton).
static vaud_context_t g_audio_ctx = NULL;

/// @brief Initialization state: 0 = not init, 1 = initialized, -1 = init failed.
static volatile int g_audio_initialized = 0;
static int8_t g_audio_paused = 0;

/// @brief Spinlock for thread-safe initialization (RACE-009 fix).
/// CONC-008: kept as spinlock (pthread_once doesn't support retry-on-failure);
/// yield hint added to reduce CPU waste under contention.
static volatile int g_audio_init_lock = 0;

#if !defined(_WIN32)
#include <sched.h>
#endif

static void audio_state_lock(void) {
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
}

static void audio_state_unlock(void) {
#if RT_COMPILER_MSVC
    _InterlockedExchange8((volatile char *)&g_audio_init_lock, 0);
#else
    __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
#endif
}

typedef struct {
    int64_t voice_id;
    int64_t group;
    int64_t base_volume;
} rt_tracked_voice;

static rt_tracked_voice g_tracked_voices[VAUD_MAX_VOICES];
static int32_t g_tracked_voice_count = 0;

//===----------------------------------------------------------------------===//
// Sound Wrapper Structure
//===----------------------------------------------------------------------===//

/// @brief Internal sound wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vaud sound handle.
typedef struct rt_sound {
    void *vptr;            ///< VTable pointer (reserved for future use)
    vaud_sound_t sound;    ///< ViperAUD sound handle
    struct rt_sound *prev; ///< Registry linkage
    struct rt_sound *next; ///< Registry linkage
} rt_sound;

static rt_sound *g_sound_wrappers = NULL;

static void sound_registry_add(rt_sound *snd) {
    if (!snd)
        return;
    snd->prev = NULL;
    snd->next = g_sound_wrappers;
    if (g_sound_wrappers)
        g_sound_wrappers->prev = snd;
    g_sound_wrappers = snd;
}

static void sound_registry_remove(rt_sound *snd) {
    if (!snd)
        return;
    if (snd->prev)
        snd->prev->next = snd->next;
    else if (g_sound_wrappers == snd)
        g_sound_wrappers = snd->next;
    if (snd->next)
        snd->next->prev = snd->prev;
    snd->prev = NULL;
    snd->next = NULL;
}

/// @brief Finalizer for sound objects.
static void rt_sound_finalize(void *obj) {
    if (!obj)
        return;

    rt_sound *snd = (rt_sound *)obj;
    audio_state_lock();
    sound_registry_remove(snd);
    audio_state_unlock();
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
typedef struct rt_music {
    void *vptr;            ///< VTable pointer (reserved for future use)
    vaud_music_t music;    ///< ViperAUD music handle
    int64_t logical_volume; ///< User-facing 0-100 volume before mix-group scaling
    struct rt_music *prev; ///< Registry linkage
    struct rt_music *next; ///< Registry linkage
} rt_music;

static rt_music *g_music_wrappers = NULL;

static void music_registry_add(rt_music *mus) {
    if (!mus)
        return;
    mus->prev = NULL;
    mus->next = g_music_wrappers;
    if (g_music_wrappers)
        g_music_wrappers->prev = mus;
    g_music_wrappers = mus;
}

static void music_registry_remove(rt_music *mus) {
    if (!mus)
        return;
    if (mus->prev)
        mus->prev->next = mus->next;
    else if (g_music_wrappers == mus)
        g_music_wrappers = mus->next;
    if (mus->next)
        mus->next->prev = mus->prev;
    mus->prev = NULL;
    mus->next = NULL;
}

/// @brief Finalizer for music objects.
static void rt_music_finalize(void *obj) {
    if (!obj)
        return;

    rt_music *mus = (rt_music *)obj;
    audio_state_lock();
    music_registry_remove(mus);
    audio_state_unlock();
    if (mus->music) {
        vaud_free_music(mus->music);
        mus->music = NULL;
    }
}

//===----------------------------------------------------------------------===//
// Audio System Management
//===----------------------------------------------------------------------===//

/// @brief Report whether the Audio runtime is usable in this build.
///
/// Mirror of the stub layer's `rt_audio_is_available` (in
/// `rt_audio_stubs.c`). The graphics-enabled implementation always
/// returns 1 — Audio doesn't depend on the windowing backend so it's
/// available wherever ViperAUD is linked. Cheap; safe to call every frame.
///
/// @return Always `1` in the real implementation; the stubbed
///         `audio-disabled` build returns `0`.
int8_t rt_audio_is_available(void) {
    return 1;
}

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
    if (g_audio_ctx)
        g_audio_paused = 0;

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

static int64_t clamp_volume_100(int64_t volume) {
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

static int64_t apply_group_volume(int64_t volume, int64_t group) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        group = RT_MIXGROUP_SFX;
    return clamp_volume_100(volume) * g_group_volume[group] / 100;
}

static void tracked_voice_remove_at(int32_t index) {
    if (index < 0 || index >= g_tracked_voice_count)
        return;
    for (int32_t i = index; i < g_tracked_voice_count - 1; i++)
        g_tracked_voices[i] = g_tracked_voices[i + 1];
    g_tracked_voice_count--;
}

static int32_t tracked_voice_find(int64_t voice_id) {
    for (int32_t i = 0; i < g_tracked_voice_count; i++) {
        if (g_tracked_voices[i].voice_id == voice_id)
            return i;
    }
    return -1;
}

static void tracked_voice_set(int64_t voice_id, int64_t group, int64_t base_volume) {
    if (voice_id < 0)
        return;
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        group = RT_MIXGROUP_SFX;
    int32_t index = tracked_voice_find(voice_id);
    if (index < 0) {
        if (g_tracked_voice_count >= VAUD_MAX_VOICES)
            tracked_voice_remove_at(0);
        index = g_tracked_voice_count++;
    }
    g_tracked_voices[index].voice_id = voice_id;
    g_tracked_voices[index].group = group;
    g_tracked_voices[index].base_volume = clamp_volume_100(base_volume);
}

static void tracked_voice_remove(int64_t voice_id) {
    int32_t index = tracked_voice_find(voice_id);
    if (index >= 0)
        tracked_voice_remove_at(index);
}

static int32_t rt_audio_find_crossfade_by_music_locked(const void *music) {
    if (!music)
        return -1;
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (!g_crossfades[i].active)
            continue;
        if (g_crossfades[i].fade_out == music || g_crossfades[i].fade_in == music)
            return i;
    }
    return -1;
}

static void rt_audio_apply_music_volume_value(rt_music *mus, int64_t logical_volume) {
    if (!mus || !mus->music)
        return;
    int64_t effective = apply_group_volume(logical_volume, RT_MIXGROUP_MUSIC);
    vaud_music_set_volume(mus->music, (float)effective / 100.0f);
}

static void rt_audio_apply_music_volume(rt_music *mus) {
    rt_audio_apply_music_volume_value(mus, mus ? mus->logical_volume : 0);
}

static void rt_audio_release_crossfade_refs_locked(rt_music_crossfade_state *xf) {
    if (!xf)
        return;
    if (xf->fade_out) {
        if (rt_obj_release_check0(xf->fade_out))
            rt_obj_free(xf->fade_out);
    }
    if (xf->fade_in) {
        if (rt_obj_release_check0(xf->fade_in))
            rt_obj_free(xf->fade_in);
    }
    xf->fade_out = NULL;
    xf->fade_in = NULL;
    xf->elapsed = 0;
    xf->duration = 0;
    xf->vol_out = 100;
    xf->vol_in = 100;
    xf->last_tick_ms = 0;
    xf->active = 0;
}

static void rt_audio_reapply_crossfade_locked(rt_music_crossfade_state *xf) {
    if (!xf || !xf->active)
        return;

    if (xf->duration <= 0) {
        if (xf->fade_out)
            rt_audio_apply_music_volume_value((rt_music *)xf->fade_out, 0);
        if (xf->fade_in)
            rt_audio_apply_music_volume_value((rt_music *)xf->fade_in, xf->vol_in);
        return;
    }

    int64_t progress = (xf->elapsed * 1000) / xf->duration;
    if (progress < 0)
        progress = 0;
    if (progress > 1000)
        progress = 1000;

    if (xf->fade_out) {
        int64_t vol = xf->vol_out * (1000 - progress) / 1000;
        rt_audio_apply_music_volume_value((rt_music *)xf->fade_out, vol);
    }
    if (xf->fade_in) {
        int64_t vol = xf->vol_in * progress / 1000;
        rt_audio_apply_music_volume_value((rt_music *)xf->fade_in, vol);
    }
}

static void rt_audio_refresh_music_group_volumes(void) {
    for (rt_music *mus = g_music_wrappers; mus; mus = mus->next) {
        if (mus->music && rt_audio_find_crossfade_by_music_locked(mus) < 0)
            rt_audio_apply_music_volume(mus);
    }
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (g_crossfades[i].active)
            rt_audio_reapply_crossfade_locked(&g_crossfades[i]);
    }
}

static void rt_audio_refresh_voice_group_volumes(int64_t group) {
    for (int32_t i = g_tracked_voice_count - 1; i >= 0; i--) {
        int64_t voice_id = g_tracked_voices[i].voice_id;
        if (!g_audio_ctx || !vaud_voice_is_playing(g_audio_ctx, (vaud_voice_id)voice_id)) {
            tracked_voice_remove_at(i);
            continue;
        }
        if (g_tracked_voices[i].group != group)
            continue;
        int64_t effective = apply_group_volume(g_tracked_voices[i].base_volume, group);
        vaud_set_voice_volume(g_audio_ctx, (vaud_voice_id)voice_id, (float)effective / 100.0f);
    }
}

static void rt_audio_invalidate_wrappers_for_shutdown(void) {
    for (rt_sound *snd = g_sound_wrappers; snd; snd = snd->next) {
        if (snd->sound)
            vaud_detach_sound(snd->sound);
    }

    for (rt_music *mus = g_music_wrappers; mus; mus = mus->next) {
        if (mus->music)
            vaud_detach_music(mus->music);
    }

    g_tracked_voice_count = 0;
}

static void rt_audio_crossfade_cancel_entry_locked(rt_music_crossfade_state *xf,
                                                   int stop_fade_out,
                                                   int stop_fade_in,
                                                   int restore_volumes) {
    if (!xf || !xf->active)
        return;

    if (xf->fade_out) {
        rt_music *fade_out = (rt_music *)xf->fade_out;
        if (stop_fade_out) {
            if (fade_out->music)
                vaud_music_stop(fade_out->music);
        } else if (restore_volumes) {
            rt_audio_apply_music_volume_value(fade_out, fade_out->logical_volume);
        }
    }

    if (xf->fade_in) {
        rt_music *fade_in = (rt_music *)xf->fade_in;
        if (stop_fade_in) {
            if (fade_in->music)
                vaud_music_stop(fade_in->music);
        } else if (restore_volumes) {
            rt_audio_apply_music_volume_value(fade_in, fade_in->logical_volume);
        }
    }

    rt_audio_release_crossfade_refs_locked(xf);
}

static void rt_audio_crossfade_cancel_all_locked(int stop_fade_out, int stop_fade_in) {
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++)
        rt_audio_crossfade_cancel_entry_locked(
            &g_crossfades[i], stop_fade_out, stop_fade_in, 0);
}

static void rt_audio_prepare_music_for_foreground_locked(void *music) {
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        rt_music_crossfade_state *xf = &g_crossfades[i];
        if (!xf->active)
            continue;

        if (xf->fade_out == music || xf->fade_in == music) {
            int stop_fade_out = (xf->fade_out != music);
            int stop_fade_in = (xf->fade_in != music);
            rt_audio_crossfade_cancel_entry_locked(
                xf, stop_fade_out, stop_fade_in, !stop_fade_out || !stop_fade_in);
        } else {
            rt_audio_crossfade_cancel_entry_locked(xf, 1, 1, 0);
        }
    }

    for (rt_music *mus = g_music_wrappers; mus; mus = mus->next) {
        if (!mus->music || mus == (rt_music *)music)
            continue;
        vaud_music_stop(mus->music);
    }
}

static void rt_audio_update_crossfade_entry_locked(rt_music_crossfade_state *xf, int64_t dt_ms);

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
    audio_state_lock();

    if (g_audio_ctx) {
        rt_audio_crossfade_cancel_all_locked(0, 0);
        rt_audio_invalidate_wrappers_for_shutdown();
        vaud_destroy(g_audio_ctx);
    g_audio_ctx = NULL;
    g_audio_paused = 0;
    }

    // Reset state to allow re-initialization
#if RT_COMPILER_MSVC
    rt_atomic_store_i32(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#endif

    audio_state_unlock();
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
    audio_state_lock();
    g_audio_paused = 1;
    if (g_audio_ctx)
        vaud_pause_all(g_audio_ctx);
    audio_state_unlock();
}

/// @brief Resume all paused sounds and music.
void rt_audio_resume_all(void) {
    audio_state_lock();
    g_audio_paused = 0;
    if (g_audio_ctx)
        vaud_resume_all(g_audio_ctx);
    audio_state_unlock();
}

/// @brief Advance time-based audio state using the monotonic clock.
void rt_audio_update(void) {
    audio_state_lock();
    int8_t any_active = 0;
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (g_crossfades[i].active) {
            any_active = 1;
            break;
        }
    }
    if (any_active) {
        int64_t now_ms = rt_timer_ms();
        int paused = g_audio_paused;

        if (paused) {
            for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
                if (g_crossfades[i].active)
                    g_crossfades[i].last_tick_ms = now_ms;
            }
        } else {
            for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
                if (!g_crossfades[i].active)
                    continue;
                if (g_crossfades[i].last_tick_ms <= 0)
                    g_crossfades[i].last_tick_ms = now_ms;
                int64_t dt_ms = now_ms - g_crossfades[i].last_tick_ms;
                if (dt_ms > 0) {
                    g_crossfades[i].last_tick_ms = now_ms;
                    rt_audio_update_crossfade_entry_locked(&g_crossfades[i], dt_ms);
                }
            }
        }
    }
    audio_state_unlock();
}

/// @brief Stop all currently playing sound effects (music is unaffected).
void rt_audio_stop_all_sounds(void) {
    if (g_audio_ctx)
        vaud_stop_all_sounds(g_audio_ctx);
}

//===----------------------------------------------------------------------===//
// Sound Effects
//===----------------------------------------------------------------------===//

/// @brief Detect audio file format from an in-memory header.
/// @return 1=WAV/RIFF, 2=OGG, 3=MP3, 0=unknown
static int detect_audio_format_mem(const void *data, size_t size) {
    if (!data || size < 4)
        return 0;
    const uint8_t *hdr = (const uint8_t *)data;
    if (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F')
        return 1;
    if (hdr[0] == 'O' && hdr[1] == 'g' && hdr[2] == 'g' && hdr[3] == 'S')
        return 2;
    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
        return 3;
    if (hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0)
        return 3;
    return 0;
}

/// @brief Detect audio file format from magic bytes.
/// @return 1=WAV/RIFF, 2=OGG, 3=MP3, 0=unknown
static int detect_audio_format(const char *filepath) {
    FILE *af = fopen(filepath, "rb");
    if (!af)
        return 0;
    uint8_t hdr[4];
    size_t n = fread(hdr, 1, sizeof(hdr), af);
    fclose(af);
    return detect_audio_format_mem(hdr, n);
}

static int build_wav_from_pcm(const int16_t *pcm,
                              size_t frames,
                              int channels,
                              int sample_rate,
                              uint8_t **out_data,
                              size_t *out_len) {
    if (!pcm || !out_data || !out_len || frames == 0 || channels < 1 || channels > 2 ||
        sample_rate <= 0)
        return -1;

    size_t data_size = frames * (size_t)channels * sizeof(int16_t);
    size_t wav_size = 44 + data_size;
    uint8_t *wav = (uint8_t *)malloc(wav_size);
    if (!wav)
        return -1;

    memcpy(wav, "RIFF", 4);
    uint32_t riff_size = (uint32_t)(wav_size - 8);
    wav[4] = (uint8_t)(riff_size);
    wav[5] = (uint8_t)(riff_size >> 8);
    wav[6] = (uint8_t)(riff_size >> 16);
    wav[7] = (uint8_t)(riff_size >> 24);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16;
    wav[17] = wav[18] = wav[19] = 0;
    wav[20] = 1;
    wav[21] = 0;
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
    wav[32] = (uint8_t)(channels * 2);
    wav[33] = 0;
    wav[34] = 16;
    wav[35] = 0;
    memcpy(wav + 36, "data", 4);
    wav[40] = (uint8_t)(data_size);
    wav[41] = (uint8_t)(data_size >> 8);
    wav[42] = (uint8_t)(data_size >> 16);
    wav[43] = (uint8_t)(data_size >> 24);
    memcpy(wav + 44, pcm, data_size);

    *out_data = wav;
    *out_len = wav_size;
    return 0;
}

static int ogg_decode_reader_to_wav(ogg_reader_t *reader, uint8_t **out_data, size_t *out_len) {
    if (!reader || !out_data || !out_len)
        return -1;

    vorbis_decoder_t *dec = vorbis_decoder_new();
    if (!dec)
        return -1;

    for (int i = 0; i < 3; i++) {
        const uint8_t *pkt_data = NULL;
        size_t pkt_len = 0;
        if (!ogg_reader_next_packet(reader, &pkt_data, &pkt_len) ||
            vorbis_decode_header(dec, pkt_data, pkt_len, i) != 0) {
            vorbis_decoder_free(dec);
            return -1;
        }
    }

    int channels = vorbis_get_channels(dec);
    int sample_rate = vorbis_get_sample_rate(dec);
    if (channels < 1 || channels > 2 || sample_rate <= 0) {
        vorbis_decoder_free(dec);
        return -1;
    }

    int16_t *pcm_buf = NULL;
    size_t pcm_frames = 0;
    size_t pcm_cap = 0;

    const uint8_t *pkt_data = NULL;
    size_t pkt_len = 0;
    while (ogg_reader_next_packet(reader, &pkt_data, &pkt_len)) {
        int16_t *frame_pcm = NULL;
        int frame_samples = 0;
        if (vorbis_decode_packet(dec, pkt_data, pkt_len, &frame_pcm, &frame_samples) != 0) {
            free(pcm_buf);
            vorbis_decoder_free(dec);
            return -1;
        }
        if (frame_samples <= 0 || !frame_pcm)
            continue;

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
                return -1;
            }
            pcm_buf = new_buf;
            pcm_cap = new_cap;
        }

        memcpy(pcm_buf + pcm_frames * (size_t)channels,
               frame_pcm,
               (size_t)frame_samples * (size_t)channels * sizeof(int16_t));
        pcm_frames += (size_t)frame_samples;
    }

    vorbis_decoder_free(dec);
    if (pcm_frames == 0) {
        free(pcm_buf);
        return -1;
    }

    int rc = build_wav_from_pcm(pcm_buf, pcm_frames, channels, sample_rate, out_data, out_len);
    free(pcm_buf);
    return rc;
}

static int ogg_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len) {
    ogg_reader_t *reader = ogg_reader_open_file(filepath);
    if (!reader)
        return -1;
    int rc = ogg_decode_reader_to_wav(reader, out_data, out_len);
    ogg_reader_free(reader);
    return rc;
}

static int ogg_mem_to_wav(const void *data, size_t size, uint8_t **out_data, size_t *out_len) {
    ogg_reader_t *reader = ogg_reader_open_mem((const uint8_t *)data, size);
    if (!reader)
        return -1;
    int rc = ogg_decode_reader_to_wav(reader, out_data, out_len);
    ogg_reader_free(reader);
    return rc;
}

static int mp3_data_to_wav(const uint8_t *data,
                           size_t size,
                           uint8_t **out_data,
                           size_t *out_len) {
    if (!data || size == 0)
        return -1;

    mp3_decoder_t *dec = mp3_decoder_new();
    if (!dec)
        return -1;

    int16_t *pcm = NULL;
    int samples = 0;
    int channels = 0;
    int sample_rate = 0;
    int rc = mp3_decode_file(dec, data, size, &pcm, &samples, &channels, &sample_rate);
    mp3_decoder_free(dec);

    if (rc != 0 || !pcm || samples <= 0 || channels < 1 || channels > 2 || sample_rate <= 0) {
        free(pcm);
        return -1;
    }

    rc = build_wav_from_pcm(pcm, (size_t)samples, channels, sample_rate, out_data, out_len);
    free(pcm);
    return rc;
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

    int rc = mp3_data_to_wav(mf_data, (size_t)mf_len, out_data, out_len);
    free(mf_data);
    return rc;
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
        if (ogg_file_to_wav(path_str, &wav_data, &wav_len) != 0)
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
    wrapper->prev = NULL;
    wrapper->next = NULL;
    rt_obj_set_finalizer(wrapper, rt_sound_finalize);
    audio_state_lock();
    sound_registry_add(wrapper);
    audio_state_unlock();

    return wrapper;
}

/// @brief Load a sound effect from an in-memory buffer (WAV/OGG/MP3 supported).
void *rt_sound_load_mem(const void *data, int64_t size) {
    if (!data || size <= 0)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    vaud_sound_t snd = NULL;
    int fmt = detect_audio_format_mem(data, (size_t)size);
    if (fmt == 2) {
        uint8_t *wav_data = NULL;
        size_t wav_len = 0;
        if (ogg_mem_to_wav(data, (size_t)size, &wav_data, &wav_len) != 0)
            return NULL;
        snd = vaud_load_sound_mem(g_audio_ctx, wav_data, wav_len);
        free(wav_data);
    } else if (fmt == 3) {
        uint8_t *wav_data = NULL;
        size_t wav_len = 0;
        if (mp3_data_to_wav((const uint8_t *)data, (size_t)size, &wav_data, &wav_len) != 0)
            return NULL;
        snd = vaud_load_sound_mem(g_audio_ctx, wav_data, wav_len);
        free(wav_data);
    } else {
        snd = vaud_load_sound_mem(g_audio_ctx, data, (size_t)size);
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
    wrapper->prev = NULL;
    wrapper->next = NULL;
    rt_obj_set_finalizer(wrapper, rt_sound_finalize);
    audio_state_lock();
    sound_registry_add(wrapper);
    audio_state_unlock();

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
    audio_state_lock();
    tracked_voice_remove(voice_id);
    audio_state_unlock();
}

/// @brief Change the volume of a playing voice (0–100).
void rt_voice_set_volume(int64_t voice_id, int64_t volume) {
    if (!g_audio_ctx || voice_id < 0)
        return;

    volume = clamp_volume_100(volume);

    audio_state_lock();
    int32_t index = tracked_voice_find(voice_id);
    if (index >= 0) {
        g_tracked_voices[index].base_volume = volume;
        int64_t effective = apply_group_volume(volume, g_tracked_voices[index].group);
        vaud_set_voice_volume(g_audio_ctx, (vaud_voice_id)voice_id, (float)effective / 100.0f);
    } else {
        vaud_set_voice_volume(g_audio_ctx, (vaud_voice_id)voice_id, (float)volume / 100.0f);
    }
    audio_state_unlock();
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

    int64_t playing = vaud_voice_is_playing(g_audio_ctx, (vaud_voice_id)voice_id) ? 1 : 0;
    if (!playing) {
        audio_state_lock();
        tracked_voice_remove(voice_id);
        audio_state_unlock();
    }
    return playing;
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
    wrapper->logical_volume = 100;
    wrapper->prev = NULL;
    wrapper->next = NULL;
    rt_obj_set_finalizer(wrapper, rt_music_finalize);
    audio_state_lock();
    music_registry_add(wrapper);
    rt_audio_apply_music_volume(wrapper);
    audio_state_unlock();

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

    audio_state_lock();
    rt_audio_prepare_music_for_foreground_locked(mus);
    vaud_music_set_loop(mus->music, loop ? 1 : 0);
    vaud_music_play(mus->music, loop ? 1 : 0);
    rt_audio_apply_music_volume(mus);
    audio_state_unlock();
}

/// @brief Stop music playback and reset the position to the beginning.
void rt_music_stop(void *music) {
    rt_music_stop_related(music);
}

/// @brief Pause music playback at the current position (can be resumed).
void rt_music_pause(void *music) {
    rt_music_pause_related(music);
}

/// @brief Resume paused music playback from where it was paused.
void rt_music_resume(void *music) {
    rt_music_resume_related(music);
}

void rt_music_set_loop(void *music, int64_t loop) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_set_loop(mus->music, loop ? 1 : 0);
}

/// @brief Set the music playback volume (0–100).
void rt_music_set_volume(void *music, int64_t volume) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    mus->logical_volume = clamp_volume_100(volume);
    audio_state_lock();
    int32_t xf_idx = rt_audio_find_crossfade_by_music_locked(mus);
    if (xf_idx >= 0) {
        rt_music_crossfade_state *xf = &g_crossfades[xf_idx];
        if (xf->fade_out == mus)
            xf->vol_out = mus->logical_volume;
        if (xf->fade_in == mus)
            xf->vol_in = mus->logical_volume;
        rt_audio_reapply_crossfade_locked(xf);
    } else {
        rt_audio_apply_music_volume(mus);
    }
    audio_state_unlock();
}

/// @brief Get the current music playback volume (0–100).
int64_t rt_music_get_volume(void *music) {
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    return mus->logical_volume;
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

    audio_state_lock();
    rt_audio_prepare_music_for_foreground_locked(mus);
    vaud_music_seek(mus->music, seconds);
    rt_audio_apply_music_volume(mus);
    audio_state_unlock();
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

void rt_music_pause_related(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    audio_state_lock();
    int32_t xf_idx = rt_audio_find_crossfade_by_music_locked(mus);
    if (xf_idx >= 0) {
        rt_music_crossfade_state *xf = &g_crossfades[xf_idx];
        if (xf->fade_out && ((rt_music *)xf->fade_out)->music)
            vaud_music_pause(((rt_music *)xf->fade_out)->music);
        if (xf->fade_in && ((rt_music *)xf->fade_in)->music)
            vaud_music_pause(((rt_music *)xf->fade_in)->music);
    } else {
        vaud_music_pause(mus->music);
    }
    audio_state_unlock();
}

void rt_music_resume_related(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    audio_state_lock();
    int32_t xf_idx = rt_audio_find_crossfade_by_music_locked(mus);
    if (xf_idx >= 0) {
        rt_music_crossfade_state *xf = &g_crossfades[xf_idx];
        if (xf->fade_out && ((rt_music *)xf->fade_out)->music)
            vaud_music_resume(((rt_music *)xf->fade_out)->music);
        if (xf->fade_in && ((rt_music *)xf->fade_in)->music)
            vaud_music_resume(((rt_music *)xf->fade_in)->music);
    } else {
        vaud_music_resume(mus->music);
    }
    audio_state_unlock();
}

void rt_music_stop_related(void *music) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    audio_state_lock();
    int32_t xf_idx = rt_audio_find_crossfade_by_music_locked(mus);
    if (xf_idx >= 0) {
        rt_audio_crossfade_cancel_entry_locked(&g_crossfades[xf_idx], 1, 1, 0);
    } else {
        vaud_music_stop(mus->music);
    }
    audio_state_unlock();
}

void rt_music_set_crossfade_pair_volume(void *music, int64_t volume) {
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    volume = clamp_volume_100(volume);

    audio_state_lock();
    int32_t xf_idx = rt_audio_find_crossfade_by_music_locked(mus);
    if (xf_idx >= 0) {
        rt_music_crossfade_state *xf = &g_crossfades[xf_idx];
        if (xf->fade_out) {
            ((rt_music *)xf->fade_out)->logical_volume = volume;
            xf->vol_out = volume;
        }
        if (xf->fade_in) {
            ((rt_music *)xf->fade_in)->logical_volume = volume;
            xf->vol_in = volume;
        }
        rt_audio_reapply_crossfade_locked(xf);
    } else {
        mus->logical_volume = volume;
        rt_audio_apply_music_volume(mus);
    }
    audio_state_unlock();
}

//===----------------------------------------------------------------------===//
// Mix Groups — real implementation
//===----------------------------------------------------------------------===//

/// @brief Set the volume for a mix group (0–100). Sounds in this group are scaled by this.
void rt_audio_set_group_volume(int64_t group, int64_t volume) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return;
    g_group_volume[group] = clamp_volume_100(volume);

    audio_state_lock();
    if (group == RT_MIXGROUP_MUSIC)
        rt_audio_refresh_music_group_volumes();
    else
        rt_audio_refresh_voice_group_volumes(group);
    audio_state_unlock();
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
///          Both tracks are retained for the duration of the crossfade.
void rt_music_crossfade_to(void *current_music, void *new_music, int64_t duration_ms) {
    audio_state_lock();

    if ((!current_music && !new_music) || current_music == new_music) {
        audio_state_unlock();
        return;
    }

    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        rt_music_crossfade_state *xf = &g_crossfades[i];
        if (!xf->active)
            continue;

        int keep_fade_out = (xf->fade_out == current_music || xf->fade_out == new_music);
        int keep_fade_in = (xf->fade_in == current_music || xf->fade_in == new_music);
        if (!keep_fade_out && !keep_fade_in)
            continue;
        rt_audio_crossfade_cancel_entry_locked(
            xf, !keep_fade_out, !keep_fade_in, keep_fade_out || keep_fade_in);
    }

    if (duration_ms <= 0) {
        if (current_music && ((rt_music *)current_music)->music)
            vaud_music_stop(((rt_music *)current_music)->music);
        if (new_music && ((rt_music *)new_music)->music) {
            vaud_music_set_loop(((rt_music *)new_music)->music, 0);
            vaud_music_play(((rt_music *)new_music)->music, 0);
            rt_audio_apply_music_volume((rt_music *)new_music);
        }
        audio_state_unlock();
        return;
    }

    int32_t slot = -1;
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (!g_crossfades[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (current_music && ((rt_music *)current_music)->music)
            vaud_music_stop(((rt_music *)current_music)->music);
        if (new_music && ((rt_music *)new_music)->music) {
            vaud_music_set_loop(((rt_music *)new_music)->music, 0);
            vaud_music_play(((rt_music *)new_music)->music, 0);
            rt_audio_apply_music_volume((rt_music *)new_music);
        }
        audio_state_unlock();
        return;
    }

    if (current_music)
        rt_obj_retain_maybe(current_music);
    if (new_music)
        rt_obj_retain_maybe(new_music);

    rt_music_crossfade_state *xf = &g_crossfades[slot];
    xf->fade_out = current_music;
    xf->fade_in = new_music;
    xf->duration = duration_ms;
    xf->elapsed = 0;
    xf->active = 1;
    xf->last_tick_ms = rt_timer_ms();
    xf->vol_out = current_music ? ((rt_music *)current_music)->logical_volume : 100;
    xf->vol_in = new_music ? ((rt_music *)new_music)->logical_volume : 100;

    if (current_music && ((rt_music *)current_music)->music) {
        vaud_music_set_loop(((rt_music *)current_music)->music, 0);
        rt_audio_reapply_crossfade_locked(xf);
    }
    if (new_music && ((rt_music *)new_music)->music) {
        rt_audio_apply_music_volume_value((rt_music *)new_music, 0);
        vaud_music_set_loop(((rt_music *)new_music)->music, 0);
        vaud_music_play(((rt_music *)new_music)->music, 0);
    }
    audio_state_unlock();
}

/// @brief Check whether a music crossfade is currently in progress.
int8_t rt_music_is_crossfading(void) {
    rt_audio_update();
    audio_state_lock();
    int8_t active = 0;
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (g_crossfades[i].active) {
            active = 1;
            break;
        }
    }
    audio_state_unlock();
    return active;
}

/// @brief Advance the crossfade by dt_ms milliseconds (call each frame during a crossfade).
void rt_music_crossfade_update(int64_t dt_ms) {
    audio_state_lock();
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++) {
        if (!g_crossfades[i].active)
            continue;
        if (dt_ms > 0) {
            g_crossfades[i].last_tick_ms = rt_timer_ms();
            rt_audio_update_crossfade_entry_locked(&g_crossfades[i], dt_ms);
        }
    }
    audio_state_unlock();
}

static void rt_audio_update_crossfade_entry_locked(rt_music_crossfade_state *xf, int64_t dt_ms) {
    if (!xf || !xf->active)
        return;
    if (dt_ms <= 0)
        return;

    xf->elapsed += dt_ms;

    if (xf->elapsed >= xf->duration) {
        if (xf->fade_out) {
            rt_music *fade_out = (rt_music *)xf->fade_out;
            if (fade_out->music)
                vaud_music_stop(fade_out->music);
            rt_audio_apply_music_volume(fade_out);
        }
        if (xf->fade_in)
            rt_audio_apply_music_volume((rt_music *)xf->fade_in);
        rt_audio_release_crossfade_refs_locked(xf);
        return;
    }

    rt_audio_reapply_crossfade_locked(xf);
}

//===----------------------------------------------------------------------===//
// Group-Aware Sound Playback — real implementation
//===----------------------------------------------------------------------===//

/// @brief Play a sound at default volume, scaled by the given mix group's volume.
int64_t rt_sound_play_in_group(void *sound, int64_t group) {
    if (!sound)
        return -1;
    int64_t voice = rt_sound_play_ex(sound, apply_group_volume(100, group), 0);
    audio_state_lock();
    tracked_voice_set(voice, group, 100);
    audio_state_unlock();
    return voice;
}

/// @brief Play a sound with explicit volume/pan, scaled by the mix group's volume.
int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    if (!sound)
        return -1;
    int64_t voice = rt_sound_play_ex(sound, apply_group_volume(volume, group), pan);
    audio_state_lock();
    tracked_voice_set(voice, group, volume);
    audio_state_unlock();
    return voice;
}

/// @brief Play a looping sound with explicit volume/pan, scaled by the mix group's volume.
int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    if (!sound)
        return -1;
    int64_t voice = rt_sound_play_loop(sound, apply_group_volume(volume, group), pan);
    audio_state_lock();
    tracked_voice_set(voice, group, volume);
    audio_state_unlock();
    return voice;
}

#else /* !VIPER_ENABLE_AUDIO */

//===----------------------------------------------------------------------===//
// Stub implementations when audio library is not available
//===----------------------------------------------------------------------===//

/// @brief Raise the canonical "audio support not compiled in" trap.
///
/// Mirror of `rt_graphics_unavailable_` in `rt_graphics_stubs.c`.
/// Shared trap sink used by every Sound / Music entry point in this
/// `#else` branch so failures from a `VIPER_ENABLE_AUDIO=OFF` build all
/// surface at the offending call site with a consistent error code.
///
/// @param msg Diagnostic string; the convention is
///            `"<Class>.<Method>: audio support not compiled in"`.
///
/// @note Never returns under normal control flow.
static void rt_audio_unavailable_(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, 0, msg);
}

/// @brief Audio-disabled override of `rt_audio_is_available`.
/// @return Always `0` in this `#else` branch (audio not compiled in).
int8_t rt_audio_is_available(void) {
    return 0;
}

/// @brief Audio-disabled stub for `rt_audio_init` — silent no-op
///        returning failure so the bootstrap can branch on the result.
/// @return `0` (init failure).
int64_t rt_audio_init(void) {
    return 0;
}

/// @brief Audio-disabled stub for `rt_audio_shutdown` — silent no-op
///        since there's nothing to shut down.
void rt_audio_shutdown(void) {}

/// @brief Audio-disabled stub for `Audio.SetMasterVolume`. Silent no-op.
/// @param volume Ignored.
void rt_audio_set_master_volume(int64_t volume) {
    (void)volume;
}

/// @brief Audio-disabled stub for `Audio.MasterVolume`.
/// @return `0` (silence).
int64_t rt_audio_get_master_volume(void) {
    return 0;
}

/// @brief Audio-disabled stub for `Audio.PauseAll`. Silent no-op.
void rt_audio_pause_all(void) {}

/// @brief Audio-disabled stub for `Audio.ResumeAll`. Silent no-op.
void rt_audio_resume_all(void) {}

/// @brief Audio-disabled stub for `Audio.Update`. Silent no-op.
void rt_audio_update(void) {}

/// @brief Audio-disabled stub for `Audio.StopAllSounds`. Silent no-op.
void rt_audio_stop_all_sounds(void) {}

/// @brief Audio-disabled stub for `Sound.Load` — traps because callers
///        always need a usable Sound handle to subsequently `Play`.
/// @param path Ignored.
/// @return Never returns normally.
void *rt_sound_load(rt_string path) {
    (void)path;
    rt_audio_unavailable_("Sound.Load: audio support not compiled in");
    return NULL;
}

/// @brief Audio-disabled stub for `Sound.LoadMem` — traps for the same
///        reason as `Sound.Load`.
/// @param data Ignored.
/// @param size Ignored.
/// @return Never returns normally.
void *rt_sound_load_mem(const void *data, int64_t size) {
    (void)data;
    (void)size;
    rt_audio_unavailable_("Sound.LoadMem: audio support not compiled in");
    return NULL;
}

/// @brief Audio-disabled stub for `Sound.Destroy` — silent no-op (the
///        handle never existed to begin with).
/// @param sound Ignored.
void rt_sound_destroy(void *sound) {
    (void)sound;
}

/// @brief Audio-disabled stub for `Sound.Play` — traps so the absence
///        of audio surfaces clearly rather than returning a fake voice
///        id that callers might pass to `Voice.Stop`.
/// @param sound Ignored.
/// @return Never returns normally.
int64_t rt_sound_play(void *sound) {
    (void)sound;
    rt_audio_unavailable_("Sound.Play: audio support not compiled in");
    return -1;
}

/// @brief Audio-disabled stub for `Sound.PlayEx` (volume + pan variant
///        of `Play`). Traps.
/// @param sound  Ignored.
/// @param volume Ignored.
/// @param pan    Ignored.
/// @return Never returns normally.
int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan) {
    (void)sound;
    (void)volume;
    (void)pan;
    rt_audio_unavailable_("Sound.PlayEx: audio support not compiled in");
    return -1;
}

/// @brief Audio-disabled stub for `Sound.PlayLoop` (looping variant of
///        `PlayEx`). Traps.
/// @param sound  Ignored.
/// @param volume Ignored.
/// @param pan    Ignored.
/// @return Never returns normally.
int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan) {
    (void)sound;
    (void)volume;
    (void)pan;
    rt_audio_unavailable_("Sound.PlayLoop: audio support not compiled in");
    return -1;
}

/// @brief Audio-disabled stub for `Voice.Stop`. Silent no-op (any voice
///        id passed in must be from a fake/cached value since `Sound.Play`
///        traps).
/// @param voice_id Ignored.
void rt_voice_stop(int64_t voice_id) {
    (void)voice_id;
}

/// @brief Audio-disabled stub for `Voice.SetVolume`. Silent no-op.
/// @param voice_id Ignored.
/// @param volume   Ignored.
void rt_voice_set_volume(int64_t voice_id, int64_t volume) {
    (void)voice_id;
    (void)volume;
}

/// @brief Audio-disabled stub for `Voice.SetPan`. Silent no-op.
/// @param voice_id Ignored.
/// @param pan      Ignored.
void rt_voice_set_pan(int64_t voice_id, int64_t pan) {
    (void)voice_id;
    (void)pan;
}

/// @brief Audio-disabled stub for `Voice.IsPlaying`.
/// @param voice_id Ignored.
/// @return `0` (never playing).
int64_t rt_voice_is_playing(int64_t voice_id) {
    (void)voice_id;
    return 0;
}

/// @brief Audio-disabled stub for `Music.Load` — traps because callers
///        need a usable Music handle for the rest of the API.
/// @param path Ignored.
/// @return Never returns normally.
void *rt_music_load(rt_string path) {
    (void)path;
    rt_audio_unavailable_("Music.Load: audio support not compiled in");
    return NULL;
}

/// @brief Audio-disabled stub for `Music.Destroy`. Silent no-op.
/// @param music Ignored.
void rt_music_destroy(void *music) {
    (void)music;
}

/// @brief Audio-disabled stub for `Music.Play`. Traps.
/// @param music Ignored.
/// @param loop  Ignored.
void rt_music_play(void *music, int64_t loop) {
    (void)music;
    (void)loop;
    rt_audio_unavailable_("Music.Play: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.Stop`. Traps.
/// @param music Ignored.
void rt_music_stop(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Stop: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.Pause`. Traps.
/// @param music Ignored.
void rt_music_pause(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Pause: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.Resume`. Traps.
/// @param music Ignored.
void rt_music_resume(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Resume: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.SetLoop`. Traps.
/// @param music Ignored.
/// @param loop  Ignored.
void rt_music_set_loop(void *music, int64_t loop) {
    (void)music;
    (void)loop;
    rt_audio_unavailable_("Music.SetLoop: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.SetVolume`. Traps.
/// @param music  Ignored.
/// @param volume Ignored.
void rt_music_set_volume(void *music, int64_t volume) {
    (void)music;
    (void)volume;
    rt_audio_unavailable_("Music.SetVolume: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.Volume`. Traps.
/// @param music Ignored.
/// @return Never returns normally.
int64_t rt_music_get_volume(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.GetVolume: audio support not compiled in");
    return 0;
}

/// @brief Audio-disabled stub for `Music.IsPlaying`. Traps.
/// @param music Ignored.
/// @return Never returns normally.
int64_t rt_music_is_playing(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.IsPlaying: audio support not compiled in");
    return 0;
}

/// @brief Audio-disabled stub for `Music.Seek`. Traps.
/// @param music       Ignored.
/// @param position_ms Ignored.
void rt_music_seek(void *music, int64_t position_ms) {
    (void)music;
    (void)position_ms;
    rt_audio_unavailable_("Music.Seek: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.Position`. Traps.
/// @param music Ignored.
/// @return Never returns normally.
int64_t rt_music_get_position(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.GetPosition: audio support not compiled in");
    return 0;
}

/// @brief Audio-disabled stub for `Music.Duration`. Traps.
/// @param music Ignored.
/// @return Never returns normally.
int64_t rt_music_get_duration(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.GetDuration: audio support not compiled in");
    return 0;
}

void rt_music_pause_related(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Pause: audio support not compiled in");
}

void rt_music_resume_related(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Resume: audio support not compiled in");
}

void rt_music_stop_related(void *music) {
    (void)music;
    rt_audio_unavailable_("Music.Stop: audio support not compiled in");
}

void rt_music_set_crossfade_pair_volume(void *music, int64_t volume) {
    (void)music;
    (void)volume;
    rt_audio_unavailable_("Music.SetVolume: audio support not compiled in");
}

// Mix group stubs — these work without audio (just store state)

/// @brief Set the per-mix-group volume even in audio-disabled builds.
///
/// Group volumes are pure state (no playback dependency), so the
/// disabled build keeps them functional. Game code that reads back the
/// values it sets (e.g. for a settings UI) gets correct round-trips
/// regardless of whether the mixer is actually mixing anything.
///
/// Out-of-range groups are silently ignored; volume is clamped to
/// `0..100`.
///
/// @param group  Mix group index in `0..RT_MIXGROUP_COUNT-1`.
/// @param volume Volume `0..100`.
void rt_audio_set_group_volume(int64_t group, int64_t volume) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return;
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    g_group_volume[group] = volume;
}

/// @brief Read the per-mix-group volume in audio-disabled builds.
///
/// Counterpart of `rt_audio_set_group_volume`. Returns the in-memory
/// state, defaulting to `100` (full volume) when the group has not been
/// explicitly set or when the index is out of range.
///
/// @param group Mix group index.
/// @return Stored volume `0..100`, or `100` for out-of-range / unset.
int64_t rt_audio_get_group_volume(int64_t group) {
    if (group < 0 || group >= RT_MIXGROUP_COUNT)
        return 100;
    return g_group_volume[group];
}

/// @brief Audio-disabled stub for `Music.CrossfadeTo`. Traps because
///        crossfading is a mixer-level operation that has no fallback
///        without the audio backend.
/// @param current_music Ignored.
/// @param new_music     Ignored.
/// @param duration_ms   Ignored.
void rt_music_crossfade_to(void *current_music, void *new_music, int64_t duration_ms) {
    (void)current_music;
    (void)new_music;
    (void)duration_ms;
    rt_audio_unavailable_("Music.CrossfadeTo: audio support not compiled in");
}

/// @brief Audio-disabled stub for `Music.IsCrossfading`.
/// @return `0` (never crossfading).
int8_t rt_music_is_crossfading(void) {
    return 0;
}

/// @brief Audio-disabled stub for `Music.CrossfadeUpdate`. Silent no-op.
/// @param dt_ms Ignored.
void rt_music_crossfade_update(int64_t dt_ms) {
    (void)dt_ms;
}

/// @brief Audio-disabled stub for `Sound.PlayInGroup`. Traps.
/// @param sound Ignored.
/// @param group Ignored.
/// @return Never returns normally.
int64_t rt_sound_play_in_group(void *sound, int64_t group) {
    (void)sound;
    (void)group;
    rt_audio_unavailable_("Sound.PlayInGroup: audio support not compiled in");
    return -1;
}

/// @brief Audio-disabled stub for `Sound.PlayExInGroup` (volume + pan +
///        mix-group routing variant of `Play`). Traps.
/// @param sound  Ignored.
/// @param volume Ignored.
/// @param pan    Ignored.
/// @param group  Ignored.
/// @return Never returns normally.
int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    (void)sound;
    (void)volume;
    (void)pan;
    (void)group;
    rt_audio_unavailable_("Sound.PlayExInGroup: audio support not compiled in");
    return -1;
}

/// @brief Audio-disabled stub for `Sound.PlayLoopInGroup` (looping
///        variant of `PlayExInGroup`). Traps.
/// @param sound  Ignored.
/// @param volume Ignored.
/// @param pan    Ignored.
/// @param group  Ignored.
/// @return Never returns normally.
int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan, int64_t group) {
    (void)sound;
    (void)volume;
    (void)pan;
    (void)group;
    rt_audio_unavailable_("Sound.PlayLoopInGroup: audio support not compiled in");
    return -1;
}

#endif /* VIPER_ENABLE_AUDIO */
