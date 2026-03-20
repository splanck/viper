//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_synth.c
// Purpose: Procedural sound synthesis — generates WAV data in memory and
//          loads it as a Sound object via rt_sound_load_mem. Supports tone
//          generation, frequency sweeps, noise, and preset game SFX.
//
// Key invariants:
//   - Output is always 16-bit PCM, mono, 44100 Hz.
//   - WAV data is built in a temporary heap buffer, loaded, then freed.
//   - Sine approximation uses a 5th-order polynomial (no libm dependency).
//   - All parameter values are clamped to safe ranges.
//
// Ownership/Lifetime:
//   - Temporary WAV buffers are malloc'd and freed within each function.
//   - Returned Sound objects are GC-managed with refcount 1.
//
// Links: rt_audio.h (rt_sound_load_mem), rt_synth.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_synth.h"
#include "rt_audio.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

#define SYNTH_SAMPLE_RATE 44100
#define SYNTH_CHANNELS 1
#define SYNTH_BITS 16
#define SYNTH_MAX_AMP 32000 /* slightly below INT16_MAX to avoid clipping */
#define WAV_HEADER_SIZE 44

/* Pi approximation — sufficient for audio synthesis */
#define SYNTH_PI 3.14159265358979323846
#define SYNTH_2PI 6.28318530717958647692

//===----------------------------------------------------------------------===//
// Fixed-point sine approximation (no libm dependency)
//===----------------------------------------------------------------------===//

/// @brief Fast sine approximation using Bhaskara I's formula.
/// @param phase Phase in range [0.0, 1.0) representing [0, 2*PI).
/// @return Sine value in range [-1.0, 1.0].
static double synth_sin(double phase)
{
    /* Normalize phase to [0, 1) */
    phase = phase - (double)(int64_t)phase;
    if (phase < 0.0)
        phase += 1.0;

    /* Map to [0, 2*PI) and use symmetry */
    double x = phase * SYNTH_2PI;

    /* Reduce to [0, PI] using sin(PI+x) = -sin(x) */
    int negate = 0;
    if (x > SYNTH_PI)
    {
        x -= SYNTH_PI;
        negate = 1;
    }

    /* Bhaskara I approximation: sin(x) ≈ 16x(PI-x) / (5*PI^2 - 4x(PI-x))
       Accurate to ~0.08% max error — inaudible for audio */
    double xpi = x * (SYNTH_PI - x);
    double result = 16.0 * xpi / (5.0 * SYNTH_PI * SYNTH_PI - 4.0 * xpi);

    return negate ? -result : result;
}

//===----------------------------------------------------------------------===//
// WAV Header Construction
//===----------------------------------------------------------------------===//

/// @brief Write a minimal WAV header for mono 16-bit PCM data.
static void write_wav_header(uint8_t *buf, int32_t num_samples)
{
    int32_t data_size = num_samples * SYNTH_CHANNELS * (SYNTH_BITS / 8);
    int32_t file_size = WAV_HEADER_SIZE + data_size - 8;
    int32_t byte_rate = SYNTH_SAMPLE_RATE * SYNTH_CHANNELS * (SYNTH_BITS / 8);
    int16_t block_align = SYNTH_CHANNELS * (SYNTH_BITS / 8);

    /* RIFF chunk */
    memcpy(buf + 0, "RIFF", 4);
    memcpy(buf + 4, &file_size, 4);
    memcpy(buf + 8, "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(buf + 12, "fmt ", 4);
    int32_t fmt_size = 16;
    memcpy(buf + 16, &fmt_size, 4);
    int16_t audio_format = 1; /* PCM */
    memcpy(buf + 20, &audio_format, 2);
    int16_t channels = SYNTH_CHANNELS;
    memcpy(buf + 22, &channels, 2);
    int32_t sample_rate = SYNTH_SAMPLE_RATE;
    memcpy(buf + 24, &sample_rate, 4);
    memcpy(buf + 28, &byte_rate, 4);
    memcpy(buf + 32, &block_align, 2);
    int16_t bits = SYNTH_BITS;
    memcpy(buf + 34, &bits, 2);

    /* data sub-chunk */
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_size, 4);
}

//===----------------------------------------------------------------------===//
// Waveform Generation
//===----------------------------------------------------------------------===//

