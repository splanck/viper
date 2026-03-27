//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_musicgen.h
// Purpose: Procedural music composition — tracker-style sequencer that builds
//          multi-channel songs with ADSR envelopes, chiptune effects (arpeggio,
//          vibrato, portamento, tremolo), and stereo panning. Generates a Sound
//          object via pre-rendering, requiring zero external audio assets.
//
// Key invariants:
//   - All generated audio is 16-bit stereo PCM at 44100 Hz.
//   - Returned Sound objects are identical to file-loaded sounds (same lifecycle).
//   - Beat timing uses centbeats (100 = 1 beat) for sub-beat precision.
//   - Effect speeds use centi-Hz (100 = 1 Hz). Pitch offsets use cents.
//   - Maximum 8 channels, 4096 notes per channel, 5 minutes max duration.
//   - Waveform types: 0=sine, 1=square, 2=sawtooth, 3=triangle, 4=noise.
//
// Ownership/Lifetime:
//   - Song builder is GC-managed (rt_obj_new_i64). No finalizer needed (pure data).
//   - Build() returns a GC-managed Sound object with refcount 1.
//
// Links: rt_audio.h (sound playback), rt_synth.h (SFX synthesis),
//        rt_soundbank.h (named registry)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //===------------------------------------------------------------------===//
    // Constants
    //===------------------------------------------------------------------===//

    /// Waveform types (extends rt_synth.h RT_WAVE_* with noise).
    #define MUSICGEN_WAVE_SINE     0
    #define MUSICGEN_WAVE_SQUARE   1
    #define MUSICGEN_WAVE_SAWTOOTH 2
    #define MUSICGEN_WAVE_TRIANGLE 3
    #define MUSICGEN_WAVE_NOISE    4

    /// Resource limits.
    #define MUSICGEN_MAX_CHANNELS  8
    #define MUSICGEN_MAX_NOTES     4096

    //===------------------------------------------------------------------===//
    // Song Builder — Creation and Configuration
    //===------------------------------------------------------------------===//

    /// @brief Create a new song builder at the given tempo.
    /// @param bpm Beats per minute (clamped to 20-300).
    /// @return Opaque song builder handle.
    void *rt_musicgen_new(int64_t bpm);

    /// @brief Add a channel with the specified waveform type.
    /// @param song Song builder handle.
    /// @param waveform Waveform type (0-4: sine/square/saw/triangle/noise).
    /// @return Channel index (0-7), or -1 if full.
    int64_t rt_musicgen_add_channel(void *song, int64_t waveform);

    /// @brief Set the ADSR envelope for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param attack_ms Attack time in milliseconds (0-5000).
    /// @param decay_ms Decay time in milliseconds (0-5000).
    /// @param sustain_pct Sustain level as percentage (0-100).
    /// @param release_ms Release time in milliseconds (0-5000).
    void rt_musicgen_set_envelope(void *song, int64_t ch,
                                  int64_t attack_ms, int64_t decay_ms,
                                  int64_t sustain_pct, int64_t release_ms);

    /// @brief Set channel volume.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param volume Volume (0-100).
    void rt_musicgen_set_channel_vol(void *song, int64_t ch, int64_t volume);

    /// @brief Set square wave duty cycle (pulse width).
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param duty Duty cycle percentage (0-100). NES: 12, 25, 50, 75.
    void rt_musicgen_set_duty(void *song, int64_t ch, int64_t duty);

    /// @brief Set stereo pan position for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param pan Pan position (-100=left, 0=center, 100=right).
    void rt_musicgen_set_pan(void *song, int64_t ch, int64_t pan);

    /// @brief Set constant pitch offset for chorusing/thickening.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param cents Pitch offset in cents (-1200 to 1200). 100 cents = 1 semitone.
    void rt_musicgen_set_detune(void *song, int64_t ch, int64_t cents);

    //===------------------------------------------------------------------===//
    // Effects — Per-Channel Modulation
    //===------------------------------------------------------------------===//

    /// @brief Set vibrato (pitch modulation) for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param depth Vibrato depth in cents (0-200). 0 disables.
    /// @param speed Vibrato speed in centi-Hz (100=1Hz, 500=5Hz).
    void rt_musicgen_set_vibrato(void *song, int64_t ch,
                                 int64_t depth, int64_t speed);

    /// @brief Set tremolo (volume modulation) for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param depth Tremolo depth as percentage (0-100). 0 disables.
    /// @param speed Tremolo speed in centi-Hz (100=1Hz, 400=4Hz).
    void rt_musicgen_set_tremolo(void *song, int64_t ch,
                                 int64_t depth, int64_t speed);

    /// @brief Set arpeggio (rapid pitch cycling) for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param semi1 First offset in semitones (0-24). Both 0 disables.
    /// @param semi2 Second offset in semitones (0-24).
    /// @param speed Arpeggio speed in centi-Hz (1500=15Hz is classic).
    void rt_musicgen_set_arpeggio(void *song, int64_t ch,
                                  int64_t semi1, int64_t semi2, int64_t speed);

    /// @brief Set portamento (pitch glide between notes) for a channel.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param speed_ms Glide time in milliseconds (0=off, 20-500 typical).
    void rt_musicgen_set_portamento(void *song, int64_t ch, int64_t speed_ms);

    //===------------------------------------------------------------------===//
    // Notes
    //===------------------------------------------------------------------===//

    /// @brief Add a note with default velocity (100).
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param beat_pos Beat position in centbeats (100 = 1 beat).
    /// @param midi_note MIDI note number (0-127, 60=C4, 69=A4=440Hz).
    /// @param duration Note duration in centbeats.
    /// @return 1 on success, 0 if channel is full.
    int64_t rt_musicgen_add_note(void *song, int64_t ch,
                                 int64_t beat_pos, int64_t midi_note,
                                 int64_t duration);

    /// @brief Add a note with explicit velocity.
    /// @param song Song builder handle.
    /// @param ch Channel index.
    /// @param beat_pos Beat position in centbeats.
    /// @param midi_note MIDI note number (0-127).
    /// @param duration Note duration in centbeats.
    /// @param velocity Note velocity (0-100).
    /// @return 1 on success, 0 if channel is full.
    int64_t rt_musicgen_add_note_vel(void *song, int64_t ch,
                                     int64_t beat_pos, int64_t midi_note,
                                     int64_t duration, int64_t velocity);

    //===------------------------------------------------------------------===//
    // Song Properties
    //===------------------------------------------------------------------===//

    /// @brief Set the song length in centbeats.
    /// @param song Song builder handle.
    /// @param length_centbeats Song length (100 = 1 beat).
    void rt_musicgen_set_length(void *song, int64_t length_centbeats);

    /// @brief Set swing amount (shifts off-beat notes forward).
    /// @param song Song builder handle.
    /// @param swing Swing amount (0-100). 0 = straight, 50 = triplet feel.
    void rt_musicgen_set_swing(void *song, int64_t swing);

    /// @brief Enable seamless loop crossfade in Build() output.
    /// @param song Song builder handle.
    /// @param loopable 1 to enable loop crossfade, 0 to disable.
    void rt_musicgen_set_loopable(void *song, int64_t loopable);

    /// @brief Get the BPM of the song.
    int64_t rt_musicgen_get_bpm(void *song);

    /// @brief Get the song length in centbeats.
    int64_t rt_musicgen_get_length(void *song);

    /// @brief Get the number of channels added.
    int64_t rt_musicgen_get_channel_count(void *song);

    //===------------------------------------------------------------------===//
    // Build — Pre-render to Sound Object
    //===------------------------------------------------------------------===//

    /// @brief Pre-render the song to a Sound object.
    ///
    /// Renders all channels and notes into a stereo 16-bit PCM buffer,
    /// applies effects (vibrato, arpeggio, tremolo, portamento, detune),
    /// ADSR envelopes, and soft-clipping. If loopable is set, applies a
    /// 10ms crossfade at the loop boundary for click-free looping.
    ///
    /// @param song Song builder handle.
    /// @return Sound object ready for playback, or NULL on failure.
    ///         Failure: 0 channels, 0 length, memory allocation error.
    void *rt_musicgen_build(void *song);

#ifdef __cplusplus
}
#endif
