//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_template.c
// Purpose: Implements a lightweight string template engine for the
//          Viper.Text.Template class. Replaces {{key}} placeholders using a
//          map (key→string) or seq (index→string) of substitution values.
//
// Key invariants:
//   - Default placeholder delimiters are "{{" and "}}".
//   - RenderWith allows custom open/close delimiters for any character pair.
//   - Keys are whitespace-trimmed before lookup: "{{ name }}" == "{{name}}".
//   - Missing keys are left as-is in the output (not replaced with empty).
//   - Seq-based rendering replaces "{{0}}", "{{1}}" with seq elements by index.
//   - All functions are thread-safe with no global mutable state.
//
// Ownership/Lifetime:
//   - The returned rendered string is a fresh allocation owned by the caller.
//   - Input template and map/seq are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_template.h (public API),
//        src/runtime/rt_map.h (map used for key→value substitutions),
//        src/runtime/rt_seq.h (seq used for index→value substitutions)
//
//===----------------------------------------------------------------------===//

#include "rt_template.h"

#include "rt_bag.h"
#include "rt_box.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Helper Functions
//=============================================================================

/// Skip whitespace and return new position
static int skip_whitespace(const char *s, int pos, int len)
{
    while (pos < len && isspace((unsigned char)s[pos]))
        pos++;
    return pos;
}

/// Reverse skip whitespace (find end of non-whitespace)
static int rskip_whitespace(const char *s, int start, int end)
{
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;
    return end;
}

/// Find substring starting at pos, return position or -1
static int find_at(const char *text, int text_len, const char *needle, int needle_len, int start)
{
    if (needle_len == 0 || start + needle_len > text_len)
        return -1;

    for (int i = start; i <= text_len - needle_len; i++)
    {
        if (memcmp(text + i, needle, needle_len) == 0)
            return i;
    }
    return -1;
}

/// Parse integer from string, return -1 if not a valid non-negative integer
static int64_t parse_index(const char *s, int len)
{
    if (len == 0)
        return -1;

    int64_t result = 0;
    for (int i = 0; i < len; i++)
    {
        if (!isdigit((unsigned char)s[i]))
            return -1;
        result = result * 10 + (s[i] - '0');
        if (result > INT64_MAX / 10)
            return -1; // Overflow protection
    }
    return result;
}

static void append_literal_unescaped(rt_string_builder *sb,
                                     const char *text,
                                     int len,
                                     const char *prefix,
                                     int prefix_len,
                                     const char *suffix,
                                     int suffix_len)
{
    int i = 0;
    while (i < len)
    {
        if (prefix_len > 0 && i + prefix_len * 2 <= len &&
            memcmp(text + i, prefix, prefix_len) == 0 &&
            memcmp(text + i + prefix_len, prefix, prefix_len) == 0)
        {
            rt_sb_append_bytes(sb, prefix, prefix_len);
            i += prefix_len * 2;
            continue;
        }
        if (suffix_len > 0 && i + suffix_len * 2 <= len &&
            memcmp(text + i, suffix, suffix_len) == 0 &&
            memcmp(text + i + suffix_len, suffix, suffix_len) == 0)
        {
            rt_sb_append_bytes(sb, suffix, suffix_len);
            i += suffix_len * 2;
            continue;
        }

        rt_sb_append_bytes(sb, text + i, 1);
        i++;
    }
}

//=============================================================================
// Core Template Rendering
//=============================================================================