/// @brief Generate a single waveform sample at the given phase.
/// @param phase Normalized phase [0.0, 1.0).
/// @param waveform Waveform type constant.
/// @return Sample value in [-1.0, 1.0].
static double waveform_sample(double phase, int64_t waveform)
{
    /* Normalize */
    phase = phase - (double)(int64_t)phase;
    if (phase < 0.0)
        phase += 1.0;

    switch (waveform)
    {
        case RT_WAVE_SQUARE:
            return phase < 0.5 ? 1.0 : -1.0;

        case RT_WAVE_SAWTOOTH:
            return 2.0 * phase - 1.0;

        case RT_WAVE_TRIANGLE:
            if (phase < 0.25)
                return 4.0 * phase;
            else if (phase < 0.75)
                return 2.0 - 4.0 * phase;
            else
                return 4.0 * phase - 4.0;

        case RT_WAVE_SINE:
        default:
            return synth_sin(phase);
    }
}

//===----------------------------------------------------------------------===//
// Sound Creation Helper
//===----------------------------------------------------------------------===//

/// @brief Build a WAV from PCM samples and load as a Sound object.
/// @param samples Array of int16_t PCM samples.
/// @param num_samples Number of samples.
/// @return Sound object or NULL on failure.
static void *samples_to_sound(const int16_t *samples, int32_t num_samples)
{
    int32_t data_size = num_samples * (int32_t)sizeof(int16_t);
    int32_t wav_size = WAV_HEADER_SIZE + data_size;

    uint8_t *wav_buf = (uint8_t *)malloc((size_t)wav_size);
    if (!wav_buf)
        return NULL;

    write_wav_header(wav_buf, num_samples);
    memcpy(wav_buf + WAV_HEADER_SIZE, samples, (size_t)data_size);

    void *sound = rt_sound_load_mem(wav_buf, wav_size);
    free(wav_buf);

    return sound;
}

//===----------------------------------------------------------------------===//
// Clamp Helpers
//===----------------------------------------------------------------------===//

static int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

void *rt_synth_tone(int64_t freq_hz, int64_t duration_ms, int64_t waveform)
{
    freq_hz = clamp_i64(freq_hz, 20, 20000);
    duration_ms = clamp_i64(duration_ms, 1, 10000);
    waveform = clamp_i64(waveform, 0, 3);

    int32_t num_samples = (int32_t)((duration_ms * SYNTH_SAMPLE_RATE) / 1000);
    if (num_samples < 1)
        return NULL;

    int16_t *samples = (int16_t *)malloc((size_t)num_samples * sizeof(int16_t));
    if (!samples)
        return NULL;

    double phase = 0.0;
    double phase_inc = (double)freq_hz / (double)SYNTH_SAMPLE_RATE;

    for (int32_t i = 0; i < num_samples; i++)
    {
        double val = waveform_sample(phase, waveform);

        /* Apply a short fade-in/fade-out to avoid clicks (10ms each) */
        int32_t fade_samples = SYNTH_SAMPLE_RATE / 100; /* 10ms */
        double env = 1.0;
        if (i < fade_samples)
            env = (double)i / (double)fade_samples;
        else if (i > num_samples - fade_samples)
            env = (double)(num_samples - i) / (double)fade_samples;

        samples[i] = (int16_t)(val * env * SYNTH_MAX_AMP);
        phase += phase_inc;
    }

    void *sound = samples_to_sound(samples, num_samples);
    free(samples);
    return sound;
}

void *rt_synth_sweep(int64_t start_hz, int64_t end_hz, int64_t duration_ms, int64_t waveform)
{
    start_hz = clamp_i64(start_hz, 20, 20000);
    end_hz = clamp_i64(end_hz, 20, 20000);
    duration_ms = clamp_i64(duration_ms, 1, 10000);
    waveform = clamp_i64(waveform, 0, 3);

    int32_t num_samples = (int32_t)((duration_ms * SYNTH_SAMPLE_RATE) / 1000);
    if (num_samples < 1)
        return NULL;

    int16_t *samples = (int16_t *)malloc((size_t)num_samples * sizeof(int16_t));
    if (!samples)
        return NULL;

    double phase = 0.0;

    for (int32_t i = 0; i < num_samples; i++)
    {
        /* Linear frequency interpolation */
        double t = (double)i / (double)num_samples;
        double freq = (double)start_hz + ((double)end_hz - (double)start_hz) * t;
        double phase_inc = freq / (double)SYNTH_SAMPLE_RATE;

        double val = waveform_sample(phase, waveform);

        /* Fade envelope */
        int32_t fade_samples = SYNTH_SAMPLE_RATE / 100;
        double env = 1.0;
        if (i < fade_samples)
            env = (double)i / (double)fade_samples;
        else if (i > num_samples - fade_samples)
            env = (double)(num_samples - i) / (double)fade_samples;

        samples[i] = (int16_t)(val * env * SYNTH_MAX_AMP);
        phase += phase_inc;
    }

    void *sound = samples_to_sound(samples, num_samples);
    free(samples);
    return sound;
}

