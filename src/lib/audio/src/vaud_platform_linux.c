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

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

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
typedef struct {
    snd_pcm_t *pcm;              ///< ALSA PCM device handle
    pthread_t thread;            ///< Audio thread
    int thread_started;          ///< pthread_create succeeded
    int16_t *mix_buffer;         ///< Preallocated audio thread mixing buffer
    volatile int running;        ///< Thread running flag
    volatile int paused;         ///< Pause state
    pthread_mutex_t pause_mutex; ///< Protects pause state
    pthread_cond_t pause_cond;   ///< Signal for pause/resume
} vaud_linux_data;

static int alsa_write_all(vaud_linux_data *plat, const int16_t *buffer, snd_pcm_uframes_t frames) {
    snd_pcm_uframes_t written_total = 0;

    while (__atomic_load_n(&plat->running, __ATOMIC_ACQUIRE) && written_total < frames) {
        const int16_t *cursor = buffer + ((size_t)written_total * VAUD_CHANNELS);
        snd_pcm_sframes_t written = snd_pcm_writei(plat->pcm, cursor, frames - written_total);

        if (written > 0) {
            written_total += (snd_pcm_uframes_t)written;
            continue;
        }

        if (written == 0)
            return 0;

        if (written == -EAGAIN)
            continue;

        if (written == -EPIPE || written == -ESTRPIPE) {
            int rc = snd_pcm_recover(plat->pcm, (int)written, 0);
            if (rc < 0)
                return 0;
            continue;
        }

        if (written < 0) {
            int rc = snd_pcm_recover(plat->pcm, (int)written, 0);
            if (rc < 0)
                return 0;
        }
    }

    return written_total == frames;
}

//===----------------------------------------------------------------------===//
// Audio Thread
//===----------------------------------------------------------------------===//

/// @brief Audio thread function - continuously mixes and outputs audio.
/// @param arg Pointer to our audio context.
/// @return NULL (never returns until shutdown).
static void *audio_thread_func(void *arg) {
    vaud_context_t ctx = (vaud_context_t)arg;
    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    int16_t *buffer = plat->mix_buffer;
    if (!buffer)
        return NULL;

    while (__atomic_load_n(&plat->running, __ATOMIC_ACQUIRE)) {
        /* Check for pause state */
        pthread_mutex_lock(&plat->pause_mutex);
        while (__atomic_load_n(&plat->paused, __ATOMIC_ACQUIRE) &&
               __atomic_load_n(&plat->running, __ATOMIC_ACQUIRE)) {
            pthread_cond_wait(&plat->pause_cond, &plat->pause_mutex);
        }
        pthread_mutex_unlock(&plat->pause_mutex);

        if (!__atomic_load_n(&plat->running, __ATOMIC_ACQUIRE))
            break;

        /* Render mixed audio */
        vaud_mixer_render(ctx, buffer, VAUD_BUFFER_FRAMES);

        /* Write the whole period; ALSA may legally accept only part of it. */
        (void)alsa_write_all(plat, buffer, VAUD_BUFFER_FRAMES);
    }

    return NULL;
}

//===----------------------------------------------------------------------===//
// Platform Interface Implementation
//===----------------------------------------------------------------------===//

int vaud_platform_init(vaud_context_t ctx) {
    if (!ctx)
        return 0;

    /* Allocate platform data */
    vaud_linux_data *plat = (vaud_linux_data *)calloc(1, sizeof(vaud_linux_data));
    if (!plat) {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate Linux audio data");
        return 0;
    }

    ctx->platform_data = plat;
    __atomic_store_n(&plat->running, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&plat->paused, 0, __ATOMIC_RELAXED);

    /* Initialize synchronization primitives */
    int err = pthread_mutex_init(&plat->pause_mutex, NULL);
    if (err != 0) {
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize ALSA pause mutex");
        return 0;
    }
    err = pthread_cond_init(&plat->pause_cond, NULL);
    if (err != 0) {
        pthread_mutex_destroy(&plat->pause_mutex);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize ALSA pause condition");
        return 0;
    }

    /* Open the default PCM device */
    err = snd_pcm_open(&plat->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
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

    if (err < 0) {
        snd_pcm_close(plat->pcm);
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to configure ALSA device");
        return 0;
    }

    plat->mix_buffer =
        (int16_t *)malloc(VAUD_BUFFER_FRAMES * VAUD_CHANNELS * sizeof(int16_t));
    if (!plat->mix_buffer) {
        snd_pcm_close(plat->pcm);
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate ALSA mix buffer");
        return 0;
    }

    /* Start the audio thread */
    __atomic_store_n(&plat->running, 1, __ATOMIC_RELEASE);
    err = pthread_create(&plat->thread, NULL, audio_thread_func, ctx);
    if (err != 0) {
        __atomic_store_n(&plat->running, 0, __ATOMIC_RELEASE);
        free(plat->mix_buffer);
        snd_pcm_close(plat->pcm);
        pthread_mutex_destroy(&plat->pause_mutex);
        pthread_cond_destroy(&plat->pause_cond);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create audio thread");
        return 0;
    }
    plat->thread_started = 1;

    return 1;
}

void vaud_platform_shutdown(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    /* Signal thread to stop */
    pthread_mutex_lock(&plat->pause_mutex);
    __atomic_store_n(&plat->running, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&plat->paused, 0, __ATOMIC_RELEASE); /* Unpause to allow thread to exit */
    pthread_cond_signal(&plat->pause_cond);
    pthread_mutex_unlock(&plat->pause_mutex);

    /* Abort any blocking snd_pcm_writei() so shutdown cannot hang behind ALSA. */
    snd_pcm_drop(plat->pcm);

    /* Wait for thread to finish */
    if (plat->thread_started)
        pthread_join(plat->thread, NULL);

    /* Close ALSA device */
    snd_pcm_close(plat->pcm);

    /* Clean up synchronization */
    pthread_mutex_destroy(&plat->pause_mutex);
    pthread_cond_destroy(&plat->pause_cond);

    free(plat->mix_buffer);
    free(plat);
    ctx->platform_data = NULL;
}

void vaud_platform_pause(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    int rc = snd_pcm_pause(plat->pcm, 1);
    if (rc < 0) {
        if (rc == -ENOSYS) {
            snd_pcm_drop(plat->pcm);
            snd_pcm_prepare(plat->pcm);
        } else if (snd_pcm_recover(plat->pcm, rc, 0) < 0) {
            snd_pcm_drop(plat->pcm);
            snd_pcm_prepare(plat->pcm);
        }
    }

    pthread_mutex_lock(&plat->pause_mutex);
    __atomic_store_n(&plat->paused, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&plat->pause_mutex);
}

void vaud_platform_resume(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_linux_data *plat = (vaud_linux_data *)ctx->platform_data;

    /* Resume ALSA playback */
    int rc = snd_pcm_pause(plat->pcm, 0);
    if (rc < 0) {
        if (rc == -ENOSYS) {
            snd_pcm_prepare(plat->pcm);
        } else if (snd_pcm_recover(plat->pcm, rc, 0) < 0) {
            snd_pcm_prepare(plat->pcm);
        }
    }

    pthread_mutex_lock(&plat->pause_mutex);
    __atomic_store_n(&plat->paused, 0, __ATOMIC_RELEASE);
    pthread_cond_signal(&plat->pause_cond);
    pthread_mutex_unlock(&plat->pause_mutex);
}

//===----------------------------------------------------------------------===//
// Timing
//===----------------------------------------------------------------------===//

int64_t vaud_platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#endif /* __linux__ */
