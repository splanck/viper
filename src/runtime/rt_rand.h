//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_rand.h
// Purpose: Cryptographically secure random number generation.
// Key invariants: Uses OS-provided CSPRNG; traps on invalid parameters.
// Ownership/Lifetime: Returned Bytes objects are newly allocated.
// Links: docs/viperlib/crypto.md
//
// Platform backends:
// - Unix/Linux/macOS: /dev/urandom
// - Windows: BCryptGenRandom (Vista+) or CryptGenRandom fallback
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
