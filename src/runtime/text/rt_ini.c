//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_ini.c
// Purpose: Implements INI/config file parsing and formatting for the
//          Zanna.Text.Ini class. Supports [sections], key=value pairs,
//          and line comments starting with ';' or '#'.
//
// Key invariants:
//   - Section names are case-sensitive; keys within a section are case-sensitive.
//   - Keys before any section header belong to the implicit root section "".
//   - Comment lines (starting with ';' or '#') are discarded during parsing.
//   - Leading and trailing whitespace is stripped from keys and values.
//   - Duplicate keys in the same section keep the last value seen.
//   - Formatting writes sections in insertion order, keys in insertion order.
//
// Ownership/Lifetime:
//   - The parsed INI object is heap-allocated and managed by the runtime GC.
//   - Section and key strings stored internally are fresh copies owned by the object.
//
// Links: src/runtime/text/rt_ini.h (public API),
//        src/runtime/rt_map.h (used internally to store section/key/value data)
//
//===----------------------------------------------------------------------===//

#include "rt_ini.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Trim leading and trailing whitespace from a substring.
/// Returns pointer to first non-space, sets *out_len to trimmed length.
static const char *ini_trim(const char *start, size_t len, size_t *out_len) {
    const char *end = start + len;
    while (start < end && isspace((unsigned char)*start))
        ++start;
    while (end > start && isspace((unsigned char)*(end - 1)))
        --end;
    *out_len = (size_t)(end - start);
    return start;
}

static void release_local_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Compute the byte length of an `rt_string`, NULL-safe.
/// @details Runtime strings are length-prefixed; embedded NUL bytes are
///          preserved when parsing/formatting.
static size_t str_len(rt_string s) {
    if (!s)
        return 0;
    int64_t len = rt_str_len(s);
    return len > 0 ? (size_t)len : 0;
}

static rt_string ini_string_from_bytes_or_trap(const char *bytes, size_t len) {
    rt_string result = rt_string_from_bytes(bytes, len);
    if (!result)
        rt_trap("Ini: string allocation failed");
    return result;
}

static void ini_check_sb(rt_string_builder *sb, rt_sb_status_t status) {
    if (status == RT_SB_OK)
        return;
    rt_sb_free(sb);
    rt_trap("Ini: string builder allocation failed");
}

