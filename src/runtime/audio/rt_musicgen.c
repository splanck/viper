//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_musicgen.c
// Purpose: Procedural music composition — tracker-style sequencer that pre-
//          renders multi-channel songs with ADSR envelopes and chiptune effects
//          into a Sound object via rt_sound_load_mem. Zero external dependencies.
//
// Key invariants:
//   - Output is always 16-bit stereo PCM at 44100 Hz.
//   - WAV data is built in a temporary heap buffer, loaded, then freed.
//   - Sine approximation uses Bhaskara I's formula (no libm dependency).
//   - All parameter values are clamped to safe ranges.
//   - Notes are sorted by beat position before rendering (required for
//     portamento which needs the previous note's frequency).
//   - The 32-bit stereo accumulator prevents clipping during channel mixing.
//
// Ownership/Lifetime:
//   - Song builder allocated via rt_obj_new_i64, no finalizer (pure data).
//   - Temporary PCM/WAV buffers are malloc'd and freed within Build().
//   - Returned Sound objects are GC-managed with refcount 1.
//
// Links: rt_musicgen.h (public API), rt_audio.h (rt_sound_load_mem),
//        rt_synth.c (reference for waveform/WAV patterns)
//
//===----------------------------------------------------------------------===//

#include "rt_musicgen.h"
#include "rt_audio.h"
#include "rt_object.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

#define MG_SAMPLE_RATE 44100
#define MG_CHANNELS 2 /* stereo output */
#define MG_BITS 16
#define MG_MAX_AMP 30000 /* below INT16_MAX to leave headroom */
#define MG_WAV_HEADER 44
#define MG_MAX_DURATION_S (5 * 60) /* 5 minutes */
#define MG_CROSSFADE_MS 10         /* loop crossfade duration */

#define MG_PI 3.14159265358979323846
#define MG_2PI 6.28318530717958647692

//===----------------------------------------------------------------------===//
// Internal Data Structures
//===----------------------------------------------------------------------===//

typedef struct {
    int64_t attack_ms;
    int64_t decay_ms;
    int64_t sustain_pct;
    int64_t release_ms;
} mg_envelope_t;

typedef struct {
    int64_t beat_pos;
    int64_t midi_note;
    int64_t duration;
    int64_t velocity;
} mg_note_t;

typedef struct {
    int64_t waveform;
    mg_envelope_t envelope;
    mg_note_t notes[MUSICGEN_MAX_NOTES];
    int32_t note_count;

    /* Basic settings */
    int64_t volume;
    int64_t duty_cycle;
    int64_t pan;
    int64_t detune_cents;

    /* Effects */
    int64_t vibrato_depth;
    int64_t vibrato_speed;
    int64_t tremolo_depth;
    int64_t tremolo_speed;
    int64_t arp_semi1;
    int64_t arp_semi2;
    int64_t arp_speed;
    int64_t portamento_ms;
} mg_channel_t;

typedef struct {
    void *vptr;
    int64_t bpm;
    int64_t length_centbeats;
    int64_t swing;
    int32_t loopable;
    mg_channel_t channels[MUSICGEN_MAX_CHANNELS];
    int32_t channel_count;
} mg_song_t;

//===----------------------------------------------------------------------===//
// Clamping Helpers
//===----------------------------------------------------------------------===//

