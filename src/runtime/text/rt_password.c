//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_password.c
// Purpose: Implements secure password hashing and verification for the
//          Viper.Text.Password class using PBKDF2-SHA256 with an automatically
//          generated random salt. Provides Hash (returns encoded string) and
//          Verify (constant-time comparison against stored hash).
//
// Key invariants:
//   - Salts are 16 bytes of CSPRNG output, unique per hash call.
//   - Hash output format: "pbkdf2-sha256$<iterations>$<hex-salt>$<hex-key>".
//   - Verification uses constant-time comparison to prevent timing attacks.
//   - Default iteration count is 300,000; custom requests clamp to at least 100,000.
//   - Verify returns false (not trap) for mismatched passwords or invalid format.
//   - The stored hash string is self-describing (includes algorithm and params).
//
// Ownership/Lifetime:
//   - The returned hash string is a fresh rt_string allocation owned by caller.
//   - Input password strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_password.h (public API),
//        src/runtime/text/rt_keyderive.h (PBKDF2-SHA256 implementation),
//        src/runtime/text/rt_rand.h (salt generation)
//
//===----------------------------------------------------------------------===//

#include "rt_password.h"

#include "rt_crypto.h"
#include "rt_hash.h"
#include "rt_internal.h"
#include "rt_keyderive_internal.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default iterations for password hashing.
#define DEFAULT_ITERATIONS 300000
#define MIN_ITERATIONS 100000
#define MAX_ITERATIONS 10000000
#define SALT_LENGTH 16
#define HASH_LENGTH 32

/// @brief Optimization-resistant zero-fill for sensitive password and hash buffers.
/// @details Volatile-pointer write defeats dead-store elimination so
///          plaintext passwords and derived hashes don't linger in
///          stack frames after `rt_password_hash` / `rt_password_verify`
///          return. Run on every transient buffer before the function
///          exits (and on every error-path early return).
static void password_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0)
        *p++ = 0;
}

//=============================================================================
// Base64 encoding/decoding helpers (for hash format)
//=============================================================================

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Encode a binary buffer as standard Base64 (RFC 4648 alphabet, with `=` padding).
/// @details Three input bytes → four output characters. Final group
///          uses `=` padding when the input doesn't divide evenly by
///          three. Caller owns the returned buffer (must `free`).
///          Returns NULL on allocation failure. Used internally to
///          serialize the salt and hash into the on-disk hash format.
static char *base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
    size_t olen = ((len + 2) / 3) * 4;
    char *output = (char *)malloc(olen + 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (i < len) ? data[i++] : 0;
        uint32_t octet_c = (i < len) ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = base64_chars[(triple >> 6) & 0x3F];
        output[j++] = base64_chars[triple & 0x3F];
    }

    // Add padding based on input length
    size_t padding = (3 - (len % 3)) % 3;
    for (size_t p = 0; p < padding; p++) {
        output[j - 1 - p] = '=';
    }

    output[j] = '\0';
    *out_len = j;
    return output;
}

/// @brief Map one Base64 alphabet character to its 6-bit value (-1 for invalid).
/// @details Hand-coded range checks instead of a lookup table because
///          this is only called from `base64_decode` (rare path) and
///          the table would itself need to live somewhere in the
///          binary — branchless ranges win on size for the few calls.
static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

/// @brief Decode a NUL-padded Base64 string into a freshly allocated byte buffer.
/// @details Requires `len` to be a multiple of 4 (validated up-front
///          to reject malformed input). `=` padding bytes shrink the
///          output length by 1 each (1 or 2 padding bytes legal).
///          On any non-Base64 byte, frees the buffer and returns
///          NULL — strict mode, no garbage-in/garbage-out.
static uint8_t *base64_decode(const char *data, size_t len, size_t *out_len) {
    if (len % 4 != 0)
        return NULL;

    size_t olen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        olen--;
    if (len > 1 && data[len - 2] == '=')
        olen--;

    uint8_t *output = (uint8_t *)malloc(olen);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        int a = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int b = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int c = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int d = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;

        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(output);
            return NULL;
        }

        uint32_t triple =
            ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;

        if (j < olen)
            output[j++] = (triple >> 16) & 0xFF;
        if (j < olen)
            output[j++] = (triple >> 8) & 0xFF;
        if (j < olen)
            output[j++] = triple & 0xFF;
    }

    *out_len = olen;
    return output;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Hash a password using PBKDF2-SHA256 with the runtime default iteration count.
/// @details Convenience wrapper that calls rt_password_hash_with_iterations
///          with the default iteration count. The 300,000 default intentionally
///          biases toward stronger offline-attack resistance for stored-password
///          hashes while remaining practical for interactive use.
/// @param password The password to hash.
/// @return Self-describing hash string: "PBKDF2$300000$<salt_b64>$<hash_b64>".
rt_string rt_password_hash(rt_string password) {
    return rt_password_hash_with_iterations(password, DEFAULT_ITERATIONS);
}

