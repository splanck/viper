//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_keyderive.h
// Purpose: Key derivation functions implementing PBKDF2-SHA256 for deriving cryptographic keys from
// passwords with configurable iteration counts.
//
// Key invariants:
//   - Minimum 1000 iterations; higher iteration counts increase brute-force resistance.
//   - Key length must be in [1, 1024] bytes.
//   - The salt should be randomly generated per password and stored alongside the hash.
//   - Output is a Bytes object containing the derived key.
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
extern "C"
{
#endif

    /// @brief Derive a key using PBKDF2-SHA256.
    /// @param password The password string.
    /// @param salt The salt as a Bytes object.
    /// @param iterations Number of iterations (minimum 1000).
    /// @param key_len Desired key length in bytes (1-1024).
    /// @return Derived key as a Bytes object.
    /// @note Traps if iterations < 1000 or key_len not in [1, 1024].
    void *rt_keyderive_pbkdf2_sha256(rt_string password,
                                     void *salt,
                                     int64_t iterations,
                                     int64_t key_len);

    /// @brief Derive a key using PBKDF2-SHA256 and return as hex string.
    /// @param password The password string.
    /// @param salt The salt as a Bytes object.
    /// @param iterations Number of iterations (minimum 1000).
    /// @param key_len Desired key length in bytes (1-1024).
    /// @return Derived key as lowercase hex string.
    /// @note Traps if iterations < 1000 or key_len not in [1, 1024].
    rt_string rt_keyderive_pbkdf2_sha256_str(rt_string password,
                                             void *salt,
                                             int64_t iterations,
                                             int64_t key_len);

#ifdef __cplusplus
}
#endif
