//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Windows Platform Backend
//
// Implements audio output using WASAPI (Windows Audio Session API).
// WASAPI is the modern low-level audio API on Windows (Vista and later),
// providing low-latency audio output with exclusive or shared mode.
//
// Key concepts:
// - IMMDevice: Audio endpoint device (speakers)
// - IAudioClient: Audio stream management
// - IAudioRenderClient: Buffer access for writing audio data
// - Event-driven: We wait on an event signaled when buffer space is available
//
// Thread model:
// - We create a dedicated audio thread that waits for buffer events
// - When signaled, we fill available buffer space with mixed audio
// - The mixer is thread-safe, called from the audio thread
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Windows WASAPI audio backend for ViperAUD.

#if defined(_WIN32)

#include "vaud_internal.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <audioclient.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

//===----------------------------------------------------------------------===//
// COM GUIDs
//===----------------------------------------------------------------------===//

/* Define GUIDs we need (to avoid linking with uuid.lib) */
static const GUID VAUD_CLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};

static const GUID VAUD_IID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};

static const GUID VAUD_IID_IAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};

static const GUID VAUD_IID_IAudioRenderClient = {
    0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};

static const GUID VAUD_KSDATAFORMAT_SUBTYPE_PCM = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static const GUID VAUD_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

typedef enum {
    VAUD_WIN32_SAMPLE_S16 = 0,
    VAUD_WIN32_SAMPLE_S24,
    VAUD_WIN32_SAMPLE_S32,
    VAUD_WIN32_SAMPLE_F32
} vaud_win32_sample_format;

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Windows WASAPI platform data.
typedef struct {
    IMMDevice *device;          ///< Audio endpoint device
    IAudioClient *client;       ///< Audio client interface
    IAudioRenderClient *render; ///< Render client for buffer access
    HANDLE thread;              ///< Audio thread handle
    HANDLE event;               ///< Buffer event
    HANDLE stop_event;          ///< Stop signal event
    WAVEFORMATEX *format;       ///< Negotiated WASAPI render format
    int16_t *mix_buffer;        ///< Internal stereo mix buffer for format conversion
    UINT32 buffer_frames;       ///< Total buffer size in frames
    UINT32 render_channels;     ///< Channels in negotiated format
    UINT32 render_sample_rate;  ///< Sample rate in negotiated format
    UINT32 render_block_align;  ///< Bytes per rendered frame
    UINT32 render_bytes_sample; ///< Bytes per channel sample
    vaud_win32_sample_format render_sample_format; ///< Sample representation
    volatile LONG running;      ///< Thread running flag
    volatile LONG paused;       ///< Pause state
    int com_initialized;        ///< This backend owns a COM apartment reference.
    CRITICAL_SECTION pause_cs;  ///< Protects pause state
} vaud_win32_data;

static int vaud_win32_guid_equal(const GUID *a, const GUID *b) {
    return a && b && memcmp(a, b, sizeof(GUID)) == 0;
}

static int vaud_win32_format_subtype(const WAVEFORMATEX *fmt,
                                     const GUID **out_subtype,
                                     WORD *out_valid_bits) {
    if (!fmt || !out_subtype || !out_valid_bits)
        return 0;

    *out_subtype = NULL;
    *out_valid_bits = fmt->wBitsPerSample;

    if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
        *out_subtype = &VAUD_KSDATAFORMAT_SUBTYPE_PCM;
        return 1;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        *out_subtype = &VAUD_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        return 1;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)fmt;
        *out_valid_bits = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample
                                                           : fmt->wBitsPerSample;
        *out_subtype = &ext->SubFormat;
        return 1;
    }
    return 0;
}

