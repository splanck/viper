// File: runtime/rt_random.c
// Purpose: Implements deterministic random number generator.
// Key invariants: Uses 64-bit LCG with fixed multiplier and increment; sequence is reproducible for
// given seed. Ownership/Lifetime: Maintains a single internal state value; not thread-safe. Links:
// docs/runtime-abi.md

#include "rt_random.h"

// Internal state for the 64-bit linear congruential generator.
// Initialized to a fixed non-zero seed so that callers who do not
// explicitly seed still observe a deterministic sequence.
// Shared globally without synchronization; not thread-safe.
static uint64_t state = 0xDEADBEEFCAFEBABEULL;

/**
 * @brief Seed the generator with an unsigned 64-bit value.
 *
 * The generator implements a 64-bit linear congruential algorithm.
 * Given the same seed, subsequent calls to rt_rnd produce a deterministic
 * sequence. This routine is not thread-safe because it mutates a shared
 * global state.
 */
void rt_randomize_u64(uint64_t seed)
{
    state = seed;
}

/**
 * @brief Seed the generator with a signed 64-bit value.
 *
 * The state is set directly, preserving the determinism of the underlying
 * linear congruential generator. Like rt_randomize_u64, this function is
 * not thread-safe because the state is shared.
 */
void rt_randomize_i64(long long seed)
{
    state = (uint64_t)seed;
}

/**
 * @brief Generate a pseudo-random double in the half-open interval [0, 1).
 *
 * Advances the 64-bit linear congruential generator and scales the result to
 * floating point. The sequence is fully deterministic given an initial seed.
 * This function is not thread-safe; concurrent calls must synchronize to
 * protect the shared state.
 */
double rt_rnd(void)
{
    state = state * 6364136223846793005ULL + 1ULL;
    uint64_t x = (state >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}
