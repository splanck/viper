//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_random.h
// Purpose: Deterministic pseudo-random number generator implementing BASIC's RND and RANDOMIZE
// functions using a 64-bit LCG algorithm producing identical sequences across all platforms for a
// given seed.
//
// Key invariants:
//   - Uses the Knuth MMIX LCG multiplier/increment constants for good statistical properties.
//   - Floating-point output uses the high 53 bits of state for maximum double precision.
//   - Generator state is stored per runtime context, with the legacy context used as fallback.
//   - rt_rand_range normalizes inverted bounds and samples the inclusive range uniformly.
//
// Ownership/Lifetime:
//   - Static generator state is held by the active RtContext.
//   - Random instances store their own independent RNG state in a GC-managed object.
//
// Links: src/runtime/core/rt_random.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define RT_RANDOM_CLASS_ID INT64_C(-0x430601)

#ifdef __cplusplus
extern "C" {
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
/// @return A random integer in the normalized inclusive range.
/// @details If min > max, the bounds are swapped automatically. The full
///          signed 64-bit range is supported and advances the generator once.
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

/// @brief Create a Random object with independent state.
/// @param seed Seed value for this Random instance.
/// @return A GC-managed Random object.
void *rt_random_new(long long seed);

// Instance methods operate on the receiver's independent RNG state.
/// @brief Instance variant of rt_rnd(): next double in [0, 1) for @p self.
double rt_rnd_method(void *self);
/// @brief Instance variant of rt_rand_int(): random integer in [0, @p max).
long long rt_rand_int_method(void *self, long long max);
/// @brief Instance variant of rt_rand_range(): random integer in
///        [@p min, @p max] (bounds swapped if inverted).
long long rt_rand_range_method(void *self, long long min, long long max);
/// @brief Re-seed @p self's independent RNG state with @p seed.
void rt_randomize_i64_method(void *self, long long seed);

#ifdef __cplusplus
}
#endif
