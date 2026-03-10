//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_synth.h
// Purpose: Procedural sound synthesis — generates Sound objects from parameters
//          without requiring WAV files. Supports tone, sweep, noise, and preset
//          game sound effects.
//
// Key invariants:
//   - All generated sounds are 16-bit PCM mono at 44100 Hz.
//   - Returned Sound objects are identical to file-loaded sounds (same GC lifecycle).
//   - Duration is in milliseconds; frequencies in Hz (integer).
//   - Waveform types: 0=sine, 1=square, 2=sawtooth, 3=triangle.
//   - SFX preset types: 0=jump, 1=coin, 2=hit, 3=explosion, 4=powerup, 5=laser.
//
// Ownership/Lifetime:
//   - Returned Sound objects are GC-managed with refcount 1.
//   - Caller owns the reference and must release via Sound.Free.
//
// Links: rt_audio.h (sound playback), rt_soundbank.h (named registry)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Waveform type constants.
    #define RT_WAVE_SINE     0
    #define RT_WAVE_SQUARE   1
    #define RT_WAVE_SAWTOOTH 2
    #define RT_WAVE_TRIANGLE 3

    /// @brief SFX preset type constants.
    #define RT_SFX_JUMP      0
    #define RT_SFX_COIN      1
    #define RT_SFX_HIT       2
    #define RT_SFX_EXPLOSION 3
    #define RT_SFX_POWERUP   4
    #define RT_SFX_LASER     5

    /// @brief Generate a tone at a fixed frequency.
    /// @param freq_hz Frequency in Hz (20-20000).
    /// @param duration_ms Duration in milliseconds (1-10000).
    /// @param waveform Waveform type (0=sine, 1=square, 2=saw, 3=triangle).
    /// @return Sound object, or NULL on failure.
    void *rt_synth_tone(int64_t freq_hz, int64_t duration_ms, int64_t waveform);

    /// @brief Generate a frequency sweep between two frequencies.
    /// @param start_hz Starting frequency in Hz.
    /// @param end_hz Ending frequency in Hz.
    /// @param duration_ms Duration in milliseconds.
    /// @param waveform Waveform type.
    /// @return Sound object, or NULL on failure.
    void *rt_synth_sweep(int64_t start_hz, int64_t end_hz, int64_t duration_ms, int64_t waveform);

    /// @brief Generate white noise.
    /// @param duration_ms Duration in milliseconds.
    /// @param volume Volume level (0-100).
    /// @return Sound object, or NULL on failure.
    void *rt_synth_noise(int64_t duration_ms, int64_t volume);

    /// @brief Generate a preset game sound effect.
    /// @param sfx_type SFX type (0=jump, 1=coin, 2=hit, 3=explosion, 4=powerup, 5=laser).
    /// @return Sound object, or NULL on failure.
    void *rt_synth_sfx(int64_t sfx_type);

#ifdef __cplusplus
}
#endif
