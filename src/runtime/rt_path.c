//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_path.c
// Purpose: Cross-platform file path manipulation utilities for Viper.IO.Path.
// Key invariants: Path operations handle both Unix and Windows path separators,
//                 normalize operations remove redundant separators and resolve
//                 . and .. components, and absolute path detection considers
//                 platform-specific conventions (drive letters, UNC paths).
// Ownership/Lifetime: All functions return newly allocated runtime strings that
//                     the caller must release.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#include "rt_path.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

/// @brief Check if a character is a path separator.
/// @param c Character to check.
/// @return Non-zero if c is a path separator, zero otherwise.
static inline int is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

/// @brief Get the length of a runtime string safely.
/// @param s Runtime string handle; may be null.
/// @return Length in bytes, or 0 if s is null or has null data.
static inline size_t rt_string_safe_len(rt_string s)
{
    if (!s || !s->data)
        return 0;
    return s->heap ? rt_heap_len(s->data) : s->literal_len;
}

/// @brief Get the data pointer of a runtime string safely.
/// @param s Runtime string handle; may be null.
/// @return Pointer to string data, or "" if null.
static inline const char *rt_string_safe_data(rt_string s)
{
    if (!s || !s->data)
        return "";
    return s->data;
}

/// @brief Join two path components with the platform separator.
/// @param a First path component.
/// @param b Second path component.
/// @return Newly allocated joined path.
rt_string rt_path_join(rt_string a, rt_string b)
{
    const char *a_data = rt_string_safe_data(a);
    size_t a_len = rt_string_safe_len(a);
    const char *b_data = rt_string_safe_data(b);
    size_t b_len = rt_string_safe_len(b);

    // If a is empty, return b
    if (a_len == 0)
        return rt_string_from_bytes(b_data, b_len);

    // If b is empty, return a
    if (b_len == 0)
        return rt_string_from_bytes(a_data, a_len);

    // If b is absolute, return b
    // Check for Unix absolute path
    if (is_path_sep(b_data[0]))
        return rt_string_from_bytes(b_data, b_len);

#ifdef _WIN32
    // Check for Windows drive letter (C:) or UNC path
    if ((b_len >= 2 && isalpha((unsigned char)b_data[0]) && b_data[1] == ':') ||
        (b_len >= 2 && is_path_sep(b_data[0]) && is_path_sep(b_data[1])))
    {
        return rt_string_from_bytes(b_data, b_len);
    }
#endif

    // Check if a ends with separator
    int a_has_sep = (a_len > 0 && is_path_sep(a_data[a_len - 1]));
    // Check if b starts with separator
    int b_has_sep = (b_len > 0 && is_path_sep(b_data[0]));

    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_append_bytes(&sb, a_data, a_len);

    if (!a_has_sep && !b_has_sep)
        rt_sb_append_bytes(&sb, PATH_SEP_STR, 1);
    else if (a_has_sep && b_has_sep)
        b_data++, b_len--; // Skip redundant separator in b

    rt_sb_append_bytes(&sb, b_data, b_len);

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

/// @brief Get the directory portion of a path.
/// @param path Path to extract directory from.
/// @return Newly allocated directory path.
rt_string rt_path_dir(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_str_empty();

    // Find the last path separator
    size_t i = len;
    while (i > 0 && !is_path_sep(data[i - 1]))
        i--;

    if (i == 0)
    {
        // No separator found - return "." for relative path
        return rt_string_from_bytes(".", 1);
    }

#ifdef _WIN32
    // Handle Windows drive root (e.g., "C:\")
    if (i == 3 && isalpha((unsigned char)data[0]) && data[1] == ':' && is_path_sep(data[2]))
        return rt_string_from_bytes(data, 3);

    // Handle UNC paths (e.g., "\\server\share\")
    if (i >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
    {
        // Find the end of server\share part
        size_t slashes = 0;
        size_t j = 2;
        while (j < len && slashes < 2)
        {
            if (is_path_sep(data[j]))
                slashes++;
            j++;
        }
        if (i <= j)
            return rt_string_from_bytes(data, j);
    }
#endif

    // Unix root
    if (i == 1 && is_path_sep(data[0]))
        return rt_string_from_bytes("/", 1);

    // Return directory without trailing separator
    return rt_string_from_bytes(data, i - 1);
}

/// @brief Get the filename portion of a path.
/// @param path Path to extract filename from.
/// @return Newly allocated filename string.
rt_string rt_path_name(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_str_empty();

    // Strip trailing separators
    while (len > 0 && is_path_sep(data[len - 1]))
        len--;

    if (len == 0)
        return rt_str_empty();

    // Find the last path separator
    size_t i = len;
    while (i > 0 && !is_path_sep(data[i - 1]))
        i--;

    return rt_string_from_bytes(data + i, len - i);
}

/// @brief Get the filename without extension.
/// @param path Path to extract stem from.
/// @return Newly allocated stem string.
rt_string rt_path_stem(rt_string path)
{
    // First get the filename
    rt_string name = rt_path_name(path);
    const char *data = rt_string_safe_data(name);
    size_t len = rt_string_safe_len(name);

    if (len == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    // Find the last dot
    size_t dot_pos = len;
    for (size_t i = len; i > 0; i--)
    {
        if (data[i - 1] == '.')
        {
            dot_pos = i - 1;
            break;
        }
    }

    // Handle special cases: ".file" and "file."
    // ".file" -> stem is ".file" (hidden file)
    // "file." -> stem is "file"
    if (dot_pos == 0)
    {
        // Starts with dot - no extension
        return name;
    }

    rt_string result = rt_string_from_bytes(data, dot_pos);
    rt_string_unref(name);
    return result;
}

/// @brief Get the file extension including the dot.
/// @param path Path to extract extension from.
/// @return Newly allocated extension string (e.g., ".txt").
rt_string rt_path_ext(rt_string path)
{
    // First get the filename
    rt_string name = rt_path_name(path);
    const char *data = rt_string_safe_data(name);
    size_t len = rt_string_safe_len(name);

    if (len == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    // Find the last dot
    size_t dot_pos = len;
    for (size_t i = len; i > 0; i--)
    {
        if (data[i - 1] == '.')
        {
            dot_pos = i - 1;
            break;
        }
    }

    // No dot found, or only at start (hidden file)
    if (dot_pos == len || dot_pos == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    rt_string result = rt_string_from_bytes(data + dot_pos, len - dot_pos);
    rt_string_unref(name);
    return result;
}

/// @brief Replace the extension of a path.
/// @param path Original path.
/// @param new_ext New extension (with or without leading dot).
/// @return Newly allocated path with new extension.
rt_string rt_path_with_ext(rt_string path, rt_string new_ext)
{
    const char *path_data = rt_string_safe_data(path);
    size_t path_len = rt_string_safe_len(path);
    const char *ext_data = rt_string_safe_data(new_ext);
    size_t ext_len = rt_string_safe_len(new_ext);

    if (path_len == 0)
        return rt_string_from_bytes(ext_data, ext_len);

    // Find the filename portion
    size_t name_start = path_len;
    while (name_start > 0 && !is_path_sep(path_data[name_start - 1]))
        name_start--;

    // Find the last dot in the filename portion
    size_t dot_pos = path_len;
    for (size_t i = path_len; i > name_start; i--)
    {
        if (path_data[i - 1] == '.')
        {
            // Don't treat leading dot as extension separator
            if (i - 1 > name_start)
            {
                dot_pos = i - 1;
                break;
            }
        }
    }

    rt_string_builder sb;
    rt_sb_init(&sb);

    // Add path up to the extension
    rt_sb_append_bytes(&sb, path_data, dot_pos);

    // Add new extension with dot if needed
    if (ext_len > 0)
    {
        if (ext_data[0] != '.')
            rt_sb_append_bytes(&sb, ".", 1);
        rt_sb_append_bytes(&sb, ext_data, ext_len);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

/// @brief Check if a path is absolute.
/// @param path Path to check.
/// @return 1 if absolute, 0 if relative.
int64_t rt_path_is_abs(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return 0;

    // Unix absolute path
    if (data[0] == '/')
        return 1;

#ifdef _WIN32
    // Windows drive letter (C:\)
    if (len >= 3 && isalpha((unsigned char)data[0]) && data[1] == ':' && is_path_sep(data[2]))
        return 1;

    // UNC path (\\server\share)
    if (len >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
        return 1;
#endif

    return 0;
}

/// @brief Convert a relative path to absolute.
/// @param path Path to convert.
/// @return Newly allocated absolute path.
rt_string rt_path_abs(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    // If already absolute, normalize and return
    if (rt_path_is_abs(path))
        return rt_path_norm(path);

    // Get current working directory
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        // Failed to get cwd, just return normalized input
        return rt_path_norm(path);
    }

    // Join cwd with path
    rt_string cwd_str = rt_string_from_bytes(cwd, strlen(cwd));
    rt_string path_str = rt_string_from_bytes(data, len);
    rt_string joined = rt_path_join(cwd_str, path_str);

    rt_string_unref(cwd_str);
    rt_string_unref(path_str);

    // Normalize the result
    rt_string result = rt_path_norm(joined);
    rt_string_unref(joined);

    return result;
}

/// @brief Normalize a path by removing redundant separators and resolving . and ..
/// @param path Path to normalize.
/// @return Newly allocated normalized path.
rt_string rt_path_norm(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_string_from_bytes(".", 1);

    // Parse path components
    // We'll store component start/end pairs
    size_t *comp_starts = (size_t *)malloc(len * sizeof(size_t));
    size_t *comp_ends = (size_t *)malloc(len * sizeof(size_t));
    size_t comp_count = 0;

    int is_absolute = 0;
    size_t prefix_len = 0;

    // Determine prefix (root portion)
#ifdef _WIN32
    if (len >= 2 && isalpha((unsigned char)data[0]) && data[1] == ':')
    {
        prefix_len = 2;
        if (len >= 3 && is_path_sep(data[2]))
        {
            prefix_len = 3;
            is_absolute = 1;
        }
    }
    else if (len >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
    {
        // UNC path
        is_absolute = 1;
        prefix_len = 2;
        // Find end of server\share
        int slashes = 0;
        while (prefix_len < len && slashes < 2)
        {
            if (is_path_sep(data[prefix_len]))
                slashes++;
            else if (slashes == 0 || slashes == 1)
                prefix_len++;
            if (slashes < 2)
                prefix_len++;
        }
    }
    else
#endif
        if (is_path_sep(data[0]))
    {
        prefix_len = 1;
        is_absolute = 1;
    }

    // Parse components
    size_t i = prefix_len;
    while (i < len)
    {
        // Skip separators
        while (i < len && is_path_sep(data[i]))
            i++;

        if (i >= len)
            break;

        // Find end of component
        size_t start = i;
        while (i < len && !is_path_sep(data[i]))
            i++;

        size_t comp_len = i - start;

        // Handle . and ..
        if (comp_len == 1 && data[start] == '.')
        {
            // Skip "." components
            continue;
        }
        else if (comp_len == 2 && data[start] == '.' && data[start + 1] == '.')
        {
            // Handle ".." - go up one level if possible
            if (comp_count > 0)
            {
                // Check if previous component is also ".."
                size_t prev_len = comp_ends[comp_count - 1] - comp_starts[comp_count - 1];
                const char *prev = data + comp_starts[comp_count - 1];
                if (prev_len == 2 && prev[0] == '.' && prev[1] == '.')
                {
                    // Previous is "..", keep this one too
                    comp_starts[comp_count] = start;
                    comp_ends[comp_count] = i;
                    comp_count++;
                }
                else
                {
                    // Remove previous component
                    comp_count--;
                }
            }
            else if (!is_absolute)
            {
                // Keep ".." at start of relative path
                comp_starts[comp_count] = start;
                comp_ends[comp_count] = i;
                comp_count++;
            }
            // else: at root, ignore ".."
        }
        else
        {
            // Regular component
            comp_starts[comp_count] = start;
            comp_ends[comp_count] = i;
            comp_count++;
        }
    }

    // Build result
    rt_string_builder sb;
    rt_sb_init(&sb);

    // Add prefix
    if (prefix_len > 0)
    {
        rt_sb_append_bytes(&sb, data, prefix_len);
#ifdef _WIN32
        // Normalize prefix separators on Windows
        for (size_t j = 0; j < sb.len; j++)
        {
            if (sb.data[j] == '/')
                sb.data[j] = '\\';
        }
#endif
    }

    // Add components
    for (size_t j = 0; j < comp_count; j++)
    {
        if (j > 0 || (prefix_len > 0 && !is_path_sep(data[prefix_len - 1])))
            rt_sb_append_bytes(&sb, PATH_SEP_STR, 1);

        rt_sb_append_bytes(&sb, data + comp_starts[j], comp_ends[j] - comp_starts[j]);
    }

    // Handle empty result
    if (sb.len == 0)
    {
        rt_sb_free(&sb);
        free(comp_starts);
        free(comp_ends);
        return rt_string_from_bytes(".", 1);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(comp_starts);
    free(comp_ends);

    return result;
}

/// @brief Get the platform-specific path separator.
/// @return Newly allocated string containing "/" or "\".
rt_string rt_path_sep(void)
{
    return rt_string_from_bytes(PATH_SEP_STR, 1);
}