static int vaud_win32_configure_render_format(vaud_win32_data *plat, const WAVEFORMATEX *fmt) {
    if (!plat || !fmt || fmt->nChannels == 0 || fmt->nChannels > 8 || fmt->nSamplesPerSec == 0 ||
        fmt->nBlockAlign == 0 || fmt->wBitsPerSample == 0 || (fmt->wBitsPerSample % 8) != 0) {
        return 0;
    }

    const GUID *subtype = NULL;
    WORD valid_bits = 0;
    if (!vaud_win32_format_subtype(fmt, &subtype, &valid_bits))
        return 0;

    UINT32 bytes_per_sample = fmt->wBitsPerSample / 8u;
    if (bytes_per_sample == 0 || bytes_per_sample > 4)
        return 0;
    if (fmt->nBlockAlign < fmt->nChannels * bytes_per_sample)
        return 0;

    vaud_win32_sample_format sample_format;
    if (vaud_win32_guid_equal(subtype, &VAUD_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
        fmt->wBitsPerSample == 32) {
        sample_format = VAUD_WIN32_SAMPLE_F32;
    } else if (vaud_win32_guid_equal(subtype, &VAUD_KSDATAFORMAT_SUBTYPE_PCM)) {
        if (fmt->wBitsPerSample == 16) {
            sample_format = VAUD_WIN32_SAMPLE_S16;
        } else if (fmt->wBitsPerSample == 24) {
            sample_format = VAUD_WIN32_SAMPLE_S24;
        } else if (fmt->wBitsPerSample == 32 && valid_bits <= 32) {
            sample_format = VAUD_WIN32_SAMPLE_S32;
        } else {
            return 0;
        }
    } else {
        return 0;
    }

    plat->render_channels = fmt->nChannels;
    plat->render_sample_rate = fmt->nSamplesPerSec;
    plat->render_block_align = fmt->nBlockAlign;
    plat->render_bytes_sample = bytes_per_sample;
    plat->render_sample_format = sample_format;
    return 1;
}

static WAVEFORMATEX *vaud_win32_copy_format(const WAVEFORMATEX *fmt) {
    if (!fmt)
        return NULL;
    size_t bytes = sizeof(WAVEFORMATEX) + (size_t)fmt->cbSize;
    WAVEFORMATEX *copy = (WAVEFORMATEX *)malloc(bytes);
    if (!copy)
        return NULL;
    memcpy(copy, fmt, bytes);
    return copy;
}

static WAVEFORMATEX *vaud_win32_select_format(IAudioClient *client) {
    if (!client)
        return NULL;

    WAVEFORMATEX requested;
    memset(&requested, 0, sizeof(requested));
    requested.wFormatTag = WAVE_FORMAT_PCM;
    requested.nChannels = VAUD_CHANNELS;
    requested.nSamplesPerSec = VAUD_SAMPLE_RATE;
    requested.wBitsPerSample = 16;
    requested.nBlockAlign = requested.nChannels * requested.wBitsPerSample / 8;
    requested.nAvgBytesPerSec = requested.nSamplesPerSec * requested.nBlockAlign;

    WAVEFORMATEX *closest = NULL;
    WAVEFORMATEX *selected = NULL;
    HRESULT hr = IAudioClient_IsFormatSupported(
        client, AUDCLNT_SHAREMODE_SHARED, &requested, &closest);
    if (hr == S_OK) {
        selected = vaud_win32_copy_format(&requested);
    } else if (hr == S_FALSE && closest) {
        vaud_win32_data scratch;
        memset(&scratch, 0, sizeof(scratch));
        if (vaud_win32_configure_render_format(&scratch, closest))
            selected = vaud_win32_copy_format(closest);
    }
    if (closest)
        CoTaskMemFree(closest);

    if (!selected) {
        WAVEFORMATEX *mix = NULL;
        hr = IAudioClient_GetMixFormat(client, &mix);
        if (SUCCEEDED(hr) && mix) {
            vaud_win32_data scratch;
            memset(&scratch, 0, sizeof(scratch));
            if (vaud_win32_configure_render_format(&scratch, mix))
                selected = vaud_win32_copy_format(mix);
        }
        if (mix)
            CoTaskMemFree(mix);
    }

    return selected;
}

static UINT32 vaud_win32_max_render_frames(const vaud_win32_data *plat) {
    if (!plat || plat->render_sample_rate == 0)
        return VAUD_BUFFER_FRAMES;
    if (plat->render_sample_rate >= VAUD_SAMPLE_RATE)
        return VAUD_BUFFER_FRAMES;
    uint64_t frames = ((uint64_t)(VAUD_BUFFER_FRAMES - 2) * plat->render_sample_rate) /
                      VAUD_SAMPLE_RATE;
    if (frames == 0)
        frames = 1;
    if (frames > VAUD_BUFFER_FRAMES)
        frames = VAUD_BUFFER_FRAMES;
    return (UINT32)frames;
}

static int32_t vaud_win32_resampled_sample(const int16_t *mix,
                                           UINT32 internal_frames,
                                           UINT32 out_frame,
                                           UINT32 out_rate,
                                           UINT32 channel) {
    if (!mix || internal_frames == 0 || out_rate == 0)
        return 0;
    if (out_rate == VAUD_SAMPLE_RATE) {
        UINT32 idx = out_frame < internal_frames ? out_frame : internal_frames - 1u;
        return mix[idx * VAUD_CHANNELS + channel];
    }

    double pos = ((double)out_frame * (double)VAUD_SAMPLE_RATE) / (double)out_rate;
    UINT32 idx = (UINT32)pos;
    if (idx >= internal_frames)
        idx = internal_frames - 1u;
    UINT32 next = (idx + 1u < internal_frames) ? idx + 1u : idx;
    double frac = pos - (double)idx;
    double a = (double)mix[idx * VAUD_CHANNELS + channel];
    double b = (double)mix[next * VAUD_CHANNELS + channel];
    return (int32_t)(a + (b - a) * frac);
}

static void vaud_win32_store_sample(vaud_win32_data *plat, BYTE *dst, int32_t sample) {
    if (sample > 32767)
        sample = 32767;
    if (sample < -32768)
        sample = -32768;

    switch (plat->render_sample_format) {
        case VAUD_WIN32_SAMPLE_F32: {
            float f = (float)sample / 32768.0f;
            memcpy(dst, &f, sizeof(f));
            break;
        }
        case VAUD_WIN32_SAMPLE_S24: {
            int32_t s24 = sample * 256;
            dst[0] = (BYTE)(s24 & 0xFF);
            dst[1] = (BYTE)((s24 >> 8) & 0xFF);
            dst[2] = (BYTE)((s24 >> 16) & 0xFF);
            break;
        }
        case VAUD_WIN32_SAMPLE_S32: {
            int32_t s32 = sample * 65536;
            memcpy(dst, &s32, sizeof(s32));
            break;
        }
        case VAUD_WIN32_SAMPLE_S16:
        default: {
            int16_t s16 = (int16_t)sample;
            memcpy(dst, &s16, sizeof(s16));
            break;
        }
    }
}

static void vaud_win32_render_to_buffer(vaud_context_t ctx,
                                        vaud_win32_data *plat,
                                        BYTE *buffer,
                                        UINT32 frames) {
    if (!ctx || !plat || !buffer || frames == 0)
        return;

    if (plat->render_channels == VAUD_CHANNELS && plat->render_sample_rate == VAUD_SAMPLE_RATE &&
        plat->render_sample_format == VAUD_WIN32_SAMPLE_S16 && plat->render_block_align == 4) {
        vaud_mixer_render(ctx, (int16_t *)buffer, (int32_t)frames);
        return;
    }

    if (!plat->mix_buffer) {
        memset(buffer, 0, (size_t)frames * plat->render_block_align);
        return;
    }

    UINT32 internal_frames = frames;
    if (plat->render_sample_rate != VAUD_SAMPLE_RATE) {
        uint64_t needed = ((uint64_t)(frames - 1u) * VAUD_SAMPLE_RATE) /
                              plat->render_sample_rate +
                          2u;
        internal_frames = needed > VAUD_BUFFER_FRAMES ? VAUD_BUFFER_FRAMES : (UINT32)needed;
    }
    if (internal_frames == 0)
        internal_frames = 1;

    vaud_mixer_render(ctx, plat->mix_buffer, (int32_t)internal_frames);

    for (UINT32 i = 0; i < frames; i++) {
        int32_t left = vaud_win32_resampled_sample(
            plat->mix_buffer, internal_frames, i, plat->render_sample_rate, 0);
        int32_t right = vaud_win32_resampled_sample(
            plat->mix_buffer, internal_frames, i, plat->render_sample_rate, 1);

        for (UINT32 ch = 0; ch < plat->render_channels; ch++) {
            int32_t sample = 0;
            if (plat->render_channels == 1)
                sample = (left + right) / 2;
            else if (ch == 0)
                sample = left;
            else if (ch == 1)
                sample = right;

            BYTE *dst = buffer + (size_t)i * plat->render_block_align +
                        (size_t)ch * plat->render_bytes_sample;
            vaud_win32_store_sample(plat, dst, sample);
        }
    }
}

static void vaud_win32_free_render_buffers(vaud_win32_data *plat) {
    if (!plat)
        return;
    free(plat->mix_buffer);
    plat->mix_buffer = NULL;
    free(plat->format);
    plat->format = NULL;
}

//===----------------------------------------------------------------------===//
// Audio Thread
//===----------------------------------------------------------------------===//

/// @brief Audio thread function - waits for buffer events and fills audio.
/// @param arg Pointer to our audio context.
/// @return 0 on exit.
static DWORD WINAPI audio_thread_func(LPVOID arg) {
    vaud_context_t ctx = (vaud_context_t)arg;
    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int com_initialized = SUCCEEDED(hr) ? 1 : 0;
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return 0;

    /* Events to wait on */
    HANDLE events[2] = {plat->event, plat->stop_event};

    while (InterlockedCompareExchange(&plat->running, 0, 0)) {
        /* Wait for buffer space or stop signal */
        DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, 100);

        if (!InterlockedCompareExchange(&plat->running, 0, 0) ||
            wait_result == WAIT_OBJECT_0 + 1) {
            /* Stop event signaled */
            break;
        }

        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }

        /* Check pause state */
        EnterCriticalSection(&plat->pause_cs);
        int is_paused = (int)InterlockedCompareExchange(&plat->paused, 0, 0);
        LeaveCriticalSection(&plat->pause_cs);

        if (is_paused) {
            continue;
        }

        /* Get available buffer space */
        UINT32 padding = 0;
        hr = IAudioClient_GetCurrentPadding(plat->client, &padding);
        if (FAILED(hr)) {
            continue;
        }

        UINT32 available = plat->buffer_frames - padding;
        if (available == 0) {
            continue;
        }

        /* Limit to our standard buffer size */
        UINT32 max_frames = vaud_win32_max_render_frames(plat);
        if (available > max_frames)
            available = max_frames;

        /* Get buffer from render client */
        BYTE *buffer = NULL;
        hr = IAudioRenderClient_GetBuffer(plat->render, available, &buffer);
        if (FAILED(hr)) {
            continue;
        }

        /* Render mixed audio in the negotiated WASAPI format. */
        vaud_win32_render_to_buffer(ctx, plat, buffer, available);

        /* Release buffer */
        IAudioRenderClient_ReleaseBuffer(plat->render, available, 0);
    }

    if (com_initialized)
        CoUninitialize();
    return 0;
}