/// Internal render with configurable delimiters and value lookup
static rt_string render_internal(const char *tmpl,
                                 int tmpl_len,
                                 void *values,
                                 bool use_seq,
                                 const char *prefix,
                                 int prefix_len,
                                 const char *suffix,
                                 int suffix_len)
{
    // Create string builder for result
    rt_string_builder sb;
    rt_sb_init(&sb);

    int pos = 0;
    while (pos < tmpl_len)
    {
        // Find next placeholder start
        int start = find_at(tmpl, tmpl_len, prefix, prefix_len, pos);
        if (start < 0)
        {
            // No more placeholders, append rest of template
            if (pos < tmpl_len)
            {
                append_literal_unescaped(
                    &sb, tmpl + pos, tmpl_len - pos, prefix, prefix_len, suffix, suffix_len);
            }
            break;
        }

        // Append text before placeholder
        if (start > pos)
        {
            append_literal_unescaped(
                &sb, tmpl + pos, start - pos, prefix, prefix_len, suffix, suffix_len);
        }

        if (start + prefix_len * 2 <= tmpl_len &&
            memcmp(tmpl + start + prefix_len, prefix, prefix_len) == 0)
        {
            // Escaped prefix - emit literal and skip
            rt_sb_append_bytes(&sb, prefix, prefix_len);
            pos = start + prefix_len * 2;
            continue;
        }

        // Find placeholder end
        int key_start = start + prefix_len;
        int end = find_at(tmpl, tmpl_len, suffix, suffix_len, key_start);

        if (end < 0)
        {
            // No closing delimiter, append rest as-is
            append_literal_unescaped(
                &sb, tmpl + start, tmpl_len - start, prefix, prefix_len, suffix, suffix_len);
            break;
        }

        // Extract and trim key
        int key_end = end;
        int trimmed_start = skip_whitespace(tmpl, key_start, key_end);
        int trimmed_end = rskip_whitespace(tmpl, trimmed_start, key_end);
        int key_len = trimmed_end - trimmed_start;

        // Handle empty key - leave as literal
        if (key_len == 0)
        {
            rt_sb_append_bytes(&sb, tmpl + start, end + suffix_len - start);
            pos = end + suffix_len;
            continue;
        }

        // Look up value
        void *boxed_value = NULL;
        bool found = false;

        if (use_seq)
        {
            // Parse index for Seq lookup
            int64_t idx = parse_index(tmpl + trimmed_start, key_len);
            if (idx >= 0 && idx < rt_seq_len(values))
            {
                boxed_value = rt_seq_get(values, idx);
                found = true;
            }
        }
        else
        {
            // Map lookup
            rt_string key = rt_string_from_bytes(tmpl + trimmed_start, key_len);
            if (rt_map_has(values, key))
            {
                boxed_value = rt_map_get(values, key);
                found = true;
            }
        }

        if (found && boxed_value)
        {
            // Handle both boxed strings and raw rt_string handles
            // Map may store either depending on how Set was called
            if (rt_string_is_handle(boxed_value))
            {
                // Raw rt_string pointer (not boxed)
                rt_string value = (rt_string)boxed_value;
                const char *val_str = rt_string_cstr(value);
                if (val_str)
                {
                    rt_sb_append_cstr(&sb, val_str);
                }
            }
            else if (rt_box_type(boxed_value) == RT_BOX_STR)
            {
                // Boxed string - unbox to get the actual string
                rt_string value = rt_unbox_str(boxed_value);
                if (value)
                {
                    const char *val_str = rt_string_cstr(value);
                    if (val_str)
                    {
                        rt_sb_append_cstr(&sb, val_str);
                    }
                    rt_string_unref(value); // Release the retained string from unbox
                }
            }
        }
        else
        {
            // Key not found, leave placeholder as-is
            rt_sb_append_bytes(&sb, tmpl + start, end + suffix_len - start);
        }

        pos = end + suffix_len;
    }

    // Build result string
    rt_string result;
    if (sb.len == 0)
    {
        result = rt_const_cstr("");
    }
    else
    {
        result = rt_string_from_bytes(sb.data, sb.len);
    }
    rt_sb_free(&sb);
    return result;
}

//=============================================================================
// Public API
//=============================================================================

rt_string rt_template_render(rt_string tmpl, void *values)
{
    if (!tmpl)
        rt_trap("Template.Render: template is null");
    if (!values)
        rt_trap("Template.Render: values map is null");

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        tmpl_str = "";

    int tmpl_len = (int)strlen(tmpl_str);

    return render_internal(tmpl_str, tmpl_len, values, false, "{{", 2, "}}", 2);
}

rt_string rt_template_render_seq(rt_string tmpl, void *values)
{
    if (!tmpl)
        rt_trap("Template.RenderSeq: template is null");
    if (!values)
        rt_trap("Template.RenderSeq: values seq is null");

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        tmpl_str = "";

    int tmpl_len = (int)strlen(tmpl_str);

    return render_internal(tmpl_str, tmpl_len, values, true, "{{", 2, "}}", 2);
}

rt_string rt_template_render_with(rt_string tmpl, void *values, rt_string prefix, rt_string suffix)
{
    if (!tmpl)
        rt_trap("Template.RenderWith: template is null");
    if (!values)
        rt_trap("Template.RenderWith: values map is null");
    if (!prefix)
        rt_trap("Template.RenderWith: prefix is null");
    if (!suffix)
        rt_trap("Template.RenderWith: suffix is null");

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        tmpl_str = "";

    const char *prefix_str = rt_string_cstr(prefix);
    if (!prefix_str)
        prefix_str = "";

    const char *suffix_str = rt_string_cstr(suffix);
    if (!suffix_str)
        suffix_str = "";

    int tmpl_len = (int)strlen(tmpl_str);
    int prefix_len = (int)strlen(prefix_str);
    int suffix_len = (int)strlen(suffix_str);

    if (prefix_len == 0)
        rt_trap("Template.RenderWith: prefix is empty");
    if (suffix_len == 0)
        rt_trap("Template.RenderWith: suffix is empty");

    return render_internal(
        tmpl_str, tmpl_len, values, false, prefix_str, prefix_len, suffix_str, suffix_len);
}

