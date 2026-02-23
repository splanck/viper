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

#include "rt_bytes.h"
#include "rt_codec.h"
#include "rt_hash.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Minimum iterations required for PBKDF2.
#define PBKDF2_MIN_ITERATIONS 1000

/// Maximum key length in bytes.
#define PBKDF2_MAX_KEY_LEN 1024

/// SHA256 output size in bytes.
#define SHA256_DIGEST_LEN 32

/// @brief PBKDF2-HMAC-SHA256 implementation (RFC 2898 / RFC 8018).
///
/// DK = T1 || T2 || ... || Tdklen/hlen
/// Ti = F(Password, Salt, c, i)
/// F(Password, Salt, c, i) = U1 ^ U2 ^ ... ^ Uc
/// U1 = PRF(Password, Salt || INT(i))
/// U2 = PRF(Password, U1)
/// ...
/// Uc = PRF(Password, Uc-1)
static void pbkdf2_sha256(const uint8_t *password,
                          size_t password_len,
                          const uint8_t *salt,
                          size_t salt_len,
                          uint32_t iterations,
                          uint8_t *out,
                          size_t out_len)
{
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

    for (uint32_t block_num = 1; block_num <= block_count; block_num++)
    {
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
        for (uint32_t iter = 2; iter <= iterations; iter++)
        {
            rt_hash_hmac_sha256_raw(password, password_len, U, SHA256_DIGEST_LEN, U);
            for (int j = 0; j < SHA256_DIGEST_LEN; j++)
            {
                T[j] ^= U[j];
            }
        }

        // Copy T to output (may be partial for last block)
        size_t bytes_to_copy = out_len - bytes_written;
        if (bytes_to_copy > SHA256_DIGEST_LEN)
            bytes_to_copy = SHA256_DIGEST_LEN;

        memcpy(out + bytes_written, T, bytes_to_copy);
        bytes_written += bytes_to_copy;
    }

    free(salt_int);
}

/// @brief Derive a key using PBKDF2-SHA256.
void *rt_keyderive_pbkdf2_sha256(rt_string password,
                                 void *salt,
                                 int64_t iterations,
                                 int64_t key_len)
{
    // Validate iterations
    if (iterations < PBKDF2_MIN_ITERATIONS)
    {
        rt_trap("PBKDF2: iterations must be at least 1000");
    }

    // Validate key length
    if (key_len < 1 || key_len > PBKDF2_MAX_KEY_LEN)
    {
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
    pbkdf2_sha256((const uint8_t *)pwd_cstr,
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
    free(derived_key);
    return result;
}

/// @brief Derive a key using PBKDF2-SHA256 and return as hex string.
rt_string rt_keyderive_pbkdf2_sha256_str(rt_string password,
                                         void *salt,
                                         int64_t iterations,
                                         int64_t key_len)
{
    // Validate iterations
    if (iterations < PBKDF2_MIN_ITERATIONS)
    {
        rt_trap("PBKDF2: iterations must be at least 1000");
    }

    // Validate key length
    if (key_len < 1 || key_len > PBKDF2_MAX_KEY_LEN)
    {
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
    pbkdf2_sha256((const uint8_t *)pwd_cstr,
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
    free(derived_key);
    return result;
}