//===----------------------------------------------------------------------===//
// Platform Interface Implementation
//===----------------------------------------------------------------------===//

int vaud_platform_init(vaud_context_t ctx) {
    if (!ctx)
        return 0;

    HRESULT hr;

    /* Allocate platform data */
    vaud_win32_data *plat = (vaud_win32_data *)calloc(1, sizeof(vaud_win32_data));
    if (!plat) {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate Windows audio data");
        return 0;
    }

    ctx->platform_data = plat;
    InterlockedExchange(&plat->running, 0);
    InterlockedExchange(&plat->paused, 0);

    InitializeCriticalSection(&plat->pause_cs);

    /* Initialize COM */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize COM");
        return 0;
    }
    plat->com_initialized = (hr == S_OK || hr == S_FALSE);

    /* Create device enumerator */
    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(&VAUD_CLSID_MMDeviceEnumerator,
                          NULL,
                          CLSCTX_ALL,
                          &VAUD_IID_IMMDeviceEnumerator,
                          (void **)&enumerator);

    if (FAILED(hr)) {
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create device enumerator");
        return 0;
    }

    /* Get default audio endpoint */
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &plat->device);

    IMMDeviceEnumerator_Release(enumerator);

    if (FAILED(hr)) {
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get audio endpoint");
        return 0;
    }

    /* Activate audio client */
    hr = IMMDevice_Activate(
        plat->device, &VAUD_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&plat->client);

    if (FAILED(hr)) {
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to activate audio client");
        return 0;
    }

    /* Negotiate a shared-mode WASAPI format and convert from the internal mixer format. */
    plat->format = vaud_win32_select_format(plat->client);
    if (!plat->format || !vaud_win32_configure_render_format(plat, plat->format)) {
        free(plat->format);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to negotiate audio format");
        return 0;
    }

    plat->mix_buffer =
        (int16_t *)malloc((size_t)VAUD_BUFFER_FRAMES * VAUD_CHANNELS * sizeof(int16_t));
    if (!plat->mix_buffer) {
        free(plat->format);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate Windows mix buffer");
        return 0;
    }

    /* Initialize audio client in shared mode with event callback */
    REFERENCE_TIME buffer_duration = 500000; /* 50ms in 100ns units */

    hr = IAudioClient_Initialize(plat->client,
                                 AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 buffer_duration,
                                 0,
                                 plat->format,
                                 NULL);

    if (FAILED(hr)) {
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize audio client");
        return 0;
    }

    /* Get actual buffer size */
    hr = IAudioClient_GetBufferSize(plat->client, &plat->buffer_frames);
    if (FAILED(hr)) {
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get buffer size");
        return 0;
    }

    /* Create events */
    plat->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    plat->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!plat->event || !plat->stop_event) {
        if (plat->event)
            CloseHandle(plat->event);
        if (plat->stop_event)
            CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create events");
        return 0;
    }

    /* Set event handle */
    hr = IAudioClient_SetEventHandle(plat->client, plat->event);
    if (FAILED(hr)) {
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to set event handle");
        return 0;
    }

    /* Get render client */
    hr =
        IAudioClient_GetService(plat->client, &VAUD_IID_IAudioRenderClient, (void **)&plat->render);

    if (FAILED(hr)) {
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get render client");
        return 0;
    }

    /* Start audio thread */
    InterlockedExchange(&plat->running, 1);
    plat->thread = CreateThread(NULL, 0, audio_thread_func, ctx, 0, NULL);

    if (!plat->thread) {
        InterlockedExchange(&plat->running, 0);
        IAudioRenderClient_Release(plat->render);
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create audio thread");
        return 0;
    }

    /* Start audio client */
    hr = IAudioClient_Start(plat->client);
    if (FAILED(hr)) {
        InterlockedExchange(&plat->running, 0);
        SetEvent(plat->stop_event);
        WaitForSingleObject(plat->thread, INFINITE);
        CloseHandle(plat->thread);
        IAudioRenderClient_Release(plat->render);
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        vaud_win32_free_render_buffers(plat);
        DeleteCriticalSection(&plat->pause_cs);
        if (plat->com_initialized)
            CoUninitialize();
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to start audio client");
        return 0;
    }

    return 1;
}

