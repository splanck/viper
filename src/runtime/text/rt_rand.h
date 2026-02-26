//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_rand.h
// Purpose: Cryptographically secure random number generation using the OS-provided CSPRNG
// (/dev/urandom on Unix, BCryptGenRandom on Windows).
//
// Key invariants:
//   - Uses OS-provided CSPRNG; output is suitable for security-sensitive applications.
//   - rt_rand_bytes returns a Bytes object of the requested length.
//   - rt_rand_i64 returns a uniformly distributed 64-bit integer.
//   - rt_rand_range returns a value in [min, max]; traps when min > max.
//
// Ownership/Lifetime:
//   - Returned Bytes objects are newly allocated; caller must release.
//   - No persistent state; each call reads fresh randomness from the OS.
//
// Links: src/runtime/text/rt_rand.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Generate cryptographically secure random bytes.
    /// @param count Number of bytes to generate (must be >= 1).
    /// @return A Bytes object containing the random bytes.
    /// @note Traps if count < 1.
    void *rt_crypto_rand_bytes(int64_t count);

    /// @brief Generate a cryptographically secure random integer in range [min, max].
    /// @param min Minimum value (inclusive).
    /// @param max Maximum value (inclusive).
    /// @return Random integer in the specified range.
    /// @note Traps if min > max.
    int64_t rt_crypto_rand_int(int64_t min, int64_t max);

#ifdef __cplusplus
}
#endif
