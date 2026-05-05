//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD Software Mixer
//
// Combines multiple audio voices and music streams into a single stereo output.
// The mixer is called from the audio thread to fill platform audio buffers.
//
// Key features:
// - Up to VAUD_MAX_VOICES simultaneous sound effects
// - Per-voice volume and stereo panning
// - Music streaming with multiple buffer support
// - Clipping prevention via soft limiting
//
// Mixing algorithm:
// 1. Clear output buffer to zero
// 2. For each active voice, add scaled samples to output
// 3. Add active music streams
// 4. Apply master volume
// 5. Soft clip to prevent distortion
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Software audio mixer for ViperAUD.

#include "vaud_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Mixer Constants
//===----------------------------------------------------------------------===//

/// @brief Maximum amplitude before soft clipping engages.
#define VAUD_CLIP_THRESHOLD 28000

/// @brief Soft clip knee factor (higher = softer knee).
#define VAUD_CLIP_KNEE 0.25f

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Apply soft clipping to prevent harsh distortion.
/// @details Uses a tanh-like curve above the threshold to gently limit peaks.
/// @param sample Input sample (may exceed 16-bit range).
/// @return Clipped sample in 16-bit range.
static inline int16_t soft_clip(int32_t sample) {
    if (sample > VAUD_CLIP_THRESHOLD) {
        float excess = (float)(sample - VAUD_CLIP_THRESHOLD);
        float compressed = VAUD_CLIP_THRESHOLD + excess * VAUD_CLIP_KNEE;
        sample = (int32_t)compressed;
    } else if (sample < -VAUD_CLIP_THRESHOLD) {
        float excess = (float)(-sample - VAUD_CLIP_THRESHOLD);
        float compressed = -(VAUD_CLIP_THRESHOLD + excess * VAUD_CLIP_KNEE);
        sample = (int32_t)compressed;
    }

    /* Hard clamp as final safety */
    if (sample > 32767)
        return 32767;
    if (sample < -32768)
        return -32768;
    return (int16_t)sample;
}

/// @brief Calculate left/right gain from pan value.
/// @param pan Pan value (-1.0 = left, 0.0 = center, 1.0 = right).
/// @param source_channels Original source channel count (1 = mono, 2 = stereo).
/// @param left_gain Output: left channel gain.
/// @param right_gain Output: right channel gain.
static void calculate_pan_gains(float pan,
                                int32_t source_channels,
                                float *left_gain,
                                float *right_gain) {
    if (!isfinite(pan))
        pan = 0.0f;
    if (pan < -1.0f)
        pan = -1.0f;
    if (pan > 1.0f)
        pan = 1.0f;

    if (source_channels == 1) {
        /* Equal-power panning for mono sources keeps center playback perceptually stable. */
        float angle = (pan + 1.0f) * 0.78539816339f; /* (pan + 1) * pi / 4 */
        *left_gain = cosf(angle);
        *right_gain = sinf(angle);
        return;
    }

    /* Stereo sources use balance semantics: center is unity, pan attenuates the far side. */
    *left_gain = (pan > 0.0f) ? (1.0f - pan) : 1.0f;
    *right_gain = (pan < 0.0f) ? (1.0f + pan) : 1.0f;
}

//===----------------------------------------------------------------------===//
// Voice Mixing
//===----------------------------------------------------------------------===//

/// @brief Mix a single voice into the output buffer.
/// @param voice Voice to mix.
/// @param output Output buffer (stereo interleaved).
/// @param frames Number of frames to mix.
/// @param master_vol Master volume multiplier.
/// @return 1 if voice is still active, 0 if finished.
static int mix_voice(vaud_voice *voice, int32_t *output, int32_t frames, float master_vol) {
    if (!voice || voice->state != VAUD_VOICE_PLAYING || !voice->sound)
        return 0;

    vaud_sound_t sound = voice->sound;
    const int16_t *samples = sound->samples;
    int64_t sound_frames = sound->frame_count;
    int64_t pos = voice->position;

    float left_gain, right_gain;
    calculate_pan_gains(voice->pan, sound->source_channels, &left_gain, &right_gain);

    float vol = voice->volume * master_vol;
    if (!isfinite(vol))
        vol = 0.0f;
    left_gain *= vol;
    right_gain *= vol;

    /* Convert to fixed-point for efficiency */
    int32_t left_gain_fp = (int32_t)(left_gain * 256.0f);
    int32_t right_gain_fp = (int32_t)(right_gain * 256.0f);

    for (int32_t i = 0; i < frames; i++) {
        if (pos >= sound_frames) {
            if (voice->loop) {
                pos = 0;
            } else {
                voice->state = VAUD_VOICE_INACTIVE;
                voice->sound = NULL;
                voice->position = pos;
                return 0;
            }
        }

        /* Source is stereo interleaved */
        int16_t src_left = samples[pos * 2];
        int16_t src_right = samples[pos * 2 + 1];

        /* Apply volume and panning, accumulate into output */
        output[i * 2] += (src_left * left_gain_fp) >> 8;
        output[i * 2 + 1] += (src_right * right_gain_fp) >> 8;

        pos++;
    }

    voice->position = pos;
    return 1;
}

