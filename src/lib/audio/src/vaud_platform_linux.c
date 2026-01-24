//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Linux Platform Backend
//
// Implements audio output using ALSA (Advanced Linux Sound Architecture).
// ALSA is the standard low-level audio API on Linux, available on all
// distributions as part of the kernel.
//
// Key concepts:
// - snd_pcm_t: PCM device handle for audio output
// - snd_pcm_writei: Interleaved write to PCM device
// - Dedicated thread: Continuously fills audio buffer in a loop
//
// Thread model:
// - We create a dedicated audio thread that loops, mixing and writing
// - The mixer is thread-safe, called from the audio thread
// - ALSA's snd_pcm_writei blocks until buffer space is available
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Linux ALSA audio backend for ViperAUD.

#if defined(__linux__)

#include "vaud_internal.h"
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Linux ALSA platform data.
typedef struct
{
    snd_pcm_t *pcm;              ///< ALSA PCM device handle
    pthread_t thread;            ///< Audio thread
    int running;                 ///< Thread running flag
    int paused;                  ///< Pause state
    pthread_mutex_t pause_mutex; ///< Protects pause state
    pthread_cond_t pause_cond;   ///< Signal for pause/resume
} vaud_linux_data;

//===----------------------------------------------------------------------===//
// Audio Thread
//===----------------------------------------------------------------------===//

/// @brief Audio thread function - continuously mixes and outputs audio.
/// @param arg Pointer to our audio context.
/// @return NULL (never returns until shutdown).
static void *audio_thread_func(void *arg)
{
    vaud_context_t ctx = (vaud_context_t)arg;
    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    /* Allocate buffer for mixing */
    int16_t *buffer = (int16_t *)malloc(VAUD_BUFFER_FRAMES * VAUD_CHANNELS * sizeof(int16_t));
    if (!buffer)
    {
        return NULL;
    }

    while (plat->running)
    {
        /* Check for pause state */
        pthread_mutex_lock(&plat->pause_mutex);
        while (plat->paused && plat->running)
        {
            pthread_cond_wait(&plat->pause_cond, &plat->pause_mutex);
        }
        pthread_mutex_unlock(&plat->pause_mutex);

        if (!plat->running)
            break;

        /* Render mixed audio */
        vaud_mixer_render(ctx, buffer, VAUD_BUFFER_FRAMES);

        /* Write to ALSA device */
        snd_pcm_sframes_t frames_written = snd_pcm_writei(plat->pcm, buffer, VAUD_BUFFER_FRAMES);

        if (frames_written < 0)
        {
            /* Handle underrun or other errors */
            if (frames_written == -EPIPE)
            {
                /* Underrun occurred - recover */
                snd_pcm_prepare(plat->pcm);
            }
            else if (frames_written == -EAGAIN)
            {
                /* Try again */
                continue;
            }
            else
            {
                /* Other error - try to recover */
                snd_pcm_recover(plat->pcm, (int)frames_written, 0);
            }
        }
    }

    free(buffer);
    return NULL;
}

//===----------------------------------------------------------------------===//
// Platform Interface Implementation
//===----------------------------------------------------------------------===//

int vaud_platform_init(vaud_context_t ctx)
{
    if (!ctx)
        return 0;

    /* Allocate platform data */
    vaud_linux_data *plat = (vaud_linux_data *)calloc(1, sizeof(vaud_linux_data));
    if (!plat)
    {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate Linux audio data");
        return 0;
    }

    ctx->platform_data = plat;
    plat->running = 0;
    plat->paused = 0;

    /* Initialize synchronization primitives */
    pthread_mutex_init(&plat->pause_mutex, NULL);
    pthread_cond_init(&plat->pause_cond, NULL);

    /* Open the default PCM device */
    int err = snd_pcm_open(&plat->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to open ALSA device");
        return 0;
    }

    /* Configure PCM parameters */
    err = snd_pcm_set_params(plat->pcm,
                             SND_PCM_FORMAT_S16_LE,         /* 16-bit signed little-endian */
                             SND_PCM_ACCESS_RW_INTERLEAVED, /* Interleaved channels */
                             VAUD_CHANNELS,                 /* Stereo */
                             VAUD_SAMPLE_RATE,              /* 44100 Hz */
                             1,                             /* Allow resampling */
                             50000                          /* Latency: 50ms in microseconds */
    );

    if (err < 0)
    {
        snd_pcm_close(plat->pcm);
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to configure ALSA device");
        return 0;
    }

    /* Start the audio thread */
    plat->running = 1;
    err = pthread_create(&plat->thread, NULL, audio_thread_func, ctx);
    if (err != 0)
    {
        plat->running = 0;
        snd_pcm_close(plat->pcm);
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create audio thread");
        return 0;
    }

    return 1;
}

void vaud_platform_shutdown(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    /* Signal thread to stop */
    pthread_mutex_lock(&plat->pause_mutex);
    plat->running = 0;
    plat->paused = 0; /* Unpause to allow thread to exit */
    pthread_cond_signal(&plat->pause_cond);
    pthread_mutex_unlock(&plat->pause_mutex);

    /* Wait for thread to finish */
    pthread_join(plat->thread, NULL);

    /* Close ALSA device */
    snd_pcm_drain(plat->pcm);
    snd_pcm_close(plat->pcm);

    /* Clean up synchronization */
    pthread_mutex_destroy(&plat->pause_mutex);
    pthread_cond_destroy(&plat->pause_cond);

    free(plat);
    ctx->platform_data = NULL;
}

void vaud_platform_pause(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    pthread_mutex_lock(&plat->pause_mutex);
    plat->paused = 1;
    pthread_mutex_unlock(&plat->pause_mutex);

    /* Pause ALSA playback */
    snd_pcm_pause(plat->pcm, 1);
}

void vaud_platform_resume(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    /* Resume ALSA playback */
    snd_pcm_pause(plat->pcm, 0);

    pthread_mutex_lock(&plat->pause_mutex);
    plat->paused = 0;
    pthread_cond_signal(&plat->pause_cond);
    pthread_mutex_unlock(&plat->pause_mutex);
}

//===----------------------------------------------------------------------===//
// Timing
//===----------------------------------------------------------------------===//

int64_t vaud_platform_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#endif /* __linux__ */
