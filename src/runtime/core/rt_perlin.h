//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_perlin.h
// Purpose: Runtime functions for Perlin noise generation.
// Key invariants: Deterministic output for same seed+coordinates.
// Ownership/Lifetime: PerlinNoise object manages its own permutation table.
// Links: src/il/runtime/classes/RuntimeClasses.inc
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