/// @brief Clamp `v` into the inclusive range `[lo, hi]`.
static int64_t mg_clamp(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

//===----------------------------------------------------------------------===//
// Sine Approximation (no libm — identical to rt_synth.c)
//===----------------------------------------------------------------------===//

/// Bhaskara I's approximation. Phase in [0,1) maps to [0,2*PI).
/// Max error ~0.08% — inaudible for audio synthesis.
static double mg_sin(double phase) {
    phase = phase - (double)(int64_t)phase;
    if (phase < 0.0)
        phase += 1.0;

    double x = phase * MG_2PI;
    int negate = 0;
    if (x > MG_PI) {
        x -= MG_PI;
        negate = 1;
    }

    double xpi = x * (MG_PI - x);
    double result = 16.0 * xpi / (5.0 * MG_PI * MG_PI - 4.0 * xpi);

    return negate ? -result : result;
}

//===----------------------------------------------------------------------===//
// MIDI-to-Frequency Table (128 entries, no libm)
//===----------------------------------------------------------------------===//

/// Pre-computed frequencies for MIDI notes 0-127.
/// Formula: freq = 440 * 2^((note - 69) / 12)
static const double midi_freq[128] = {
    /* C-1  to B-1  (MIDI 0-11) */
    8.17580,
    8.66196,
    9.17702,
    9.72272,
    10.30086,
    10.91338,
    11.56233,
    12.24986,
    12.97827,
    13.75000,
    14.56762,
    15.43385,
    /* C0   to B0   (MIDI 12-23) */
    16.35160,
    17.32391,
    18.35405,
    19.44544,
    20.60172,
    21.82676,
    23.12465,
    24.49971,
    25.95654,
    27.50000,
    29.13524,
    30.86771,
    /* C1   to B1   (MIDI 24-35) */
    32.70320,
    34.64783,
    36.70810,
    38.89087,
    41.20344,
    43.65353,
    46.24930,
    48.99943,
    51.91309,
    55.00000,
    58.27047,
    61.73541,
    /* C2   to B2   (MIDI 36-47) */
    65.40639,
    69.29566,
    73.41619,
    77.78175,
    82.40689,
    87.30706,
    92.49861,
    97.99886,
    103.82617,
    110.00000,
    116.54094,
    123.47083,
    /* C3   to B3   (MIDI 48-59) */
    130.81278,
    138.59132,
    146.83238,
    155.56349,
    164.81378,
    174.61412,
    184.99721,
    195.99772,
    207.65235,
    220.00000,
    233.08188,
    246.94165,
    /* C4   to B4   (MIDI 60-71) */
    261.62557,
    277.18263,
    293.66477,
    311.12698,
    329.62756,
    349.22823,
    369.99442,
    391.99544,
    415.30470,
    440.00000,
    466.16376,
    493.88330,
    /* C5   to B5   (MIDI 72-83) */
    523.25113,
    554.36526,
    587.32954,
    622.25397,
    659.25511,
    698.45646,
    739.98885,
    783.99087,
    830.60940,
    880.00000,
    932.32752,
    987.76660,
    /* C6   to B6   (MIDI 84-95) */
    1046.50226,
    1108.73052,
    1174.65907,
    1244.50793,
    1318.51023,
    1396.91293,
    1479.97769,
    1567.98174,
    1661.21879,
    1760.00000,
    1864.65505,
    1975.53321,
    /* C7   to B7   (MIDI 96-107) */
    2093.00452,
    2217.46105,
    2349.31814,
    2489.01587,
    2637.02046,
    2793.82585,
    2959.95538,
    3135.96349,
    3322.43758,
    3520.00000,
    3729.31009,
    3951.06641,
    /* C8   to B8   (MIDI 108-119) */
    4186.00904,
    4434.92210,
    4698.63629,
    4978.03174,
    5274.04091,
    5587.65170,
    5919.91076,
    6271.92698,
    6644.87516,
    7040.00000,
    7458.62018,
    7902.13282,
    /* C9   to G9   (MIDI 120-127) */
    8372.01809,
    8869.84419,
    9397.27257,
    9956.06348,
    10548.08182,
    11175.30341,
    11839.82153,
    12543.85306};

//===----------------------------------------------------------------------===//
// pow2_cents — Pitch Offset Helper (no libm)
//===----------------------------------------------------------------------===//

/// Pre-computed 2^(i/12) for i=0..12 (semitone ratios within one octave).
static const double semitone_ratio[13] = {
    1.00000000000, /* 0  */
    1.05946309436, /* 1  */
    1.12246204831, /* 2  */
    1.18920711500, /* 3  */
    1.25992104989, /* 4  */
    1.33483985417, /* 5  */
    1.41421356237, /* 6  */
    1.49830707688, /* 7  */
    1.58740105197, /* 8  */
    1.68179283051, /* 9  */
    1.78179743628, /* 10 */
    1.88774862536, /* 11 */
    2.00000000000  /* 12 */
};

/// @brief Compute 2^(cents/1200) without libm.
/// @param cents Pitch offset in cents (-2400 to +2400).
/// @return Frequency multiplier.
static double pow2_cents(int64_t cents) {
    if (cents == 0)
        return 1.0;

    /* Handle negative values via reciprocal */
    int negate = 0;
    if (cents < 0) {
        negate = 1;
        cents = -cents;
    }

    /* Decompose: cents = semitones * 100 + remainder */
    int64_t semitones = cents / 100;
    int64_t remainder = cents % 100;

    /* Octave component: 2^(semitones/12) */
    /* Split into octaves and sub-octave semitones */
    int64_t octaves = semitones / 12;
    int64_t sub_semi = semitones % 12;

    /* Start with octave power (exact powers of 2) */
    double result = 1.0;
    for (int64_t i = 0; i < octaves; i++)
        result *= 2.0;

    /* Apply sub-octave semitone from table */
    result *= semitone_ratio[sub_semi];

    /* Apply fractional cents via linear interpolation between semitone ratios */
    if (remainder > 0) {
        double lo = semitone_ratio[sub_semi];
        double hi = semitone_ratio[sub_semi + 1];
        /* Linear interp: approximate 2^(remainder/1200) */
        double frac = (double)remainder / 100.0;
        /* We already applied lo via result, so scale by (hi/lo)^frac ≈ 1 + frac*(hi/lo - 1) */
        double ratio = hi / lo;
        result *= 1.0 + frac * (ratio - 1.0);
    }

    return negate ? (1.0 / result) : result;
}

//===----------------------------------------------------------------------===//
// Waveform Generation
//===----------------------------------------------------------------------===//

/// @brief Generate a waveform sample with duty cycle support.
/// @param phase Normalized phase [0.0, 1.0).
/// @param waveform Waveform type (0-3). Noise handled separately.
/// @param duty Duty cycle percentage (0-100, only for square wave).
/// @return Sample value in [-1.0, 1.0].
static double mg_waveform(double phase, int64_t waveform, int64_t duty) {
    phase = phase - (double)(int64_t)phase;
    if (phase < 0.0)
        phase += 1.0;

    switch (waveform) {
        case MUSICGEN_WAVE_SQUARE: {
            double threshold = (double)duty / 100.0;
            if (threshold < 0.01)
                threshold = 0.01;
            if (threshold > 0.99)
                threshold = 0.99;
            return phase < threshold ? 1.0 : -1.0;
        }

        case MUSICGEN_WAVE_SAWTOOTH:
            return 2.0 * phase - 1.0;

        case MUSICGEN_WAVE_TRIANGLE:
            if (phase < 0.25)
                return 4.0 * phase;
            else if (phase < 0.75)
                return 2.0 - 4.0 * phase;
            else
                return 4.0 * phase - 4.0;

        case MUSICGEN_WAVE_SINE:
        default:
            return mg_sin(phase);
    }
}

//===----------------------------------------------------------------------===//
// ADSR Envelope
//===----------------------------------------------------------------------===//

/// @brief Calculate ADSR envelope amplitude at a given sample offset.
/// @param env Envelope parameters.
/// @param sample_offset Samples since note-on.
/// @param note_dur_samples Note duration in samples (before release).
/// @return Amplitude multiplier in [0.0, 1.0].
static double mg_adsr_note_level_at(const mg_envelope_t *env, double t_s) {
    double atk_s = (double)env->attack_ms / 1000.0;
    double dec_s = (double)env->decay_ms / 1000.0;
    double sus = (double)env->sustain_pct / 100.0;

    if (t_s < atk_s)
        return (atk_s > 0.0) ? (t_s / atk_s) : 1.0;

    t_s -= atk_s;
    if (t_s < dec_s) {
        double decay_t = (dec_s > 0.0) ? (t_s / dec_s) : 1.0;
        return 1.0 - (1.0 - sus) * decay_t;
    }

    return sus;
}

static double mg_adsr(const mg_envelope_t *env, int32_t sample_offset, int32_t note_dur_samples) {
    double t_s = (double)sample_offset / (double)MG_SAMPLE_RATE;
    double rel_s = (double)env->release_ms / 1000.0;
    double note_dur_s = (double)note_dur_samples / (double)MG_SAMPLE_RATE;

    /* Sustain phase — hold until note-off */
    if (t_s < note_dur_s)
        return mg_adsr_note_level_at(env, t_s);

    /* Release phase */
    double rel_t = t_s - note_dur_s;
    if (rel_t >= rel_s)
        return 0.0;

    double release_start = mg_adsr_note_level_at(env, note_dur_s);
    return (rel_s > 0.0) ? (release_start * (1.0 - rel_t / rel_s)) : 0.0;
}

//===----------------------------------------------------------------------===//
// Noise Generator
//===----------------------------------------------------------------------===//

/// Simple LCG PRNG state for deterministic noise.
typedef struct {
    uint32_t state;
    double prev_sample; /* for one-pole lowpass filter */
} mg_noise_t;

/// @brief Initialize noise generator with a seed.
static void mg_noise_init(mg_noise_t *n, uint32_t seed) {
    n->state = seed;
    n->prev_sample = 0.0;
}

/// @brief Generate filtered noise sample.
/// @param n Noise state.
/// @param cutoff_freq Lowpass cutoff frequency in Hz.
/// @return Sample in [-1.0, 1.0].
static double mg_noise_sample(mg_noise_t *n, double cutoff_freq) {
    /* LCG step */
    n->state = n->state * 1103515245u + 12345u;
    double white = (double)(int16_t)(n->state >> 16) / 32768.0;

    /* One-pole lowpass: y[n] = y[n-1] + alpha * (x[n] - y[n-1])
       alpha = dt / (RC + dt), where RC = 1/(2*PI*cutoff) */
    double dt = 1.0 / (double)MG_SAMPLE_RATE;
    double rc = 1.0 / (MG_2PI * cutoff_freq);
    double alpha = dt / (rc + dt);

    /* Clamp alpha for stability */
    if (alpha > 1.0)
        alpha = 1.0;
    if (alpha < 0.001)
        alpha = 0.001;

    n->prev_sample = n->prev_sample + alpha * (white - n->prev_sample);
    return n->prev_sample;
}

//===----------------------------------------------------------------------===//
// Note Sorting (for portamento)
//===----------------------------------------------------------------------===//

/// @brief Compare notes by beat position for qsort.
static int mg_note_compare(const void *a, const void *b) {
    const mg_note_t *na = (const mg_note_t *)a;
    const mg_note_t *nb = (const mg_note_t *)b;
    if (na->beat_pos < nb->beat_pos)
        return -1;
    if (na->beat_pos > nb->beat_pos)
        return 1;
    return 0;
}

//===----------------------------------------------------------------------===//
// WAV Header (stereo variant)
//===----------------------------------------------------------------------===//

/// @brief Write a WAV header for stereo 16-bit PCM data.
static void mg_write_wav_header(uint8_t *buf, int32_t num_frames) {
    int32_t data_size = num_frames * MG_CHANNELS * (MG_BITS / 8);
    int32_t file_size = MG_WAV_HEADER + data_size - 8;
    int32_t byte_rate = MG_SAMPLE_RATE * MG_CHANNELS * (MG_BITS / 8);
    int16_t block_align = MG_CHANNELS * (MG_BITS / 8);

    memcpy(buf + 0, "RIFF", 4);
    memcpy(buf + 4, &file_size, 4);
    memcpy(buf + 8, "WAVE", 4);

    memcpy(buf + 12, "fmt ", 4);
    int32_t fmt_size = 16;
    memcpy(buf + 16, &fmt_size, 4);
    int16_t audio_format = 1; /* PCM */
    memcpy(buf + 20, &audio_format, 2);
    int16_t channels = MG_CHANNELS;
    memcpy(buf + 22, &channels, 2);
    int32_t sample_rate = MG_SAMPLE_RATE;
    memcpy(buf + 24, &sample_rate, 4);
    memcpy(buf + 28, &byte_rate, 4);
    memcpy(buf + 32, &block_align, 2);
    int16_t bits = MG_BITS;
    memcpy(buf + 34, &bits, 2);

    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_size, 4);
}