/// @brief Hash a password with a custom PBKDF2 iteration count.
/// @details Generates a 16-byte CSPRNG salt, derives a 32-byte key using
///          PBKDF2-HMAC-SHA256, and returns a self-describing string that
///          encodes the algorithm, iteration count, salt, and hash. The format
///          is "PBKDF2$<iterations>$<salt_b64>$<hash_b64>", which allows
///          rt_password_verify to extract all parameters without separate
///          salt storage. Iterations are clamped to [100,000, 10,000,000].
/// @param password   The password to hash.
/// @param iterations Number of PBKDF2 iterations (higher = slower + more secure).
/// @return Encoded hash string safe for database storage.
rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations) {
    // Clamp iterations to minimum
    if (iterations < MIN_ITERATIONS) {
        iterations = MIN_ITERATIONS;
    }
    if (iterations > MAX_ITERATIONS) {
        rt_trap("Password.HashIters: iterations must not exceed 10000000");
    }

    // Generate random salt
    uint8_t salt[SALT_LENGTH];
    rt_crypto_random_bytes(salt, SALT_LENGTH);

    // Derive key
    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);

    uint8_t hash[HASH_LENGTH];
    rt_keyderive_pbkdf2_sha256_raw(
        (const uint8_t *)pwd, pwd_len, salt, SALT_LENGTH, (uint32_t)iterations, hash, HASH_LENGTH);

    // Encode salt and hash to base64
    size_t salt_b64_len, hash_b64_len;
    char *salt_b64 = base64_encode(salt, SALT_LENGTH, &salt_b64_len);
    char *hash_b64 = base64_encode(hash, HASH_LENGTH, &hash_b64_len);

    // Build output: "PBKDF2$iterations$salt_b64$hash_b64"
    // Max: 6 + 1 + 10 + 1 + 24 + 1 + 44 + 1 = 88 chars
    char buffer[128];
    snprintf(
        buffer, sizeof(buffer), "PBKDF2$%lld$%s$%s", (long long)iterations, salt_b64, hash_b64);

    free(salt_b64);
    free(hash_b64);
    password_secure_zero(hash, sizeof(hash));
    password_secure_zero(salt, sizeof(salt));

    return rt_string_from_bytes(buffer, strlen(buffer));
}

/// @brief Verify a password against a stored hash string.
/// @details Parses the stored hash to extract the algorithm, iteration count,
///          salt (base64-decoded), and expected hash. Re-derives the key from
///          the candidate password using the same parameters, then compares
///          using constant-time byte comparison (XOR accumulator) to prevent
///          timing side-channel attacks. Returns 0 (not trap) for any mismatch,
///          malformed input, or unrecognized format — callers should never
///          distinguish "wrong password" from "corrupt hash".
/// @param password The candidate password to verify.
/// @param hash     The stored hash string from rt_password_hash.
/// @return 1 if the password matches, 0 otherwise.
int8_t rt_password_verify(rt_string password, rt_string hash) {
    if (!hash)
        return 0;
    const char *hash_str = rt_string_cstr(hash);
    if (!hash_str)
        return 0;

    // Parse format: "PBKDF2$iterations$salt_b64$hash_b64"
    if (strncmp(hash_str, "PBKDF2$", 7) != 0) {
        return 0; // Invalid format
    }

    // Find delimiters
    const char *p = hash_str + 7;
    char *end;

    // Parse iterations
    long long iterations = strtoll(p, &end, 10);
    if (*end != '$' || iterations < MIN_ITERATIONS || iterations > MAX_ITERATIONS) {
        return 0;
    }
    p = end + 1;

    // Find salt end
    const char *salt_start = p;
    while (*p && *p != '$')
        p++;
    if (*p != '$') {
        return 0;
    }
    size_t salt_b64_len = p - salt_start;
    p++;

    // Rest is hash
    const char *hash_b64_start = p;
    size_t hash_b64_len = strlen(hash_b64_start);

    // Decode salt
    char *salt_b64 = (char *)malloc(salt_b64_len + 1);
    if (!salt_b64)
        return 0;
    memcpy(salt_b64, salt_start, salt_b64_len);
    salt_b64[salt_b64_len] = '\0';

    size_t salt_len;
    uint8_t *salt = base64_decode(salt_b64, salt_b64_len, &salt_len);
    free(salt_b64);

    if (!salt) {
        return 0;
    }

    // Decode expected hash
    size_t expected_len;
    uint8_t *expected = base64_decode(hash_b64_start, hash_b64_len, &expected_len);
    if (!expected) {
        free(salt);
        return 0;
    }

    // Compute hash with same parameters
    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);

    uint8_t computed[HASH_LENGTH];
    rt_keyderive_pbkdf2_sha256_raw(
        (const uint8_t *)pwd, pwd_len, salt, salt_len, (uint32_t)iterations, computed, HASH_LENGTH);

    password_secure_zero(salt, salt_len);
    free(salt);

    // Constant-time comparison
    uint8_t diff = 0;
    size_t cmp_len = expected_len < HASH_LENGTH ? expected_len : HASH_LENGTH;
    for (size_t i = 0; i < cmp_len; i++) {
        diff |= computed[i] ^ expected[i];
    }
    // Also check lengths match
    diff |= (expected_len != HASH_LENGTH) ? 1 : 0;

    password_secure_zero(expected, expected_len);
    free(expected);
    password_secure_zero(computed, sizeof(computed));

    return diff == 0 ? 1 : 0;
}
