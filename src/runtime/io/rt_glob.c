//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_glob.c
// Purpose: Implements glob-style wildcard pattern matching against filesystem
//          paths for the Viper.IO.Glob class. Supports single-component '*',
//          cross-directory '**', '?' wildcards, and character classes '[...]'.
//
// Key invariants:
//   - '*' matches any characters within a single path component (no '/').
//   - '**' matches any sequence of characters including directory separators.
//   - '?' matches exactly one character but not '/'.
//   - '[...]' character classes follow POSIX semantics including negation '[!'.
//   - Pattern matching is case-sensitive on Unix and case-insensitive on Windows.
//   - Directory traversal respects the current working directory of the process.
//
// Ownership/Lifetime:
//   - Returned path strings are fresh rt_string allocations owned by the caller.
//   - The returned sequence is a new rt_seq owned by the caller.
//
// Links: src/runtime/io/rt_glob.h (public API),
//        src/runtime/io/rt_dir.h (directory enumeration used internally),
//        src/runtime/io/rt_path.h (path component splitting)
//
//===----------------------------------------------------------------------===//

#include "rt_glob.h"

#include "rt_dir.h"
#include "rt_file_ext.h"
#include "rt_file_path.h"
#include "rt_object.h"
#include "rt_path.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

//=============================================================================
// Pattern Matching
//=============================================================================

/// @brief Compare two glob characters with platform case sensitivity.
///
/// On Windows paths are conventionally case-insensitive, so both sides
/// are lowered before comparison. On POSIX the compare is exact.
static int glob_char_eq(char a, char b) {
#ifdef _WIN32
    return tolower((unsigned char)a) == tolower((unsigned char)b);
#else
    return a == b;
#endif
}

/// @brief Test whether `ch` falls in `[start..end]` for `[a-z]`-style classes.
///
/// Normalizes the three chars with the same platform case rule as
/// `glob_char_eq`, then normalizes the range so reversed specs
/// (`[z-a]`) still work.
static int glob_char_in_range(char ch, char start, char end) {
#ifdef _WIN32
    unsigned char c = (unsigned char)tolower((unsigned char)ch);
    unsigned char lo = (unsigned char)tolower((unsigned char)start);
    unsigned char hi = (unsigned char)tolower((unsigned char)end);
#else
    unsigned char c = (unsigned char)ch;
    unsigned char lo = (unsigned char)start;
    unsigned char hi = (unsigned char)end;
#endif
    if (lo > hi) {
        unsigned char tmp = lo;
        lo = hi;
        hi = tmp;
    }
    return c >= lo && c <= hi;
}

/// @brief Parse a `[...]` character class and test a single character against it.
///
/// Handles four class elements: plain literals (`[abc]`), backslash
/// escapes (`[\]]`), explicit ranges (`[a-z]`), and negation by a
/// leading `!` or `^`. On success advances `*pattern_ptr` past the
/// closing `]` and returns 1 (match) or 0 (no match). Returns -1 on
/// a malformed class (missing `]`), so the caller can fall back to
/// treating the `[` as a literal. The `-` only starts a range when a
/// previous character was seen and a non-`]` character follows, so
/// leading/trailing dashes match literally.
static int glob_match_class(const char **pattern_ptr, char ch, int allow_slash) {
    const char *p = *pattern_ptr + 1; // skip '['
    int negate = 0;
    int matched = 0;
    int has_prev = 0;
    char prev = '\0';

    if (!ch || (ch == '/' && !allow_slash))
        return 0;

    if (*p == '!' || *p == '^') {
        negate = 1;
        p++;
    }

    while (*p && *p != ']') {
        char token = *p++;
        if (token == '\\' && *p)
            token = *p++;

        if (token == '-' && has_prev && *p && *p != ']') {
            char end = *p++;
            if (end == '\\' && *p)
                end = *p++;
            if (glob_char_in_range(ch, prev, end))
                matched = 1;
            has_prev = 0;
            continue;
        }

        if (glob_char_eq(ch, token))
            matched = 1;
        prev = token;
        has_prev = 1;
    }

    if (*p != ']')
        return -1;

    *pattern_ptr = p + 1;
    return negate ? !matched : matched;
}