//===----------------------------------------------------------------------===//
// Render a Single Note into Accumulator
//===----------------------------------------------------------------------===//

/// Per-channel rendering state tracked across notes.
typedef struct {
    double prev_freq; /* last note's target frequency (for portamento) */
} mg_render_state_t;

/// @brief Render one note's audio into the stereo accumulator.
/// @param accum 32-bit stereo interleaved accumulator.
/// @param total_frames Total frames in the accumulator.
/// @param note The note to render.
/// @param chan The channel configuration.
/// @param samples_per_beat Samples per beat (derived from BPM).
/// @param swing Swing amount (0-100).
/// @param state Per-channel render state (portamento).
/// @param channel_count Number of active channels (for gain division).
static void mg_render_note(int32_t *accum,
                           int32_t total_frames,
                           const mg_note_t *note,
                           const mg_channel_t *chan,
                           int32_t samples_per_beat,
                           int64_t swing,
                           mg_render_state_t *state,
                           int32_t channel_count) {
    /* Calculate note timing */
    int32_t start = (int32_t)((note->beat_pos * (int64_t)samples_per_beat) / 100);

    /* Apply swing: shift notes on off-beats (at half-beat boundaries) */
    if (swing > 0) {
        int64_t half_beat = 50; /* centbeats */
        /* Check if this note falls on an odd half-beat */
        int64_t half_beats = note->beat_pos / half_beat;
        if ((half_beats % 2) == 1) {
            int32_t shift = (int32_t)((swing * samples_per_beat / 2) / 100);
            start += shift;
        }
    }

    int32_t dur = (int32_t)((note->duration * (int64_t)samples_per_beat) / 100);
    int32_t rel_samples = (int32_t)(chan->envelope.release_ms * MG_SAMPLE_RATE / 1000);
    int32_t end = start + dur + rel_samples;

    if (start >= total_frames)
        return;
    if (end > total_frames)
        end = total_frames;
    if (start < 0)
        start = 0;

    /* Base frequency from MIDI table */
    int64_t base_midi = mg_clamp(note->midi_note, 0, 127);
    double base_freq = midi_freq[base_midi];

    /* Apply detune */
    double detuned_freq = base_freq * pow2_cents(chan->detune_cents);

    /* Portamento: glide from previous note's frequency */
    double porta_start_freq = detuned_freq;
    int32_t porta_samples = 0;
    if (chan->portamento_ms > 0 && state->prev_freq > 0.0) {
        porta_start_freq = state->prev_freq;
        porta_samples = (int32_t)(chan->portamento_ms * MG_SAMPLE_RATE / 1000);
    }

    /* Update state for next note's portamento */
    state->prev_freq = detuned_freq;

    /* Velocity and volume scaling */
    double vel_scale = (double)mg_clamp(note->velocity, 0, 100) / 100.0;
    double vol_scale = (double)mg_clamp(chan->volume, 0, 100) / 100.0;
    double gain = vel_scale * vol_scale * (double)MG_MAX_AMP / (double)channel_count;

    /* Pan gains (equal-power approximation via linear law) */
    int64_t pan = mg_clamp(chan->pan, -100, 100);
    double left_gain = (double)(100 - pan) / 200.0;
    double right_gain = (double)(100 + pan) / 200.0;

    /* Effect parameters */
    double vib_depth = (double)chan->vibrato_depth;
    double vib_speed_hz = (double)chan->vibrato_speed / 100.0;
    double trem_depth_frac = (double)chan->tremolo_depth / 200.0;
    double trem_speed_hz = (double)chan->tremolo_speed / 100.0;
    double arp_speed_hz = (double)chan->arp_speed / 100.0;
    int arp_enabled = (chan->arp_semi1 != 0 || chan->arp_semi2 != 0);

    /* Noise state (if noise channel) */
    mg_noise_t noise;
    if (chan->waveform == MUSICGEN_WAVE_NOISE) {
        /* Seed noise deterministically per note index */
        uint32_t seed = (uint32_t)(note->beat_pos * 0x9E3779B9u + note->midi_note * 0x517CC1B7u);
        mg_noise_init(&noise, seed);
    }

    /* Waveform phase accumulator */
    double phase = 0.0;
    double vibrato_phase = 0.0;
    double tremolo_phase = 0.0;

    for (int32_t i = start; i < end; i++) {
        int32_t offset = i - start;
        double elapsed_s = (double)offset / (double)MG_SAMPLE_RATE;

        /* --- Frequency chain --- */
        double freq = detuned_freq;

        /* Portamento: lerp from previous note frequency */
        if (porta_samples > 0 && offset < porta_samples) {
            double t = (double)offset / (double)porta_samples;
            freq = porta_start_freq + (detuned_freq - porta_start_freq) * t;
        }

        /* Arpeggio: cycle through [0, semi1, semi2] */
        if (arp_enabled && arp_speed_hz > 0.0) {
            int arp_step = (int)(elapsed_s * arp_speed_hz) % 3;
            int64_t arp_offset = 0;
            if (arp_step == 1)
                arp_offset = chan->arp_semi1;
            else if (arp_step == 2)
                arp_offset = chan->arp_semi2;
            if (arp_offset != 0) {
                int64_t arp_midi = mg_clamp(base_midi + arp_offset, 0, 127);
                double arp_freq = midi_freq[arp_midi];
                /* Apply the ratio of arpeggio freq to base freq */
                freq *= arp_freq / base_freq;
            }
        }

        /* Vibrato: sinusoidal pitch modulation */
        if (vib_depth > 0.0 && vib_speed_hz > 0.0) {
            double vib_val = mg_sin(vibrato_phase);
            freq *= pow2_cents((int64_t)(vib_val * vib_depth));
            vibrato_phase += vib_speed_hz / (double)MG_SAMPLE_RATE;
        }

        /* --- Sample generation --- */
        double sample;
        if (chan->waveform == MUSICGEN_WAVE_NOISE) {
            /* Noise channel: MIDI note controls lowpass cutoff */
            double cutoff = midi_freq[mg_clamp(base_midi, 10, 120)];
            sample = mg_noise_sample(&noise, cutoff);
        } else {
            sample = mg_waveform(phase, chan->waveform, chan->duty_cycle);
            phase += freq / (double)MG_SAMPLE_RATE;
        }

        /* ADSR envelope */
        double env = mg_adsr(&chan->envelope, offset, dur);

        /* Tremolo: sinusoidal volume modulation */
        double trem = 1.0;
        if (trem_depth_frac > 0.0 && trem_speed_hz > 0.0) {
            double trem_val = mg_sin(tremolo_phase);
            trem = 1.0 - trem_depth_frac * (1.0 + trem_val);
            if (trem < 0.0)
                trem = 0.0;
            tremolo_phase += trem_speed_hz / (double)MG_SAMPLE_RATE;
        }

        /* Final amplitude */
        double amp = sample * env * trem * gain;

        /* Accumulate into stereo buffer */
        int32_t idx = i * 2;
        accum[idx] += (int32_t)(amp * left_gain);
        accum[idx + 1] += (int32_t)(amp * right_gain);
    }
}

