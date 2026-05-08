//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_password.h
// Purpose: Secure password hashing with automatic salt generation using scrypt-SHA256 or
// PBKDF2-SHA256 and constant-time verification to prevent timing attacks.
//
// Key invariants:
//   - Hash output includes the salt and iteration count; no separate salt storage needed.
//   - Verification uses constant-time comparison to prevent timing side-channels.
//   - Default Hash output uses scrypt with bounded memory-hard parameters.
//   - Verification rejects hostile work factors above the implementation caps.
//   - Supported hash formats are "SCRYPT$ln$r$p$salt_b64$hash_b64" and legacy
//     "PBKDF2$iterations$salt_b64$hash_b64".
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
/// @details Uses scrypt-SHA256 with the runtime default memory-hard parameters and a 16-byte salt.
///          Returns a string in the format: "SCRYPT$ln$r$p$salt_b64$hash_b64".
/// @param password The password to hash.
/// @return Encoded hash string (safe to store in database).
rt_string rt_password_hash(rt_string password);

/// @brief Hash a password with custom iteration count.
/// @details Uses PBKDF2-SHA256 with specified iterations and 16-byte salt.
/// @param password The password to hash.
/// @param iterations Number of iterations. Values below 100,000 trap.
/// @return Encoded hash string.
rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations);

/// @brief Hash a password using scrypt with default memory-hard parameters.
rt_string rt_password_hash_scrypt(rt_string password);

/// @brief Hash a password using scrypt with caller-provided parameters at or above policy minimums.
rt_string rt_password_hash_scrypt_params(rt_string password, int64_t n, int64_t r, int64_t p);

/// @brief Verify a password against a stored hash.
/// @details Extracts salt and iterations from the hash string and verifies.
///          Uses constant-time comparison to prevent timing attacks.
/// @param password The password to verify.
/// @param hash The stored hash string (from rt_password_hash).
/// @return 1 if password matches, 0 otherwise.
int8_t rt_password_verify(rt_string password, rt_string hash);

/// @brief Report whether a stored password hash should be upgraded to current defaults.
/// @return 1 for invalid, unsupported, legacy, or non-current hashes; 0 for current hashes.
int8_t rt_password_needs_rehash(rt_string hash);

#ifdef __cplusplus
}
#endif