void vaud_platform_shutdown(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    /* Signal thread to stop */
    InterlockedExchange(&plat->running, 0);
    SetEvent(plat->stop_event);

    /* Wait for thread */
    if (plat->thread) {
        WaitForSingleObject(plat->thread, INFINITE);
        CloseHandle(plat->thread);
    }

    /* Stop audio client */
    if (plat->client) {
        IAudioClient_Stop(plat->client);
    }

    /* Release interfaces */
    if (plat->render)
        IAudioRenderClient_Release(plat->render);
    if (plat->client)
        IAudioClient_Release(plat->client);
    if (plat->device)
        IMMDevice_Release(plat->device);

    /* Close handles */
    if (plat->event)
        CloseHandle(plat->event);
    if (plat->stop_event)
        CloseHandle(plat->stop_event);

    int com_initialized = plat->com_initialized;
    vaud_win32_free_render_buffers(plat);
    DeleteCriticalSection(&plat->pause_cs);
    free(plat);
    ctx->platform_data = NULL;

    if (com_initialized)
        CoUninitialize();
}

void vaud_platform_pause(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    EnterCriticalSection(&plat->pause_cs);
    InterlockedExchange(&plat->paused, 1);
    LeaveCriticalSection(&plat->pause_cs);

    if (plat->client) {
        IAudioClient_Stop(plat->client);
    }
}