//===----------------------------------------------------------------------===//
// Soft Clipping (matches vaud_mixer.c approach)
//===----------------------------------------------------------------------===//

#define MG_CLIP_THRESHOLD 28000

/// @brief Soft-clip a 32-bit accumulator into the 16-bit PCM range with knee compression.
///
/// Beyond `MG_CLIP_THRESHOLD` (~28k of int16's 32767 range) we
/// scale the excess by 1/4 to round off the corner instead of
/// hard-clipping. Saturates at ±32767. Avoids the harshness
/// of digital clipping on summed-note loud passages.
static int16_t mg_soft_clip(int32_t v) {
    if (v > MG_CLIP_THRESHOLD) {
        v = MG_CLIP_THRESHOLD + (v - MG_CLIP_THRESHOLD) / 4;
        if (v > 32767)
            v = 32767;
    } else if (v < -MG_CLIP_THRESHOLD) {
        v = -MG_CLIP_THRESHOLD + (v + MG_CLIP_THRESHOLD) / 4;
        if (v < -32767)
            v = -32767;
    }
    return (int16_t)v;
}

//===----------------------------------------------------------------------===//
// Public API — Song Builder
//===----------------------------------------------------------------------===//

/// @brief Create a new procedural music song builder at the given BPM (20–300).
/// @details MusicGen builds chiptune-style music programmatically. Add channels
///          with different waveforms, set per-channel effects (vibrato, tremolo,
///          arpeggio, portamento), add notes, then call build() to render to a
///          playable Sound or Music handle.
void *rt_musicgen_new(int64_t bpm) {
    mg_song_t *song = (mg_song_t *)rt_obj_new_i64(0, (int64_t)sizeof(mg_song_t));
    if (!song)
        return NULL;

    song->vptr = NULL;
    song->bpm = mg_clamp(bpm, 20, 300);
    song->length_centbeats = 0;
    song->swing = 0;
    song->loopable = 0;
    song->channel_count = 0;
    memset(song->channels, 0, sizeof(song->channels));

    /* Set defaults for all channel slots */
    for (int i = 0; i < MUSICGEN_MAX_CHANNELS; i++) {
        mg_channel_t *ch = &song->channels[i];
        ch->envelope.attack_ms = 10;
        ch->envelope.decay_ms = 50;
        ch->envelope.sustain_pct = 80;
        ch->envelope.release_ms = 100;
        ch->volume = 80;
        ch->duty_cycle = 50;
        ch->pan = 0;
        ch->detune_cents = 0;
        ch->vibrato_depth = 0;
        ch->vibrato_speed = 500; /* 5 Hz */
        ch->tremolo_depth = 0;
        ch->tremolo_speed = 400; /* 4 Hz */
        ch->arp_semi1 = 0;
        ch->arp_semi2 = 0;
        ch->arp_speed = 1500; /* 15 Hz */
        ch->portamento_ms = 0;
    }

    /* No finalizer needed — pure data, no sub-object references */
    return song;
}

