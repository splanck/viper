//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bytes.c
// Purpose: Implement efficient byte array storage for binary data.
// Structure: [len | data*]
// - len: number of bytes
// - data: contiguous byte storage
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// @brief Bytes implementation structure.
typedef struct rt_bytes_impl
{
    int64_t len;   ///< Number of bytes.
    uint8_t *data; ///< Byte storage.
} rt_bytes_impl;

static rt_bytes_impl *rt_bytes_alloc(int64_t len)
{
    if (len < 0)
        len = 0;

    size_t total = sizeof(rt_bytes_impl);
    if (len > 0)
    {
        if ((uint64_t)len > (uint64_t)SIZE_MAX - total)
            rt_trap("Bytes: memory allocation failed");
        total += (size_t)len;
    }
    if (total > (size_t)INT64_MAX)
        rt_trap("Bytes: memory allocation failed");

    rt_bytes_impl *bytes = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)total);
    if (!bytes)
        rt_trap("Bytes: memory allocation failed");

    bytes->len = len;
    bytes->data = len > 0 ? ((uint8_t *)bytes + sizeof(rt_bytes_impl)) : NULL;
    return bytes;
}

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

void *rt_bytes_new(int64_t len)
{
    return rt_bytes_alloc(len);
}

void *rt_bytes_from_str(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        return rt_bytes_new(0);

    size_t len = strlen(cstr);

    rt_bytes_impl *bytes = rt_bytes_alloc((int64_t)len);
    if (len > 0)
        memcpy(bytes->data, cstr, len);
    return bytes;
}

void *rt_bytes_from_hex(rt_string hex)
{
    const char *hex_str = rt_string_cstr(hex);
    if (!hex_str)
        return rt_bytes_new(0);

    size_t hex_len = strlen(hex_str);

    if (hex_len % 2 != 0)
        rt_trap("Bytes.FromHex: hex string length must be even");

    int64_t len = (int64_t)(hex_len / 2);
    rt_bytes_impl *bytes = rt_bytes_alloc(len);
    for (int64_t i = 0; i < len; i++)
    {
        int hi = hex_digit_value(hex_str[i * 2]);
        int lo = hex_digit_value(hex_str[i * 2 + 1]);

        if (hi < 0 || lo < 0)
            rt_trap("Bytes.FromHex: invalid hex character");

        bytes->data[i] = (uint8_t)((hi << 4) | lo);
    }

    return bytes;
}

int64_t rt_bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_bytes_impl *)obj)->len;
}

int64_t rt_bytes_get(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Bytes.Get: null bytes");

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (idx < 0 || idx >= bytes->len)
        rt_trap("Bytes.Get: index out of bounds");

    return bytes->data[idx];
}

void rt_bytes_set(void *obj, int64_t idx, int64_t val)
{
    if (!obj)
        rt_trap("Bytes.Set: null bytes");

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (idx < 0 || idx >= bytes->len)
        rt_trap("Bytes.Set: index out of bounds");

    bytes->data[idx] = (uint8_t)(val & 0xFF);
}

void *rt_bytes_slice(void *obj, int64_t start, int64_t end)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    // Clamp bounds
    if (start < 0)
        start = 0;
    if (end > bytes->len)
        end = bytes->len;
    if (start >= end)
        return rt_bytes_new(0);

    int64_t new_len = end - start;

    rt_bytes_impl *result = rt_bytes_alloc(new_len);
    if (new_len > 0)
        memcpy(result->data, bytes->data + start, (size_t)new_len);
    return result;
}

void rt_bytes_copy(void *dst, int64_t dst_idx, void *src, int64_t src_idx, int64_t count)
{
    if (!dst)
        rt_trap("Bytes.Copy: null destination");
    if (!src)
        rt_trap("Bytes.Copy: null source");

    rt_bytes_impl *dst_bytes = (rt_bytes_impl *)dst;
    rt_bytes_impl *src_bytes = (rt_bytes_impl *)src;

    if (count < 0)
        rt_trap("Bytes.Copy: count cannot be negative");

    if (count == 0)
        return;

    if (src_idx < 0 || src_idx + count > src_bytes->len)
        rt_trap("Bytes.Copy: source range out of bounds");

    if (dst_idx < 0 || dst_idx + count > dst_bytes->len)
        rt_trap("Bytes.Copy: destination range out of bounds");

    memmove(dst_bytes->data + dst_idx, src_bytes->data + src_idx, (size_t)count);
}

rt_string rt_bytes_to_str(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    return rt_string_from_bytes((const char *)bytes->data, (size_t)bytes->len);
}

rt_string rt_bytes_to_hex(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (bytes->len == 0)
        return rt_string_from_bytes("", 0);

    size_t result_len = (size_t)bytes->len * 2;
    char *result = (char *)malloc(result_len + 1);
    if (!result)
        rt_trap("Bytes: memory allocation failed");

    for (int64_t i = 0; i < bytes->len; i++)
    {
        result[i * 2] = hex_chars[(bytes->data[i] >> 4) & 0xF];
        result[i * 2 + 1] = hex_chars[bytes->data[i] & 0xF];
    }
    result[result_len] = '\0';

    rt_string str = rt_string_from_bytes(result, result_len);
    free(result);
    return str;
}

