// File: runtime/rt_random.c
// Purpose: Implement deterministic random number utilities.
// Key invariants: 64-bit linear congruential generator; top 53 bits scaled to [0,1).
// Ownership/Lifetime: Global state with default non-zero seed; single-threaded.
// Links: docs/runtime-abi.md

#include "rt_random.h"

#ifdef __cplusplus
extern "C"
{
#endif

    static uint64_t state = 0x0123456789abcdefULL; // Default non-zero seed

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
        double x = (double)((state >> 11) & ((1ULL << 53) - 1));
        return x * (1.0 / 9007199254740992.0);
    }

#ifdef __cplusplus
}
#endif
