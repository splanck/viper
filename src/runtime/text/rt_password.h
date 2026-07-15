//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_password.h
// Purpose: Password hashing with automatic salt generation using scrypt-SHA256
// in compatibility mode or PBKDF2-HMAC-SHA256 in approved mode, plus strict
// stored-record parsing and fixed-time final-value comparison.
//
// Key invariants:
//   - Hash output includes the salt and iteration count; no separate salt storage needed.
//   - Verification's final 32-byte comparison does not exit on the first mismatch;
//     parsing, algorithm selection, and KDF work are not constant-time.
//   - Default Hash output uses bounded scrypt in compatibility mode and PBKDF2 in approved mode.
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
/// @details Uses scrypt-SHA256 and the `SCRYPT$...` format in compatibility mode;
///          approved mode uses PBKDF2-HMAC-SHA256 and `PBKDF2$...`. Both generate
///          a 16-byte salt.
/// @param password The password to hash.
/// @return Encoded hash string (safe to store in database).
rt_string rt_password_hash(rt_string password);

/// @brief Hash a password with custom iteration count.
/// @details Uses PBKDF2-SHA256 with specified iterations and 16-byte salt.
/// @param password The password to hash.
/// @param iterations Number of iterations from 100,000 through 10,000,000.
/// @return Encoded hash string.
rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations);

/// @brief Hash a password using scrypt with default memory-hard parameters.
rt_string rt_password_hash_scrypt(rt_string password);

/// @brief Hash a password using scrypt with caller-provided parameters at or above policy minimums.
rt_string rt_password_hash_scrypt_params(rt_string password, int64_t n, int64_t r, int64_t p);

/// @brief Verify a password against a stored hash.
/// @details Strictly parses the supported record, re-derives the 32-byte value,
///          and compares that final value without a first-mismatch exit.
/// @param password The password to verify.
/// @param hash The stored hash string (from rt_password_hash).
/// @return 1 if password matches, 0 otherwise.
int8_t rt_password_verify(rt_string password, rt_string hash);

/// @brief Report whether a stored password hash should be upgraded to current defaults.
/// @return 1 for invalid, unsupported, legacy, or non-current hashes; 0 for the
///         exact default scrypt tuple in compatibility mode or qualifying PBKDF2
///         in approved mode. Stronger custom scrypt tuples are currently marked stale.
int8_t rt_password_needs_rehash(rt_string hash);

#ifdef __cplusplus
}
#endif
