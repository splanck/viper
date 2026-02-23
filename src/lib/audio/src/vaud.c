//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Core Implementation
//
// Platform-agnostic implementation of the ViperAUD API. Provides audio context
// management, sound/music loading, and playback control. Platform-specific
// functionality is delegated to backend implementations.
//
// Key responsibilities:
// - Context lifecycle (create, destroy)
// - Sound effect loading and management
// - Music stream loading and management
// - Playback control (play, stop, volume, pan)
// - Thread synchronization for audio state
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Core implementation of the ViperAUD API.

#include "vaud_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Thread-Local Error State
//===----------------------------------------------------------------------===//

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Thread_local static const char *g_last_error = NULL;
_Thread_local static vaud_error_t g_last_error_code = VAUD_OK;
#elif defined(_WIN32)
__declspec(thread) static const char *g_last_error = NULL;
__declspec(thread) static vaud_error_t g_last_error_code = VAUD_OK;
#else
static const char *g_last_error = NULL;
static vaud_error_t g_last_error_code = VAUD_OK;
#endif

void vaud_set_error(vaud_error_t code, const char *msg)
{
    g_last_error_code = code;
    g_last_error = msg;
}

const char *vaud_get_last_error(void)
{
    return g_last_error;
}

void vaud_clear_error(void)
{
    g_last_error = NULL;
    g_last_error_code = VAUD_OK;
}

//===----------------------------------------------------------------------===//
// Threading Utilities
//===----------------------------------------------------------------------===//

#if defined(VAUD_PLATFORM_WINDOWS)

void vaud_mutex_init(vaud_mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
}

void vaud_mutex_destroy(vaud_mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
}

void vaud_mutex_lock(vaud_mutex_t *mutex)
{
    EnterCriticalSection(mutex);
}

void vaud_mutex_unlock(vaud_mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
}

#else /* POSIX */

void vaud_mutex_init(vaud_mutex_t *mutex)
{
    pthread_mutex_init(mutex, NULL);
}

void vaud_mutex_destroy(vaud_mutex_t *mutex)
{
    pthread_mutex_destroy(mutex);
}

void vaud_mutex_lock(vaud_mutex_t *mutex)
{
    pthread_mutex_lock(mutex);
}

void vaud_mutex_unlock(vaud_mutex_t *mutex)
{
    pthread_mutex_unlock(mutex);
}

#endif

//===----------------------------------------------------------------------===//
// Version Functions
//===----------------------------------------------------------------------===//

uint32_t vaud_version(void)
{
    return (VAUD_VERSION_MAJOR << 16) | (VAUD_VERSION_MINOR << 8) | VAUD_VERSION_PATCH;
}

const char *vaud_version_string(void)
{
    return "1.0.0";
}

//===----------------------------------------------------------------------===//
// Context Management
//===----------------------------------------------------------------------===//

vaud_context_t vaud_create(void)
{
    vaud_context_t ctx = (vaud_context_t)calloc(1, sizeof(struct vaud_context));
    if (!ctx)
    {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate audio context");
        return NULL;
    }

    /* Initialize state */
    ctx->master_volume = VAUD_DEFAULT_MASTER_VOLUME;
    ctx->next_voice_id = 1; /* Start at 1 so 0 is never valid */
    ctx->frame_counter = 0;
    ctx->running = 1;
    ctx->paused = 0;
    ctx->music_count = 0;

    /* Initialize all voices as inactive */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        ctx->voices[i].state = VAUD_VOICE_INACTIVE;
        ctx->voices[i].sound = NULL;
        ctx->voices[i].id = VAUD_INVALID_VOICE;
    }

    /* Initialize music slots */
    for (int32_t i = 0; i < VAUD_MAX_MUSIC; i++)
    {
        ctx->active_music[i] = NULL;
    }

    /* Initialize mutex */
    vaud_mutex_init(&ctx->mutex);

    /* Initialize platform backend */
    if (!vaud_platform_init(ctx))
    {
        vaud_mutex_destroy(&ctx->mutex);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void vaud_destroy(vaud_context_t ctx)
{
    if (!ctx)
        return;

    /* Stop running flag first */
    ctx->running = 0;

    /* Shutdown platform (stops audio thread) */
    vaud_platform_shutdown(ctx);

    /* Free all loaded sounds - voices reference sounds so they become invalid */
    /* Note: sounds track their own context, so we just clear voices here */
    vaud_mutex_lock(&ctx->mutex);
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        ctx->voices[i].state = VAUD_VOICE_INACTIVE;
        ctx->voices[i].sound = NULL;
    }

    /* Free all music streams */
    for (int32_t i = 0; i < ctx->music_count; i++)
    {
        if (ctx->active_music[i])
        {
            vaud_free_music(ctx->active_music[i]);
            ctx->active_music[i] = NULL;
        }
    }
    vaud_mutex_unlock(&ctx->mutex);

    vaud_mutex_destroy(&ctx->mutex);
    free(ctx);
}

