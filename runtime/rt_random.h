// File: runtime/rt_random.h
// Purpose: Declare deterministic random number utilities for the runtime.
// Key invariants: 64-bit LCG with reproducible output; 53-bit double precision.
// Ownership/Lifetime: Global state local to runtime; single-threaded use.
// Links: docs/runtime-abi.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Seed the RNG with an exact 64-bit value.
    void rt_randomize_u64(uint64_t seed);

    /// @brief Seed the RNG with a signed 64-bit value (cast to unsigned).
    void rt_randomize_i64(long long seed);

    /// @brief Generate a uniform double in [0,1) with 53 bits of precision.
    double rt_rnd(void);

#ifdef __cplusplus
}
#endif
