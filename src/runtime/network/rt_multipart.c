//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_multipart.c
// Purpose: Multipart form-data builder/parser for HTTP file uploads.
// Key invariants:
//   - RFC 2046 compliant boundary generation and formatting.
//   - Builder is append-only; parts are concatenated on Build().
// Ownership/Lifetime:
//   - Multipart objects are GC-managed.
// Links: rt_multipart.h (API)
//
//===----------------------------------------------------------------------===//

#include "rt_multipart.h"

#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structures
//=============================================================================

#define MAX_PARTS 128

typedef struct
{
    char *name;
    char *filename; // NULL for text fields
    uint8_t *data;
    size_t data_len;
    int is_file;
} multipart_part_t;

typedef struct
{
    char boundary[64];
    multipart_part_t parts[MAX_PARTS];
    int part_count;
} rt_multipart_impl;

//=============================================================================
// Internal Helpers
//=============================================================================

static void generate_boundary(char *buf, size_t buf_len)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t random[32];
    rt_crypto_random_bytes(random, sizeof(random));

    size_t len = buf_len - 1;
    if (len > 40)
        len = 40;
    for (size_t i = 0; i < len; i++)
        buf[i] = chars[random[i % sizeof(random)] % (sizeof(chars) - 1)];
    buf[len] = '\0';
}

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len_impl(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Finalizer
//=============================================================================

static void rt_multipart_finalize(void *obj)
{
    if (!obj)
        return;
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;
    for (int i = 0; i < mp->part_count; i++)
    {
        free(mp->parts[i].name);
        free(mp->parts[i].filename);
        free(mp->parts[i].data);
    }
}

//=============================================================================
// Public API — Builder
//=============================================================================

void *rt_multipart_new(void)
{
    rt_multipart_impl *mp =
        (rt_multipart_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_multipart_impl));
    if (!mp)
        rt_trap("Multipart: memory allocation failed");
    memset(mp, 0, sizeof(*mp));
    generate_boundary(mp->boundary, sizeof(mp->boundary));
    rt_obj_set_finalizer(mp, rt_multipart_finalize);
    return mp;
}

void *rt_multipart_add_field(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        rt_trap("Multipart: NULL object");
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;
    if (mp->part_count >= MAX_PARTS)
        rt_trap("Multipart: too many parts (max 128)");

    const char *n = rt_string_cstr(name);
    const char *v = rt_string_cstr(value);
    size_t v_len = v ? strlen(v) : 0;

    multipart_part_t *part = &mp->parts[mp->part_count++];
    part->name = n ? strdup(n) : strdup("");
    part->filename = NULL;
    part->data = (uint8_t *)malloc(v_len);
    if (part->data && v_len > 0)
        memcpy(part->data, v, v_len);
    part->data_len = v_len;
    part->is_file = 0;

    return obj;
}

void *rt_multipart_add_file(void *obj, rt_string name, rt_string filename, void *data)
{
    if (!obj)
        rt_trap("Multipart: NULL object");
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;
    if (mp->part_count >= MAX_PARTS)
        rt_trap("Multipart: too many parts (max 128)");

    const char *n = rt_string_cstr(name);
    const char *fn = rt_string_cstr(filename);
    int64_t len = data ? bytes_len_impl(data) : 0;
    uint8_t *ptr = data ? bytes_data(data) : NULL;

    multipart_part_t *part = &mp->parts[mp->part_count++];
    part->name = n ? strdup(n) : strdup("");
    part->filename = fn ? strdup(fn) : strdup("file");
    part->data = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
    if (part->data && len > 0)
        memcpy(part->data, ptr, (size_t)len);
    part->data_len = (size_t)(len > 0 ? len : 0);
    part->is_file = 1;

    return obj;
}

rt_string rt_multipart_content_type(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;

    char buf[128];
    snprintf(buf, sizeof(buf), "multipart/form-data; boundary=%s", mp->boundary);
    return rt_string_from_bytes(buf, strlen(buf));
}

