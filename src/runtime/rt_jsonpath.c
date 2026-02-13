//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_jsonpath.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Helper: navigate one segment ---

static void *navigate_segment(void *current, const char *seg, int64_t len)
{
    if (!current || len == 0)
        return NULL;

    // Array index: numeric segment
    if (isdigit((unsigned char)seg[0]) || (seg[0] == '-' && len > 1))
    {
        char buf[32];
        int64_t copy_len = len < 31 ? len : 31;
        memcpy(buf, seg, (size_t)copy_len);
        buf[copy_len] = '\0';
        int64_t idx = strtoll(buf, NULL, 10);

        // Try as seq index
        int64_t slen = rt_seq_len(current);
        if (slen > 0)
        {
            if (idx < 0)
                idx += slen;
            if (idx >= 0 && idx < slen)
                return rt_seq_get(current, idx);
        }
        return NULL;
    }

    // Map key lookup
    rt_string key = rt_string_from_bytes(seg, len);
    void *val = rt_map_get(current, key);
    rt_string_unref(key);
    return val;
}

// --- Helper: resolve path ---

static void *resolve_path(void *root, const char *path)
{
    if (!root || !path || !*path)
        return root;

    void *current = root;
    const char *p = path;

    // Skip leading '$.' if present
    if (p[0] == '$' && p[1] == '.')
        p += 2;
    else if (p[0] == '$')
        p += 1;

    while (*p && current)
    {
        // Skip dots
        if (*p == '.')
        {
            p++;
            continue;
        }

        // Bracket notation: [index] or ["key"]
        if (*p == '[')
        {
            p++;
            if (*p == '"' || *p == '\'')
            {
                char quote = *p;
                p++;
                const char *start = p;
                while (*p && *p != quote)
                    p++;
                current = navigate_segment(current, start, (int64_t)(p - start));
                if (*p == quote)
                    p++;
                if (*p == ']')
                    p++;
            }
            else
            {
                const char *start = p;
                while (*p && *p != ']')
                    p++;
                current = navigate_segment(current, start, (int64_t)(p - start));
                if (*p == ']')
                    p++;
            }
            continue;
        }

        // Dot notation: key
        const char *start = p;
        while (*p && *p != '.' && *p != '[')
            p++;
        current = navigate_segment(current, start, (int64_t)(p - start));
    }

    return current;
}

// --- Helper: wildcard query ---

static void collect_wildcard(void *current, const char *remaining, void *results)
{
    if (!current)
        return;

    // Try as seq first. For maps, the first field (vptr) is NULL so
    // rt_seq_len interprets it as 0, letting us fall through to map handling.
    int64_t slen = rt_seq_len(current);
    if (slen > 0)
    {
        for (int64_t i = 0; i < slen; i++)
        {
            void *val = rt_seq_get(current, i);
            if (!remaining || !*remaining)
            {
                rt_seq_push(results, val);
            }
            else
            {
                void *sub = resolve_path(val, remaining);
                if (sub)
                    rt_seq_push(results, sub);
            }
        }
        return;
    }

    // Try as map - iterate all values
    void *keys = rt_map_keys(current);
    if (keys)
    {
        int64_t n = rt_seq_len(keys);
        for (int64_t i = 0; i < n; i++)
        {
            void *val = rt_map_get(current, (rt_string)rt_seq_get(keys, i));
            if (!remaining || !*remaining)
            {
                rt_seq_push(results, val);
            }
            else
            {
                void *sub = resolve_path(val, remaining);
                if (sub)
                    rt_seq_push(results, sub);
            }
        }
    }
}

// --- Public API ---

/// @brief Auto-detect if root is a raw JSON string and parse it.
/// @details Checks the RT_STRING_MAGIC header to identify raw strings,
///          and also handles boxed strings (from Zia str→ptr conversion).
static void *auto_parse_root(void *root)
{
    if (!root)
        return NULL;
    uint64_t magic = *(uint64_t *)root;
    if (magic == RT_STRING_MAGIC)
    {
        // root is a raw string — try to parse it as JSON
        void *parsed = rt_json_parse((rt_string)root);
        return parsed;
    }
    if (magic == RT_BOX_STR)
    {
        // root is a boxed string — unbox and parse
        rt_string s = rt_unbox_str(root);
        if (s)
        {
            void *parsed = rt_json_parse(s);
            return parsed;
        }
    }
    return root;
}

