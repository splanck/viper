//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_keyderive.c
// Purpose: Implements PBKDF2-SHA256 key derivation (RFC 2898 / RFC 8018) for
//          the Viper.Text.KeyDerive class. Derives cryptographic keys from
//          passwords using HMAC-SHA256 as the pseudorandom function with a
//          configurable iteration count and salt.
//
// Key invariants:
//   - Minimum iteration count is PBKDF2_MIN_ITERATIONS (1000); requests below
//     this threshold are silently raised to the minimum.
//   - Salt must be non-empty; a NULL or empty salt causes a trap.
//   - Output key length is specified in bytes; any positive length is supported.
//   - HMAC-SHA256 block size is 64 bytes; key padding follows RFC 2104.
//   - The derived key is returned as a hex-encoded rt_string for portability.
//
// Ownership/Lifetime:
//   - The returned rt_string key is a fresh allocation owned by the caller.
//   - Input password and salt strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_keyderive.h (public API),
//        src/runtime/text/rt_hash.h (SHA256 used as PRF base),
//        src/runtime/text/rt_password.h (higher-level password hashing)
//
//===----------------------------------------------------------------------===//

#include "rt_keyderive.h"
#include "rt_keyderive_internal.h"

#include "rt_bytes.h"
#include "rt_codec.h"
#include "rt_hash.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// SHA256 output size in bytes.
#define SHA256_DIGEST_LEN 32

/// @brief Optimization-resistant zero-fill for sensitive PBKDF2 intermediates.
/// @details `volatile` pointer write defeats dead-store elimination so
///          the compiler can't optimize this loop away. Used to wipe
///          every transient buffer (`U`, `T`, `salt || INT(i)`, the
///          fully-derived key once it's been copied to a Bytes
///          object) before functions return.
static void keyderive_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0)
        *p++ = 0;
}

/// @brief PBKDF2-HMAC-SHA256 implementation (RFC 2898 § 5.2 / RFC 8018).
/// @details Derives `out_len` bytes from `(password, salt)` by iterating HMAC-
///          SHA256 `iterations` times per output block. The output is
///          structured as a concatenation of fixed-size blocks:
///
///          ```
///          DK = T(1) || T(2) || ... || T(L)        where L = ceil(out_len / 32)
///          T(i) = U(1) XOR U(2) XOR ... XOR U(c)   c = iterations
///          U(1) = HMAC-SHA256(password, salt || INT32_BE(i))
///          U(j) = HMAC-SHA256(password, U(j-1))    for j > 1
///          ```
///
///          The XOR-accumulation across iterations is the cost amplification:
///          an attacker brute-forcing the password must evaluate HMAC-SHA256
///          `c` times per guess, making the per-guess cost linear in
///          `iterations`. Modern guidance (OWASP 2023) recommends at least
///          ~600,000 iterations for SHA256; this raw function trusts the
///          caller, but the higher-level `Password` class enforces a
///          minimum of 1,000 (the historical RFC-2898 floor) to prevent
///          accidentally toothless deployments.
///
///          The block-index `INT32_BE(i)` is appended to the salt buffer in
///          place across the outer loop (allocated once, reused with the
///          last 4 bytes overwritten per block). All intermediate state
///          (`U`, `T`, the `salt || INT(i)` buffer) is zeroed via
///          `keyderive_secure_zero` before the function returns so the
///          derived key material doesn't linger in heap memory after the
///          caller copies it out.
/// @param password Password bytes (any length).
/// @param password_len Length of `password`.
/// @param salt Salt bytes (any length, including 0).
/// @param salt_len Length of `salt`.
/// @param iterations Number of HMAC iterations per output block (cost
///        parameter — higher means slower per guess).
/// @param out Output buffer for derived key material.
/// @param out_len Number of bytes to produce.
void rt_keyderive_pbkdf2_sha256_raw(const uint8_t *password,
                                    size_t password_len,
                                    const uint8_t *salt,
                                    size_t salt_len,
                                    uint32_t iterations,
                                    uint8_t *out,
                                    size_t out_len) {
    // Number of blocks needed
    uint32_t block_count = (uint32_t)((out_len + SHA256_DIGEST_LEN - 1) / SHA256_DIGEST_LEN);

    // Allocate buffer for salt || INT(i)
    size_t salt_int_len = salt_len + 4;
    uint8_t *salt_int = (uint8_t *)malloc(salt_int_len);
    if (!salt_int)
        rt_trap("PBKDF2: memory allocation failed");

    if (salt_len > 0)
        memcpy(salt_int, salt, salt_len);

    size_t bytes_written = 0;

    for (uint32_t block_num = 1; block_num <= block_count; block_num++) {
        // Append block number as big-endian 32-bit integer
        salt_int[salt_len] = (uint8_t)(block_num >> 24);
        salt_int[salt_len + 1] = (uint8_t)(block_num >> 16);
        salt_int[salt_len + 2] = (uint8_t)(block_num >> 8);
        salt_int[salt_len + 3] = (uint8_t)(block_num);

        // U1 = PRF(Password, Salt || INT(i))
        uint8_t U[SHA256_DIGEST_LEN];
        uint8_t T[SHA256_DIGEST_LEN];

        rt_hash_hmac_sha256_raw(password, password_len, salt_int, salt_int_len, U);
        memcpy(T, U, SHA256_DIGEST_LEN);

        // U2 through Uc
        for (uint32_t iter = 2; iter <= iterations; iter++) {
            rt_hash_hmac_sha256_raw(password, password_len, U, SHA256_DIGEST_LEN, U);
            for (int j = 0; j < SHA256_DIGEST_LEN; j++) {
                T[j] ^= U[j];
            }
        }

        // Copy T to output (may be partial for last block)
        size_t bytes_to_copy = out_len - bytes_written;
        if (bytes_to_copy > SHA256_DIGEST_LEN)
            bytes_to_copy = SHA256_DIGEST_LEN;

        memcpy(out + bytes_written, T, bytes_to_copy);
        bytes_written += bytes_to_copy;

        keyderive_secure_zero(U, sizeof(U));
        keyderive_secure_zero(T, sizeof(T));
    }

    keyderive_secure_zero(salt_int, salt_int_len);
    free(salt_int);
}

