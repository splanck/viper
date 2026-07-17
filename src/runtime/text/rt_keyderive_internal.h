//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#define RT_PBKDF2_MIN_ITERATIONS 100000U
#define RT_PBKDF2_MAX_ITERATIONS 10000000U
#define RT_PBKDF2_MAX_KEY_LEN 1024U

#define RT_SCRYPT_DEFAULT_N_LOG2 14U
#define RT_SCRYPT_DEFAULT_R 8U
#define RT_SCRYPT_DEFAULT_P 1U
#define RT_SCRYPT_MAX_N_LOG2 20U
#define RT_SCRYPT_MAX_R 32U
#define RT_SCRYPT_MAX_P 32U
#define RT_SCRYPT_MAX_KEY_LEN 1024U
#define RT_SCRYPT_MAX_MEMORY (64U * 1024U * 1024U)

/// @brief Raw PBKDF2-HMAC-SHA256 key derivation (RFC 2898 / RFC 8018).
/// @details Derives a key of arbitrary length from a password and salt by
///          iteratively applying HMAC-SHA256. This is the low-level C function
///          used by all password-based encryption in Zanna. It operates on raw
///          byte buffers and does not allocate — the caller provides the output.
/// @param password     Password bytes (not NUL-terminated).
/// @param password_len Password length in bytes.
/// @param salt         Salt bytes.
/// @param salt_len     Salt length in bytes.
/// @param iterations   Number of PBKDF2 iterations (must be positive; public wrappers enforce
/// policy).
/// @param out          Output buffer for the derived key.
/// @param out_len      Desired key length in bytes (max RT_PBKDF2_MAX_KEY_LEN).
void rt_keyderive_pbkdf2_sha256_raw(const uint8_t *password,
                                    size_t password_len,
                                    const uint8_t *salt,
                                    size_t salt_len,
                                    uint32_t iterations,
                                    uint8_t *out,
                                    size_t out_len);

/// @brief Raw scrypt-SHA256 key derivation (RFC 7914).
void rt_keyderive_scrypt_sha256_raw(const uint8_t *password,
                                    size_t password_len,
                                    const uint8_t *salt,
                                    size_t salt_len,
                                    uint64_t n,
                                    uint32_t r,
                                    uint32_t p,
                                    uint8_t *out,
                                    size_t out_len);

/// @brief Return whether scrypt parameters satisfy runtime CPU/memory policy.
int rt_keyderive_scrypt_params_supported(uint64_t n, uint32_t r, uint32_t p, size_t out_len);

#ifdef __cplusplus
}
#endif