//===----------------------------------------------------------------------===//
// Music Mixing
//===----------------------------------------------------------------------===//

/// @brief Mix music stream into the output buffer.
/// @param music Music stream to mix.
/// @param output Output buffer (stereo interleaved).
/// @param frames Number of frames to mix.
/// @param master_vol Master volume multiplier.
static void mix_music(vaud_music_t music, int32_t *output, int32_t frames, float master_vol) {
    if (!music || music->state != VAUD_MUSIC_PLAYING)
        return;
    if (music->refill_in_progress)
        return;

    float vol = music->volume * master_vol;
    if (!isfinite(vol))
        vol = 0.0f;
    int32_t vol_fp = (int32_t)(vol * 256.0f);

    int32_t frames_remaining = frames;
    int32_t output_offset = 0;

    while (frames_remaining > 0) {
        if (music->buffer_position >= music->buffer_frames[music->current_buffer]) {
            music->buffer_frames[music->current_buffer] = 0;
            music->buffer_position = 0;
            music->current_buffer = (music->current_buffer + 1) % VAUD_MUSIC_BUFFER_COUNT;

            if (music->buffer_frames[music->current_buffer] <= 0) {
                if (music->loop && music->stream_eof) {
                    music->stream_loop_pending = 1;
                }

                if (music->buffer_frames[music->current_buffer] <= 0 && !music->loop &&
                    music->stream_eof) {
                    music->state = VAUD_MUSIC_STOPPED;
                }
                if (music->buffer_frames[music->current_buffer] <= 0)
                    return;
            }
        }

        /* Mix from current buffer */
        int16_t *src = music->buffers[music->current_buffer];
        int32_t available = music->buffer_frames[music->current_buffer] - music->buffer_position;
        int32_t to_mix = (frames_remaining < available) ? frames_remaining : available;

        int32_t src_offset = music->buffer_position * 2; /* Stereo */
        for (int32_t i = 0; i < to_mix; i++) {
            int16_t left = src[src_offset + i * 2];
            int16_t right = src[src_offset + i * 2 + 1];

            output[(output_offset + i) * 2] += (left * vol_fp) >> 8;
            output[(output_offset + i) * 2 + 1] += (right * vol_fp) >> 8;
        }

        music->buffer_position += to_mix;
        music->position += to_mix;
        output_offset += to_mix;
        frames_remaining -= to_mix;
    }
}

//===----------------------------------------------------------------------===//
// Main Mixer Entry Point
//===----------------------------------------------------------------------===//

void vaud_mixer_render(vaud_context_t ctx, int16_t *output, int32_t frames) {
    if (!ctx || !output || frames <= 0)
        return;

    if (vaud_atomic_load_i32(&ctx->destroying) != 0) {
        size_t output_bytes = 0;
        if (vaud_pcm_s16_buffer_size(frames, VAUD_CHANNELS, &output_bytes))
            memset(output, 0, output_bytes);
        return;
    }

    if (frames > VAUD_BUFFER_FRAMES) {
        int32_t offset = 0;
        while (offset < frames) {
            int32_t chunk = frames - offset;
            if (chunk > VAUD_BUFFER_FRAMES)
                chunk = VAUD_BUFFER_FRAMES;
            vaud_mixer_render(ctx, output + (size_t)offset * VAUD_CHANNELS, chunk);
            offset += chunk;
        }
        return;
    }

    /* H-1: Use pre-allocated 32-bit accumulator (no malloc in real-time audio callback).
     * ctx->accum_buf holds VAUD_BUFFER_FRAMES * VAUD_CHANNELS int32s. Oversized
     * backend periods are rendered above in bounded chunks. */
    int32_t *accum = ctx->accum_buf;

    /* Clear accumulator */
    size_t sample_count = (size_t)frames * (size_t)VAUD_CHANNELS;
    memset(accum, 0, sample_count * sizeof(int32_t));

    /* Correctness beats dropping an otherwise valid buffer: wait for the state
     * lock instead of emitting silence when control-side work is in progress. */
    vaud_mutex_lock(&ctx->mutex);

    float master = ctx->master_volume;
    if (!isfinite(master))
        master = 0.0f;

    /* Mix all active voices */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
        mix_voice(&ctx->voices[i], accum, frames, master);
    }

    /* Mix active music streams */
    for (int32_t i = 0; i < ctx->music_count; i++) {
        if (ctx->active_music[i]) {
            mix_music(ctx->active_music[i], accum, frames, master);
        }
    }

    ctx->frame_counter += frames;

    vaud_mutex_unlock(&ctx->mutex);

    /* Convert to 16-bit with soft clipping */
    for (size_t i = 0; i < sample_count; i++) {
        output[i] = soft_clip(accum[i]);
    }
    /* H-1: accum is ctx->accum_buf — no free needed */
}

