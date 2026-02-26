//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_compiled_pattern.c
// Purpose: Implements pre-compiled regex patterns for the Viper.Text.Pattern
//          class. Compiles a regex string once into an internal representation
//          and supports IsMatch, Find, FindAll, Replace, and Split operations
//          with better performance for repeated use on the same pattern.
//
// Key invariants:
//   - Patterns are compiled exactly once at construction; compilation errors trap.
//   - The compiled form is immutable after creation; all match operations are
//     read-only and thread-safe on the same pattern object.
//   - Find returns the first match start and length; FindAll returns all matches.
//   - Replace substitutes all non-overlapping matches with the replacement string.
//   - Split divides the input at each match position.
//
// Ownership/Lifetime:
//   - Pattern objects are heap-allocated and managed by the runtime GC.
//   - The internal compiled state is freed in the finalizer.
//   - Returned match strings and sequences are fresh allocations owned by caller.
//
// Links: src/runtime/text/rt_compiled_pattern.h (public API),
//        src/runtime/text/rt_regex.h (underlying regex engine)
//
//===----------------------------------------------------------------------===//

#include "rt_compiled_pattern.h"
#include "rt_object.h"
#include "rt_regex_internal.h"

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

/// @brief Safely cast strlen() result to int, trapping on overflow.
static int safe_strlen_int(const char *s)
{
    size_t n = strlen(s);
    if (n > (size_t)INT_MAX)
        rt_trap("CompiledPattern: string too long for regex engine");
    return (int)n;
}

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
    re_compiled_pattern *pattern;
} compiled_pattern_obj;

#define MAX_CAPTURE_GROUPS 32

//=============================================================================
// Creation and Lifecycle
//=============================================================================

void *rt_compiled_pattern_new(rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    if (!pat_str)
        rt_trap("CompiledPattern: null pattern");

    compiled_pattern_obj *obj =
        (compiled_pattern_obj *)rt_obj_new_i64(0, (int64_t)sizeof(compiled_pattern_obj));

    obj->pattern = re_compile(pat_str);
    return obj;
}

rt_string rt_compiled_pattern_get_pattern(void *obj)
{
    if (!obj)
        return rt_const_cstr("");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *pat = re_get_pattern(cpo->pattern);
    return rt_string_from_bytes(pat, strlen(pat));
}

//=============================================================================
// Matching Operations
//=============================================================================

int8_t rt_compiled_pattern_is_match(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    int match_start, match_end;
    return re_find_match(
        cpo->pattern, txt_str, safe_strlen_int(txt_str), 0, &match_start, &match_end);
}

rt_string rt_compiled_pattern_find(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    int text_len = safe_strlen_int(txt_str);
    int match_start, match_end;

    if (re_find_match(cpo->pattern, txt_str, text_len, 0, &match_start, &match_end))
    {
        return rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    }
    return rt_const_cstr("");
}

rt_string rt_compiled_pattern_find_from(void *obj, rt_string text, int64_t start)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    int text_len = safe_strlen_int(txt_str);
    if (start < 0)
        start = 0;
    if (start > text_len)
        return rt_const_cstr("");

    int match_start, match_end;
    if (re_find_match(cpo->pattern, txt_str, text_len, (int)start, &match_start, &match_end))
    {
        return rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    }
    return rt_const_cstr("");
}

int64_t rt_compiled_pattern_find_pos(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    int match_start, match_end;
    if (re_find_match(cpo->pattern, txt_str, safe_strlen_int(txt_str), 0, &match_start, &match_end))
    {
        return (int64_t)match_start;
    }
    return -1;
}

void *rt_compiled_pattern_find_all(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    void *seq = rt_seq_new();
    int text_len = safe_strlen_int(txt_str);
    int pos = 0;

    while (pos <= text_len)
    {
        int match_start, match_end;
        if (!re_find_match(cpo->pattern, txt_str, text_len, pos, &match_start, &match_end))
            break;

        rt_string match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        rt_seq_push(seq, (void *)match);

        pos = match_end > match_start ? match_end : match_start + 1;
    }

    return seq;
}

//=============================================================================
// Capture Groups
//=============================================================================

void *rt_compiled_pattern_captures(void *obj, rt_string text)
{
    return rt_compiled_pattern_captures_from(obj, text, 0);
}

void *rt_compiled_pattern_captures_from(void *obj, rt_string text, int64_t start)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    void *seq = rt_seq_new();
    int text_len = safe_strlen_int(txt_str);

    if (start < 0)
        start = 0;
    if (start > text_len)
        return seq;

    int group_starts[MAX_CAPTURE_GROUPS];
    int group_ends[MAX_CAPTURE_GROUPS];
    int match_start, match_end, num_groups;

    if (re_find_match_with_groups(cpo->pattern,
                                  txt_str,
                                  text_len,
                                  (int)start,
                                  &match_start,
                                  &match_end,
                                  group_starts,
                                  group_ends,
                                  MAX_CAPTURE_GROUPS,
                                  &num_groups))
    {
        // Group 0 is the full match
        rt_string full_match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        rt_seq_push(seq, (void *)full_match);

        // Add captured groups
        for (int i = 0; i < num_groups; i++)
        {
            rt_string group =
                rt_string_from_bytes(txt_str + group_starts[i], group_ends[i] - group_starts[i]);
            rt_seq_push(seq, (void *)group);
        }
    }

    return seq;
}

