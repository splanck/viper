//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_ini.c
// Purpose: INI/config file parsing and formatting.
//          Supports [sections], key=value, comments (; and #).
//
//===----------------------------------------------------------------------===//

#include "rt_ini.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Trim leading and trailing whitespace from a substring.
/// Returns pointer to first non-space, sets *out_len to trimmed length.
static const char *ini_trim(const char *start, size_t len, size_t *out_len)
{
    const char *end = start + len;
    while (start < end && isspace((unsigned char)*start))
        ++start;
    while (end > start && isspace((unsigned char)*(end - 1)))
        --end;
    *out_len = (size_t)(end - start);
    return start;
}

static size_t str_len(rt_string s)
{
    if (!s)
        return 0;
    const char *c = rt_string_cstr(s);
    return c ? strlen(c) : 0;
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

void *rt_ini_parse(rt_string text)
{
    void *root = rt_map_new();
    if (!text)
        return root;

    const char *src = rt_string_cstr(text);
    if (!src)
        return root;

    size_t src_len = strlen(src);
    const char *end = src + src_len;

    // Current section name (starts as "" for default section)
    rt_string current_section = rt_string_from_bytes("", 0);
    void *current_map = rt_map_new();
    rt_map_set(root, current_section, current_map);

    const char *line_start = src;
    while (line_start <= end)
    {
        // Find end of line
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n' && *line_end != '\r')
            ++line_end;

        size_t raw_len = (size_t)(line_end - line_start);

        // Trim the line
        size_t tlen;
        const char *tline = ini_trim(line_start, raw_len, &tlen);

        if (tlen == 0 || tline[0] == ';' || tline[0] == '#')
        {
            // Empty line or comment — skip
        }
        else if (tline[0] == '[' && tlen >= 2)
        {
            // Section header: find closing ]
            const char *close = memchr(tline + 1, ']', tlen - 1);
            if (close)
            {
                size_t name_len;
                const char *name = ini_trim(tline + 1, (size_t)(close - tline - 1), &name_len);
                rt_string_unref(current_section);
                current_section = rt_string_from_bytes(name, name_len);

                // Create section map if it doesn't exist
                if (!rt_map_has(root, current_section))
                {
                    void *new_section = rt_map_new();
                    rt_map_set(root, current_section, new_section);
                    current_map = new_section;
                }
                else
                {
                    current_map = rt_map_get(root, current_section);
                }
            }
        }
        else
        {
            // Key=value pair
            const char *eq = memchr(tline, '=', tlen);
            if (eq)
            {
                size_t key_len;
                const char *key = ini_trim(tline, (size_t)(eq - tline), &key_len);
                size_t val_len;
                const char *val = ini_trim(eq + 1, tlen - (size_t)(eq - tline) - 1, &val_len);

                rt_string k = rt_string_from_bytes(key, key_len);
                rt_string v = rt_string_from_bytes(val, val_len);
                rt_map_set(current_map, k, (void *)v);
                rt_string_unref(k);
                // v is retained by the map
            }
        }

        // Advance past end-of-line
        if (line_end < end)
        {
            line_start = line_end + 1;
            // Handle \r\n
            if (*line_end == '\r' && line_start < end && *line_start == '\n')
                ++line_start;
        }
        else
        {
            break;
        }
    }

    rt_string_unref(current_section);
    return root;
}

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------

rt_string rt_ini_format(void *ini_map)
{
    if (!ini_map)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    // Get all section names
    void *sections = rt_map_keys(ini_map);
    int64_t sect_count = rt_seq_len(sections);

    // Write default section (empty key) first if it exists
    rt_string empty = rt_string_from_bytes("", 0);
    void *default_sec = rt_map_get(ini_map, empty);
    if (default_sec && rt_map_len(default_sec) > 0)
    {
        void *keys = rt_map_keys(default_sec);
        int64_t kcount = rt_seq_len(keys);
        for (int64_t i = 0; i < kcount; ++i)
        {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            rt_string val = (rt_string)rt_map_get(default_sec, key);
            const char *kc = rt_string_cstr(key);
            const char *vc = val ? rt_string_cstr(val) : "";
            if (kc)
                rt_sb_append_cstr(&sb, kc);
            rt_sb_append_cstr(&sb, " = ");
            if (vc)
                rt_sb_append_cstr(&sb, vc);
            rt_sb_append_bytes(&sb, "\n", 1);
        }
    }
    rt_string_unref(empty);

    // Write named sections
    for (int64_t s = 0; s < sect_count; ++s)
    {
        rt_string sect_name = (rt_string)rt_seq_get(sections, s);
        if (!sect_name || str_len(sect_name) == 0)
            continue; // Skip default section (already written)

        void *sect_map = rt_map_get(ini_map, sect_name);
        if (!sect_map)
            continue;

        rt_sb_append_bytes(&sb, "\n[", 2);
        const char *sc = rt_string_cstr(sect_name);
        if (sc)
            rt_sb_append_cstr(&sb, sc);
        rt_sb_append_bytes(&sb, "]\n", 2);

        void *keys = rt_map_keys(sect_map);
        int64_t kcount = rt_seq_len(keys);
        for (int64_t i = 0; i < kcount; ++i)
        {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            rt_string val = (rt_string)rt_map_get(sect_map, key);
            const char *kc = rt_string_cstr(key);
            const char *vc = val ? rt_string_cstr(val) : "";
            if (kc)
                rt_sb_append_cstr(&sb, kc);
            rt_sb_append_cstr(&sb, " = ");
            if (vc)
                rt_sb_append_cstr(&sb, vc);
            rt_sb_append_bytes(&sb, "\n", 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// Get / Set / Remove
// ---------------------------------------------------------------------------

rt_string rt_ini_get(void *ini_map, rt_string section, rt_string key)
{
    if (!ini_map || !section || !key)
        return rt_string_from_bytes("", 0);

    void *sect_map = rt_map_get(ini_map, section);
    if (!sect_map)
        return rt_string_from_bytes("", 0);

    rt_string val = (rt_string)rt_map_get(sect_map, key);
    if (!val)
        return rt_string_from_bytes("", 0);

    // Return a retained copy so caller can unref
    rt_obj_retain_maybe(val);
    return val;
}

void rt_ini_set(void *ini_map, rt_string section, rt_string key, rt_string value)
{
    if (!ini_map || !section || !key)
        return;

    void *sect_map = rt_map_get(ini_map, section);
    if (!sect_map)
    {
        sect_map = rt_map_new();
        rt_map_set(ini_map, section, sect_map);
    }
    rt_map_set(sect_map, key, (void *)value);
}

int8_t rt_ini_has_section(void *ini_map, rt_string section)
{
    if (!ini_map || !section)
        return 0;
    return rt_map_has(ini_map, section);
}

void *rt_ini_sections(void *ini_map)
{
    if (!ini_map)
        return rt_seq_new();
    return rt_map_keys(ini_map);
}

int8_t rt_ini_remove(void *ini_map, rt_string section, rt_string key)
{
    if (!ini_map || !section || !key)
        return 0;

    void *sect_map = rt_map_get(ini_map, section);
    if (!sect_map)
        return 0;

    return rt_map_remove(sect_map, key);
}
