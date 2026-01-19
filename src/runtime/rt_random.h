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

    //=========================================================================
    // Random Distributions
    //=========================================================================

    /// @brief Generate a random integer in the range [min, max] (inclusive).
    /// @param min Lower bound (inclusive).
    /// @param max Upper bound (inclusive).
    /// @return A random integer in [min, max].
    /// @details If min > max, the bounds are swapped automatically.
    long long rt_rand_range(long long min, long long max);

    /// @brief Generate a random number from a Gaussian (normal) distribution.
    /// @param mean The mean (center) of the distribution.
    /// @param stddev The standard deviation (spread). Must be non-negative.
    /// @return A random double drawn from N(mean, stddev^2).
    /// @details Uses the Box-Muller transform for generating normally
    ///          distributed values. When stddev is 0, returns mean.
    double rt_rand_gaussian(double mean, double stddev);

    /// @brief Generate a random number from an exponential distribution.
    /// @param lambda The rate parameter (1/mean). Must be positive.
    /// @return A random double drawn from Exp(lambda).
    /// @details Uses inverse transform sampling: -ln(1-U)/lambda.
    ///          When lambda <= 0, returns 0.
    double rt_rand_exponential(double lambda);

    /// @brief Simulate a dice roll (1 to sides inclusive).
    /// @param sides Number of sides on the die. Must be positive.
    /// @return A random integer in [1, sides].
    /// @details Convenience function for game programming. Returns 1 if
    ///          sides <= 0.
    long long rt_rand_dice(long long sides);

    /// @brief Generate a random boolean with given probability.
    /// @param probability Probability of returning true (0.0 to 1.0).
    /// @return 1 (true) with probability p, 0 (false) with probability 1-p.
    /// @details Clamped to [0, 1]. Returns 0 if p <= 0, 1 if p >= 1.
    long long rt_rand_chance(double probability);

    /// @brief Shuffle elements in a Seq randomly.
    /// @param seq A Viper.Collections.Seq object.
    /// @details Performs Fisher-Yates shuffle using the current RNG state.
    ///          Deterministic when the RNG is seeded.
    void rt_rand_shuffle(void *seq);

#ifdef __cplusplus
}
#endif