/// @brief Recursive glob engine that matches `pattern` against `text`.
///
/// Walks the pattern left-to-right. Most tokens consume one character
/// from both sides, but `*` and `**` branch: `*` tries each possible
/// split of the remaining text (stopping at `/` when
/// `allow_slash=0`), while `**` acts like `*` with `allow_slash=1`
/// so it can span path components. To keep adversarial patterns like
/// `**/**/**/*.c` from blowing up exponentially, the `**` branch
/// caps the recursive retry loop at 10,000 attempts — on real paths
/// this is never reached. `[...]` classes delegate to
/// `glob_match_class` and fall back to literal `[` on malformed
/// inputs. Returns 1 on full-pattern match consuming all of `text`.
static int glob_match_impl(const char *pattern, const char *text, int allow_slash) {
    while (*pattern) {
        if (*pattern == '*') {
            // Check for **
            if (pattern[1] == '*') {
                pattern += 2;
                // Skip any following /
                while (*pattern == '/')
                    pattern++;

                // ** matches everything including /
                if (!*pattern)
                    return 1; // ** at end matches everything

                // Try matching rest of pattern at each position
                // Limit iterations to prevent exponential backtracking on adversarial input
                int attempts = 0;
                for (const char *p = text; *p; p++) {
                    if (++attempts > 10000)
                        return 0; // Bail out on pathological patterns
                    if (glob_match_impl(pattern, p, 1))
                        return 1;
                }
                return glob_match_impl(pattern, text + strlen(text), 1);
            }

            // Single * - doesn't match /
            pattern++;

            // * at end matches everything (except / if not allow_slash)
            if (!*pattern) {
                while (*text) {
                    if (*text == '/' && !allow_slash)
                        return 0;
                    text++;
                }
                return 1;
            }

            // Try matching rest of pattern at each position
            while (*text) {
                if (*text == '/' && !allow_slash) {
                    // Can't skip past /
                    return glob_match_impl(pattern, text, allow_slash);
                }
                if (glob_match_impl(pattern, text, allow_slash))
                    return 1;
                text++;
            }
            return glob_match_impl(pattern, text, allow_slash);
        } else if (*pattern == '?') {
            // ? matches any single char except /
            if (!*text || (*text == '/' && !allow_slash))
                return 0;
            pattern++;
            text++;
        } else if (*pattern == '[') {
            const char *after = pattern;
            int match = glob_match_class(&after, *text, allow_slash);
            if (match >= 0) {
                if (!match)
                    return 0;
                pattern = after;
                text++;
            } else {
                if (!glob_char_eq(*pattern, *text))
                    return 0;
                pattern++;
                text++;
            }
        } else {
            // Literal character match
            if (!glob_char_eq(*pattern, *text))
                return 0;
            pattern++;
            text++;
        }
    }

    return *text == '\0';
}

/// @brief `Glob.Match(path, pattern)` — bool test of a single path against a pattern.
///
/// Does not touch the filesystem — pure string pattern match. `*`
/// matches within a component, `**` spans directories, `?` matches
/// one non-slash character, `[...]` is a character class. See the
/// file header for the full supported subset.
int8_t rt_glob_match(rt_string path, rt_string pattern) {
    if (!path || !pattern)
        return 0;
    const char *pat = rt_string_cstr(pattern);
    const char *txt = rt_string_cstr(path);
    if (!pat || !txt)
        return 0;
    return glob_match_impl(pat, txt, 0) ? 1 : 0;
}

static void glob_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Check if `path` refers to a real directory (not a symlink/reparse point).
///
/// Used by the recursive walker to decide where to descend. On Windows
/// explicitly refuses reparse points so glob traversal can't loop
/// through junction/symlink cycles; on POSIX uses `lstat` so a symlink
/// to a directory isn't followed — the caller already enumerates
/// real subdirs through `rt_dir_list_seq`.
static int glob_is_real_directory(rt_string path) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;

#ifdef _WIN32
    wchar_t *wide = rt_file_path_utf8_to_wide(cpath);
    if (!wide)
        return 0;
    DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return 0;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        return 0;
    return 1;
#else
    struct stat st;
    if (lstat(cpath, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

//=============================================================================
// File Finding
//=============================================================================

/// @brief `Glob.Files(dir, pattern)` — list files in `dir` matching `pattern`.
///
/// Single-level: enumerates direct children of `dir` (not subdirectories),
/// tests each filename against the pattern, and returns full joined paths
/// for matches. Use `Glob.FilesRecursive` for subtree traversal. Returns
/// an empty owning Seq if `dir` is missing or `pattern` is NULL.
void *rt_glob_files(rt_string dir, rt_string pattern) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    if (!dir || !pattern)
        return result;
    const char *pat_cstr = rt_string_cstr(pattern);
    if (!pat_cstr)
        return result;

    void *files = rt_dir_files_seq(dir);

    int64_t count = rt_seq_len(files);
    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get(files, i);
        const char *name_cstr = rt_string_cstr(name);

        if (glob_match_impl(pat_cstr, name_cstr, 0)) {
            // Build full path
            rt_string full_path = rt_path_join(dir, name);
            rt_seq_push(result, full_path);
            rt_string_unref(full_path);
        }
    }

    // Release the intermediate files Seq
    if (rt_obj_release_check0(files))
        rt_obj_free(files);

    return result;
}

