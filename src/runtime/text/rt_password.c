//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_password.c
// Purpose: Secure password hashing with auto-salt and verify (PBKDF2-SHA256).
//
//===----------------------------------------------------------------------===//

#include "rt_password.h"

#include "rt_crypto.h"
#include "rt_hash.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default iterations for password hashing (100k is reasonable for 2024+)
#define DEFAULT_ITERATIONS 100000
#define MIN_ITERATIONS 10000
#define SALT_LENGTH 16
#define HASH_LENGTH 32

//=============================================================================
// Internal PBKDF2-SHA256 implementation
//=============================================================================

static void pbkdf2_sha256(const uint8_t *password,
                          size_t password_len,
                          const uint8_t *salt,
                          size_t salt_len,
                          int64_t iterations,
                          uint8_t *output,
                          size_t output_len)
{
    // PBKDF2 with SHA-256: DK = T1 || T2 || ... || Tdklen/hlen
    // Ti = F(Password, Salt, c, i)
    // F(Password, Salt, c, i) = U1 ^ U2 ^ ... ^ Uc
    // U1 = PRF(Password, Salt || INT(i))
    // U2 = PRF(Password, U1)
    // ...

    // Allocate buffer for salt + 4-byte block counter
    uint8_t *block_salt = (uint8_t *)malloc(salt_len + 4);
    if (!block_salt)
        return;
    memcpy(block_salt, salt, salt_len);

    size_t pos = 0;
    uint32_t block_num = 1;

    while (pos < output_len)
    {
        // Set block number (big-endian)
        block_salt[salt_len + 0] = (block_num >> 24) & 0xFF;
        block_salt[salt_len + 1] = (block_num >> 16) & 0xFF;
        block_salt[salt_len + 2] = (block_num >> 8) & 0xFF;
        block_salt[salt_len + 3] = block_num & 0xFF;

        // U1 = HMAC-SHA256(password, salt || block_num)
        uint8_t u[32], t[32];
        rt_hash_hmac_sha256_raw(password, password_len, block_salt, salt_len + 4, u);
        memcpy(t, u, 32);

        // Ui = HMAC-SHA256(password, U(i-1)), T ^= Ui
        for (int64_t i = 1; i < iterations; i++)
        {
            rt_hash_hmac_sha256_raw(password, password_len, u, 32, u);
            for (int j = 0; j < 32; j++)
            {
                t[j] ^= u[j];
            }
        }

        // Copy result to output
        size_t copy = output_len - pos;
        if (copy > 32)
            copy = 32;
        memcpy(output + pos, t, copy);

        pos += copy;
        block_num++;
    }

    free(block_salt);
}

//=============================================================================
// Base64 encoding/decoding helpers (for hash format)
//=============================================================================

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const uint8_t *data, size_t len, size_t *out_len)
{
    size_t olen = ((len + 2) / 3) * 4;
    char *output = (char *)malloc(olen + 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len)
    {
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
    for (size_t p = 0; p < padding; p++)
    {
        output[j - 1 - p] = '=';
    }

    output[j] = '\0';
    *out_len = j;
    return output;
}

static int base64_decode_char(char c)
{
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

static uint8_t *base64_decode(const char *data, size_t len, size_t *out_len)
{
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
    while (i < len)
    {
        int a = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int b = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int c = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;
        int d = data[i] == '=' ? 0 : base64_decode_char(data[i]);
        i++;

        if (a < 0 || b < 0 || c < 0 || d < 0)
        {
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

rt_string rt_password_hash(rt_string password)
{
    return rt_password_hash_with_iterations(password, DEFAULT_ITERATIONS);
}

rt_string rt_password_hash_with_iterations(rt_string password, int64_t iterations)
{
    // Clamp iterations to minimum
    if (iterations < MIN_ITERATIONS)
    {
        iterations = MIN_ITERATIONS;
    }

    // Generate random salt
    uint8_t salt[SALT_LENGTH];
    rt_crypto_random_bytes(salt, SALT_LENGTH);

    // Derive key
    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);

    uint8_t hash[HASH_LENGTH];
    pbkdf2_sha256((const uint8_t *)pwd, pwd_len, salt, SALT_LENGTH, iterations, hash, HASH_LENGTH);

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

    return rt_string_from_bytes(buffer, strlen(buffer));
}

int8_t rt_password_verify(rt_string password, rt_string hash)
{
    const char *hash_str = rt_string_cstr(hash);

    // Parse format: "PBKDF2$iterations$salt_b64$hash_b64"
    if (strncmp(hash_str, "PBKDF2$", 7) != 0)
    {
        return 0; // Invalid format
    }

    // Find delimiters
    const char *p = hash_str + 7;
    char *end;

    // Parse iterations
    long long iterations = strtoll(p, &end, 10);
    if (*end != '$' || iterations < 1)
    {
        return 0;
    }
    p = end + 1;

    // Find salt end
    const char *salt_start = p;
    while (*p && *p != '$')
        p++;
    if (*p != '$')
    {
        return 0;
    }
    size_t salt_b64_len = p - salt_start;
    p++;

    // Rest is hash
    const char *hash_b64_start = p;
    size_t hash_b64_len = strlen(hash_b64_start);

    // Decode salt
    char *salt_b64 = (char *)malloc(salt_b64_len + 1);
    memcpy(salt_b64, salt_start, salt_b64_len);
    salt_b64[salt_b64_len] = '\0';

    size_t salt_len;
    uint8_t *salt = base64_decode(salt_b64, salt_b64_len, &salt_len);
    free(salt_b64);

    if (!salt)
    {
        return 0;
    }

    // Decode expected hash
    size_t expected_len;
    uint8_t *expected = base64_decode(hash_b64_start, hash_b64_len, &expected_len);
    if (!expected)
    {
        free(salt);
        return 0;
    }

    // Compute hash with same parameters
    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);

    uint8_t computed[HASH_LENGTH];
    pbkdf2_sha256((const uint8_t *)pwd, pwd_len, salt, salt_len, iterations, computed, HASH_LENGTH);

    free(salt);

    // Constant-time comparison
    uint8_t diff = 0;
    size_t cmp_len = expected_len < HASH_LENGTH ? expected_len : HASH_LENGTH;
    for (size_t i = 0; i < cmp_len; i++)
    {
        diff |= computed[i] ^ expected[i];
    }
    // Also check lengths match
    diff |= (expected_len != HASH_LENGTH) ? 1 : 0;

    free(expected);

    return diff == 0 ? 1 : 0;
}