void *rt_jsonpath_get(void *root, rt_string path)
{
    if (!root || !path)
        return NULL;
    root = auto_parse_root(root);
    if (!root)
        return NULL;
    return resolve_path(root, rt_string_cstr(path));
}

void *rt_jsonpath_get_or(void *root, rt_string path, void *def)
{
    void *result = rt_jsonpath_get(root, path);
    return result ? result : def;
}

int8_t rt_jsonpath_has(void *root, rt_string path)
{
    return rt_jsonpath_get(root, path) != NULL ? 1 : 0;
}

void *rt_jsonpath_query(void *root, rt_string path)
{
    void *results = rt_seq_new();
    if (!root || !path)
        return results;

    root = auto_parse_root(root);
    if (!root)
        return results;

    const char *p = rt_string_cstr(path);

    // Skip leading '$.'
    if (p[0] == '$' && p[1] == '.')
        p += 2;
    else if (p[0] == '$')
        p += 1;

    // Find wildcard '*'
    const char *star = strchr(p, '*');
    if (!star)
    {
        // No wildcard - just get single result
        void *val = resolve_path(root, p);
        if (val)
            rt_seq_push(results, val);
        return results;
    }

    // Navigate to the parent of the wildcard
    void *parent = root;
    if (star > p)
    {
        // Extract path before wildcard
        int64_t pre_len = (int64_t)(star - p);
        if (pre_len > 0 && p[pre_len - 1] == '.')
            pre_len--;
        char *pre = (char *)malloc((size_t)(pre_len + 1));
        memcpy(pre, p, (size_t)pre_len);
        pre[pre_len] = '\0';
        parent = resolve_path(root, pre);
        free(pre);
    }

    // Get remaining path after wildcard
    const char *remaining = star + 1;
    if (*remaining == '.')
        remaining++;

    collect_wildcard(parent, remaining, results);
    return results;
}

rt_string rt_jsonpath_get_str(void *root, rt_string path)
{
    void *val = rt_jsonpath_get(root, path);
    if (!val)
        return rt_string_from_bytes("", 0);
    // If it's already a string, return it directly
    if (rt_string_is_handle(val))
        return (rt_string)val;
    // If it's a boxed value, try to extract string or convert
    int64_t tag = rt_box_type(val);
    if (tag == RT_BOX_STR)
        return rt_unbox_str(val);
    if (tag == RT_BOX_I64)
    {
        int64_t n = rt_unbox_i64(val);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)n);
        return rt_string_from_bytes(buf, strlen(buf));
    }
    if (tag == RT_BOX_F64)
    {
        double d = rt_unbox_f64(val);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", d);
        return rt_string_from_bytes(buf, strlen(buf));
    }
    if (tag == RT_BOX_I1)
    {
        int64_t b = rt_unbox_i1(val);
        return b ? rt_string_from_bytes("true", 4) : rt_string_from_bytes("false", 5);
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_jsonpath_get_int(void *root, rt_string path)
{
    void *val = rt_jsonpath_get(root, path);
    if (!val)
        return 0;
    // If it's a boxed number, unbox it
    int64_t tag = rt_box_type(val);
    if (tag == RT_BOX_I64)
        return rt_unbox_i64(val);
    if (tag == RT_BOX_F64)
        return (int64_t)rt_unbox_f64(val);
    if (tag == RT_BOX_I1)
        return rt_unbox_i1(val);
    if (tag == RT_BOX_STR)
    {
        rt_string s = rt_unbox_str(val);
        const char *cs = rt_string_cstr(s);
        return strtoll(cs, NULL, 10);
    }
    // If it's a raw string, parse it
    if (rt_string_is_handle(val))
    {
        const char *s = rt_string_cstr((rt_string)val);
        return strtoll(s, NULL, 10);
    }
    return 0;
}