void *rt_multipart_build(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;

    // Calculate total size
    size_t total = 0;
    size_t blen = strlen(mp->boundary);
    for (int i = 0; i < mp->part_count; i++)
    {
        total += 2 + blen + 2; // --boundary\r\n
        total += 256;          // headers (generous estimate)
        total += mp->parts[i].data_len;
        total += 2; // \r\n
    }
    total += 2 + blen + 4; // --boundary--\r\n

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf)
        return rt_bytes_new(0);

    size_t pos = 0;
    for (int i = 0; i < mp->part_count; i++)
    {
        multipart_part_t *part = &mp->parts[i];

        // Boundary
        pos += (size_t)snprintf((char *)buf + pos, total - pos, "--%s\r\n", mp->boundary);

        // Headers
        if (part->is_file)
        {
            pos += (size_t)snprintf((char *)buf + pos, total - pos,
                                    "Content-Disposition: form-data; name=\"%s\"; "
                                    "filename=\"%s\"\r\n"
                                    "Content-Type: application/octet-stream\r\n\r\n",
                                    part->name, part->filename);
        }
        else
        {
            pos += (size_t)snprintf((char *)buf + pos, total - pos,
                                    "Content-Disposition: form-data; name=\"%s\"\r\n\r\n",
                                    part->name);
        }

        // Data
        if (part->data_len > 0 && pos + part->data_len <= total)
        {
            memcpy(buf + pos, part->data, part->data_len);
            pos += part->data_len;
        }
        pos += (size_t)snprintf((char *)buf + pos, total - pos, "\r\n");
    }

    // Final boundary
    pos += (size_t)snprintf((char *)buf + pos, total - pos, "--%s--\r\n", mp->boundary);

    void *result = rt_bytes_new((int64_t)pos);
    memcpy(bytes_data(result), buf, pos);
    free(buf);
    return result;
}

int64_t rt_multipart_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_multipart_impl *)obj)->part_count;
}

//=============================================================================
// Public API — Parser
//=============================================================================

