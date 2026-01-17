//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_audio.c
// Purpose: Runtime bridge functions for ViperAUD audio library.
// Key invariants: All functions check for NULL handles.
// Ownership/Lifetime: Audio context is global; sounds/music are ref-counted.
// Links: src/lib/audio/include/vaud.h
//
//===----------------------------------------------------------------------===//

#include "rt_audio.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
static volatile int g_audio_init_lock = 0;

//===----------------------------------------------------------------------===//
// Sound Wrapper Structure
//===----------------------------------------------------------------------===//

/// @brief Internal sound wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vaud sound handle.
typedef struct
{
    void *vptr;          ///< VTable pointer (reserved for future use)
    vaud_sound_t sound;  ///< ViperAUD sound handle
} rt_sound;

/// @brief Finalizer for sound objects.
static void rt_sound_finalize(void *obj)
{
    if (!obj)
        return;

    rt_sound *snd = (rt_sound *)obj;
    if (snd->sound)
    {
        vaud_free_sound(snd->sound);
        snd->sound = NULL;
    }
}

//===----------------------------------------------------------------------===//
// Music Wrapper Structure
//===----------------------------------------------------------------------===//

/// @brief Internal music wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vaud music handle.
typedef struct
{
    void *vptr;          ///< VTable pointer (reserved for future use)
    vaud_music_t music;  ///< ViperAUD music handle
} rt_music;

/// @brief Finalizer for music objects.
static void rt_music_finalize(void *obj)
{
    if (!obj)
        return;

    rt_music *mus = (rt_music *)obj;
    if (mus->music)
    {
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
static int ensure_audio_init(void)
{
    // Fast path: already initialized (use acquire to sync with the release below)
#if RT_COMPILER_MSVC
    int state = rt_atomic_load_i32(&g_audio_initialized, __ATOMIC_ACQUIRE);
#else
    int state = __atomic_load_n(&g_audio_initialized, __ATOMIC_ACQUIRE);
#endif
    if (state != 0)
        return state > 0;

    // Slow path: acquire spinlock and double-check
    while (__atomic_test_and_set(&g_audio_init_lock, __ATOMIC_ACQUIRE))
    {
        // Spin until we acquire the lock
    }

    // Double-check under lock
#if RT_COMPILER_MSVC
    state = rt_atomic_load_i32(&g_audio_initialized, __ATOMIC_RELAXED);
#else
    state = __atomic_load_n(&g_audio_initialized, __ATOMIC_RELAXED);
#endif
    if (state != 0)
    {
        // Another thread already initialized - release lock and return
        __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
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

    __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
    return g_audio_ctx != NULL;
}

int64_t rt_audio_init(void)
{
    return ensure_audio_init() ? 1 : 0;
}

void rt_audio_shutdown(void)
{
    // Acquire lock to ensure exclusive access during shutdown
    while (__atomic_test_and_set(&g_audio_init_lock, __ATOMIC_ACQUIRE))
    {
        // Spin until we acquire the lock
    }

    if (g_audio_ctx)
    {
        vaud_destroy(g_audio_ctx);
        g_audio_ctx = NULL;
    }

    // Reset state to allow re-initialization
#if RT_COMPILER_MSVC
    rt_atomic_store_i32(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&g_audio_initialized, 0, __ATOMIC_RELEASE);
#endif

    __atomic_clear(&g_audio_init_lock, __ATOMIC_RELEASE);
}

void rt_audio_set_master_volume(int64_t volume)
{
    if (!ensure_audio_init())
        return;

    /* Clamp to 0-100 range and convert to 0.0-1.0 */
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    vaud_set_master_volume(g_audio_ctx, (float)volume / 100.0f);
}

int64_t rt_audio_get_master_volume(void)
{
    if (!g_audio_ctx)
        return 0;

    float vol = vaud_get_master_volume(g_audio_ctx);
    return (int64_t)(vol * 100.0f + 0.5f);
}

void rt_audio_pause_all(void)
{
    if (g_audio_ctx)
        vaud_pause_all(g_audio_ctx);
}

void rt_audio_resume_all(void)
{
    if (g_audio_ctx)
        vaud_resume_all(g_audio_ctx);
}

void rt_audio_stop_all_sounds(void)
{
    if (g_audio_ctx)
        vaud_stop_all_sounds(g_audio_ctx);
}

//===----------------------------------------------------------------------===//
// Sound Effects
//===----------------------------------------------------------------------===//

void *rt_sound_load(rt_string path)
{
    if (!path)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
        return NULL;

    /* Load the sound via ViperAUD */
    vaud_sound_t snd = vaud_load_sound(g_audio_ctx, path_str);
    if (!snd)
        return NULL;

    /* Allocate wrapper object */
    rt_sound *wrapper = (rt_sound *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sound));
    if (!wrapper)
    {
        vaud_free_sound(snd);
        return NULL;
    }

    wrapper->vptr = NULL;
    wrapper->sound = snd;
    rt_obj_set_finalizer(wrapper, rt_sound_finalize);

    return wrapper;
}

void rt_sound_free(void *sound)
{
    if (!sound)
        return;

    if (rt_obj_release_check0(sound))
        rt_obj_free(sound);
}

int64_t rt_sound_play(void *sound)
{
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    vaud_voice_id voice = vaud_play(snd->sound);
    return (int64_t)voice;
}

int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan)
{
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    /* Convert from 0-100 to 0.0-1.0 */
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    float vol = (float)volume / 100.0f;

    /* Convert from -100 to 100 to -1.0 to 1.0 */
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;
    float p = (float)pan / 100.0f;

    vaud_voice_id voice = vaud_play_ex(snd->sound, vol, p);
    return (int64_t)voice;
}

int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan)
{
    if (!sound)
        return -1;

    rt_sound *snd = (rt_sound *)sound;
    if (!snd->sound)
        return -1;

    /* Convert from 0-100 to 0.0-1.0 */
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    float vol = (float)volume / 100.0f;

    /* Convert from -100 to 100 to -1.0 to 1.0 */
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;
    float p = (float)pan / 100.0f;

    vaud_voice_id voice = vaud_play_loop(snd->sound, vol, p);
    return (int64_t)voice;
}

