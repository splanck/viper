// File: runtime/rt_random.h
// Purpose: Declares deterministic random number helpers.
// Key invariants: 64-bit linear congruential generator; reproducible across platforms.
// Ownership/Lifetime: Uses internal global state; single-threaded.
// Links: docs/runtime-abi.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void rt_randomize_u64(uint64_t seed);
    void rt_randomize_i64(long long seed);
    double rt_rnd(void);

#ifdef __cplusplus
}
#endif