/// @brief Add a synthesis channel with the given waveform (0=sine, 1=square, 2=saw, 3=triangle,
/// 4=noise).
int64_t rt_musicgen_add_channel(void *song_ptr, int64_t waveform) {
    if (!song_ptr)
        return -1;
    mg_song_t *song = (mg_song_t *)song_ptr;

    if (song->channel_count >= MUSICGEN_MAX_CHANNELS)
        return -1;

    waveform = mg_clamp(waveform, 0, 4);
    int32_t idx = song->channel_count;
    song->channels[idx].waveform = waveform;
    song->channel_count++;

    return (int64_t)idx;
}

//===----------------------------------------------------------------------===//
// Public API — Channel Configuration
//===----------------------------------------------------------------------===//

/// Helper to validate song + channel index.
static mg_channel_t *mg_get_channel(void *song_ptr, int64_t ch) {
    if (!song_ptr)
        return NULL;
    mg_song_t *song = (mg_song_t *)song_ptr;
    if (ch < 0 || ch >= (int64_t)song->channel_count)
        return NULL;
    return &song->channels[ch];
}

/// @brief Configure the ADSR envelope (Attack, Decay, Sustain level, Release) for a track.
///
/// Times are in milliseconds; sustain is a 0.0–1.0 amplitude scale.
/// Applied to every note added to the track until changed again.
void rt_musicgen_set_envelope(void *song,
                              int64_t ch,
                              int64_t attack_ms,
                              int64_t decay_ms,
                              int64_t sustain_pct,
                              int64_t release_ms) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->envelope.attack_ms = mg_clamp(attack_ms, 0, 5000);
    c->envelope.decay_ms = mg_clamp(decay_ms, 0, 5000);
    c->envelope.sustain_pct = mg_clamp(sustain_pct, 0, 100);
    c->envelope.release_ms = mg_clamp(release_ms, 0, 5000);
}

