// File: src/runtime/rt_random.h
// Purpose: Declares deterministic random number helpers.
// Key invariants: 64-bit linear congruential generator; reproducible across platforms.
// Ownership/Lifetime: Uses internal global state; single-threaded.
// Links: docs/runtime-vm.md#runtime-abi
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

#ifdef __cplusplus
}
#endif
