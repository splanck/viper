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
//   - '*' is intended to stay within one component; after a preceding '**',
//     the current matcher incorrectly lets later wildcards cross separators.
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
#include "rt_trap.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

//=============================================================================
// Pattern Matching
//=============================================================================

/// @brief Compare two glob characters with platform case sensitivity.
///
/// On Windows paths are conventionally case-insensitive, so both sides
/// are lowered before comparison. On POSIX the compare is exact.
static int glob_char_eq(char a, char b) {
#ifdef _WIN32
    if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
        return 1;
    return tolower((unsigned char)a) == tolower((unsigned char)b);
#else
    return a == b;
#endif
}

/// @brief Return non-zero if `ch` is a path separator ('/' on POSIX, '/' or '\\' on Windows).
static int glob_is_path_sep(char ch) {
#ifdef _WIN32
    return ch == '/' || ch == '\\';
#else
    return ch == '/';
#endif
}

static const char *glob_string_cstr_no_nul(rt_string value) {
    if (!value)
        return NULL;
    const char *cstr = rt_string_cstr(value);
    int64_t len = rt_str_len(value);
    if (!cstr || len < 0)
        return NULL;
    if (memchr(cstr, '\0', (size_t)len) != NULL)
        return NULL;
    return cstr;
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

    if (!ch || (glob_is_path_sep(ch) && !allow_slash))
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

typedef struct {
    size_t pi;
    size_t ti;
    int allow_slash;
} glob_state;

static void glob_push_state(glob_state *stack,
                            size_t *sp,
                            uint8_t *visited,
                            size_t plane,
                            size_t stride,
                            size_t pi,
                            size_t ti,
                            int allow_slash) {
    size_t idx = (allow_slash ? plane : 0) + pi * stride + ti;
    if (visited[idx])
        return;
    visited[idx] = 1;
    stack[(*sp)++] = (glob_state){pi, ti, allow_slash ? 1 : 0};
}

/// @brief Iterative glob engine that matches `pattern` against `text`.
static int glob_match_impl(const char *pattern, const char *text, int allow_slash) {
    size_t pattern_len = strlen(pattern);
    size_t text_len = strlen(text);
    if (pattern_len > SIZE_MAX - 1 || text_len > SIZE_MAX - 1) {
        rt_trap("Glob.Match: pattern or text too long");
        return 0;
    }
    size_t rows = pattern_len + 1;
    size_t cols = text_len + 1;
    if (rows > SIZE_MAX / cols || rows * cols > SIZE_MAX / 2) {
        rt_trap("Glob.Match: pattern or text too long");
        return 0;
    }
    size_t cells = rows * cols * 2;
    if (cells > SIZE_MAX / sizeof(glob_state)) {
        rt_trap("Glob.Match: pattern or text too long");
        return 0;
    }
    uint8_t *visited = (uint8_t *)calloc(cells, 1);
    glob_state *stack = (glob_state *)malloc(cells * sizeof(glob_state));
    if (!visited || !stack) {
        free(visited);
        free(stack);
        rt_trap("Glob.Match: memory allocation failed");
        return 0;
    }

    size_t sp = 0;
    size_t plane = rows * cols;
    glob_push_state(stack, &sp, visited, plane, cols, 0, 0, allow_slash ? 1 : 0);

    while (sp > 0) {
        glob_state st = stack[--sp];
        size_t pi = st.pi;
        size_t ti = st.ti;
        int slash_ok = st.allow_slash;

        if (pi >= pattern_len) {
            if (ti == text_len) {
                free(stack);
                free(visited);
                return 1;
            }
            continue;
        }

        if (pattern[pi] == '*') {
            if (pi + 1 < pattern_len && pattern[pi + 1] == '*') {
                pi += 2;
                while (pi < pattern_len && glob_is_path_sep(pattern[pi]))
                    pi++;
                if (pi >= pattern_len) {
                    free(stack);
                    free(visited);
                    return 1;
                }
                for (size_t p = ti; p <= text_len; ++p)
                    glob_push_state(stack, &sp, visited, plane, cols, pi, p, 1);
                continue;
            }

            pi++;
            if (pi >= pattern_len) {
                int ok = 1;
                for (size_t p = ti; p < text_len; ++p) {
                    if (glob_is_path_sep(text[p]) && !slash_ok) {
                        ok = 0;
                        break;
                    }
                }
                if (ok) {
                    free(stack);
                    free(visited);
                    return 1;
                }
                continue;
            }

            for (size_t p = ti; p <= text_len; ++p) {
                if (p < text_len && glob_is_path_sep(text[p]) && !slash_ok) {
                    glob_push_state(stack, &sp, visited, plane, cols, pi, p, slash_ok);
                    break;
                }
                glob_push_state(stack, &sp, visited, plane, cols, pi, p, slash_ok);
                if (p == text_len)
                    break;
            }
            continue;
        }

        if (ti >= text_len)
            continue;

        if (pattern[pi] == '?') {
            if (!(glob_is_path_sep(text[ti]) && !slash_ok))
                glob_push_state(stack, &sp, visited, plane, cols, pi + 1, ti + 1, slash_ok);
        } else if (pattern[pi] == '[') {
            const char *after = pattern + pi;
            int match = glob_match_class(&after, text[ti], slash_ok);
            if (match > 0) {
                glob_push_state(
                    stack, &sp, visited, plane, cols, (size_t)(after - pattern), ti + 1, slash_ok);
            } else if (match < 0 && glob_char_eq(pattern[pi], text[ti])) {
                glob_push_state(stack, &sp, visited, plane, cols, pi + 1, ti + 1, slash_ok);
            }
        } else if (glob_char_eq(pattern[pi], text[ti])) {
            glob_push_state(stack, &sp, visited, plane, cols, pi + 1, ti + 1, slash_ok);
        }
    }

    free(stack);
    free(visited);
    return 0;
}

/// @brief `Glob.Match(path, pattern)` — bool test of a single path against a pattern.
///
/// Does not touch the filesystem — pure string pattern match. `**` spans
/// directories. `*`, `?`, and classes normally stay within one component,
/// but the current state machine leaks the separator permission from an earlier
/// `**` into later tokens (VDOC-186). See the file header for the supported subset.
int8_t rt_glob_match(rt_string path, rt_string pattern) {
    if (!path || !pattern)
        return 0;
    const char *pat = glob_string_cstr_no_nul(pattern);
    const char *txt = glob_string_cstr_no_nul(path);
    if (!pat || !txt)
        return 0;
    return glob_match_impl(pat, txt, 0) ? 1 : 0;
}

/// @brief Release a GC object returned by dir-listing helpers if its ref-count hits zero.
static void glob_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void glob_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
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
    void *volatile result = rt_seq_new();
    void *volatile files = NULL;
    rt_string volatile full_path = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        glob_save_trap_error(saved_error, sizeof(saved_error), "Glob.Files: result append failed");
        rt_trap_clear_recovery();
        if (full_path)
            rt_string_unref((rt_string)full_path);
        glob_release_object((void *)files);
        glob_release_object((void *)result);
        rt_trap(saved_error);
        return rt_seq_new();
    }

    rt_seq_set_owns_elements(result, 1);
    if (!dir || !pattern) {
        rt_trap_clear_recovery();
        return result;
    }
    const char *pat_cstr = glob_string_cstr_no_nul(pattern);
    if (!pat_cstr) {
        rt_trap_clear_recovery();
        return result;
    }

    files = rt_dir_files_seq(dir);

    int64_t count = rt_seq_len((void *)files);
    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get((void *)files, i);
        const char *name_cstr = rt_string_cstr(name);

        if (glob_match_impl(pat_cstr, name_cstr, 0)) {
            // Build full path
            full_path = rt_path_join(dir, name);
            rt_seq_push((void *)result, (void *)full_path);
            rt_string_unref((rt_string)full_path);
            full_path = NULL;
        }
    }

    glob_release_object((void *)files);
    files = NULL;
    rt_trap_clear_recovery();

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
static void glob_recursive_helper(
    rt_string base_dir, rt_string rel_path, const char *pattern, void *result, size_t depth) {
    if (depth > 4096) {
        rt_trap("Glob.FilesRecursive: recursion depth exceeded");
        return;
    }
    rt_string volatile current_dir = NULL;
    rt_string volatile full_path = NULL;
    rt_string volatile entry_rel = NULL;
    rt_string volatile temp = NULL;
    rt_string volatile rel_ref = NULL;
    rt_string volatile name_ref = NULL;
    void *volatile entries = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        glob_save_trap_error(
            saved_error, sizeof(saved_error), "Glob.FilesRecursive: traversal failed");
        rt_trap_clear_recovery();
        if (name_ref)
            rt_string_unref((rt_string)name_ref);
        if (rel_ref)
            rt_string_unref((rt_string)rel_ref);
        if (temp)
            rt_string_unref((rt_string)temp);
        if (entry_rel)
            rt_string_unref((rt_string)entry_rel);
        if (full_path)
            rt_string_unref((rt_string)full_path);
        glob_release_object((void *)entries);
        if (current_dir)
            rt_string_unref((rt_string)current_dir);
        rt_trap(saved_error);
        return;
    }

    // List all entries in current directory
    if (rt_str_len(rel_path) == 0) {
        current_dir = rt_string_ref(base_dir);
    } else {
        current_dir = rt_path_join(base_dir, rel_path);
    }

    entries = rt_dir_list_seq((rt_string)current_dir);
    int64_t count = rt_seq_len((void *)entries);

    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get((void *)entries, i);
        full_path = rt_path_join((rt_string)current_dir, name);

        // Build relative path for matching
        if (rt_str_len(rel_path) == 0) {
            entry_rel = rt_string_ref(name);
        } else {
            rt_string slash = rt_const_cstr("/");
            rel_ref = rt_string_ref(rel_path);
            temp = rt_str_concat((rt_string)rel_ref, slash);
            rel_ref = NULL;
            name_ref = rt_string_ref(name);
            entry_rel = rt_str_concat((rt_string)temp, (rt_string)name_ref);
            temp = NULL;
            name_ref = NULL;
        }

        // Check if this entry matches the pattern
        if (glob_match_impl(pattern, rt_string_cstr((rt_string)entry_rel), 0)) {
            // Only add files, not directories
            if (rt_io_file_exists((rt_string)full_path)) {
                rt_seq_push(result, (void *)full_path);
            }
        }

        // If directory, recurse into it
        if (glob_is_real_directory((rt_string)full_path)) {
            glob_recursive_helper(base_dir, (rt_string)entry_rel, pattern, result, depth + 1);
        }

        rt_string_unref((rt_string)entry_rel);
        entry_rel = NULL;
        rt_string_unref((rt_string)full_path);
        full_path = NULL;
    }

    glob_release_object((void *)entries);
    entries = NULL;
    rt_string_unref((rt_string)current_dir);
    current_dir = NULL;
    rt_trap_clear_recovery();
}

