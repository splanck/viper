//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_fx.c
// Purpose: Implements real-time-safe mix-group insert effects for audio buses.
//
//===----------------------------------------------------------------------===//

#include "rt_audio_fx.h"

#include "rt_mixgroup.h"
#include "rt_platform.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if RT_COMPILER_MSVC
#include <intrin.h>
#endif

#define RT_AUDIO_FX_DEFAULT_RATE 44100
#define RT_AUDIO_FX_MAX_DELAY_SECONDS 2.0
#define RT_AUDIO_FX_PI 3.14159265358979323846

typedef enum {
    RT_AUDIO_FX_BIQUAD = 0,
    RT_AUDIO_FX_DELAY = 1,
    RT_AUDIO_FX_REVERB = 2,
} rt_audio_fx_kind;

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1_l;
    float z2_l;
    float z1_r;
    float z2_r;
} rt_biquad_state;

typedef struct {
    float *buffer;
    int32_t frames;
    int32_t pos;
    float feedback;
    float wet;
    float dry;
} rt_delay_state;

typedef struct {
    float *buffer;
    int32_t frames;
    int32_t pos;
    float filter;
} rt_reverb_line;

typedef struct {
    rt_reverb_line comb_l[8];
    rt_reverb_line comb_r[8];
    rt_reverb_line allpass_l[4];
    rt_reverb_line allpass_r[4];
    float room;
    float damp;
    float wet;
    float dry;
} rt_reverb_state;

typedef struct rt_audio_fx {
    int64_t id;
    rt_audio_fx_kind kind;
    int8_t bypass;
    union {
        rt_biquad_state biquad;
        rt_delay_state delay;
        rt_reverb_state reverb;
    } u;
    struct rt_audio_fx *next;
} rt_audio_fx;

static rt_audio_fx *g_group_fx[RT_MIXGROUP_MAX_GROUPS] = {0};
static int64_t g_next_fx_id = 1;
static volatile int g_fx_lock = 0;

static int fx_try_lock(void) {
#if RT_COMPILER_MSVC
    return _InterlockedExchange8((volatile char *)&g_fx_lock, 1) == 0;
#else
    return !__atomic_test_and_set(&g_fx_lock, __ATOMIC_ACQUIRE);
#endif
}

static void fx_lock(void) {
    while (!fx_try_lock()) {
    }
}

static void fx_unlock(void) {
#if RT_COMPILER_MSVC
    _InterlockedExchange8((volatile char *)&g_fx_lock, 0);
#else
    __atomic_clear(&g_fx_lock, __ATOMIC_RELEASE);
#endif
}

static int group_valid(int64_t group) {
    return group >= 0 && group < RT_MIXGROUP_MAX_GROUPS;
}

