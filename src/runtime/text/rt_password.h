//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_password.h
// Purpose: Secure password hashing with automatic salt generation using PBKDF2-SHA256 and
// constant-time verification to prevent timing attacks.
//
// Key invariants:
//   - Hash output includes the salt and iteration count; no separate salt storage needed.
//   - Verification uses constant-time comparison to prevent timing side-channels.
//   - Default iteration count is 300,000 and verification rejects hostile work factors above
//     the implementation cap.
//   - Hash output format: "PBKDF2$iterations$salt_b64$hash_b64".
//
// Ownership/Lifetime:
//   - Returned hash strings are newly allocated; caller must release.
//   - Password strings are borrowed for the duration of the call; not retained.
//
// Links: src/runtime/text/rt_password.c (implementation), src/runtime/text/rt_keyderive.h,
// src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Hash a password with auto-generated salt.
/// @details Uses PBKDF2-SHA256 with 300,000 iterations and a 16-byte salt.
///          Returns a string in the format: "PBKDF2$iterations$salt_b64$hash_b64"
/// @param password The password to hash.
/// @return Encoded hash string (safe to store in database).
rt_string rt_password_hash(rt_string password);

/// @brief Hash a password with custom iteration count.
/// @details Uses PBKDF2-SHA256 with specified iterations and 16-byte salt.
/// @param password The password to hash.
/// @param iterations Number of iterations. Values below 100,000 clamp up.
/// @return Encoded hash string.
rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations);

/// @brief Verify a password against a stored hash.
/// @details Extracts salt and iterations from the hash string and verifies.
///          Uses constant-time comparison to prevent timing attacks.
/// @param password The password to verify.
/// @param hash The stored hash string (from rt_password_hash).
/// @return 1 if password matches, 0 otherwise.
int8_t rt_password_verify(rt_string password, rt_string hash);

#ifdef __cplusplus
}
#endif
