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

/// Hex character lookup table for encoding.
static const char hex_chars[] = "0123456789abcdef";

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

void *rt_bytes_new(int64_t len)
{
    if (len < 0)
        len = 0;

    rt_bytes_impl *bytes = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bytes_impl));
    if (!bytes)
        rt_trap("Bytes: memory allocation failed");

    bytes->len = len;
    if (len > 0)
    {
        bytes->data = (uint8_t *)calloc((size_t)len, 1);
        if (!bytes->data)
            rt_trap("Bytes: memory allocation failed");
    }
    else
    {
        bytes->data = NULL;
    }

    return bytes;
}

void *rt_bytes_from_str(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        return rt_bytes_new(0);

    size_t len = strlen(cstr);

    rt_bytes_impl *bytes = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bytes_impl));
    if (!bytes)
        rt_trap("Bytes: memory allocation failed");

    bytes->len = (int64_t)len;
    if (len > 0)
    {
        bytes->data = (uint8_t *)malloc(len);
        if (!bytes->data)
            rt_trap("Bytes: memory allocation failed");
        memcpy(bytes->data, cstr, len);
    }
    else
    {
        bytes->data = NULL;
    }

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

    rt_bytes_impl *bytes = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bytes_impl));
    if (!bytes)
        rt_trap("Bytes: memory allocation failed");

    bytes->len = len;
    if (len > 0)
    {
        bytes->data = (uint8_t *)malloc((size_t)len);
        if (!bytes->data)
            rt_trap("Bytes: memory allocation failed");

        for (int64_t i = 0; i < len; i++)
        {
            int hi = hex_digit_value(hex_str[i * 2]);
            int lo = hex_digit_value(hex_str[i * 2 + 1]);

            if (hi < 0 || lo < 0)
                rt_trap("Bytes.FromHex: invalid hex character");

            bytes->data[i] = (uint8_t)((hi << 4) | lo);
        }
    }
    else
    {
        bytes->data = NULL;
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

    rt_bytes_impl *result = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bytes_impl));
    if (!result)
        rt_trap("Bytes: memory allocation failed");

    result->len = new_len;
    result->data = (uint8_t *)malloc((size_t)new_len);
    if (!result->data)
        rt_trap("Bytes: memory allocation failed");

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
