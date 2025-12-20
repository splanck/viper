//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_keyderive.h
// Purpose: Key derivation functions (PBKDF2-SHA256).
// Key invariants: Minimum 1000 iterations, key length 1-1024 bytes.
// Ownership/Lifetime: Returned objects are newly allocated.
// Links: docs/viperlib/crypto.md
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
    void *rt_keyderive_pbkdf2_sha256(rt_string password, void *salt, int64_t iterations,
                                     int64_t key_len);

    /// @brief Derive a key using PBKDF2-SHA256 and return as hex string.
    /// @param password The password string.
    /// @param salt The salt as a Bytes object.
    /// @param iterations Number of iterations (minimum 1000).
    /// @param key_len Desired key length in bytes (1-1024).
    /// @return Derived key as lowercase hex string.
    /// @note Traps if iterations < 1000 or key_len not in [1, 1024].
    rt_string rt_keyderive_pbkdf2_sha256_str(rt_string password, void *salt, int64_t iterations,
                                             int64_t key_len);

#ifdef __cplusplus
}
#endif
