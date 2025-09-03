// File: runtime/rt_random.c
// Purpose: Implements deterministic random number generator.
// Key invariants: Uses 64-bit LCG with fixed multiplier and increment; sequence is reproducible for
// given seed. Ownership/Lifetime: Maintains a single internal state value; not thread-safe. Links:
// docs/runtime-abi.md

#include "rt_random.h"

static uint64_t state = 0xDEADBEEFCAFEBABEULL; // default non-zero seed

void rt_randomize_u64(uint64_t seed)
{
    state = seed;
}

void rt_randomize_i64(long long seed)
{
    state = (uint64_t)seed;
}

double rt_rnd(void)
{
    state = state * 6364136223846793005ULL + 1ULL;
    uint64_t x = (state >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}
