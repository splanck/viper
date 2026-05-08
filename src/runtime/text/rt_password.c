//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_password.c
// Purpose: Implements secure password hashing and verification for the
//          Viper.Crypto.Password class using scrypt-SHA256 or legacy
//          PBKDF2-SHA256 with automatically generated random salts.
//
// Key invariants:
//   - Salts are 16 bytes of CSPRNG output, unique per hash call.
//   - Hash output format: "SCRYPT$<log2N>$<r>$<p>$<salt_b64>$<hash_b64>".
//   - Legacy PBKDF2 format remains accepted: "PBKDF2$<iterations>$<salt_b64>$<hash_b64>".
//   - Verification uses constant-time comparison to prevent timing attacks.
//   - Default Hash uses bounded memory-hard scrypt parameters.
//   - Custom PBKDF2 requests below 100,000 trap instead of silently clamping.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Legacy PBKDF2 policy.
#define DEFAULT_ITERATIONS 300000
#define MIN_ITERATIONS 100000
#define MAX_ITERATIONS 10000000

// Current password-hash policy.
#define PASSWORD_SCRYPT_N_LOG2 RT_SCRYPT_DEFAULT_N_LOG2
#define PASSWORD_SCRYPT_R RT_SCRYPT_DEFAULT_R
#define PASSWORD_SCRYPT_P RT_SCRYPT_DEFAULT_P
#define SALT_LENGTH 16
#define HASH_LENGTH 32
#define SALT_B64_LENGTH 24
#define HASH_B64_LENGTH 44

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

/// @brief Extract a raw byte pointer and byte count from an rt_string password.
///        Returns an empty C string and sets *len = 0 for null or zero-length input.
static const uint8_t *password_string_bytes(rt_string password, size_t *len) {
    int64_t len64 = rt_str_len(password);
    if (len64 <= 0) {
        *len = 0;
        return (const uint8_t *)"";
    }

    const char *pwd = rt_string_cstr(password);
    if (!pwd) {
        *len = 0;
        return (const uint8_t *)"";
    }

    *len = (size_t)len64;
    return (const uint8_t *)pwd;
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
    if (out_len)
        *out_len = 0;
    if (!data || len % 4 != 0)
        return NULL;

    size_t first_pad = len;
    for (size_t k = 0; k < len; k++) {
        if (data[k] == '=') {
            if (first_pad == len)
                first_pad = k;
            continue;
        }
        if (first_pad != len)
            return NULL;
        if (base64_decode_char(data[k]) < 0)
            return NULL;
    }

    size_t padding = first_pad == len ? 0 : len - first_pad;
    if (padding > 2)
        return NULL;
    if (padding > 0 && first_pad < len - 2)
        return NULL;
    if (len > 0 && (data[0] == '=' || data[1] == '='))
        return NULL;

    size_t olen = (len / 4) * 3;
    olen -= padding;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
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

static int password_fixed_time_eq(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}

static int scrypt_log2_from_n(uint64_t n) {
    if (n < 2 || (n & (n - 1)) != 0)
        return -1;
    int log2n = 0;
    while (n > 1) {
        n >>= 1;
        log2n++;
    }
    return log2n;
}

static rt_string password_format_hash(const char *prefix,
                                      const char *params,
                                      const uint8_t salt[SALT_LENGTH],
                                      const uint8_t hash[HASH_LENGTH],
                                      const char *op_name) {
    size_t salt_b64_len, hash_b64_len;
    char *salt_b64 = base64_encode(salt, SALT_LENGTH, &salt_b64_len);
    char *hash_b64 = base64_encode(hash, HASH_LENGTH, &hash_b64_len);
    if (!salt_b64 || !hash_b64 || salt_b64_len != SALT_B64_LENGTH ||
        hash_b64_len != HASH_B64_LENGTH) {
        free(salt_b64);
        free(hash_b64);
        rt_trap(op_name);
    }

    char buffer[192];
    if (params && params[0])
        snprintf(buffer, sizeof(buffer), "%s$%s$%s$%s", prefix, params, salt_b64, hash_b64);
    else
        snprintf(buffer, sizeof(buffer), "%s$%s$%s", prefix, salt_b64, hash_b64);

    free(salt_b64);
    free(hash_b64);
    return rt_string_from_bytes(buffer, strlen(buffer));
}

rt_string rt_password_hash(rt_string password) {
    return rt_password_hash_scrypt(password);
}

rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations) {
    if (iterations < MIN_ITERATIONS)
        rt_trap("Password.HashIters: iterations must be at least 100000");
    if (iterations > MAX_ITERATIONS)
        rt_trap("Password.HashIters: iterations must not exceed 10000000");

    uint8_t salt[SALT_LENGTH];
    rt_crypto_random_bytes(salt, SALT_LENGTH);

    size_t pwd_len;
    const uint8_t *pwd = password_string_bytes(password, &pwd_len);

    uint8_t hash[HASH_LENGTH];
    rt_keyderive_pbkdf2_sha256_raw(pwd, pwd_len, salt, SALT_LENGTH, (uint32_t)iterations, hash, HASH_LENGTH);

    char params[32];
    snprintf(params, sizeof(params), "%lld", (long long)iterations);
    rt_string result = password_format_hash(
        "PBKDF2", params, salt, hash, "Password.HashIters: memory allocation failed");
    password_secure_zero(hash, sizeof(hash));
    password_secure_zero(salt, sizeof(salt));
    return result;
}

