//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_codec.c
// Purpose: Base64, Hex, and URL encoding/decoding utilities for strings.
// Key invariants: All functions operate on strings; encoding is reversible.
// Ownership/Lifetime: Returned strings are newly allocated.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#include "rt_codec.h"

#include "rt_internal.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Hex character lookup table for encoding.
static const char hex_chars[] = "0123456789abcdef";

/// Base64 character lookup table for encoding (RFC 4648).
static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Convert hex character to value (0-15).
/// @return Value 0-15, or -1 if invalid.
static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// @brief Convert Base64 character to value (0-63).
/// @return Value 0-63, -2 for '=', or -1 if invalid.
static int b64_digit_value(char c)
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
    if (c == '=')
        return -2;
    return -1;
}

/// @brief Check if character is unreserved in URL encoding.
/// @details Unreserved: A-Z a-z 0-9 - _ . ~
static int is_url_unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

//=============================================================================
// URL Encoding/Decoding
//=============================================================================

rt_string rt_codec_url_encode(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t input_len = strlen(input);
    if (input_len == 0)
        return rt_string_from_bytes("", 0);

    // Calculate output length (worst case: every char needs encoding)
    size_t out_len = 0;
    for (size_t i = 0; i < input_len; i++)
    {
        unsigned char c = (unsigned char)input[i];
        if (is_url_unreserved(c))
            out_len++;
        else
            out_len += 3; // %XX
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out)
        rt_trap("Codec.UrlEncode: memory allocation failed");

    size_t o = 0;
    for (size_t i = 0; i < input_len; i++)
    {
        unsigned char c = (unsigned char)input[i];
        if (is_url_unreserved(c))
        {
            out[o++] = (char)c;
        }
        else
        {
            out[o++] = '%';
            out[o++] = hex_chars[(c >> 4) & 0xF];
            out[o++] = hex_chars[c & 0xF];
        }
    }
    out[o] = '\0';

    rt_string result = rt_string_from_bytes(out, o);
    free(out);
    return result;
}

