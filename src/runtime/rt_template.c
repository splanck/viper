//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_template.c
/// @brief Simple string templating with placeholder substitution.
///
/// Implements a lightweight template engine for string interpolation:
///
/// **Placeholder Syntax:**
/// ```
/// Template: "Hello {{name}}, you have {{count}} messages."
/// Values:   { "name": "Alice", "count": "5" }
/// Result:   "Hello Alice, you have 5 messages."
/// ```
///
/// **Key Features:**
/// - Map-based: {{key}} replaced with Map.Get(key)
/// - Seq-based: {{0}} {{1}} replaced with Seq.Get(index)
/// - Custom delimiters: RenderWith("$name$", map, "$", "$")
/// - Missing keys left as-is (explicit, easy to debug)
/// - Whitespace trimmed from keys: {{ name }} = {{name}}
///
/// **Thread Safety:** All functions are thread-safe (no global state).
///
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
                rt_sb_append_bytes(&sb, tmpl + pos, tmpl_len - pos);
            }
            break;
        }

        // Append text before placeholder
        if (start > pos)
        {
            rt_sb_append_bytes(&sb, tmpl + pos, start - pos);
        }

        // Find placeholder end
        int key_start = start + prefix_len;
        int end = find_at(tmpl, tmpl_len, suffix, suffix_len, key_start);

        if (end < 0)
        {
            // No closing delimiter, append rest as-is
            rt_sb_append_bytes(&sb, tmpl + start, tmpl_len - start);
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
            // Values are boxed strings - unbox to get the actual string
            if (rt_box_type(boxed_value) == RT_BOX_STR)
            {
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

bool rt_template_has(rt_string tmpl, rt_string key)
{
    if (!tmpl)
        return false;
    if (!key)
        return false;

    const char *tmpl_str = rt_string_cstr(tmpl);
    if (!tmpl_str)
        return false;

    const char *key_str = rt_string_cstr(key);
    if (!key_str)
        return false;

    int tmpl_len = (int)strlen(tmpl_str);
    int key_len = (int)strlen(key_str);

    if (key_len == 0)
        return false;

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
            return true;
        }

        pos = end + 2;
    }

    return false;
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