/// @brief `Glob.FilesRecursive(base, pattern)` — descend the `base` subtree.
///
/// Unlike `Glob.Files`, the pattern is matched against each entry's
/// *relative path from base* (with `/` separators), so patterns like
/// `**/*.png` or `assets/**/*.wav` span multiple directories. Symlinks
/// are not followed.
void *rt_glob_files_recursive(rt_string base, rt_string pattern) {
    void *volatile result = rt_seq_new();
    rt_string volatile empty = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        glob_save_trap_error(
            saved_error, sizeof(saved_error), "Glob.FilesRecursive: traversal failed");
        rt_trap_clear_recovery();
        if (empty)
            rt_string_unref((rt_string)empty);
        glob_release_object((void *)result);
        rt_trap(saved_error);
        return rt_seq_new();
    }

    rt_seq_set_owns_elements(result, 1);
    empty = rt_str_empty();
    if (!base || !pattern) {
        rt_string_unref((rt_string)empty);
        rt_trap_clear_recovery();
        return result;
    }
    const char *pat = glob_string_cstr_no_nul(pattern);
    if (!pat) {
        rt_string_unref((rt_string)empty);
        rt_trap_clear_recovery();
        return result;
    }
    if (!glob_string_cstr_no_nul(base)) {
        rt_string_unref((rt_string)empty);
        rt_trap_clear_recovery();
        return result;
    }

    glob_recursive_helper(base, (rt_string)empty, pat, (void *)result, 0);
    rt_string_unref((rt_string)empty);
    empty = NULL;
    rt_trap_clear_recovery();

    return result;
}