rt_string rt_codec_url_decode(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t input_len = strlen(input);
    if (input_len == 0)
        return rt_string_from_bytes("", 0);

    // Output is at most as long as input
    char *out = (char *)malloc(input_len + 1);
    if (!out)
        rt_trap("Codec.UrlDecode: memory allocation failed");

    size_t o = 0;
    for (size_t i = 0; i < input_len; i++)
    {
        char c = input[i];
        if (c == '%' && i + 2 < input_len)
        {
            int hi = hex_digit_value(input[i + 1]);
            int lo = hex_digit_value(input[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out[o++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        else if (c == '+')
        {
            // Treat + as space (form encoding convention)
            out[o++] = ' ';
            continue;
        }
        out[o++] = c;
    }
    out[o] = '\0';

    rt_string result = rt_string_from_bytes(out, o);
    free(out);
    return result;
}

//=============================================================================
// Base64 Encoding/Decoding
//=============================================================================

rt_string rt_codec_base64_enc(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t input_len = strlen(input);
    if (input_len == 0)
        return rt_string_from_bytes("", 0);

    size_t output_len = ((input_len + 2) / 3) * 4;

    char *out = (char *)malloc(output_len + 1);
    if (!out)
        rt_trap("Codec.Base64Enc: memory allocation failed");

    const uint8_t *data = (const uint8_t *)input;
    size_t i = 0;
    size_t o = 0;

    // Process 3-byte groups
    while (i + 3 <= input_len)
    {
        uint32_t triple = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[o++] = b64_chars[(triple >> 18) & 0x3F];
        out[o++] = b64_chars[(triple >> 12) & 0x3F];
        out[o++] = b64_chars[(triple >> 6) & 0x3F];
        out[o++] = b64_chars[triple & 0x3F];
        i += 3;
    }

    // Handle remaining bytes
    if (i < input_len)
    {
        uint32_t triple = (uint32_t)data[i] << 16;
        int two = 0;
        if (i + 1 < input_len)
        {
            triple |= (uint32_t)data[i + 1] << 8;
            two = 1;
        }

        out[o++] = b64_chars[(triple >> 18) & 0x3F];
        out[o++] = b64_chars[(triple >> 12) & 0x3F];
        if (two)
        {
            out[o++] = b64_chars[(triple >> 6) & 0x3F];
            out[o++] = '=';
        }
        else
        {
            out[o++] = '=';
            out[o++] = '=';
        }
    }

    out[o] = '\0';
    rt_string result = rt_string_from_bytes(out, o);
    free(out);
    return result;
}

rt_string rt_codec_base64_dec(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t b64_len = strlen(input);
    if (b64_len == 0)
        return rt_string_from_bytes("", 0);

    if (b64_len % 4 != 0)
        rt_trap("Codec.Base64Dec: base64 length must be a multiple of 4");

    size_t padding = 0;
    if (b64_len >= 1 && input[b64_len - 1] == '=')
    {
        padding = 1;
        if (b64_len >= 2 && input[b64_len - 2] == '=')
            padding = 2;
    }

    // Check for invalid padding position
    for (size_t i = 0; i < b64_len - padding; ++i)
    {
        if (input[i] == '=')
            rt_trap("Codec.Base64Dec: invalid padding");
    }

    size_t out_len = (b64_len / 4) * 3 - padding;
    if (out_len == 0)
        return rt_string_from_bytes("", 0);

    char *out = (char *)malloc(out_len + 1);
    if (!out)
        rt_trap("Codec.Base64Dec: memory allocation failed");

    size_t out_pos = 0;
    for (size_t i = 0; i < b64_len; i += 4)
    {
        char c0 = input[i];
        char c1 = input[i + 1];
        char c2 = input[i + 2];
        char c3 = input[i + 3];

        int v0 = b64_digit_value(c0);
        int v1 = b64_digit_value(c1);
        int v2 = b64_digit_value(c2);
        int v3 = b64_digit_value(c3);

        if (v0 < 0 || v1 < 0)
        {
            free(out);
            if (v0 == -2 || v1 == -2)
                rt_trap("Codec.Base64Dec: invalid padding");
            rt_trap("Codec.Base64Dec: invalid base64 character");
        }

        if (v2 == -1 || v3 == -1)
        {
            free(out);
            rt_trap("Codec.Base64Dec: invalid base64 character");
        }

        if (v2 == -2)
        {
            if (v3 != -2 || i + 4 != b64_len)
            {
                free(out);
                rt_trap("Codec.Base64Dec: invalid padding");
            }
            if ((v1 & 0x0F) != 0)
            {
                free(out);
                rt_trap("Codec.Base64Dec: invalid padding");
            }

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12);
            out[out_pos++] = (char)((triple >> 16) & 0xFF);
            break;
        }

        if (v3 == -2)
        {
            if (i + 4 != b64_len)
            {
                free(out);
                rt_trap("Codec.Base64Dec: invalid padding");
            }
            if ((v2 & 0x03) != 0)
            {
                free(out);
                rt_trap("Codec.Base64Dec: invalid padding");
            }

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6);
            out[out_pos++] = (char)((triple >> 16) & 0xFF);
            out[out_pos++] = (char)((triple >> 8) & 0xFF);
            break;
        }

        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
        out[out_pos++] = (char)((triple >> 16) & 0xFF);
        out[out_pos++] = (char)((triple >> 8) & 0xFF);
        out[out_pos++] = (char)(triple & 0xFF);
    }

    if (out_pos != out_len)
    {
        free(out);
        rt_trap("Codec.Base64Dec: invalid padding");
    }

    out[out_pos] = '\0';
    rt_string result = rt_string_from_bytes(out, out_pos);
    free(out);
    return result;
}

//=============================================================================
// Hex Encoding/Decoding
//=============================================================================

rt_string rt_codec_hex_enc(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t input_len = strlen(input);
    if (input_len == 0)
        return rt_string_from_bytes("", 0);

    size_t output_len = input_len * 2;
    char *out = (char *)malloc(output_len + 1);
    if (!out)
        rt_trap("Codec.HexEnc: memory allocation failed");

    const uint8_t *data = (const uint8_t *)input;
    for (size_t i = 0; i < input_len; i++)
    {
        out[i * 2] = hex_chars[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[data[i] & 0xF];
    }
    out[output_len] = '\0';

    rt_string result = rt_string_from_bytes(out, output_len);
    free(out);
    return result;
}

rt_string rt_codec_hex_dec(rt_string str)
{
    const char *input = rt_string_cstr(str);
    if (!input)
        return rt_string_from_bytes("", 0);

    size_t hex_len = strlen(input);
    if (hex_len == 0)
        return rt_string_from_bytes("", 0);

    if (hex_len % 2 != 0)
        rt_trap("Codec.HexDec: hex string length must be even");

    size_t out_len = hex_len / 2;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        rt_trap("Codec.HexDec: memory allocation failed");

    for (size_t i = 0; i < out_len; i++)
    {
        int hi = hex_digit_value(input[i * 2]);
        int lo = hex_digit_value(input[i * 2 + 1]);

        if (hi < 0 || lo < 0)
        {
            free(out);
            rt_trap("Codec.HexDec: invalid hex character");
        }

        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';

    rt_string result = rt_string_from_bytes(out, out_len);
    free(out);
    return result;
}