void vaud_set_master_volume(vaud_context_t ctx, float volume)
{
    if (!ctx)
        return;
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;

    vaud_mutex_lock(&ctx->mutex);
    ctx->master_volume = volume;
    vaud_mutex_unlock(&ctx->mutex);
}

float vaud_get_master_volume(vaud_context_t ctx)
{
    if (!ctx)
        return 0.0f;
    /* H-3: setter holds mutex; reader must too (torn read on ARM64 otherwise) */
    vaud_mutex_lock(&ctx->mutex);
    float vol = ctx->master_volume;
    vaud_mutex_unlock(&ctx->mutex);
    return vol;
}

void vaud_pause_all(vaud_context_t ctx)
{
    if (!ctx)
        return;

    vaud_mutex_lock(&ctx->mutex);
    ctx->paused = 1;
    vaud_mutex_unlock(&ctx->mutex);

    vaud_platform_pause(ctx);
}

void vaud_resume_all(vaud_context_t ctx)
{
    if (!ctx)
        return;

    vaud_mutex_lock(&ctx->mutex);
    ctx->paused = 0;
    vaud_mutex_unlock(&ctx->mutex);

    vaud_platform_resume(ctx);
}

//===----------------------------------------------------------------------===//
// Sound Effect Loading
//===----------------------------------------------------------------------===//

vaud_sound_t vaud_load_sound(vaud_context_t ctx, const char *path)
{
    if (!ctx || !path)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL context or path");
        return NULL;
    }

    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;

    if (!vaud_wav_load_file(path, &samples, &frames, &sample_rate, &channels))
    {
        return NULL;
    }

    /* Resample if necessary */
    int16_t *final_samples = samples;
    int64_t final_frames = frames;

    if (sample_rate != VAUD_SAMPLE_RATE)
    {
        final_frames = vaud_resample_output_frames(frames, sample_rate, VAUD_SAMPLE_RATE);
        final_samples = (int16_t *)malloc((size_t)(final_frames * 2 * sizeof(int16_t)));

        if (!final_samples)
        {
            free(samples);
            vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate resampled buffer");
            return NULL;
        }

        vaud_resample(
            samples, frames, sample_rate, final_samples, final_frames, VAUD_SAMPLE_RATE, 2);
        free(samples);
    }

    /* Allocate sound structure */
    vaud_sound_t sound = (vaud_sound_t)malloc(sizeof(struct vaud_sound));
    if (!sound)
    {
        free(final_samples);
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate sound structure");
        return NULL;
    }

    sound->ctx = ctx;
    sound->samples = final_samples;
    sound->frame_count = final_frames;
    sound->sample_rate = sample_rate;
    sound->channels = channels;
    sound->default_volume = VAUD_DEFAULT_SOUND_VOLUME;

    return sound;
}

vaud_sound_t vaud_load_sound_mem(vaud_context_t ctx, const void *data, size_t size)
{
    if (!ctx || !data || size == 0)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL context or data");
        return NULL;
    }

    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;

    if (!vaud_wav_load_mem(data, size, &samples, &frames, &sample_rate, &channels))
    {
        return NULL;
    }

    /* Resample if necessary */
    int16_t *final_samples = samples;
    int64_t final_frames = frames;

    if (sample_rate != VAUD_SAMPLE_RATE)
    {
        final_frames = vaud_resample_output_frames(frames, sample_rate, VAUD_SAMPLE_RATE);
        final_samples = (int16_t *)malloc((size_t)(final_frames * 2 * sizeof(int16_t)));

        if (!final_samples)
        {
            free(samples);
            vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate resampled buffer");
            return NULL;
        }

        vaud_resample(
            samples, frames, sample_rate, final_samples, final_frames, VAUD_SAMPLE_RATE, 2);
        free(samples);
    }

    /* Allocate sound structure */
    vaud_sound_t sound = (vaud_sound_t)malloc(sizeof(struct vaud_sound));
    if (!sound)
    {
        free(final_samples);
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate sound structure");
        return NULL;
    }

    sound->ctx = ctx;
    sound->samples = final_samples;
    sound->frame_count = final_frames;
    sound->sample_rate = sample_rate;
    sound->channels = channels;
    sound->default_volume = VAUD_DEFAULT_SOUND_VOLUME;

    return sound;
}

