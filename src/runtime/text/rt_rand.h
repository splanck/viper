//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_rand.h
// Purpose: Cryptographically secure random number generation using the OS-provided CSPRNG
// in compatibility mode and the in-tree crypto-module HMAC-DRBG in approved mode.
//
// Key invariants:
//   - Compatibility mode uses the OS CSPRNG; approved mode uses the module HMAC-DRBG.
//   - rt_crypto_rand_bytes accepts zero and returns the requested-length Bytes object.
//   - rt_crypto_rand_int uses rejection sampling and returns a value in [min, max].
//
// Ownership/Lifetime:
//   - Returned Bytes objects are newly allocated; caller must release.
//   - Approved mode and the Unix fallback retain process-global DRBG/descriptor state.
//
// Links: src/runtime/text/rt_rand.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Generate cryptographically secure random bytes.
/// @param count Number of bytes to generate (must be >= 0).
/// @return A Bytes object containing the random bytes.
/// @note Zero returns an empty Bytes object; negative values trap.
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