void rt_voice_stop(int64_t voice_id)
{
    if (!g_audio_ctx || voice_id < 0)
        return;

    vaud_stop_voice(g_audio_ctx, (vaud_voice_id)voice_id);
}

void rt_voice_set_volume(int64_t voice_id, int64_t volume)
{
    if (!g_audio_ctx || voice_id < 0)
        return;

    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    float vol = (float)volume / 100.0f;

    vaud_set_voice_volume(g_audio_ctx, (vaud_voice_id)voice_id, vol);
}

void rt_voice_set_pan(int64_t voice_id, int64_t pan)
{
    if (!g_audio_ctx || voice_id < 0)
        return;

    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;
    float p = (float)pan / 100.0f;

    vaud_set_voice_pan(g_audio_ctx, (vaud_voice_id)voice_id, p);
}

int64_t rt_voice_is_playing(int64_t voice_id)
{
    if (!g_audio_ctx || voice_id < 0)
        return 0;

    return vaud_voice_is_playing(g_audio_ctx, (vaud_voice_id)voice_id) ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// Music Streaming
//===----------------------------------------------------------------------===//

void *rt_music_load(rt_string path)
{
    if (!path)
        return NULL;

    if (!ensure_audio_init())
        return NULL;

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
        return NULL;

    /* Load the music via ViperAUD */
    vaud_music_t mus = vaud_load_music(g_audio_ctx, path_str);
    if (!mus)
        return NULL;

    /* Allocate wrapper object */
    rt_music *wrapper = (rt_music *)rt_obj_new_i64(0, (int64_t)sizeof(rt_music));
    if (!wrapper)
    {
        vaud_free_music(mus);
        return NULL;
    }

    wrapper->vptr = NULL;
    wrapper->music = mus;
    rt_obj_set_finalizer(wrapper, rt_music_finalize);

    return wrapper;
}

void rt_music_free(void *music)
{
    if (!music)
        return;

    if (rt_obj_release_check0(music))
        rt_obj_free(music);
}

void rt_music_play(void *music, int64_t loop)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    vaud_music_play(mus->music, loop ? 1 : 0);
}