//===----------------------------------------------------------------------===//
// Voice Management
//===----------------------------------------------------------------------===//

static vaud_voice_id vaud_next_voice_id(vaud_context_t ctx) {
    if (!ctx)
        return VAUD_INVALID_VOICE;

    for (int32_t attempts = 0; attempts < INT32_MAX; attempts++) {
        int32_t candidate = ctx->next_voice_id;
        if (ctx->next_voice_id >= INT32_MAX || ctx->next_voice_id <= 0 ||
            ctx->next_voice_id == VAUD_INVALID_VOICE) {
            ctx->next_voice_id = 1;
        } else {
            ctx->next_voice_id++;
        }
        if (candidate <= 0 || candidate == VAUD_INVALID_VOICE)
            continue;

        int in_use = 0;
        for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
            if (ctx->voices[i].state != VAUD_VOICE_INACTIVE && ctx->voices[i].id == candidate) {
                in_use = 1;
                break;
            }
        }
        if (!in_use)
            return (vaud_voice_id)candidate;
    }

    return VAUD_INVALID_VOICE;
}

vaud_voice *vaud_alloc_voice(vaud_context_t ctx) {
    if (!ctx)
        return NULL;

    vaud_voice *oldest = NULL;
    int64_t oldest_time = ctx->frame_counter + 1;

    /* First pass: look for inactive voice */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
        if (ctx->voices[i].state == VAUD_VOICE_INACTIVE) {
            ctx->voices[i].id = vaud_next_voice_id(ctx);
            if (ctx->voices[i].id == VAUD_INVALID_VOICE)
                return NULL;
            ctx->voices[i].start_time = ctx->frame_counter;
            return &ctx->voices[i];
        }

        /* Track oldest non-looping voice for stealing */
        if (!ctx->voices[i].loop && ctx->voices[i].start_time < oldest_time) {
            oldest = &ctx->voices[i];
            oldest_time = ctx->voices[i].start_time;
        }
    }

    /* Second pass: steal oldest non-looping voice */
    if (oldest) {
        oldest->state = VAUD_VOICE_INACTIVE;
        oldest->sound = NULL;
        oldest->id = vaud_next_voice_id(ctx);
        if (oldest->id == VAUD_INVALID_VOICE)
            return NULL;
        oldest->start_time = ctx->frame_counter;
        return oldest;
    }

    /* All voices are looping - steal absolute oldest */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
        if (ctx->voices[i].start_time < oldest_time) {
            oldest = &ctx->voices[i];
            oldest_time = ctx->voices[i].start_time;
        }
    }

    if (oldest) {
        oldest->state = VAUD_VOICE_INACTIVE;
        oldest->sound = NULL;
        oldest->id = vaud_next_voice_id(ctx);
        if (oldest->id == VAUD_INVALID_VOICE)
            return NULL;
        oldest->start_time = ctx->frame_counter;
        return oldest;
    }

    return NULL;
}

vaud_voice *vaud_find_voice(vaud_context_t ctx, vaud_voice_id id) {
    if (!ctx || id == VAUD_INVALID_VOICE)
        return NULL;

    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
        if (ctx->voices[i].id == id && ctx->voices[i].state != VAUD_VOICE_INACTIVE) {
            return &ctx->voices[i];
        }
    }

    return NULL;
}