//=============================================================================
// Replacement Operations
//=============================================================================

rt_string rt_compiled_pattern_replace(void *obj, rt_string text, rt_string replacement)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    const char *rep_str = rt_string_cstr(replacement);

    if (!txt_str)
        txt_str = "";
    if (!rep_str)
        rep_str = "";

    int text_len = safe_strlen_int(txt_str);
    int rep_len = safe_strlen_int(rep_str);

    // Build result
    size_t result_cap = text_len + 64;
    char *result = (char *)malloc(result_cap);
    if (!result)
        rt_trap("CompiledPattern: memory allocation failed");
    size_t result_len = 0;

    int pos = 0;
    while (pos <= text_len)
    {
        int match_start, match_end;
        if (!re_find_match(cpo->pattern, txt_str, text_len, pos, &match_start, &match_end))
        {
            // Copy rest of text
            size_t remaining = text_len - pos;
            if (result_len + remaining >= result_cap)
            {
                result_cap = result_len + remaining + 1;
                result = (char *)realloc(result, result_cap);
                if (!result)
                    rt_trap("CompiledPattern: memory allocation failed");
            }
            memcpy(result + result_len, txt_str + pos, remaining);
            result_len += remaining;
            break;
        }

        // Copy text before match
        size_t before_len = match_start - pos;
        if (result_len + before_len + rep_len >= result_cap)
        {
            result_cap = (result_len + before_len + rep_len) * 2 + 64;
            result = (char *)realloc(result, result_cap);
            if (!result)
                rt_trap("CompiledPattern: memory allocation failed");
        }
        memcpy(result + result_len, txt_str + pos, before_len);
        result_len += before_len;

        // Copy replacement
        memcpy(result + result_len, rep_str, rep_len);
        result_len += rep_len;

        // Move past match
        pos = match_end > match_start ? match_end : match_start + 1;
    }

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    return out;
}

rt_string rt_compiled_pattern_replace_first(void *obj, rt_string text, rt_string replacement)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    const char *rep_str = rt_string_cstr(replacement);

    if (!txt_str)
        txt_str = "";
    if (!rep_str)
        rep_str = "";

    int text_len = safe_strlen_int(txt_str);
    int rep_len = safe_strlen_int(rep_str);

    int match_start, match_end;
    if (!re_find_match(cpo->pattern, txt_str, text_len, 0, &match_start, &match_end))
    {
        return rt_string_from_bytes(txt_str, text_len);
    }

    // Build result: before + replacement + after
    size_t result_len = match_start + rep_len + (text_len - match_end);
    char *result = (char *)malloc(result_len + 1);
    if (!result)
        rt_trap("CompiledPattern: memory allocation failed");

    memcpy(result, txt_str, match_start);
    memcpy(result + match_start, rep_str, rep_len);
    memcpy(result + match_start + rep_len, txt_str + match_end, text_len - match_end);

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    return out;
}

//=============================================================================
// Split Operation
//=============================================================================

void *rt_compiled_pattern_split(void *obj, rt_string text)
{
    return rt_compiled_pattern_split_n(obj, text, 0);
}

void *rt_compiled_pattern_split_n(void *obj, rt_string text, int64_t limit)
{
    if (!obj)
        rt_trap("CompiledPattern: null pattern object");

    compiled_pattern_obj *cpo = (compiled_pattern_obj *)obj;
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    void *seq = rt_seq_new();
    int text_len = safe_strlen_int(txt_str);
    int pos = 0;
    int64_t split_count = 0;

    while (pos <= text_len)
    {
        // Check limit (0 means unlimited)
        if (limit > 0 && split_count >= limit - 1)
        {
            // Add remaining text as final element
            rt_string part = rt_string_from_bytes(txt_str + pos, text_len - pos);
            rt_seq_push(seq, (void *)part);
            return seq;
        }

        int match_start, match_end;
        if (!re_find_match(cpo->pattern, txt_str, text_len, pos, &match_start, &match_end))
        {
            // No more matches; add remaining text
            rt_string part = rt_string_from_bytes(txt_str + pos, text_len - pos);
            rt_seq_push(seq, (void *)part);
            break;
        }

        // Add text before match
        rt_string part = rt_string_from_bytes(txt_str + pos, match_start - pos);
        rt_seq_push(seq, (void *)part);
        split_count++;

        // Move past match
        pos = match_end > match_start ? match_end : match_start + 1;

        // If at end after match, add empty string
        if (pos > text_len)
        {
            rt_seq_push(seq, (void *)rt_const_cstr(""));
        }
    }

    // Handle empty result
    if (rt_seq_len(seq) == 0)
    {
        rt_seq_push(seq, (void *)rt_string_from_bytes(txt_str, text_len));
    }

    return seq;
}
