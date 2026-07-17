//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_keyderive.h
// Purpose: Key derivation functions implementing PBKDF2-SHA256 and scrypt-SHA256 for deriving
// cryptographic keys from passwords with configurable cost parameters.
//
// Key invariants:
//   - PBKDF2 iterations are accepted from 100,000 through 10,000,000.
//   - scrypt N must be a power of two through 2^20; r/p are capped at 32 and
//     the ROMix allocation is capped at 64 MiB.
//   - Key length must be in [1, 1024] bytes.
//   - Callers should generate an independent salt for each derivation context and
//     retain it wherever the same key must be reproduced.
//   - Output is either a Bytes object or lowercase hex string, by entry point.
//
// Ownership/Lifetime:
//   - Returned objects are newly allocated; caller must release.
//   - Password and salt strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_keyderive.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Derive a key using PBKDF2-SHA256.
/// @param password The password string.
/// @param salt The salt as a Bytes object.
/// @param iterations Number of iterations (100,000 through 10,000,000).
/// @param key_len Desired key length in bytes (1-1024).
/// @return Derived key as a Bytes object.
/// @note Traps if iterations are outside policy or key_len is not in [1, 1024].
void *rt_keyderive_pbkdf2_sha256(rt_string password,
                                 void *salt,
                                 int64_t iterations,
                                 int64_t key_len);

/// @brief Derive a key using PBKDF2-SHA256 and return as hex string.
/// @param password The password string.
/// @param salt The salt as a Bytes object.
/// @param iterations Number of iterations (100,000 through 10,000,000).
/// @param key_len Desired key length in bytes (1-1024).
/// @return Derived key as lowercase hex string.
/// @note Traps if iterations are outside policy or key_len is not in [1, 1024].
rt_string rt_keyderive_pbkdf2_sha256_str(rt_string password,
                                         void *salt,
                                         int64_t iterations,
                                         int64_t key_len);

/// @brief Derive a key using scrypt-SHA256.
/// @param password The password string.
/// @param salt The salt as a Bytes object.
/// @param n CPU/memory cost parameter; must be a power of two greater than 1.
/// @param r Block size parameter.
/// @param p Parallelization parameter.
/// @param key_len Desired key length in bytes (1-1024).
/// @return Derived key as a Bytes object.
void *rt_keyderive_scrypt_sha256(
    rt_string password, void *salt, int64_t n, int64_t r, int64_t p, int64_t key_len);

/// @brief Derive a key using scrypt-SHA256 and return as hex string.
rt_string rt_keyderive_scrypt_sha256_str(
    rt_string password, void *salt, int64_t n, int64_t r, int64_t p, int64_t key_len);

#ifdef __cplusplus
}
#endif
