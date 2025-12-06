//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime's deterministic pseudo-random number generator,
// implementing BASIC's RND and RANDOMIZE functions. The generator uses a 64-bit
// linear congruential generator (LCG) that produces identical sequences across
// all platforms for a given seed.
//
// BASIC programs depend on reproducible random number sequences for testing and
// deterministic simulation. Unlike C's rand() which varies across implementations,
// Viper's RNG uses a fixed algorithm that generates the same sequence on all
// platforms given the same seed value.
//
// Implementation Design:
// - 64-bit LCG: Uses the multiplier and increment constants from Knuth's MMIX
//   for full period and good statistical properties
// - Floating-point output: Maps the 64-bit state to [0, 1) using the high 53 bits
//   for maximum precision in double representation
// - Global state: Maintains a single generator state for the entire program
//   (BASIC's random functions are inherently global, not per-thread)
// - Deterministic seeding: RANDOMIZE sets the state explicitly, enabling
//   reproducible sequences in tests and simulations
//
// The generator is designed for BASIC compatibility and determinism, not
// cryptographic security or advanced statistical applications.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Seed the random number generator with an unsigned 64-bit value.
    /// @param seed New seed value used to initialize the internal state.
    void rt_randomize_u64(uint64_t seed);

    /// @brief Seed the random number generator with a signed 64-bit value.
    /// @param seed New seed value cast to unsigned and stored as internal state.
    void rt_randomize_i64(long long seed);

    /// @brief Generate the next deterministic pseudo-random number.
    /// @return A double in the half-open interval [0, 1).
    /// @details Advances a 64-bit linear congruential generator and maps the
    /// resulting bits to a double in the range [0,1) with 53 bits of precision.
    double rt_rnd(void);

    /// @brief Generate a random integer in the range [0, max).
    /// @param max Upper bound (exclusive). Must be positive.
    /// @return A random integer in [0, max).
    /// @details Uses the same LCG as rt_rnd() to ensure deterministic sequences.
    long long rt_rand_int(long long max);

#ifdef __cplusplus
}
#endif