/// @brief Depth-first walker backing `rt_glob_files_recursive`.
///
/// Maintains a relative path string (`rel_path`) so the pattern can
/// be matched against the path from the glob root, letting
/// `**/*.c`-style patterns span directories. For every directory
/// entry: builds both the full filesystem path (for existence and
/// recursion checks) and the relative path (for pattern matching);
/// if it matches the pattern and is a real file, adds it to the
/// result Seq; if it's a real directory, recurses. Symlinked
/// directories are skipped via `glob_is_real_directory` to prevent
/// cycles.
static void glob_recursive_helper(rt_string base_dir,
                                  rt_string rel_path,
                                  const char *pattern,
                                  void *result) {
    // List all entries in current directory
    rt_string current_dir;
    int owns_current_dir = 0;
    if (rt_str_len(rel_path) == 0) {
        current_dir = rt_string_ref(base_dir);
    } else {
        current_dir = rt_path_join(base_dir, rel_path);
        owns_current_dir = 1;
    }

    void *entries = rt_dir_list_seq(current_dir);
    int64_t count = rt_seq_len(entries);

    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get(entries, i);
        rt_string full_path = rt_path_join(current_dir, name);

        // Build relative path for matching
        rt_string entry_rel;
        if (rt_str_len(rel_path) == 0) {
            entry_rel = rt_string_ref(name);
        } else {
            rt_string slash = rt_const_cstr("/");
            rt_string temp = rt_str_concat(rt_string_ref(rel_path), slash);
            entry_rel = rt_str_concat(temp, rt_string_ref(name));
            rt_string_unref(temp);
        }

        // Check if this entry matches the pattern
        if (glob_match_impl(pattern, rt_string_cstr(entry_rel), 0)) {
            // Only add files, not directories
            if (rt_io_file_exists(full_path)) {
                rt_seq_push(result, full_path);
            }
        }

        // If directory, recurse into it
        if (glob_is_real_directory(full_path)) {
            glob_recursive_helper(base_dir, entry_rel, pattern, result);
        }

        rt_string_unref(entry_rel);
        rt_string_unref(full_path);
    }

    glob_release_object(entries);
    if (owns_current_dir) {
        rt_string_unref(current_dir);
    } else {
        rt_string_unref(current_dir);
    }
}

/// @brief `Glob.FilesRecursive(base, pattern)` — descend the `base` subtree.
///
/// Unlike `Glob.Files`, the pattern is matched against each entry's
/// *relative path from base* (with `/` separators), so patterns like
/// `**/*.png` or `assets/**/*.wav` span multiple directories. Symlinks
/// are not followed.
void *rt_glob_files_recursive(rt_string base, rt_string pattern) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    rt_string empty = rt_str_empty();
    if (!base || !pattern) {
        rt_string_unref(empty);
        return result;
    }
    const char *pat = rt_string_cstr(pattern);
    if (!pat) {
        rt_string_unref(empty);
        return result;
    }

    glob_recursive_helper(base, empty, pat, result);
    rt_string_unref(empty);

    return result;
}

/// @brief `Glob.Entries(dir, pattern)` — list files *and* subdirectories.
///
/// Sibling of `Glob.Files`, but includes directory entries in the
/// results (via `rt_dir_list_seq` instead of `rt_dir_files_seq`).
/// Useful when the caller wants to pattern-match directory names.
void *rt_glob_entries(rt_string dir, rt_string pattern) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    if (!dir || !pattern)
        return result;
    const char *pat_cstr = rt_string_cstr(pattern);
    if (!pat_cstr)
        return result;

    void *entries = rt_dir_list_seq(dir);

    int64_t count = rt_seq_len(entries);
    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get(entries, i);
        const char *name_cstr = rt_string_cstr(name);

        if (glob_match_impl(pat_cstr, name_cstr, 0)) {
            // Build full path
            rt_string full_path = rt_path_join(dir, name);
            rt_seq_push(result, full_path);
            rt_string_unref(full_path);
        }
    }

    glob_release_object(entries);
    return result;
}
