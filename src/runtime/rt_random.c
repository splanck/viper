//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project and is made available under the
// terms of the MIT License. See the LICENSE file for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the deterministic pseudo-random number generator exposed through
// the C runtime ABI.  The generator mirrors the BASIC `RND` semantics by using
// a 64-bit linear congruential algorithm with a fixed multiplier and
// increment.  Runtime clients can reseed the generator using signed or unsigned
// inputs and retrieve uniform doubles in the half-open interval [0, 1).  The
// state intentionally lives in static storage so successive calls remain
// reproducible across the VM and native backends.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Deterministic pseudo-random number generator for the runtime.
/// @details Provides seeding helpers for both signed and unsigned integers and
///          exposes the BASIC-compatible `rt_rnd` entry point that returns
///          doubles in the range [0, 1).  The implementation uses a linear
///          congruential generator so that the VM and native builds produce
///          identical sequences for a given seed.

#include "rt_random.h"

/// @brief Internal state for the 64-bit linear congruential generator.
/// @details Initialised to a non-zero default so that callers who skip an
///          explicit seed still observe a deterministic sequence.  The state is
///          stored at translation-unit scope without synchronisation and is
///          therefore not thread-safe.
static uint64_t state = 0xDEADBEEFCAFEBABEULL;

/// @brief Seed the random generator with an unsigned 64-bit value.
/// @details Replaces the linear congruential generator state directly with the
///          provided seed so that future calls to @ref rt_rnd produce the same
///          deterministic sequence.  The function performs no synchronisation
///          and therefore requires external serialisation when used from
///          multiple threads.
void rt_randomize_u64(uint64_t seed)
{
    state = seed;
}

/// @brief Seed the random generator with a signed 64-bit value.
/// @details Casts the argument to an unsigned representation before updating
///          the shared state so that negative seeds map to the expected bit
///          patterns produced by the BASIC runtime.  Thread-safety matches
///          @ref rt_randomize_u64 and must be enforced by the caller.
void rt_randomize_i64(long long seed)
{
    state = (uint64_t)seed;
}

/// @brief Produce a pseudo-random double in the half-open interval [0, 1).
/// @details Advances the linear congruential generator using the Numerical
///          Recipes multiplier and increment, extracts the top 53 bits, and
///          scales them into IEEE 754 double range.  The algorithm mirrors the
///          VM implementation so identical seeds yield identical sequences.
///          Callers must coordinate access if used concurrently.
double rt_rnd(void)
{
    state = state * 6364136223846793005ULL + 1ULL;
    uint64_t x = (state >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}