void vaud_free_sound(vaud_sound_t sound)
{
    if (!sound)
        return;

    vaud_context_t ctx = sound->ctx;

    /* Stop any voices playing this sound */
    if (ctx)
    {
        vaud_mutex_lock(&ctx->mutex);
        for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
        {
            if (ctx->voices[i].sound == sound)
            {
                ctx->voices[i].state = VAUD_VOICE_INACTIVE;
                ctx->voices[i].sound = NULL;
            }
        }
        vaud_mutex_unlock(&ctx->mutex);
    }

    free(sound->samples);
    free(sound);
}

//===----------------------------------------------------------------------===//
// Sound Effect Playback
//===----------------------------------------------------------------------===//

vaud_voice_id vaud_play(vaud_sound_t sound)
{
    return vaud_play_ex(sound, VAUD_DEFAULT_SOUND_VOLUME, VAUD_DEFAULT_PAN);
}

vaud_voice_id vaud_play_ex(vaud_sound_t sound, float volume, float pan)
{
    if (!sound || !sound->ctx)
        return VAUD_INVALID_VOICE;

    vaud_context_t ctx = sound->ctx;

    vaud_mutex_lock(&ctx->mutex);

    vaud_voice *voice = vaud_alloc_voice(ctx);
    if (!voice)
    {
        vaud_mutex_unlock(&ctx->mutex);
        return VAUD_INVALID_VOICE;
    }

    voice->sound = sound;
    voice->position = 0;
    voice->volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    voice->pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
    voice->loop = 0;
    voice->state = VAUD_VOICE_PLAYING;

    vaud_voice_id id = voice->id;

    vaud_mutex_unlock(&ctx->mutex);

    return id;
}

vaud_voice_id vaud_play_loop(vaud_sound_t sound, float volume, float pan)
{
    if (!sound || !sound->ctx)
        return VAUD_INVALID_VOICE;

    vaud_context_t ctx = sound->ctx;

    vaud_mutex_lock(&ctx->mutex);

    vaud_voice *voice = vaud_alloc_voice(ctx);
    if (!voice)
    {
        vaud_mutex_unlock(&ctx->mutex);
        return VAUD_INVALID_VOICE;
    }

    voice->sound = sound;
    voice->position = 0;
    voice->volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    voice->pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
    voice->loop = 1;
    voice->state = VAUD_VOICE_PLAYING;

    vaud_voice_id id = voice->id;

    vaud_mutex_unlock(&ctx->mutex);

    return id;
}

void vaud_stop_voice(vaud_context_t ctx, vaud_voice_id voice_id)
{
    if (!ctx || voice_id == VAUD_INVALID_VOICE)
        return;

    vaud_mutex_lock(&ctx->mutex);
    vaud_voice *voice = vaud_find_voice(ctx, voice_id);
    if (voice)
    {
        voice->state = VAUD_VOICE_INACTIVE;
        voice->sound = NULL;
    }
    vaud_mutex_unlock(&ctx->mutex);
}

void vaud_set_voice_volume(vaud_context_t ctx, vaud_voice_id voice_id, float volume)
{
    if (!ctx || voice_id == VAUD_INVALID_VOICE)
        return;

    vaud_mutex_lock(&ctx->mutex);
    vaud_voice *voice = vaud_find_voice(ctx, voice_id);
    if (voice)
    {
        voice->volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    }
    vaud_mutex_unlock(&ctx->mutex);
}

void vaud_set_voice_pan(vaud_context_t ctx, vaud_voice_id voice_id, float pan)
{
    if (!ctx || voice_id == VAUD_INVALID_VOICE)
        return;

    vaud_mutex_lock(&ctx->mutex);
    vaud_voice *voice = vaud_find_voice(ctx, voice_id);
    if (voice)
    {
        voice->pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
    }
    vaud_mutex_unlock(&ctx->mutex);
}

int vaud_voice_is_playing(vaud_context_t ctx, vaud_voice_id voice_id)
{
    if (!ctx || voice_id == VAUD_INVALID_VOICE)
        return 0;

    vaud_mutex_lock(&ctx->mutex);
    vaud_voice *voice = vaud_find_voice(ctx, voice_id);
    int playing = (voice && voice->state == VAUD_VOICE_PLAYING) ? 1 : 0;
    vaud_mutex_unlock(&ctx->mutex);

    return playing;
}