static void append_rt_string(rt_string_builder *sb, rt_string s) {
    if (!s)
        return;
    const char *c = rt_string_cstr(s);
    if (c)
        ini_check_sb(sb, rt_sb_append_bytes(sb, c, str_len(s)));
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

/// @brief Parse INI-format text into a nested Map: top-level keys are section names, values are
/// inner Maps of key→value strings. Keys outside any section go under the empty-string section.
void *rt_ini_parse(rt_string text) {
    void *root = rt_map_new();
    if (!text)
        return root;

    const char *src = rt_string_cstr(text);
    if (!src)
        return root;

    size_t src_len = (size_t)rt_str_len(text);
    const char *end = src + src_len;

    // Current section name (starts as "" for the default section). The default
    // section's Map is created lazily on the first out-of-section key so that
    // Parse(NULL) and Parse("") produce the same empty document shape.
    rt_string current_section = ini_string_from_bytes_or_trap("", 0);
    void *current_map = NULL;

    const char *line_start = src;
    while (line_start <= end) {
        // Find end of line
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n' && *line_end != '\r')
            ++line_end;

        size_t raw_len = (size_t)(line_end - line_start);

        // Trim the line
        size_t tlen;
        const char *tline = ini_trim(line_start, raw_len, &tlen);

        if (tlen == 0 || tline[0] == ';' || tline[0] == '#') {
            // Empty line or comment — skip
        } else if (tline[0] == '[' && tlen >= 2) {
            // Section header: find closing ]
            const char *close = memchr(tline + 1, ']', tlen - 1);
            if (close) {
                size_t name_len;
                const char *name = ini_trim(tline + 1, (size_t)(close - tline - 1), &name_len);
                rt_string_unref(current_section);
                current_section = ini_string_from_bytes_or_trap(name, name_len);

                // Create section map if it doesn't exist
                if (!rt_map_has(root, current_section)) {
                    void *new_section = rt_map_new();
                    rt_map_set(root, current_section, new_section);
                    current_map = new_section;
                    release_local_obj(new_section);
                } else {
                    current_map = rt_map_get(root, current_section);
                }
            }
        } else {
            // Key=value pair
            const char *eq = memchr(tline, '=', tlen);
            if (eq) {
                size_t key_len;
                const char *key = ini_trim(tline, (size_t)(eq - tline), &key_len);
                size_t val_len;
                const char *val = ini_trim(eq + 1, tlen - (size_t)(eq - tline) - 1, &val_len);

                rt_string k = ini_string_from_bytes_or_trap(key, key_len);
                rt_string v = ini_string_from_bytes_or_trap(val, val_len);
                if (!current_map) {
                    // First key outside any section: materialize the default section.
                    void *default_section = rt_map_new();
                    rt_map_set(root, current_section, default_section);
                    current_map = default_section;
                    release_local_obj(default_section);
                }
                rt_map_set(current_map, k, (void *)v);
                rt_string_unref(k);
                rt_string_unref(v);
            }
        }

        // Advance past end-of-line
        if (line_end < end) {
            line_start = line_end + 1;
            // Handle \r\n
            if (*line_end == '\r' && line_start < end && *line_start == '\n')
                ++line_start;
        } else {
            break;
        }
    }

    rt_string_unref(current_section);
    return root;
}

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------

/// @brief Serialize a parsed INI map back into INI-format text.
/// @details Layout matches what most INI tools accept:
///          1. Default-section keys (under empty-string section name)
///             are written first, no `[section]` header.
///          2. Each named section gets a leading blank line, then
///             `[name]`, then its `key = value` entries.
///          3. Values are written verbatim — no quoting, no escaping.
///          The blank line before each section is what gives the
///          rendered file its conventional INI look. Insertion order
///          within a section is preserved by the underlying Map.
rt_string rt_ini_format(void *ini_map) {
    if (!ini_map)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    // Get all section names
    void *sections = rt_map_keys(ini_map);
    int64_t sect_count = rt_seq_len(sections);

    // Write default section (empty key) first if it exists
    rt_string empty = ini_string_from_bytes_or_trap("", 0);
    void *default_sec = rt_map_get(ini_map, empty);
    if (default_sec && rt_map_len(default_sec) > 0) {
        void *keys = rt_map_keys(default_sec);
        int64_t kcount = rt_seq_len(keys);
        for (int64_t i = 0; i < kcount; ++i) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            rt_string val = (rt_string)rt_map_get(default_sec, key);
            append_rt_string(&sb, key);
            ini_check_sb(&sb, rt_sb_append_cstr(&sb, " = "));
            append_rt_string(&sb, val);
            ini_check_sb(&sb, rt_sb_append_bytes(&sb, "\n", 1));
        }
        release_local_obj(keys);
    }
    rt_string_unref(empty);

    // Write named sections
    for (int64_t s = 0; s < sect_count; ++s) {
        rt_string sect_name = (rt_string)rt_seq_get(sections, s);
        if (!sect_name || str_len(sect_name) == 0)
            continue; // Skip default section (already written)

        void *sect_map = rt_map_get(ini_map, sect_name);
        if (!sect_map)
            continue;

        ini_check_sb(&sb, rt_sb_append_bytes(&sb, "\n[", 2));
        append_rt_string(&sb, sect_name);
        ini_check_sb(&sb, rt_sb_append_bytes(&sb, "]\n", 2));

        void *keys = rt_map_keys(sect_map);
        int64_t kcount = rt_seq_len(keys);
        for (int64_t i = 0; i < kcount; ++i) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            rt_string val = (rt_string)rt_map_get(sect_map, key);
            append_rt_string(&sb, key);
            ini_check_sb(&sb, rt_sb_append_cstr(&sb, " = "));
            append_rt_string(&sb, val);
            ini_check_sb(&sb, rt_sb_append_bytes(&sb, "\n", 1));
        }
        release_local_obj(keys);
    }
    release_local_obj(sections);

    rt_string result = ini_string_from_bytes_or_trap(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// Get / Set / Remove
// ---------------------------------------------------------------------------

/// @brief Get a value from the ini.
rt_string rt_ini_get(void *ini_map, rt_string section, rt_string key) {
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

/// @brief Set a value in the ini.
void rt_ini_set(void *ini_map, rt_string section, rt_string key, rt_string value) {
    if (!ini_map || !section || !key)
        return;

    void *sect_map = rt_map_get(ini_map, section);
    if (!sect_map) {
        sect_map = rt_map_new();
        rt_map_set(ini_map, section, sect_map);
        release_local_obj(sect_map);
    }
    rt_map_set(sect_map, key, (void *)value);
}

/// @brief Check whether a named section exists in the parsed INI map.
int8_t rt_ini_has_section(void *ini_map, rt_string section) {
    if (!ini_map || !section)
        return 0;
    return rt_map_has(ini_map, section);
}

/// @brief Return a Seq of all section names (top-level keys) in the parsed INI tree.
void *rt_ini_sections(void *ini_map) {
    if (!ini_map)
        return rt_seq_new();
    return rt_map_keys(ini_map);
}

/// @brief Remove an entry from the ini.
int8_t rt_ini_remove(void *ini_map, rt_string section, rt_string key) {
    if (!ini_map || !section || !key)
        return 0;

    void *sect_map = rt_map_get(ini_map, section);
    if (!sect_map)
        return 0;

    return rt_map_remove(sect_map, key);
}
