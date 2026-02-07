//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_glob.c
// Purpose: File glob pattern matching implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_glob.h"

#include "rt_dir.h"
#include "rt_file_ext.h"
#include "rt_path.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Pattern Matching
//=============================================================================

/// @brief Match a pattern against a string (internal helper).
/// @param pattern Pattern string (null-terminated).
/// @param text Text string (null-terminated).
/// @param allow_slash Whether * matches /
/// @return 1 if match, 0 otherwise.
static int glob_match_impl(const char *pattern, const char *text, int allow_slash)
{
    while (*pattern)
    {
        if (*pattern == '*')
        {
            // Check for **
            if (pattern[1] == '*')
            {
                pattern += 2;
                // Skip any following /
                while (*pattern == '/')
                    pattern++;

                // ** matches everything including /
                if (!*pattern)
                    return 1; // ** at end matches everything

                // Try matching rest of pattern at each position
                for (const char *p = text; *p; p++)
                {
                    if (glob_match_impl(pattern, p, 1))
                        return 1;
                }
                return glob_match_impl(pattern, text + strlen(text), 1);
            }

            // Single * - doesn't match /
            pattern++;

            // * at end matches everything (except / if not allow_slash)
            if (!*pattern)
            {
                while (*text)
                {
                    if (*text == '/' && !allow_slash)
                        return 0;
                    text++;
                }
                return 1;
            }

            // Try matching rest of pattern at each position
            while (*text)
            {
                if (*text == '/' && !allow_slash)
                {
                    // Can't skip past /
                    return glob_match_impl(pattern, text, allow_slash);
                }
                if (glob_match_impl(pattern, text, allow_slash))
                    return 1;
                text++;
            }
            return glob_match_impl(pattern, text, allow_slash);
        }
        else if (*pattern == '?')
        {
            // ? matches any single char except /
            if (!*text || (*text == '/' && !allow_slash))
                return 0;
            pattern++;
            text++;
        }
        else
        {
            // Literal character match
            if (*pattern != *text)
                return 0;
            pattern++;
            text++;
        }
    }

    return *text == '\0';
}

int8_t rt_glob_match(rt_string pattern, rt_string path)
{
    const char *pat = rt_string_cstr(pattern);
    const char *txt = rt_string_cstr(path);
    return glob_match_impl(pat, txt, 0) ? 1 : 0;
}

//=============================================================================
// File Finding
//=============================================================================

void *rt_glob_files(rt_string dir, rt_string pattern)
{
    void *result = rt_seq_new();
    void *files = rt_dir_files_seq(dir);

    int64_t count = rt_seq_len(files);
    for (int64_t i = 0; i < count; i++)
    {
        rt_string name = (rt_string)rt_seq_get(files, i);
        const char *name_cstr = rt_string_cstr(name);
        const char *pat_cstr = rt_string_cstr(pattern);

        if (glob_match_impl(pat_cstr, name_cstr, 0))
        {
            // Build full path
            rt_string full_path = rt_path_join(dir, name);
            rt_seq_push(result, full_path);
        }
    }

    return result;
}

/// @brief Recursive helper for glob_files_recursive.
static void glob_recursive_helper(rt_string base_dir,
                                  rt_string rel_path,
                                  const char *pattern,
                                  void *result)
{
    // List all entries in current directory
    rt_string current_dir;
    if (rt_str_len(rel_path) == 0)
    {
        current_dir = rt_string_ref(base_dir);
    }
    else
    {
        current_dir = rt_path_join(base_dir, rel_path);
    }

    void *entries = rt_dir_list_seq(current_dir);
    int64_t count = rt_seq_len(entries);

    for (int64_t i = 0; i < count; i++)
    {
        rt_string name = (rt_string)rt_seq_get(entries, i);
        rt_string full_path = rt_path_join(current_dir, name);

        // Build relative path for matching
        rt_string entry_rel;
        if (rt_str_len(rel_path) == 0)
        {
            entry_rel = rt_string_ref(name);
        }
        else
        {
            rt_string slash = rt_const_cstr("/");
            rt_string temp = rt_str_concat(rt_string_ref(rel_path), slash);
            entry_rel = rt_str_concat(temp, rt_string_ref(name));
        }

        // Check if this entry matches the pattern
        if (glob_match_impl(pattern, rt_string_cstr(entry_rel), 0))
        {
            // Only add files, not directories
            if (rt_io_file_exists(full_path))
            {
                rt_seq_push(result, rt_string_ref(full_path));
            }
        }

        // If directory, recurse into it
        if (rt_dir_exists(full_path))
        {
            glob_recursive_helper(base_dir, entry_rel, pattern, result);
        }

        rt_string_unref(entry_rel);
        rt_string_unref(full_path);
    }

    if (current_dir != base_dir)
    {
        rt_string_unref(current_dir);
    }
}

void *rt_glob_files_recursive(rt_string base, rt_string pattern)
{
    void *result = rt_seq_new();
    rt_string empty = rt_str_empty();

    glob_recursive_helper(base, empty, rt_string_cstr(pattern), result);

    return result;
}

void *rt_glob_entries(rt_string dir, rt_string pattern)
{
    void *result = rt_seq_new();
    void *entries = rt_dir_list_seq(dir);

    int64_t count = rt_seq_len(entries);
    for (int64_t i = 0; i < count; i++)
    {
        rt_string name = (rt_string)rt_seq_get(entries, i);
        const char *name_cstr = rt_string_cstr(name);
        const char *pat_cstr = rt_string_cstr(pattern);

        if (glob_match_impl(pat_cstr, name_cstr, 0))
        {
            // Build full path
            rt_string full_path = rt_path_join(dir, name);
            rt_seq_push(result, full_path);
        }
    }

    return result;
}