//===----------------------------------------------------------------------===//
// Music Loading and Playback
//===----------------------------------------------------------------------===//

vaud_music_t vaud_load_music(vaud_context_t ctx, const char *path)
{
    if (!ctx || !path)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL context or path");
        return NULL;
    }

    void *file = NULL;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;

    if (!vaud_wav_open_stream(
            path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits))
    {
        return NULL;
    }

    vaud_music_t music = (vaud_music_t)calloc(1, sizeof(struct vaud_music));
    if (!music)
    {
        fclose((FILE *)file);
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate music structure");
        return NULL;
    }

    /* Allocate streaming buffers */
    for (int32_t i = 0; i < VAUD_MUSIC_BUFFER_COUNT; i++)
    {
        music->buffers[i] = (int16_t *)malloc(VAUD_MUSIC_BUFFER_FRAMES * 2 * sizeof(int16_t));
        if (!music->buffers[i])
        {
            for (int32_t j = 0; j < i; j++)
                free(music->buffers[j]);
            free(music);
            fclose((FILE *)file);
            vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate music buffer");
            return NULL;
        }
        music->buffer_frames[i] = 0;
    }

    music->ctx = ctx;
    music->file = file;
    music->data_offset = data_offset;
    music->data_size = data_size;
    music->frame_count = frames;
    music->sample_rate = sample_rate;
    music->channels = channels;
    music->bits_per_sample = bits;
    music->state = VAUD_MUSIC_STOPPED;
    music->position = 0;
    music->loop = 0;
    music->volume = VAUD_DEFAULT_MUSIC_VOLUME;
    music->current_buffer = 0;
    music->buffer_position = 0;

    /* Pre-fill first buffer */
    int32_t read =
        vaud_wav_read_frames(file, music->buffers[0], VAUD_MUSIC_BUFFER_FRAMES, channels, bits);
    music->buffer_frames[0] = read;

    /* Add to context's music list (H-4: return NULL and free if list is full) */
    vaud_mutex_lock(&ctx->mutex);
    if (ctx->music_count < VAUD_MAX_MUSIC)
    {
        ctx->active_music[ctx->music_count++] = music;
        vaud_mutex_unlock(&ctx->mutex);
    }
    else
    {
        vaud_mutex_unlock(&ctx->mutex);
        /* Music was never added to the active list — vaud_free_music's
         * remove loop is a safe no-op, then it closes the file and frees buffers. */
        vaud_free_music(music);
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "Maximum simultaneous music streams reached");
        return NULL;
    }

    return music;
}

void vaud_free_music(vaud_music_t music)
{
    if (!music)
        return;

    vaud_context_t ctx = music->ctx;

    /* Remove from context's music list */
    if (ctx)
    {
        vaud_mutex_lock(&ctx->mutex);
        for (int32_t i = 0; i < ctx->music_count; i++)
        {
            if (ctx->active_music[i] == music)
            {
                /* Shift remaining entries */
                for (int32_t j = i; j < ctx->music_count - 1; j++)
                {
                    ctx->active_music[j] = ctx->active_music[j + 1];
                }
                ctx->music_count--;
                ctx->active_music[ctx->music_count] = NULL;
                break;
            }
        }
        vaud_mutex_unlock(&ctx->mutex);
    }

    /* Close file */
    if (music->file)
    {
        fclose((FILE *)music->file);
    }

    /* Free buffers */
    for (int32_t i = 0; i < VAUD_MUSIC_BUFFER_COUNT; i++)
    {
        free(music->buffers[i]);
    }

    free(music);
}

void vaud_music_play(vaud_music_t music, int loop)
{
    if (!music || !music->ctx)
        return;

    vaud_mutex_lock(&music->ctx->mutex);

    music->loop = loop ? 1 : 0;
    music->state = VAUD_MUSIC_PLAYING;

    /* Seek to beginning if stopped */
    if (music->position == 0 && music->file)
    {
        fseek((FILE *)music->file, (long)music->data_offset, SEEK_SET);
        music->current_buffer = 0;
        music->buffer_position = 0;

        /* Pre-fill first buffer */
        int32_t read = vaud_wav_read_frames(music->file,
                                            music->buffers[0],
                                            VAUD_MUSIC_BUFFER_FRAMES,
                                            music->channels,
                                            music->bits_per_sample);
        music->buffer_frames[0] = read;
    }

    vaud_mutex_unlock(&music->ctx->mutex);
}