void *rt_synth_noise(int64_t duration_ms, int64_t volume)
{
    duration_ms = clamp_i64(duration_ms, 1, 10000);
    volume = clamp_i64(volume, 0, 100);

    int32_t num_samples = (int32_t)((duration_ms * SYNTH_SAMPLE_RATE) / 1000);
    if (num_samples < 1)
        return NULL;

    int16_t *samples = (int16_t *)malloc((size_t)num_samples * sizeof(int16_t));
    if (!samples)
        return NULL;

    /* Simple LCG PRNG for reproducible noise (no external dependency) */
    uint32_t rng_state = 0x12345678;
    double vol_scale = (double)volume / 100.0;

    for (int32_t i = 0; i < num_samples; i++)
    {
        /* LCG: state = state * 1103515245 + 12345 */
        rng_state = rng_state * 1103515245u + 12345u;
        int16_t noise_val = (int16_t)(rng_state >> 16);

        /* Quadratic decay envelope */
        double t = (double)i / (double)num_samples;
        double env = 1.0 - t; /* Linear decay */
        env = env * env;      /* Quadratic decay for more natural sound */

        samples[i] = (int16_t)((double)noise_val * env * vol_scale);
    }

    void *sound = samples_to_sound(samples, num_samples);
    free(samples);
    return sound;
}

//===----------------------------------------------------------------------===//
// Preset SFX
//===----------------------------------------------------------------------===//

/// @brief Generate "jump" sound: quick ascending frequency sweep.
static void *sfx_jump(void)
{
    return rt_synth_sweep(200, 600, 150, RT_WAVE_SQUARE);
}

/// @brief Generate "coin" sound: two quick high-pitched tones.
static void *sfx_coin(void)
{
    /* Two-tone coin: 880Hz + 1760Hz, 80ms total */
    int32_t half = (int32_t)(80 * SYNTH_SAMPLE_RATE / 1000) / 2;
    int32_t num_samples = half * 2;

    int16_t *samples = (int16_t *)malloc((size_t)num_samples * sizeof(int16_t));
    if (!samples)
        return NULL;

    double phase = 0.0;
    for (int32_t i = 0; i < num_samples; i++)
    {
        double freq = (i < half) ? 880.0 : 1760.0;
        double phase_inc = freq / (double)SYNTH_SAMPLE_RATE;

        double val = waveform_sample(phase, RT_WAVE_SQUARE);

        /* Tiny amplitude to keep it crisp */
        double env = 0.6;
        /* Quick fade at boundaries */
        int32_t fade = SYNTH_SAMPLE_RATE / 200; /* 5ms */
        if (i < fade)
            env = 0.6 * (double)i / (double)fade;
        else if (i > num_samples - fade)
            env = 0.6 * (double)(num_samples - i) / (double)fade;

        samples[i] = (int16_t)(val * env * SYNTH_MAX_AMP);
        phase += phase_inc;

        /* Reset phase at tone boundary for clean transition */
        if (i == half - 1)
            phase = 0.0;
    }

    void *sound = samples_to_sound(samples, num_samples);
    free(samples);
    return sound;
}

/// @brief Generate "hit" sound: short noise burst with fast decay.
static void *sfx_hit(void)
{
    return rt_synth_noise(80, 90);
}

/// @brief Generate "explosion" sound: longer noise with slow decay.
static void *sfx_explosion(void)
{
    return rt_synth_noise(500, 100);
}

/// @brief Generate "powerup" sound: ascending sweep with triangle wave.
static void *sfx_powerup(void)
{
    return rt_synth_sweep(300, 1200, 400, RT_WAVE_TRIANGLE);
}

/// @brief Generate "laser" sound: quick descending sweep with sawtooth.
static void *sfx_laser(void)
{
    return rt_synth_sweep(1500, 200, 120, RT_WAVE_SAWTOOTH);
}

void *rt_synth_sfx(int64_t sfx_type)
{
    switch (sfx_type)
    {
        case RT_SFX_JUMP:
            return sfx_jump();
        case RT_SFX_COIN:
            return sfx_coin();
        case RT_SFX_HIT:
            return sfx_hit();
        case RT_SFX_EXPLOSION:
            return sfx_explosion();
        case RT_SFX_POWERUP:
            return sfx_powerup();
        case RT_SFX_LASER:
            return sfx_laser();
        default:
            return NULL;
    }
}
