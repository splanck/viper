//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_perlin.h
// Purpose: Perlin noise generator for the Viper.Perlin runtime class, providing seeded 2D/3D noise and fractal octave noise with deterministic output per seed.
//
// Key invariants:
//   - Output is deterministic: same seed and coordinates always produce the same value.
//   - Noise values are in the range [-1.0, 1.0].
//   - Octave noise amplitude is controlled by the persistence parameter (typically 0.5).
//   - The permutation table is built once at creation time from the seed.
//
// Ownership/Lifetime:
//   - PerlinNoise objects are heap-allocated opaque pointers.
//   - Callers are responsible for lifetime management; no reference counting.
//
// Links: src/runtime/core/rt_perlin.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Perlin noise generator with the given seed.
    /// @param seed Random seed for permutation table.
    /// @return Pointer to PerlinNoise object.
    void *rt_perlin_new(int64_t seed);

    /// @brief Generate 2D Perlin noise.
    /// @param obj PerlinNoise pointer.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @return Noise value in range [-1, 1].
    double rt_perlin_noise2d(void *obj, double x, double y);

    /// @brief Generate 3D Perlin noise.
    /// @param obj PerlinNoise pointer.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param z Z coordinate.
    /// @return Noise value in range [-1, 1].
    double rt_perlin_noise3d(void *obj, double x, double y, double z);

    /// @brief Generate fractal/octave 2D noise.
    /// @param obj PerlinNoise pointer.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param octaves Number of octaves (layers of detail).
    /// @param persistence Amplitude multiplier per octave (typically 0.5).
    /// @return Noise value.
    double rt_perlin_octave2d(void *obj, double x, double y, int64_t octaves, double persistence);

    /// @brief Generate fractal/octave 3D noise.
    /// @param obj PerlinNoise pointer.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param z Z coordinate.
    /// @param octaves Number of octaves.
    /// @param persistence Amplitude multiplier per octave.
    /// @return Noise value.
    double rt_perlin_octave3d(
        void *obj, double x, double y, double z, int64_t octaves, double persistence);

#ifdef __cplusplus
}
#endif