rt_string rt_password_hash_scrypt(rt_string password) {
    return rt_password_hash_scrypt_params(password,
                                          (int64_t)(UINT64_C(1) << PASSWORD_SCRYPT_N_LOG2),
                                          PASSWORD_SCRYPT_R,
                                          PASSWORD_SCRYPT_P);
}

rt_string rt_password_hash_scrypt_params(rt_string password, int64_t n64, int64_t r64, int64_t p64) {
    if (n64 < 2 || r64 < 1 || p64 < 1 || r64 > RT_SCRYPT_MAX_R || p64 > RT_SCRYPT_MAX_P)
        rt_trap("Password.HashScrypt: invalid scrypt parameters");
    uint64_t n = (uint64_t)n64;
    uint32_t r = (uint32_t)r64;
    uint32_t p = (uint32_t)p64;
    int log2n = scrypt_log2_from_n(n);
    if (log2n < 1 || (uint32_t)log2n > RT_SCRYPT_MAX_N_LOG2)
        rt_trap("Password.HashScrypt: N must be a supported power of two");

    uint8_t salt[SALT_LENGTH];
    rt_crypto_random_bytes(salt, SALT_LENGTH);

    size_t pwd_len;
    const uint8_t *pwd = password_string_bytes(password, &pwd_len);

    uint8_t hash[HASH_LENGTH];
    rt_keyderive_scrypt_sha256_raw(pwd, pwd_len, salt, SALT_LENGTH, n, r, p, hash, HASH_LENGTH);

    char params[48];
    snprintf(params, sizeof(params), "%d$%u$%u", log2n, r, p);
    rt_string result = password_format_hash(
        "SCRYPT", params, salt, hash, "Password.HashScrypt: memory allocation failed");
    password_secure_zero(hash, sizeof(hash));
    password_secure_zero(salt, sizeof(salt));
    return result;
}

static int password_parse_b64_field(const char **p,
                                    size_t expected_b64_len,
                                    char **out,
                                    size_t *out_len) {
    const char *start = *p;
    while (**p && **p != '$')
        (*p)++;
    size_t len = (size_t)(*p - start);
    if (len != expected_b64_len)
        return 0;
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return 0;
    memcpy(copy, start, len);
    copy[len] = '\0';
    *out = copy;
    *out_len = len;
    if (**p == '$')
        (*p)++;
    return 1;
}

static int password_decode_salt_hash(const char *salt_start,
                                     size_t salt_b64_len,
                                     const char *hash_start,
                                     size_t hash_b64_len,
                                     uint8_t **salt,
                                     size_t *salt_len,
                                     uint8_t **expected,
                                     size_t *expected_len) {
    *salt = base64_decode(salt_start, salt_b64_len, salt_len);
    if (!*salt || *salt_len != SALT_LENGTH) {
        if (*salt) {
            password_secure_zero(*salt, *salt_len);
            free(*salt);
        }
        return 0;
    }
    *expected = base64_decode(hash_start, hash_b64_len, expected_len);
    if (!*expected || *expected_len != HASH_LENGTH) {
        password_secure_zero(*salt, *salt_len);
        free(*salt);
        if (*expected) {
            password_secure_zero(*expected, *expected_len);
            free(*expected);
        }
        return 0;
    }
    return 1;
}