void vaud_music_stop(vaud_music_t music)
{
    if (!music || !music->ctx)
        return;

    vaud_mutex_lock(&music->ctx->mutex);

    music->state = VAUD_MUSIC_STOPPED;
    music->position = 0;
    music->current_buffer = 0;
    music->buffer_position = 0;

    /* Seek to beginning */
    if (music->file)
    {
        fseek((FILE *)music->file, (long)music->data_offset, SEEK_SET);
    }

    vaud_mutex_unlock(&music->ctx->mutex);
}

void vaud_music_pause(vaud_music_t music)
{
    if (!music || !music->ctx)
        return;

    vaud_mutex_lock(&music->ctx->mutex);
    if (music->state == VAUD_MUSIC_PLAYING)
    {
        music->state = VAUD_MUSIC_PAUSED;
    }
    vaud_mutex_unlock(&music->ctx->mutex);
}

void vaud_music_resume(vaud_music_t music)
{
    if (!music || !music->ctx)
        return;

    vaud_mutex_lock(&music->ctx->mutex);
    if (music->state == VAUD_MUSIC_PAUSED)
    {
        music->state = VAUD_MUSIC_PLAYING;
    }
    vaud_mutex_unlock(&music->ctx->mutex);
}

void vaud_music_set_volume(vaud_music_t music, float volume)
{
    if (!music)
        return;

    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;

    if (music->ctx)
    {
        vaud_mutex_lock(&music->ctx->mutex);
        music->volume = volume;
        vaud_mutex_unlock(&music->ctx->mutex);
    }
    else
    {
        music->volume = volume;
    }
}

float vaud_music_get_volume(vaud_music_t music)
{
    if (!music)
        return 0.0f;
    /* H-3: read volume under mutex (setter holds it; torn read possible on ARM64) */
    if (music->ctx)
    {
        vaud_mutex_lock(&music->ctx->mutex);
        float vol = music->volume;
        vaud_mutex_unlock(&music->ctx->mutex);
        return vol;
    }
    return music->volume;
}

int vaud_music_is_playing(vaud_music_t music)
{
    if (!music)
        return 0;
    return (music->state == VAUD_MUSIC_PLAYING) ? 1 : 0;
}

void vaud_music_seek(vaud_music_t music, float seconds)
{
    if (!music || !music->ctx || !music->file)
        return;

    vaud_mutex_lock(&music->ctx->mutex);

    int64_t target_frame = (int64_t)(seconds * music->sample_rate);
    if (target_frame < 0)
        target_frame = 0;
    if (target_frame >= music->frame_count)
        target_frame = music->frame_count - 1;

    int32_t bytes_per_frame = (music->bits_per_sample / 8) * music->channels;
    int64_t byte_offset = music->data_offset + target_frame * bytes_per_frame;

    fseek((FILE *)music->file, (long)byte_offset, SEEK_SET);
    music->position = target_frame;
    music->current_buffer = 0;
    music->buffer_position = 0;

    /* Refill buffer */
    int32_t read = vaud_wav_read_frames(music->file,
                                        music->buffers[0],
                                        VAUD_MUSIC_BUFFER_FRAMES,
                                        music->channels,
                                        music->bits_per_sample);
    music->buffer_frames[0] = read;

    vaud_mutex_unlock(&music->ctx->mutex);
}

float vaud_music_get_position(vaud_music_t music)
{
    if (!music)
        return 0.0f;
    return (float)music->position / (float)music->sample_rate;
}

float vaud_music_get_duration(vaud_music_t music)
{
    if (!music)
        return 0.0f;
    return (float)music->frame_count / (float)music->sample_rate;
}

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

int32_t vaud_get_active_voice_count(vaud_context_t ctx)
{
    if (!ctx)
        return 0;

    int32_t count = 0;
    vaud_mutex_lock(&ctx->mutex);
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        if (ctx->voices[i].state == VAUD_VOICE_PLAYING)
        {
            count++;
        }
    }
    vaud_mutex_unlock(&ctx->mutex);

    return count;
}

void vaud_stop_all_sounds(vaud_context_t ctx)
{
    if (!ctx)
        return;

    vaud_mutex_lock(&ctx->mutex);
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        ctx->voices[i].state = VAUD_VOICE_INACTIVE;
        ctx->voices[i].sound = NULL;
    }
    vaud_mutex_unlock(&ctx->mutex);
}

float vaud_get_latency_ms(vaud_context_t ctx)
{
    if (!ctx)
        return 0.0f;

    /* Latency is approximately buffer size in frames / sample rate */
    return (float)VAUD_BUFFER_FRAMES / (float)VAUD_SAMPLE_RATE * 1000.0f;
}
