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
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdlib.h>

//===----------------------------------------------------------------------===//
// COM GUIDs
//===----------------------------------------------------------------------===//

/* Define GUIDs we need (to avoid linking with uuid.lib) */
static const GUID VAUD_CLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C,
    {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}
};

static const GUID VAUD_IID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35,
    {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}
};

static const GUID VAUD_IID_IAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32,
    {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}
};

static const GUID VAUD_IID_IAudioRenderClient = {
    0xF294ACFC, 0x3146, 0x4483,
    {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}
};

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Windows WASAPI platform data.
typedef struct
{
    IMMDevice *device;              ///< Audio endpoint device
    IAudioClient *client;           ///< Audio client interface
    IAudioRenderClient *render;     ///< Render client for buffer access
    HANDLE thread;                  ///< Audio thread handle
    HANDLE event;                   ///< Buffer event
    HANDLE stop_event;              ///< Stop signal event
    UINT32 buffer_frames;           ///< Total buffer size in frames
    int running;                    ///< Thread running flag
    int paused;                     ///< Pause state
    CRITICAL_SECTION pause_cs;      ///< Protects pause state
} vaud_win32_data;

//===----------------------------------------------------------------------===//
// Audio Thread
//===----------------------------------------------------------------------===//

/// @brief Audio thread function - waits for buffer events and fills audio.
/// @param arg Pointer to our audio context.
/// @return 0 on exit.
static DWORD WINAPI audio_thread_func(LPVOID arg)
{
    vaud_context_t ctx = (vaud_context_t)arg;
    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;
    HRESULT hr;

    /* Events to wait on */
    HANDLE events[2] = { plat->event, plat->stop_event };

    while (plat->running)
    {
        /* Wait for buffer space or stop signal */
        DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, 100);

        if (!plat->running || wait_result == WAIT_OBJECT_0 + 1)
        {
            /* Stop event signaled */
            break;
        }

        if (wait_result == WAIT_TIMEOUT)
        {
            continue;
        }

        /* Check pause state */
        EnterCriticalSection(&plat->pause_cs);
        int is_paused = plat->paused;
        LeaveCriticalSection(&plat->pause_cs);

        if (is_paused)
        {
            continue;
        }

        /* Get available buffer space */
        UINT32 padding = 0;
        hr = IAudioClient_GetCurrentPadding(plat->client, &padding);
        if (FAILED(hr))
        {
            continue;
        }

        UINT32 available = plat->buffer_frames - padding;
        if (available == 0)
        {
            continue;
        }

        /* Limit to our standard buffer size */
        if (available > VAUD_BUFFER_FRAMES)
        {
            available = VAUD_BUFFER_FRAMES;
        }

        /* Get buffer from render client */
        BYTE *buffer = NULL;
        hr = IAudioRenderClient_GetBuffer(plat->render, available, &buffer);
        if (FAILED(hr))
        {
            continue;
        }

        /* Render mixed audio */
        vaud_mixer_render(ctx, (int16_t *)buffer, (int32_t)available);

        /* Release buffer */
        IAudioRenderClient_ReleaseBuffer(plat->render, available, 0);
    }

    return 0;
}

//===----------------------------------------------------------------------===//
// Platform Interface Implementation
//===----------------------------------------------------------------------===//