static int password_verify_pbkdf2(rt_string password, const char *hash_str) {
    const char *p = hash_str + 7;
    char *end;

    errno = 0;
    long long iterations = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$' || iterations < MIN_ITERATIONS ||
        iterations > MAX_ITERATIONS)
        return 0;
    p = end + 1;

    char *salt_b64 = NULL;
    size_t salt_b64_len = 0;
    if (!password_parse_b64_field(&p, SALT_B64_LENGTH, &salt_b64, &salt_b64_len))
        return 0;

    const char *hash_b64_start = p;
    size_t hash_b64_len = strlen(hash_b64_start);
    if (hash_b64_len != HASH_B64_LENGTH) {
        free(salt_b64);
        return 0;
    }

    size_t salt_len;
    size_t expected_len;
    uint8_t *salt = NULL;
    uint8_t *expected = NULL;
    int ok = password_decode_salt_hash(
        salt_b64, salt_b64_len, hash_b64_start, hash_b64_len, &salt, &salt_len, &expected, &expected_len);
    free(salt_b64);
    if (!ok)
        return 0;

    size_t pwd_len;
    const uint8_t *pwd = password_string_bytes(password, &pwd_len);
    uint8_t computed[HASH_LENGTH];
    rt_keyderive_pbkdf2_sha256_raw(pwd, pwd_len, salt, salt_len, (uint32_t)iterations, computed, HASH_LENGTH);

    ok = password_fixed_time_eq(computed, expected, HASH_LENGTH);
    password_secure_zero(salt, salt_len);
    free(salt);
    password_secure_zero(expected, expected_len);
    free(expected);
    password_secure_zero(computed, sizeof(computed));
    return ok;
}

static int password_verify_scrypt(rt_string password, const char *hash_str) {
    const char *p = hash_str + 7;
    char *end;

    errno = 0;
    long long log2n_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$' || log2n_ll < 1 ||
        log2n_ll > RT_SCRYPT_MAX_N_LOG2)
        return 0;
    p = end + 1;

    errno = 0;
    long long r_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$' || r_ll < 1 || r_ll > RT_SCRYPT_MAX_R)
        return 0;
    p = end + 1;

    errno = 0;
    long long p_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$' || p_ll < 1 || p_ll > RT_SCRYPT_MAX_P)
        return 0;
    p = end + 1;

    char *salt_b64 = NULL;
    size_t salt_b64_len = 0;
    if (!password_parse_b64_field(&p, SALT_B64_LENGTH, &salt_b64, &salt_b64_len))
        return 0;

    const char *hash_b64_start = p;
    size_t hash_b64_len = strlen(hash_b64_start);
    if (hash_b64_len != HASH_B64_LENGTH) {
        free(salt_b64);
        return 0;
    }

    size_t salt_len;
    size_t expected_len;
    uint8_t *salt = NULL;
    uint8_t *expected = NULL;
    int ok = password_decode_salt_hash(
        salt_b64, salt_b64_len, hash_b64_start, hash_b64_len, &salt, &salt_len, &expected, &expected_len);
    free(salt_b64);
    if (!ok)
        return 0;

    size_t pwd_len;
    const uint8_t *pwd = password_string_bytes(password, &pwd_len);
    uint8_t computed[HASH_LENGTH];
    uint64_t n = UINT64_C(1) << (uint32_t)log2n_ll;
    rt_keyderive_scrypt_sha256_raw(
        pwd, pwd_len, salt, salt_len, n, (uint32_t)r_ll, (uint32_t)p_ll, computed, HASH_LENGTH);

    ok = password_fixed_time_eq(computed, expected, HASH_LENGTH);

    password_secure_zero(salt, salt_len);
    free(salt);
    password_secure_zero(expected, expected_len);
    free(expected);
    password_secure_zero(computed, sizeof(computed));
    return ok;
}

int8_t rt_password_verify(rt_string password, rt_string hash) {
    if (!hash)
        return 0;
    int64_t hash_len64 = rt_str_len(hash);
    if (hash_len64 <= 0)
        return 0;
    const char *hash_str = rt_string_cstr(hash);
    if (!hash_str)
        return 0;
    if (strlen(hash_str) != (size_t)hash_len64)
        return 0;

    if (strncmp(hash_str, "SCRYPT$", 7) == 0)
        return password_verify_scrypt(password, hash_str) ? 1 : 0;
    if (strncmp(hash_str, "PBKDF2$", 7) == 0)
        return password_verify_pbkdf2(password, hash_str) ? 1 : 0;
    return 0;
}

int8_t rt_password_needs_rehash(rt_string hash) {
    if (!hash)
        return 1;
    int64_t hash_len64 = rt_str_len(hash);
    if (hash_len64 <= 0)
        return 1;
    const char *hash_str = rt_string_cstr(hash);
    if (!hash_str || strlen(hash_str) != (size_t)hash_len64)
        return 1;

    if (strncmp(hash_str, "SCRYPT$", 7) != 0)
        return 1;

    const char *p = hash_str + 7;
    char *end;
    errno = 0;
    long long log2n_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$')
        return 1;
    p = end + 1;
    errno = 0;
    long long r_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$')
        return 1;
    p = end + 1;
    errno = 0;
    long long p_ll = strtoll(p, &end, 10);
    if (errno != 0 || end == p || *end != '$')
        return 1;

    if (log2n_ll != PASSWORD_SCRYPT_N_LOG2 || r_ll != PASSWORD_SCRYPT_R ||
        p_ll != PASSWORD_SCRYPT_P)
        return 1;
    return 0;
}