/// @brief Set the volume of a channel (0–100).
void rt_musicgen_set_channel_vol(void *song, int64_t ch, int64_t volume) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->volume = mg_clamp(volume, 0, 100);
}

/// @brief Set the duty cycle for square wave channels (1–99, default 50).
void rt_musicgen_set_duty(void *song, int64_t ch, int64_t duty) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->duty_cycle = mg_clamp(duty, 1, 99);
}

/// @brief Set the stereo pan for a channel (-100=left, 0=center, 100=right).
void rt_musicgen_set_pan(void *song, int64_t ch, int64_t pan) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->pan = mg_clamp(pan, -100, 100);
}

/// @brief Detune a channel by the given number of cents (-1200 to +1200).
void rt_musicgen_set_detune(void *song, int64_t ch, int64_t cents) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->detune_cents = mg_clamp(cents, -1200, 1200);
}

/// @brief Set vibrato (pitch wobble) depth and speed for a channel.
void rt_musicgen_set_vibrato(void *song, int64_t ch, int64_t depth, int64_t speed) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->vibrato_depth = mg_clamp(depth, 0, 200);
    c->vibrato_speed = mg_clamp(speed, 0, 5000);
}

/// @brief Set tremolo (volume wobble) depth and speed for a channel.
void rt_musicgen_set_tremolo(void *song, int64_t ch, int64_t depth, int64_t speed) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->tremolo_depth = mg_clamp(depth, 0, 100);
    c->tremolo_speed = mg_clamp(speed, 0, 5000);
}

