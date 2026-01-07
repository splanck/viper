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
static inline int16_t soft_clip(int32_t sample)
{
    if (sample > VAUD_CLIP_THRESHOLD)
    {
        float excess = (float)(sample - VAUD_CLIP_THRESHOLD);
        float compressed = VAUD_CLIP_THRESHOLD + excess * VAUD_CLIP_KNEE;
        sample = (int32_t)compressed;
    }
    else if (sample < -VAUD_CLIP_THRESHOLD)
    {
        float excess = (float)(-sample - VAUD_CLIP_THRESHOLD);
        float compressed = -(VAUD_CLIP_THRESHOLD + excess * VAUD_CLIP_KNEE);
        sample = (int32_t)compressed;
    }

    /* Hard clamp as final safety */
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return (int16_t)sample;
}

/// @brief Calculate left/right gain from pan value.
/// @param pan Pan value (-1.0 = left, 0.0 = center, 1.0 = right).
/// @param left_gain Output: left channel gain.
/// @param right_gain Output: right channel gain.
static void calculate_pan_gains(float pan, float *left_gain, float *right_gain)
{
    /* Constant power panning law */
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    /* Simple linear pan for efficiency (close enough for games) */
    *left_gain = 1.0f - (pan + 1.0f) * 0.5f * 0.5f;   /* 1.0 at left, 0.5 at right */
    *right_gain = 0.5f + (pan + 1.0f) * 0.5f * 0.5f;  /* 0.5 at left, 1.0 at right */
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
static int mix_voice(vaud_voice *voice, int32_t *output, int32_t frames, float master_vol)
{
    if (!voice || voice->state != VAUD_VOICE_PLAYING || !voice->sound)
        return 0;

    vaud_sound_t sound = voice->sound;
    const int16_t *samples = sound->samples;
    int64_t sound_frames = sound->frame_count;
    int64_t pos = voice->position;

    float left_gain, right_gain;
    calculate_pan_gains(voice->pan, &left_gain, &right_gain);

    float vol = voice->volume * master_vol;
    left_gain *= vol;
    right_gain *= vol;

    /* Convert to fixed-point for efficiency */
    int32_t left_gain_fp = (int32_t)(left_gain * 256.0f);
    int32_t right_gain_fp = (int32_t)(right_gain * 256.0f);

    for (int32_t i = 0; i < frames; i++)
    {
        if (pos >= sound_frames)
        {
            if (voice->loop)
            {
                pos = 0;
            }
            else
            {
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
static void mix_music(vaud_music_t music, int32_t *output, int32_t frames, float master_vol)
{
    if (!music || music->state != VAUD_MUSIC_PLAYING)
        return;

    float vol = music->volume * master_vol;
    int32_t vol_fp = (int32_t)(vol * 256.0f);

    int32_t frames_remaining = frames;
    int32_t output_offset = 0;

    while (frames_remaining > 0)
    {
        /* Check if we need to refill buffer */
        if (music->buffer_position >= music->buffer_frames[music->current_buffer])
        {
            /* Move to next buffer */
            int32_t next_buffer = (music->current_buffer + 1) % VAUD_MUSIC_BUFFER_COUNT;

            /* Refill current buffer in background (simplified: do it inline for now) */
            if (music->file)
            {
                int16_t *buf = music->buffers[music->current_buffer];
                int32_t read = vaud_wav_read_frames(music->file, buf, VAUD_MUSIC_BUFFER_FRAMES,
                                                    music->channels, music->bits_per_sample);

                if (read == 0)
                {
                    if (music->loop)
                    {
                        /* Seek to beginning of data */
                        fseek((FILE *)music->file, (long)music->position, SEEK_SET);
                        music->position = 0;
                        read = vaud_wav_read_frames(music->file, buf, VAUD_MUSIC_BUFFER_FRAMES,
                                                    music->channels, music->bits_per_sample);
                    }

                    if (read == 0)
                    {
                        music->state = VAUD_MUSIC_STOPPED;
                        return;
                    }
                }

                music->buffer_frames[music->current_buffer] = read;
            }

            music->current_buffer = next_buffer;
            music->buffer_position = 0;
        }

        /* Mix from current buffer */
        int16_t *src = music->buffers[music->current_buffer];
        int32_t available = music->buffer_frames[music->current_buffer] - music->buffer_position;
        int32_t to_mix = (frames_remaining < available) ? frames_remaining : available;

        int32_t src_offset = music->buffer_position * 2;  /* Stereo */
        for (int32_t i = 0; i < to_mix; i++)
        {
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

void vaud_mixer_render(vaud_context_t ctx, int16_t *output, int32_t frames)
{
    if (!ctx || !output || frames <= 0)
        return;

    /* Use 32-bit accumulator to prevent clipping during mixing */
    int32_t *accum = (int32_t *)malloc((size_t)(frames * 2 * sizeof(int32_t)));
    if (!accum)
    {
        /* Fallback: output silence */
        memset(output, 0, (size_t)(frames * 2 * sizeof(int16_t)));
        return;
    }

    /* Clear accumulator */
    memset(accum, 0, (size_t)(frames * 2 * sizeof(int32_t)));

    /* Lock mixer state */
    vaud_mutex_lock(&ctx->mutex);

    float master = ctx->master_volume;

    /* Mix all active voices */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        mix_voice(&ctx->voices[i], accum, frames, master);
    }

    /* Mix active music streams */
    for (int32_t i = 0; i < ctx->music_count; i++)
    {
        if (ctx->active_music[i])
        {
            mix_music(ctx->active_music[i], accum, frames, master);
        }
    }

    ctx->frame_counter += frames;

    vaud_mutex_unlock(&ctx->mutex);

    /* Convert to 16-bit with soft clipping */
    for (int32_t i = 0; i < frames * 2; i++)
    {
        output[i] = soft_clip(accum[i]);
    }

    free(accum);
}

//===----------------------------------------------------------------------===//
// Voice Management
//===----------------------------------------------------------------------===//

vaud_voice *vaud_alloc_voice(vaud_context_t ctx)
{
    if (!ctx)
        return NULL;

    vaud_voice *oldest = NULL;
    int64_t oldest_time = ctx->frame_counter + 1;

    /* First pass: look for inactive voice */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        if (ctx->voices[i].state == VAUD_VOICE_INACTIVE)
        {
            ctx->voices[i].id = ctx->next_voice_id++;
            ctx->voices[i].start_time = ctx->frame_counter;
            return &ctx->voices[i];
        }

        /* Track oldest non-looping voice for stealing */
        if (!ctx->voices[i].loop && ctx->voices[i].start_time < oldest_time)
        {
            oldest = &ctx->voices[i];
            oldest_time = ctx->voices[i].start_time;
        }
    }

    /* Second pass: steal oldest non-looping voice */
    if (oldest)
    {
        oldest->state = VAUD_VOICE_INACTIVE;
        oldest->sound = NULL;
        oldest->id = ctx->next_voice_id++;
        oldest->start_time = ctx->frame_counter;
        return oldest;
    }

    /* All voices are looping - steal absolute oldest */
    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        if (ctx->voices[i].start_time < oldest_time)
        {
            oldest = &ctx->voices[i];
            oldest_time = ctx->voices[i].start_time;
        }
    }

    if (oldest)
    {
        oldest->state = VAUD_VOICE_INACTIVE;
        oldest->sound = NULL;
        oldest->id = ctx->next_voice_id++;
        oldest->start_time = ctx->frame_counter;
        return oldest;
    }

    return NULL;
}

vaud_voice *vaud_find_voice(vaud_context_t ctx, vaud_voice_id id)
{
    if (!ctx || id == VAUD_INVALID_VOICE)
        return NULL;

    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++)
    {
        if (ctx->voices[i].id == id && ctx->voices[i].state != VAUD_VOICE_INACTIVE)
        {
            return &ctx->voices[i];
        }
    }

    return NULL;
}