int vaud_platform_init(vaud_context_t ctx)
{
    if (!ctx)
        return 0;

    HRESULT hr;

    /* Allocate platform data */
    vaud_win32_data *plat = (vaud_win32_data *)calloc(1, sizeof(vaud_win32_data));
    if (!plat)
    {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate Windows audio data");
        return 0;
    }

    ctx->platform_data = plat;
    plat->running = 0;
    plat->paused = 0;

    InitializeCriticalSection(&plat->pause_cs);

    /* Initialize COM */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
    {
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize COM");
        return 0;
    }

    /* Create device enumerator */
    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(
        &VAUD_CLSID_MMDeviceEnumerator,
        NULL,
        CLSCTX_ALL,
        &VAUD_IID_IMMDeviceEnumerator,
        (void **)&enumerator
    );

    if (FAILED(hr))
    {
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create device enumerator");
        return 0;
    }

    /* Get default audio endpoint */
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
        enumerator,
        eRender,
        eConsole,
        &plat->device
    );

    IMMDeviceEnumerator_Release(enumerator);

    if (FAILED(hr))
    {
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get audio endpoint");
        return 0;
    }

    /* Activate audio client */
    hr = IMMDevice_Activate(
        plat->device,
        &VAUD_IID_IAudioClient,
        CLSCTX_ALL,
        NULL,
        (void **)&plat->client
    );

    if (FAILED(hr))
    {
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to activate audio client");
        return 0;
    }

    /* Set up audio format: 16-bit stereo PCM at 44.1kHz */
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = VAUD_CHANNELS;
    format.nSamplesPerSec = VAUD_SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    /* Initialize audio client in shared mode with event callback */
    REFERENCE_TIME buffer_duration = 500000;  /* 50ms in 100ns units */

    hr = IAudioClient_Initialize(
        plat->client,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        buffer_duration,
        0,
        &format,
        NULL
    );

    if (FAILED(hr))
    {
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to initialize audio client");
        return 0;
    }

    /* Get actual buffer size */
    hr = IAudioClient_GetBufferSize(plat->client, &plat->buffer_frames);
    if (FAILED(hr))
    {
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get buffer size");
        return 0;
    }

    /* Create events */
    plat->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    plat->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!plat->event || !plat->stop_event)
    {
        if (plat->event) CloseHandle(plat->event);
        if (plat->stop_event) CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create events");
        return 0;
    }

    /* Set event handle */
    hr = IAudioClient_SetEventHandle(plat->client, plat->event);
    if (FAILED(hr))
    {
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to set event handle");
        return 0;
    }

    /* Get render client */
    hr = IAudioClient_GetService(
        plat->client,
        &VAUD_IID_IAudioRenderClient,
        (void **)&plat->render
    );

    if (FAILED(hr))
    {
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to get render client");
        return 0;
    }

    /* Start audio thread */
    plat->running = 1;
    plat->thread = CreateThread(NULL, 0, audio_thread_func, ctx, 0, NULL);

    if (!plat->thread)
    {
        plat->running = 0;
        IAudioRenderClient_Release(plat->render);
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to create audio thread");
        return 0;
    }

    /* Start audio client */
    hr = IAudioClient_Start(plat->client);
    if (FAILED(hr))
    {
        plat->running = 0;
        SetEvent(plat->stop_event);
        WaitForSingleObject(plat->thread, INFINITE);
        CloseHandle(plat->thread);
        IAudioRenderClient_Release(plat->render);
        CloseHandle(plat->event);
        CloseHandle(plat->stop_event);
        IAudioClient_Release(plat->client);
        IMMDevice_Release(plat->device);
        DeleteCriticalSection(&plat->pause_cs);
        free(plat);
        ctx->platform_data = NULL;
        vaud_set_error(VAUD_ERR_PLATFORM, "Failed to start audio client");
        return 0;
    }

    return 1;
}

void vaud_platform_shutdown(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    /* Signal thread to stop */
    plat->running = 0;
    SetEvent(plat->stop_event);

    /* Wait for thread */
    if (plat->thread)
    {
        WaitForSingleObject(plat->thread, INFINITE);
        CloseHandle(plat->thread);
    }

    /* Stop audio client */
    if (plat->client)
    {
        IAudioClient_Stop(plat->client);
    }

    /* Release interfaces */
    if (plat->render) IAudioRenderClient_Release(plat->render);
    if (plat->client) IAudioClient_Release(plat->client);
    if (plat->device) IMMDevice_Release(plat->device);

    /* Close handles */
    if (plat->event) CloseHandle(plat->event);
    if (plat->stop_event) CloseHandle(plat->stop_event);

    DeleteCriticalSection(&plat->pause_cs);
    free(plat);
    ctx->platform_data = NULL;
}

void vaud_platform_pause(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    EnterCriticalSection(&plat->pause_cs);
    plat->paused = 1;
    LeaveCriticalSection(&plat->pause_cs);

    if (plat->client)
    {
        IAudioClient_Stop(plat->client);
    }
}

void vaud_platform_resume(vaud_context_t ctx)
{
    if (!ctx || !ctx->platform_data)
        return;

    vaud_win32_data *plat = (vaud_win32_data *)ctx->platform_data;

    if (plat->client)
    {
        IAudioClient_Start(plat->client);
    }

    EnterCriticalSection(&plat->pause_cs);
    plat->paused = 0;
    LeaveCriticalSection(&plat->pause_cs);
}

//===----------------------------------------------------------------------===//
// Timing
//===----------------------------------------------------------------------===//

int64_t vaud_platform_now_ms(void)
{
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    return (int64_t)(counter.QuadPart * 1000 / freq.QuadPart);
}

#endif /* _WIN32 */