/// @brief Set arpeggio (rapid pitch cycling) intervals and speed for a channel.
void rt_musicgen_set_arpeggio(void *song, int64_t ch, int64_t semi1, int64_t semi2, int64_t speed) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->arp_semi1 = mg_clamp(semi1, 0, 24);
    c->arp_semi2 = mg_clamp(semi2, 0, 24);
    c->arp_speed = mg_clamp(speed, 0, 5000);
}

/// @brief Set portamento (pitch slide) speed for a channel (0 = off, ms to reach new pitch).
void rt_musicgen_set_portamento(void *song, int64_t ch, int64_t speed_ms) {
    mg_channel_t *c = mg_get_channel(song, ch);
    if (!c)
        return;
    c->portamento_ms = mg_clamp(speed_ms, 0, 2000);
}

//===----------------------------------------------------------------------===//
// Public API — Notes
//===----------------------------------------------------------------------===//

/// @brief Schedule a note on a track. Equivalent to `add_note_vel` with full velocity (127).
int64_t rt_musicgen_add_note(
    void *song, int64_t ch, int64_t beat_pos, int64_t midi_note, int64_t duration) {
    return rt_musicgen_add_note_vel(song, ch, beat_pos, midi_note, duration, 100);
}

/// @brief Schedule a note with explicit MIDI velocity (0-127) on a track.
///
/// `time_ms` is the song-time start, `duration_ms` how long the
/// note sounds, `pitch` is the MIDI note number (0-127, 60 = middle C).
/// Velocity scales note volume linearly. Returns a sequential
/// note ID (0-based) for later editing.
int64_t rt_musicgen_add_note_vel(void *song_ptr,
                                 int64_t ch,
                                 int64_t beat_pos,
                                 int64_t midi_note,
                                 int64_t duration,
                                 int64_t velocity) {
    mg_channel_t *c = mg_get_channel(song_ptr, ch);
    if (!c)
        return 0;

    if (c->note_count >= MUSICGEN_MAX_NOTES)
        return 0;

    mg_note_t *note = &c->notes[c->note_count];
    note->beat_pos = (beat_pos < 0) ? 0 : beat_pos;
    note->midi_note = mg_clamp(midi_note, 0, 127);
    note->duration = (duration < 1) ? 1 : duration;
    note->velocity = mg_clamp(velocity, 0, 100);

    c->note_count++;
    return 1;
}

//===----------------------------------------------------------------------===//
// Public API — Song Properties
//===----------------------------------------------------------------------===//

/// @brief Set the total song length in centbeats (100 centbeats = 1 beat).
void rt_musicgen_set_length(void *song_ptr, int64_t length_centbeats) {
    if (!song_ptr)
        return;
    mg_song_t *song = (mg_song_t *)song_ptr;
    song->length_centbeats = (length_centbeats < 0) ? 0 : length_centbeats;
}

/// @brief Set the swing amount (0–100; offbeat notes shifted later for groove feel).
void rt_musicgen_set_swing(void *song_ptr, int64_t swing) {
    if (!song_ptr)
        return;
    mg_song_t *song = (mg_song_t *)song_ptr;
    song->swing = mg_clamp(swing, 0, 100);
}

/// @brief Mark the song as loopable (seamless loop point at the end).
void rt_musicgen_set_loopable(void *song_ptr, int64_t loopable) {
    if (!song_ptr)
        return;
    mg_song_t *song = (mg_song_t *)song_ptr;
    song->loopable = (loopable != 0) ? 1 : 0;
}

/// @brief Get the song's beats-per-minute.
int64_t rt_musicgen_get_bpm(void *song_ptr) {
    if (!song_ptr)
        return 0;
    return ((mg_song_t *)song_ptr)->bpm;
}

/// @brief Get the song length in centbeats.
int64_t rt_musicgen_get_length(void *song_ptr) {
    if (!song_ptr)
        return 0;
    return ((mg_song_t *)song_ptr)->length_centbeats;
}

/// @brief Get the number of channels added to the song.
int64_t rt_musicgen_get_channel_count(void *song_ptr) {
    if (!song_ptr)
        return 0;
    return (int64_t)((mg_song_t *)song_ptr)->channel_count;
}

//===----------------------------------------------------------------------===//
// Public API — Build (Pre-render to Sound)
//===----------------------------------------------------------------------===//