static float clampf_range(float value, float lo, float hi) {
    if (!isfinite(value))
        return lo;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static float sanitize_feedback(float value) {
    if (fabsf(value) < 1.0e-20f)
        return 0.0f;
    if (!isfinite(value))
        return 0.0f;
    return value;
}

static int32_t seconds_to_frames(double seconds) {
    if (!isfinite(seconds) || seconds <= 0.0)
        seconds = 0.001;
    if (seconds > RT_AUDIO_FX_MAX_DELAY_SECONDS)
        seconds = RT_AUDIO_FX_MAX_DELAY_SECONDS;
    double frames = seconds * (double)RT_AUDIO_FX_DEFAULT_RATE;
    if (frames < 1.0)
        frames = 1.0;
    return (int32_t)(frames + 0.5);
}

static int alloc_reverb_line(rt_reverb_line *line, int32_t frames) {
    if (!line || frames <= 0)
        return 0;
    line->buffer = (float *)calloc((size_t)frames, sizeof(float));
    if (!line->buffer)
        return 0;
    line->frames = frames;
    line->pos = 0;
    line->filter = 0.0f;
    return 1;
}

static void free_reverb_line(rt_reverb_line *line) {
    if (!line)
        return;
    free(line->buffer);
    line->buffer = NULL;
    line->frames = 0;
    line->pos = 0;
    line->filter = 0.0f;
}

static void free_fx(rt_audio_fx *fx) {
    if (!fx)
        return;
    if (fx->kind == RT_AUDIO_FX_DELAY) {
        free(fx->u.delay.buffer);
    } else if (fx->kind == RT_AUDIO_FX_REVERB) {
        for (int i = 0; i < 8; i++) {
            free_reverb_line(&fx->u.reverb.comb_l[i]);
            free_reverb_line(&fx->u.reverb.comb_r[i]);
        }
        for (int i = 0; i < 4; i++) {
            free_reverb_line(&fx->u.reverb.allpass_l[i]);
            free_reverb_line(&fx->u.reverb.allpass_r[i]);
        }
    }
    free(fx);
}

static void biquad_set(rt_biquad_state *bq,
                       int mode,
                       double freq_hz,
                       double q,
                       double gain_db) {
    if (!bq)
        return;
    if (!isfinite(freq_hz) || freq_hz < 10.0)
        freq_hz = 10.0;
    if (freq_hz > (double)RT_AUDIO_FX_DEFAULT_RATE * 0.45)
        freq_hz = (double)RT_AUDIO_FX_DEFAULT_RATE * 0.45;
    if (!isfinite(q) || q < 0.05)
        q = 0.707;
    if (q > 20.0)
        q = 20.0;

    double w0 = 2.0 * RT_AUDIO_FX_PI * freq_hz / (double)RT_AUDIO_FX_DEFAULT_RATE;
    double cw = cos(w0);
    double sw = sin(w0);
    double alpha = sw / (2.0 * q);
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;

    if (mode == 0) {
        b0 = (1.0 - cw) * 0.5;
        b1 = 1.0 - cw;
        b2 = (1.0 - cw) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
    } else if (mode == 1) {
        b0 = (1.0 + cw) * 0.5;
        b1 = -(1.0 + cw);
        b2 = (1.0 + cw) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
    } else {
        double a = pow(10.0, gain_db / 40.0);
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / a;
    }

    if (fabs(a0) < 1.0e-12)
        a0 = 1.0;
    bq->b0 = (float)(b0 / a0);
    bq->b1 = (float)(b1 / a0);
    bq->b2 = (float)(b2 / a0);
    bq->a1 = (float)(a1 / a0);
    bq->a2 = (float)(a2 / a0);
    bq->z1_l = bq->z2_l = bq->z1_r = bq->z2_r = 0.0f;
}

static float biquad_sample(rt_biquad_state *bq, float in, int right) {
    float *z1 = right ? &bq->z1_r : &bq->z1_l;
    float *z2 = right ? &bq->z2_r : &bq->z2_l;
    float out = in * bq->b0 + *z1;
    *z1 = in * bq->b1 + *z2 - bq->a1 * out;
    *z2 = in * bq->b2 - bq->a2 * out;
    return sanitize_feedback(out);
}

static void process_biquad(rt_biquad_state *bq, float *samples, int32_t frames, int32_t channels) {
    if (!bq || !samples || channels < 1)
        return;
    for (int32_t i = 0; i < frames; i++) {
        samples[(size_t)i * channels] = biquad_sample(bq, samples[(size_t)i * channels], 0);
        if (channels > 1)
            samples[(size_t)i * channels + 1] =
                biquad_sample(bq, samples[(size_t)i * channels + 1], 1);
    }
}

static void process_delay(rt_delay_state *delay,
                          float *samples,
                          int32_t frames,
                          int32_t channels) {
    if (!delay || !delay->buffer || !samples || delay->frames <= 0 || channels < 1)
        return;
    for (int32_t i = 0; i < frames; i++) {
        size_t sample_base = (size_t)i * (size_t)channels;
        size_t delay_base = (size_t)delay->pos * 2u;
        float in_l = samples[sample_base];
        float in_r = channels > 1 ? samples[sample_base + 1] : in_l;
        float delayed_l = delay->buffer[delay_base];
        float delayed_r = delay->buffer[delay_base + 1];
        samples[sample_base] = sanitize_feedback(in_l * delay->dry + delayed_l * delay->wet);
        if (channels > 1)
            samples[sample_base + 1] =
                sanitize_feedback(in_r * delay->dry + delayed_r * delay->wet);
        delay->buffer[delay_base] = sanitize_feedback(in_l + delayed_l * delay->feedback);
        delay->buffer[delay_base + 1] = sanitize_feedback(in_r + delayed_r * delay->feedback);
        delay->pos++;
        if (delay->pos >= delay->frames)
            delay->pos = 0;
    }
}

static float process_comb(rt_reverb_line *line, float input, float room, float damp) {
    float out = line->buffer[line->pos];
    line->filter = sanitize_feedback(out * (1.0f - damp) + line->filter * damp);
    line->buffer[line->pos] = sanitize_feedback(input + line->filter * room);
    line->pos++;
    if (line->pos >= line->frames)
        line->pos = 0;
    return out;
}

static float process_allpass(rt_reverb_line *line, float input) {
    float buffered = line->buffer[line->pos];
    float out = -input + buffered;
    line->buffer[line->pos] = sanitize_feedback(input + buffered * 0.5f);
    line->pos++;
    if (line->pos >= line->frames)
        line->pos = 0;
    return sanitize_feedback(out);
}

static void process_reverb(rt_reverb_state *rv, float *samples, int32_t frames, int32_t channels) {
    if (!rv || !samples || channels < 1)
        return;
    for (int32_t i = 0; i < frames; i++) {
        size_t base = (size_t)i * (size_t)channels;
        float dry_l = samples[base];
        float dry_r = channels > 1 ? samples[base + 1] : dry_l;
        float mono = (dry_l + dry_r) * 0.5f;
        float wet_l = 0.0f;
        float wet_r = 0.0f;
        for (int c = 0; c < 8; c++) {
            wet_l += process_comb(&rv->comb_l[c], mono, rv->room, rv->damp);
            wet_r += process_comb(&rv->comb_r[c], mono, rv->room, rv->damp);
        }
        wet_l *= 0.125f;
        wet_r *= 0.125f;
        for (int a = 0; a < 4; a++) {
            wet_l = process_allpass(&rv->allpass_l[a], wet_l);
            wet_r = process_allpass(&rv->allpass_r[a], wet_r);
        }
        samples[base] = sanitize_feedback(dry_l * rv->dry + wet_l * rv->wet);
        if (channels > 1)
            samples[base + 1] = sanitize_feedback(dry_r * rv->dry + wet_r * rv->wet);
    }
}

static int64_t append_fx(int64_t group, rt_audio_fx *fx) {
    if (!group_valid(group) || !fx)
        return -1;
    fx_lock();
    fx->id = g_next_fx_id++;
    if (g_next_fx_id <= 0)
        g_next_fx_id = 1;
    fx->next = NULL;
    rt_audio_fx **tail = &g_group_fx[group];
    while (*tail)
        tail = &(*tail)->next;
    *tail = fx;
    int64_t id = fx->id;
    fx_unlock();
    return id;
}

int64_t rt_audio_fx_add_lowpass(int64_t group, double cutoff_hz, double q) {
    rt_audio_fx *fx = (rt_audio_fx *)calloc(1, sizeof(rt_audio_fx));
    if (!fx)
        return -1;
    fx->kind = RT_AUDIO_FX_BIQUAD;
    biquad_set(&fx->u.biquad, 0, cutoff_hz, q, 0.0);
    return append_fx(group, fx);
}

int64_t rt_audio_fx_add_highpass(int64_t group, double cutoff_hz, double q) {
    rt_audio_fx *fx = (rt_audio_fx *)calloc(1, sizeof(rt_audio_fx));
    if (!fx)
        return -1;
    fx->kind = RT_AUDIO_FX_BIQUAD;
    biquad_set(&fx->u.biquad, 1, cutoff_hz, q, 0.0);
    return append_fx(group, fx);
}

int64_t rt_audio_fx_add_peaking(int64_t group, double freq_hz, double q, double gain_db) {
    rt_audio_fx *fx = (rt_audio_fx *)calloc(1, sizeof(rt_audio_fx));
    if (!fx)
        return -1;
    fx->kind = RT_AUDIO_FX_BIQUAD;
    biquad_set(&fx->u.biquad, 2, freq_hz, q, gain_db);
    return append_fx(group, fx);
}

int64_t rt_audio_fx_add_delay(int64_t group, double delay_ms, double feedback, double wet) {
    rt_audio_fx *fx = (rt_audio_fx *)calloc(1, sizeof(rt_audio_fx));
    if (!fx)
        return -1;
    fx->kind = RT_AUDIO_FX_DELAY;
    int32_t frames = seconds_to_frames(delay_ms / 1000.0);
    fx->u.delay.buffer = (float *)calloc((size_t)frames * 2u, sizeof(float));
    if (!fx->u.delay.buffer) {
        free(fx);
        return -1;
    }
    fx->u.delay.frames = frames;
    fx->u.delay.feedback = clampf_range((float)feedback, 0.0f, 0.95f);
    fx->u.delay.wet = clampf_range((float)wet, 0.0f, 1.0f);
    fx->u.delay.dry = 1.0f - fx->u.delay.wet;
    return append_fx(group, fx);
}

int64_t rt_audio_fx_add_reverb(int64_t group, double room_size, double damping, double wet) {
    static const int comb_l[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int comb_r[8] = {1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640};
    static const int all_l[4] = {556, 441, 341, 225};
    static const int all_r[4] = {579, 464, 364, 248};
    rt_audio_fx *fx = (rt_audio_fx *)calloc(1, sizeof(rt_audio_fx));
    if (!fx)
        return -1;
    fx->kind = RT_AUDIO_FX_REVERB;
    fx->u.reverb.room = 0.55f + clampf_range((float)room_size, 0.0f, 1.0f) * 0.4f;
    fx->u.reverb.damp = clampf_range((float)damping, 0.0f, 1.0f) * 0.8f;
    fx->u.reverb.wet = clampf_range((float)wet, 0.0f, 1.0f);
    fx->u.reverb.dry = 1.0f - fx->u.reverb.wet;
    for (int i = 0; i < 8; i++) {
        if (!alloc_reverb_line(&fx->u.reverb.comb_l[i], comb_l[i]) ||
            !alloc_reverb_line(&fx->u.reverb.comb_r[i], comb_r[i])) {
            free_fx(fx);
            return -1;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!alloc_reverb_line(&fx->u.reverb.allpass_l[i], all_l[i]) ||
            !alloc_reverb_line(&fx->u.reverb.allpass_r[i], all_r[i])) {
            free_fx(fx);
            return -1;
        }
    }
    return append_fx(group, fx);
}

void rt_audio_fx_set_bypass(int64_t group, int64_t fx_id, int8_t bypass) {
    if (!group_valid(group) || fx_id <= 0)
        return;
    fx_lock();
    for (rt_audio_fx *fx = g_group_fx[group]; fx; fx = fx->next) {
        if (fx->id == fx_id) {
            fx->bypass = bypass ? 1 : 0;
            break;
        }
    }
    fx_unlock();
}

void rt_audio_fx_remove(int64_t group, int64_t fx_id) {
    if (!group_valid(group) || fx_id <= 0)
        return;
    fx_lock();
    rt_audio_fx **cur = &g_group_fx[group];
    while (*cur) {
        if ((*cur)->id == fx_id) {
            rt_audio_fx *dead = *cur;
            *cur = dead->next;
            fx_unlock();
            free_fx(dead);
            return;
        }
        cur = &(*cur)->next;
    }
    fx_unlock();
}

void rt_audio_fx_clear_group(int64_t group) {
    if (!group_valid(group))
        return;
    fx_lock();
    rt_audio_fx *cur = g_group_fx[group];
    g_group_fx[group] = NULL;
    fx_unlock();
    while (cur) {
        rt_audio_fx *next = cur->next;
        free_fx(cur);
        cur = next;
    }
}

void rt_audio_fx_clear_all(void) {
    for (int64_t group = 0; group < RT_MIXGROUP_MAX_GROUPS; group++)
        rt_audio_fx_clear_group(group);
}

int rt_audio_fx_group_has_effects(int64_t group) {
    if (!group_valid(group))
        return 0;
    if (!fx_try_lock())
        return 0;
    int result = 0;
    for (rt_audio_fx *fx = g_group_fx[group]; fx; fx = fx->next) {
        if (!fx->bypass) {
            result = 1;
            break;
        }
    }
    fx_unlock();
    return result;
}

void rt_audio_fx_process_group(int64_t group,
                               float *samples,
                               int32_t frames,
                               int32_t channels,
                               int32_t sample_rate) {
    (void)sample_rate;
    if (!group_valid(group) || !samples || frames <= 0 || channels <= 0)
        return;
    if (!fx_try_lock())
        return;
    for (rt_audio_fx *fx = g_group_fx[group]; fx; fx = fx->next) {
        if (fx->bypass)
            continue;
        if (fx->kind == RT_AUDIO_FX_BIQUAD) {
            process_biquad(&fx->u.biquad, samples, frames, channels);
        } else if (fx->kind == RT_AUDIO_FX_DELAY) {
            process_delay(&fx->u.delay, samples, frames, channels);
        } else if (fx->kind == RT_AUDIO_FX_REVERB) {
            process_reverb(&fx->u.reverb, samples, frames, channels);
        }
    }
    fx_unlock();
}
