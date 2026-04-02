//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_keyderive_internal.h
// Purpose: Internal header exposing the raw PBKDF2-HMAC-SHA256 function for
//          use by rt_cipher.c, rt_password.c, and rt_aes.c without pulling in
//          the full rt_keyderive.h public API (which depends on rt_string).
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_PBKDF2_MIN_ITERATIONS 1000U
#define RT_PBKDF2_MAX_KEY_LEN 1024U

/// @brief Raw PBKDF2-HMAC-SHA256 key derivation (RFC 2898 / RFC 8018).
/// @details Derives a key of arbitrary length from a password and salt by
///          iteratively applying HMAC-SHA256. This is the low-level C function
///          used by all password-based encryption in Viper. It operates on raw
///          byte buffers and does not allocate — the caller provides the output.
/// @param password     Password bytes (not NUL-terminated).
/// @param password_len Password length in bytes.
/// @param salt         Salt bytes.
/// @param salt_len     Salt length in bytes.
/// @param iterations   Number of PBKDF2 iterations (min RT_PBKDF2_MIN_ITERATIONS).
/// @param out          Output buffer for the derived key.
/// @param out_len      Desired key length in bytes (max RT_PBKDF2_MAX_KEY_LEN).
void rt_keyderive_pbkdf2_sha256_raw(const uint8_t *password,
                                    size_t password_len,
                                    const uint8_t *salt,
                                    size_t salt_len,
                                    uint32_t iterations,
                                    uint8_t *out,
                                    size_t out_len);

#ifdef __cplusplus
}
#endif