void vaud_platform_resume(vaud_context_t ctx) {
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    if (plat->client) {
        IAudioClient_Start(plat->client);
    }

    EnterCriticalSection(&plat->pause_cs);
    InterlockedExchange(&plat->paused, 0);
    LeaveCriticalSection(&plat->pause_cs);
}

//===----------------------------------------------------------------------===//
// Timing
//===----------------------------------------------------------------------===//

static INIT_ONCE g_vaud_qpc_init_once = INIT_ONCE_STATIC_INIT;
static LARGE_INTEGER g_vaud_qpc_frequency = {0};

static BOOL CALLBACK vaud_qpc_init_once(PINIT_ONCE init_once, PVOID parameter, PVOID *context) {
    (void)init_once;
    (void)parameter;
    (void)context;
    if (!QueryPerformanceFrequency(&g_vaud_qpc_frequency) ||
        g_vaud_qpc_frequency.QuadPart <= 0) {
        g_vaud_qpc_frequency.QuadPart = 1000;
        return FALSE;
    }
    return TRUE;
}

int64_t vaud_platform_now_ms(void) {
    if (!InitOnceExecuteOnce(&g_vaud_qpc_init_once, vaud_qpc_init_once, NULL, NULL) ||
        g_vaud_qpc_frequency.QuadPart <= 0)
        return 0;

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    long double millis =
        ((long double)counter.QuadPart * 1000.0L) / (long double)g_vaud_qpc_frequency.QuadPart;
    if (millis > (long double)INT64_MAX)
        return INT64_MAX;
    return (int64_t)millis;
}

#endif /* _WIN32 */