void rt_music_stop(void *music)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_stop(mus->music);
}

void rt_music_pause(void *music)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_pause(mus->music);
}

void rt_music_resume(void *music)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (mus->music)
        vaud_music_resume(mus->music);
}

void rt_music_set_volume(void *music, int64_t volume)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    float vol = (float)volume / 100.0f;

    vaud_music_set_volume(mus->music, vol);
}

int64_t rt_music_get_volume(void *music)
{
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float vol = vaud_music_get_volume(mus->music);
    return (int64_t)(vol * 100.0f + 0.5f);
}

int64_t rt_music_is_playing(void *music)
{
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    return vaud_music_is_playing(mus->music) ? 1 : 0;
}

void rt_music_seek(void *music, int64_t position_ms)
{
    if (!music)
        return;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return;

    if (position_ms < 0) position_ms = 0;
    float seconds = (float)position_ms / 1000.0f;

    vaud_music_seek(mus->music, seconds);
}

int64_t rt_music_get_position(void *music)
{
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float seconds = vaud_music_get_position(mus->music);
    return (int64_t)(seconds * 1000.0f + 0.5f);
}

int64_t rt_music_get_duration(void *music)
{
    if (!music)
        return 0;

    rt_music *mus = (rt_music *)music;
    if (!mus->music)
        return 0;

    float seconds = vaud_music_get_duration(mus->music);
    return (int64_t)(seconds * 1000.0f + 0.5f);
}

#else /* !VIPER_ENABLE_AUDIO */

//===----------------------------------------------------------------------===//
// Stub implementations when audio library is not available
//===----------------------------------------------------------------------===//

int64_t rt_audio_init(void)
{
    return 0;
}

void rt_audio_shutdown(void)
{
}

void rt_audio_set_master_volume(int64_t volume)
{
    (void)volume;
}

int64_t rt_audio_get_master_volume(void)
{
    return 0;
}

void rt_audio_pause_all(void)
{
}

void rt_audio_resume_all(void)
{
}

void rt_audio_stop_all_sounds(void)
{
}

void *rt_sound_load(rt_string path)
{
    (void)path;
    return NULL;
}

void rt_sound_free(void *sound)
{
    (void)sound;
}

int64_t rt_sound_play(void *sound)
{
    (void)sound;
    return -1;
}

int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan)
{
    (void)sound;
    (void)volume;
    (void)pan;
    return -1;
}

int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan)
{
    (void)sound;
    (void)volume;
    (void)pan;
    return -1;
}

void rt_voice_stop(int64_t voice_id)
{
    (void)voice_id;
}

void rt_voice_set_volume(int64_t voice_id, int64_t volume)
{
    (void)voice_id;
    (void)volume;
}

void rt_voice_set_pan(int64_t voice_id, int64_t pan)
{
    (void)voice_id;
    (void)pan;
}

int64_t rt_voice_is_playing(int64_t voice_id)
{
    (void)voice_id;
    return 0;
}

void *rt_music_load(rt_string path)
{
    (void)path;
    return NULL;
}

void rt_music_free(void *music)
{
    (void)music;
}

void rt_music_play(void *music, int64_t loop)
{
    (void)music;
    (void)loop;
}

void rt_music_stop(void *music)
{
    (void)music;
}

void rt_music_pause(void *music)
{
    (void)music;
}

void rt_music_resume(void *music)
{
    (void)music;
}

void rt_music_set_volume(void *music, int64_t volume)
{
    (void)music;
    (void)volume;
}

int64_t rt_music_get_volume(void *music)
{
    (void)music;
    return 0;
}

int64_t rt_music_is_playing(void *music)
{
    (void)music;
    return 0;
}

void rt_music_seek(void *music, int64_t position_ms)
{
    (void)music;
    (void)position_ms;
}

int64_t rt_music_get_position(void *music)
{
    (void)music;
    return 0;
}

int64_t rt_music_get_duration(void *music)
{
    (void)music;
    return 0;
}

#endif /* VIPER_ENABLE_AUDIO */