/// @brief `Glob.Entries(dir, pattern)` — list files *and* subdirectories.
///
/// Sibling of `Glob.Files`, but includes directory entries in the
/// results (via `rt_dir_list_seq` instead of `rt_dir_files_seq`).
/// Useful when the caller wants to pattern-match directory names.
void *rt_glob_entries(rt_string dir, rt_string pattern) {
    void *volatile result = rt_seq_new();
    void *volatile entries = NULL;
    rt_string volatile full_path = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        glob_save_trap_error(
            saved_error, sizeof(saved_error), "Glob.Entries: result append failed");
        rt_trap_clear_recovery();
        if (full_path)
            rt_string_unref((rt_string)full_path);
        glob_release_object((void *)entries);
        glob_release_object((void *)result);
        rt_trap(saved_error);
        return rt_seq_new();
    }

    rt_seq_set_owns_elements(result, 1);
    if (!dir || !pattern) {
        rt_trap_clear_recovery();
        return result;
    }
    const char *pat_cstr = glob_string_cstr_no_nul(pattern);
    if (!pat_cstr) {
        rt_trap_clear_recovery();
        return result;
    }

    entries = rt_dir_list_seq(dir);

    int64_t count = rt_seq_len((void *)entries);
    for (int64_t i = 0; i < count; i++) {
        rt_string name = (rt_string)rt_seq_get((void *)entries, i);
        const char *name_cstr = rt_string_cstr(name);

        if (glob_match_impl(pat_cstr, name_cstr, 0)) {
            // Build full path
            full_path = rt_path_join(dir, name);
            rt_seq_push((void *)result, (void *)full_path);
            rt_string_unref((rt_string)full_path);
            full_path = NULL;
        }
    }

    glob_release_object((void *)entries);
    entries = NULL;
    rt_trap_clear_recovery();
    return result;
}