void *rt_multipart_parse(rt_string content_type, void *body)
{
    if (!body)
        return rt_multipart_new();

    const char *ct = rt_string_cstr(content_type);
    if (!ct)
        return rt_multipart_new();

    // Extract boundary from content-type
    const char *bnd = strstr(ct, "boundary=");
    if (!bnd)
        return rt_multipart_new();
    bnd += 9;

    // Strip quotes if present
    char boundary[128] = {0};
    if (*bnd == '"')
    {
        bnd++;
        const char *end = strchr(bnd, '"');
        size_t len = end ? (size_t)(end - bnd) : strlen(bnd);
        if (len >= sizeof(boundary))
            len = sizeof(boundary) - 1;
        memcpy(boundary, bnd, len);
    }
    else
    {
        size_t len = 0;
        while (bnd[len] && bnd[len] != ';' && bnd[len] != ' ' && bnd[len] != '\r' &&
               bnd[len] != '\n')
            len++;
        if (len >= sizeof(boundary))
            len = sizeof(boundary) - 1;
        memcpy(boundary, bnd, len);
    }

    rt_multipart_impl *mp =
        (rt_multipart_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_multipart_impl));
    if (!mp)
        rt_trap("Multipart: memory allocation failed");
    memset(mp, 0, sizeof(*mp));
    memcpy(mp->boundary, boundary, sizeof(mp->boundary) - 1);
    rt_obj_set_finalizer(mp, rt_multipart_finalize);

    // Parse body
    int64_t body_len = bytes_len_impl(body);
    const uint8_t *data = bytes_data(body);
    if (!data || body_len <= 0)
        return mp;

    char delim[140];
    int dlen = snprintf(delim, sizeof(delim), "--%s", boundary);
    char end_delim[142];
    int elen = snprintf(end_delim, sizeof(end_delim), "--%s--", boundary);

    // Find each part between boundaries
    const char *s = (const char *)data;
    const char *s_end = s + body_len;

    // Skip to first boundary
    const char *p = strstr(s, delim);
    if (!p)
        return mp;

    while (p && p < s_end && mp->part_count < MAX_PARTS)
    {
        p += dlen;
        if (*p == '-' && *(p + 1) == '-')
            break; // End boundary
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;

        // Find end of headers (double CRLF)
        const char *headers_end = strstr(p, "\r\n\r\n");
        if (!headers_end)
            break;

        // Parse name from Content-Disposition
        const char *name_start = strstr(p, "name=\"");
        char part_name[256] = {0};
        char part_filename[256] = {0};
        int is_file = 0;

        if (name_start && name_start < headers_end)
        {
            name_start += 6;
            const char *name_end = strchr(name_start, '"');
            if (name_end)
            {
                size_t nlen = (size_t)(name_end - name_start);
                if (nlen >= sizeof(part_name))
                    nlen = sizeof(part_name) - 1;
                memcpy(part_name, name_start, nlen);
            }
        }

        const char *fn_start = strstr(p, "filename=\"");
        if (fn_start && fn_start < headers_end)
        {
            fn_start += 10;
            const char *fn_end = strchr(fn_start, '"');
            if (fn_end)
            {
                size_t fnlen = (size_t)(fn_end - fn_start);
                if (fnlen >= sizeof(part_filename))
                    fnlen = sizeof(part_filename) - 1;
                memcpy(part_filename, fn_start, fnlen);
                is_file = 1;
            }
        }

        // Data starts after headers
        const char *data_start = headers_end + 4;

        // Find next boundary
        const char *next = strstr(data_start, delim);
        if (!next)
            next = s_end;

        // Data ends 2 bytes before next boundary (strip trailing \r\n)
        size_t data_size = (size_t)(next - data_start);
        if (data_size >= 2 && data_start[data_size - 2] == '\r' &&
            data_start[data_size - 1] == '\n')
            data_size -= 2;

        multipart_part_t *part = &mp->parts[mp->part_count++];
        part->name = strdup(part_name);
        part->filename = is_file ? strdup(part_filename) : NULL;
        part->data = (uint8_t *)malloc(data_size > 0 ? data_size : 1);
        if (part->data && data_size > 0)
            memcpy(part->data, data_start, data_size);
        part->data_len = data_size;
        part->is_file = is_file;

        p = next;
    }

    (void)elen;
    (void)end_delim;
    return mp;
}

rt_string rt_multipart_get_field(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;
    const char *n = rt_string_cstr(name);
    if (!n)
        return rt_string_from_bytes("", 0);

    for (int i = 0; i < mp->part_count; i++)
    {
        if (!mp->parts[i].is_file && mp->parts[i].name && strcmp(mp->parts[i].name, n) == 0)
        {
            return rt_string_from_bytes((const char *)mp->parts[i].data, mp->parts[i].data_len);
        }
    }
    return rt_string_from_bytes("", 0);
}

void *rt_multipart_get_file(void *obj, rt_string name)
{
    if (!obj)
        return rt_bytes_new(0);
    rt_multipart_impl *mp = (rt_multipart_impl *)obj;
    const char *n = rt_string_cstr(name);
    if (!n)
        return rt_bytes_new(0);

    for (int i = 0; i < mp->part_count; i++)
    {
        if (mp->parts[i].is_file && mp->parts[i].name && strcmp(mp->parts[i].name, n) == 0)
        {
            void *result = rt_bytes_new((int64_t)mp->parts[i].data_len);
            if (mp->parts[i].data_len > 0)
                memcpy(bytes_data(result), mp->parts[i].data, mp->parts[i].data_len);
            return result;
        }
    }
    return rt_bytes_new(0);
}