/// @brief Convert byte array to an RFC 4648 Base64 string.
/// @details Uses the standard Base64 alphabet (A–Z a–z 0–9 + /) with '=' padding and emits
///          no line breaks. Returns an empty string for null or empty Bytes.
rt_string rt_bytes_to_base64(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    if (bytes->len <= 0 || !bytes->data)
        return rt_string_from_bytes("", 0);

    size_t input_len = (size_t)bytes->len;
    size_t output_len = ((input_len + 2) / 3) * 4;

    char *out = (char *)malloc(output_len + 1);
    if (!out)
        rt_trap("Bytes: memory allocation failed");

    size_t i = 0;
    size_t o = 0;
    while (i + 3 <= input_len)
    {
        uint32_t triple =
            ((uint32_t)bytes->data[i] << 16) | ((uint32_t)bytes->data[i + 1] << 8) | bytes->data[i + 2];
        out[o++] = b64_chars[(triple >> 18) & 0x3F];
        out[o++] = b64_chars[(triple >> 12) & 0x3F];
        out[o++] = b64_chars[(triple >> 6) & 0x3F];
        out[o++] = b64_chars[triple & 0x3F];
        i += 3;
    }

    if (i < input_len)
    {
        uint32_t triple = (uint32_t)bytes->data[i] << 16;
        int two = 0;
        if (i + 1 < input_len)
        {
            triple |= (uint32_t)bytes->data[i + 1] << 8;
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

/// @brief Create a byte array from an RFC 4648 Base64 string.
/// @details Uses the standard Base64 alphabet (A–Z a–z 0–9 + /) with '=' padding. Traps on
///          invalid characters, invalid padding, or invalid length. Returns empty Bytes for
///          empty input.
void *rt_bytes_from_base64(rt_string b64)
{
    const char *b64_str = rt_string_cstr(b64);
    if (!b64_str)
        return rt_bytes_new(0);

    size_t b64_len = strlen(b64_str);
    if (b64_len == 0)
        return rt_bytes_new(0);

    if (b64_len % 4 != 0)
        rt_trap("Bytes.FromBase64: base64 length must be a multiple of 4");

    size_t padding = 0;
    if (b64_len >= 1 && b64_str[b64_len - 1] == '=')
    {
        padding = 1;
        if (b64_len >= 2 && b64_str[b64_len - 2] == '=')
            padding = 2;
    }

    for (size_t i = 0; i < b64_len - padding; ++i)
    {
        if (b64_str[i] == '=')
            rt_trap("Bytes.FromBase64: invalid padding");
    }

    size_t out_len = (b64_len / 4) * 3 - padding;
    if (out_len == 0)
        return rt_bytes_new(0);

    if (out_len > (size_t)INT64_MAX)
        rt_trap("Bytes.FromBase64: decoded data too large");

    rt_bytes_impl *bytes = rt_bytes_alloc((int64_t)out_len);

    size_t out_pos = 0;
    for (size_t i = 0; i < b64_len; i += 4)
    {
        char c0 = b64_str[i];
        char c1 = b64_str[i + 1];
        char c2 = b64_str[i + 2];
        char c3 = b64_str[i + 3];

        int v0 = b64_digit_value(c0);
        int v1 = b64_digit_value(c1);
        int v2 = b64_digit_value(c2);
        int v3 = b64_digit_value(c3);

        if (v0 < 0 || v1 < 0)
        {
            if (v0 == -2 || v1 == -2)
                rt_trap("Bytes.FromBase64: invalid padding");
            rt_trap("Bytes.FromBase64: invalid base64 character");
        }

        if (v2 == -1 || v3 == -1)
            rt_trap("Bytes.FromBase64: invalid base64 character");

        if (v2 == -2)
        {
            if (v3 != -2 || i + 4 != b64_len)
                rt_trap("Bytes.FromBase64: invalid padding");
            if ((v1 & 0x0F) != 0)
                rt_trap("Bytes.FromBase64: invalid padding");

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12);
            bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
            break;
        }

        if (v3 == -2)
        {
            if (i + 4 != b64_len)
                rt_trap("Bytes.FromBase64: invalid padding");
            if ((v2 & 0x03) != 0)
                rt_trap("Bytes.FromBase64: invalid padding");

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6);
            bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
            bytes->data[out_pos++] = (uint8_t)((triple >> 8) & 0xFF);
            break;
        }

        uint32_t triple =
            ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
        bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
        bytes->data[out_pos++] = (uint8_t)((triple >> 8) & 0xFF);
        bytes->data[out_pos++] = (uint8_t)(triple & 0xFF);
    }

    if (out_pos != out_len)
        rt_trap("Bytes.FromBase64: invalid padding");

    return bytes;
}

void rt_bytes_fill(void *obj, int64_t val)
{
    if (!obj)
        return;

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    if (bytes->len > 0 && bytes->data)
    {
        memset(bytes->data, (uint8_t)(val & 0xFF), (size_t)bytes->len);
    }
}

int64_t rt_bytes_find(void *obj, int64_t val)
{
    if (!obj)
        return -1;

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    uint8_t byte = (uint8_t)(val & 0xFF);

    for (int64_t i = 0; i < bytes->len; i++)
    {
        if (bytes->data[i] == byte)
            return i;
    }

    return -1;
}

void *rt_bytes_clone(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    return rt_bytes_slice(obj, 0, bytes->len);
}