/// @brief Render the song to PCM audio and return a playable Sound handle.
/// @details Mixes all channels into a stereo 44100 Hz WAV buffer. Each note is
///          synthesized with its channel's waveform, ADSR envelope, and effects
///          (vibrato, tremolo, arpeggio, portamento). The result can be played
///          with rt_sound_play or loaded as music.
void *rt_musicgen_build(void *song_ptr) {
    if (!song_ptr)
        return NULL;
    mg_song_t *song = (mg_song_t *)song_ptr;

    if (song->channel_count <= 0 || song->length_centbeats <= 0)
        return NULL;

    /* Calculate total frames */
    int32_t samples_per_beat = (int32_t)((int64_t)MG_SAMPLE_RATE * 60 / song->bpm);
    int64_t total_frames_64 = (song->length_centbeats * (int64_t)samples_per_beat) / 100;

    /* Cap at 5 minutes */
    int64_t max_frames = (int64_t)MG_MAX_DURATION_S * MG_SAMPLE_RATE;
    if (total_frames_64 > max_frames)
        total_frames_64 = max_frames;
    if (total_frames_64 <= 0)
        return NULL;

    int32_t total_frames = (int32_t)total_frames_64;

    /* Allocate 32-bit stereo accumulator (zeroed) */
    size_t accum_size = (size_t)total_frames * 2 * sizeof(int32_t);
    int32_t *accum = (int32_t *)calloc(1, accum_size);
    if (!accum)
        return NULL;

    /* Sort each channel's notes by beat position (required for portamento) */
    for (int32_t ch = 0; ch < song->channel_count; ch++) {
        mg_channel_t *chan = &song->channels[ch];
        if (chan->note_count > 1) {
            qsort(chan->notes, (size_t)chan->note_count, sizeof(mg_note_t), mg_note_compare);
        }
    }

    int32_t active_channel_count = 0;
    for (int32_t ch = 0; ch < song->channel_count; ch++) {
        mg_channel_t *chan = &song->channels[ch];
        if (chan->note_count > 0 && mg_clamp(chan->volume, 0, 100) > 0)
            active_channel_count++;
    }
    if (active_channel_count <= 0)
        active_channel_count = 1;

    /* Render all channels and notes */
    for (int32_t ch = 0; ch < song->channel_count; ch++) {
        mg_channel_t *chan = &song->channels[ch];
        if (chan->note_count <= 0 || mg_clamp(chan->volume, 0, 100) <= 0)
            continue;
        mg_render_state_t state;
        state.prev_freq = 0.0;

        for (int32_t n = 0; n < chan->note_count; n++) {
            mg_render_note(accum,
                           total_frames,
                           &chan->notes[n],
                           chan,
                           samples_per_beat,
                           song->swing,
                           &state,
                           active_channel_count);
        }
    }

    /* Soft-clip to 16-bit stereo */
    size_t pcm_count = (size_t)total_frames * 2;
    int16_t *pcm = (int16_t *)malloc(pcm_count * sizeof(int16_t));
    if (!pcm) {
        free(accum);
        return NULL;
    }

    for (size_t i = 0; i < pcm_count; i++)
        pcm[i] = mg_soft_clip(accum[i]);

    free(accum);

    /* Loop crossfade: blend the end into the start for seamless looping */
    if (song->loopable && total_frames > 0) {
        int32_t fade_frames = MG_CROSSFADE_MS * MG_SAMPLE_RATE / 1000;
        /* Don't exceed 1/4 of the song */
        if (fade_frames > total_frames / 4)
            fade_frames = total_frames / 4;

        if (fade_frames > 0) {
            for (int32_t i = 0; i < fade_frames; i++) {
                double t = (double)i / (double)fade_frames;
                int32_t end_idx = (total_frames - fade_frames + i) * 2;
                int32_t start_idx = i * 2;

                /* Blend: start = start * t + end * (1 - t) */
                for (int c = 0; c < 2; c++) {
                    double s = (double)pcm[start_idx + c];
                    double e = (double)pcm[end_idx + c];
                    pcm[start_idx + c] = (int16_t)(s * t + e * (1.0 - t));
                }
            }

            /* Trim the tail that we've folded into the start */
            total_frames -= fade_frames;
            pcm_count = (size_t)total_frames * 2;
        }
    }

    /* Build stereo WAV and create Sound object */
    int32_t data_size = (int32_t)(pcm_count * sizeof(int16_t));
    int32_t wav_size = MG_WAV_HEADER + data_size;

    uint8_t *wav_buf = (uint8_t *)malloc((size_t)wav_size);
    if (!wav_buf) {
        free(pcm);
        return NULL;
    }

    mg_write_wav_header(wav_buf, total_frames);
    memcpy(wav_buf + MG_WAV_HEADER, pcm, (size_t)data_size);
    free(pcm);

    void *sound = rt_sound_load_mem(wav_buf, wav_size);
    free(wav_buf);

    return sound;
}