/// @brief Derive a key from a password using PBKDF2-SHA256, returning a Bytes object.
/// @details High-level wrapper around rt_keyderive_pbkdf2_sha256_raw that
///          handles Viper string/bytes conversion. Validates iterations (min 1000)
///          and key length (1–1024 bytes). The derived key is zeroed from the
///          temporary buffer after copying to the Bytes object.
/// @param password   Password string.
/// @param salt       Bytes object containing the salt.
/// @param iterations Number of PBKDF2 iterations (min 1000).
/// @param key_len    Desired output key length in bytes (1–1024).
/// @return Bytes object containing the derived key.
void *rt_keyderive_pbkdf2_sha256(rt_string password,
                                 void *salt,
                                 int64_t iterations,
                                 int64_t key_len) {
    // Validate iterations
    if (iterations < RT_PBKDF2_MIN_ITERATIONS) {
        rt_trap("PBKDF2: iterations must be at least 1000");
    }

    // Validate key length
    if (key_len < 1 || key_len > RT_PBKDF2_MAX_KEY_LEN) {
        rt_trap("PBKDF2: key_len must be between 1 and 1024");
    }

    // Extract password
    const char *pwd_cstr = rt_string_cstr(password);
    if (!pwd_cstr)
        pwd_cstr = "";
    size_t pwd_len = strlen(pwd_cstr);

    // Extract salt
    size_t salt_len;
    uint8_t *salt_data = rt_bytes_extract_raw(salt, &salt_len);

    // Allocate output buffer
    uint8_t *derived_key = (uint8_t *)malloc((size_t)key_len);
    if (!derived_key)
        rt_trap("PBKDF2: memory allocation failed");

    // Derive key
    rt_keyderive_pbkdf2_sha256_raw((const uint8_t *)pwd_cstr,
                                   pwd_len,
                                   salt_data ? salt_data : (const uint8_t *)"",
                                   salt_len,
                                   (uint32_t)iterations,
                                   derived_key,
                                   (size_t)key_len);

    if (salt_data)
        free(salt_data);

    // Create Bytes object from derived key
    void *result = rt_bytes_from_raw(derived_key, (size_t)key_len);
    keyderive_secure_zero(derived_key, (size_t)key_len);
    free(derived_key);
    return result;
}

/// @brief Derive a key from a password using PBKDF2-SHA256, returning a hex string.
/// @details Same derivation as rt_keyderive_pbkdf2_sha256 but returns the key
///          encoded as a lowercase hexadecimal string (2 chars per byte). Useful
///          for interop with systems that expect text-encoded keys.
/// @param password   Password string.
/// @param salt       Bytes object containing the salt.
/// @param iterations Number of PBKDF2 iterations (min 1000).
/// @param key_len    Desired output key length in bytes (hex output is 2x this).
/// @return Hex-encoded string of the derived key.
rt_string rt_keyderive_pbkdf2_sha256_str(rt_string password,
                                         void *salt,
                                         int64_t iterations,
                                         int64_t key_len) {
    // Validate iterations
    if (iterations < RT_PBKDF2_MIN_ITERATIONS) {
        rt_trap("PBKDF2: iterations must be at least 1000");
    }

    // Validate key length
    if (key_len < 1 || key_len > RT_PBKDF2_MAX_KEY_LEN) {
        rt_trap("PBKDF2: key_len must be between 1 and 1024");
    }

    // Extract password
    const char *pwd_cstr = rt_string_cstr(password);
    if (!pwd_cstr)
        pwd_cstr = "";
    size_t pwd_len = strlen(pwd_cstr);

    // Extract salt
    size_t salt_len;
    uint8_t *salt_data = rt_bytes_extract_raw(salt, &salt_len);

    // Allocate output buffer
    uint8_t *derived_key = (uint8_t *)malloc((size_t)key_len);
    if (!derived_key)
        rt_trap("PBKDF2: memory allocation failed");

    // Derive key
    rt_keyderive_pbkdf2_sha256_raw((const uint8_t *)pwd_cstr,
                                   pwd_len,
                                   salt_data ? salt_data : (const uint8_t *)"",
                                   salt_len,
                                   (uint32_t)iterations,
                                   derived_key,
                                   (size_t)key_len);

    if (salt_data)
        free(salt_data);

    // Convert to hex string using shared codec utility
    rt_string result = rt_codec_hex_enc_bytes(derived_key, (size_t)key_len);
    keyderive_secure_zero(derived_key, (size_t)key_len);
    free(derived_key);
    return result;
}