int8_t rt_template_has(rt_string tmpl, rt_string key)
{
    if (!tmpl)
        return 0;
    if (!key)
        return 0;

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        return 0;

    const char *key_str = rt_string_cstr(key);
    if (!key_str)
        return 0;

    int tmpl_len = (int)strlen(tmpl_str);
    int key_len = (int)strlen(key_str);

    if (key_len == 0)
        return 0;

    int pos = 0;
    while (pos < tmpl_len)
    {
        int start = find_at(tmpl_str, tmpl_len, "{{", 2, pos);
        if (start < 0)
            break;

        int key_start = start + 2;
        int end = find_at(tmpl_str, tmpl_len, "}}", 2, key_start);
        if (end < 0)
            break;

        // Extract and trim placeholder key
        int trimmed_start = skip_whitespace(tmpl_str, key_start, end);
        int trimmed_end = rskip_whitespace(tmpl_str, trimmed_start, end);
        int placeholder_key_len = trimmed_end - trimmed_start;

        // Compare with target key
        if (placeholder_key_len == key_len &&
            memcmp(tmpl_str + trimmed_start, key_str, key_len) == 0)
        {
            return 1;
        }

        pos = end + 2;
    }

    return 0;
}

void *rt_template_keys(rt_string tmpl)
{
    void *bag = rt_bag_new();

    if (!tmpl)
        return bag;

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        return bag;

    int tmpl_len = (int)strlen(tmpl_str);

    int pos = 0;
    while (pos < tmpl_len)
    {
        int start = find_at(tmpl_str, tmpl_len, "{{", 2, pos);
        if (start < 0)
            break;

        int key_start = start + 2;
        int end = find_at(tmpl_str, tmpl_len, "}}", 2, key_start);
        if (end < 0)
            break;

        // Extract and trim key
        int trimmed_start = skip_whitespace(tmpl_str, key_start, end);
        int trimmed_end = rskip_whitespace(tmpl_str, trimmed_start, end);
        int key_len = trimmed_end - trimmed_start;

        // Add non-empty keys to bag
        if (key_len > 0)
        {
            rt_string key = rt_string_from_bytes(tmpl_str + trimmed_start, key_len);
            rt_bag_put(bag, key);
        }

        pos = end + 2;
    }

    return bag;
}

rt_string rt_template_escape(rt_string text)
{
    if (!text)
        return rt_const_cstr("");

    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        return rt_const_cstr("");

    int txt_len = (int)strlen(txt_str);

    // Count {{ and }} occurrences
    int escape_count = 0;
    for (int i = 0; i < txt_len - 1; i++)
    {
        if ((txt_str[i] == '{' && txt_str[i + 1] == '{') ||
            (txt_str[i] == '}' && txt_str[i + 1] == '}'))
        {
            escape_count++;
            i++; // Skip second char of pair
        }
    }

    if (escape_count == 0)
        return text;

    // Allocate result (each {{ or }} becomes {{{{ or }}}})
    int result_len = txt_len + escape_count * 2;
    char *result = (char *)malloc(result_len + 1);
    if (!result)
        rt_trap("Template.Escape: out of memory");

    int j = 0;
    for (int i = 0; i < txt_len; i++)
    {
        if (i < txt_len - 1)
        {
            if (txt_str[i] == '{' && txt_str[i + 1] == '{')
            {
                // Escape {{ as {{{{
                result[j++] = '{';
                result[j++] = '{';
                result[j++] = '{';
                result[j++] = '{';
                i++; // Skip second {
                continue;
            }
            if (txt_str[i] == '}' && txt_str[i + 1] == '}')
            {
                // Escape }} as }}}}
                result[j++] = '}';
                result[j++] = '}';
                result[j++] = '}';
                result[j++] = '}';
                i++; // Skip second }
                continue;
            }
        }
        result[j++] = txt_str[i];
    }
    result[j] = '\0';

    rt_string rs = rt_string_from_bytes(result, j);
    free(result);
    return rs;
}
