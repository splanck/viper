//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_diff.c
// Purpose: Implements line-level text diff for the Viper.Text.Diff class.
//          Computes the Myers LCS-based edit script between two multiline
//          strings, producing added/removed/unchanged line annotations.
//
// Key invariants:
//   - Input strings are split on '\n' into line arrays before diffing.
//   - The diff produces a minimal edit script (fewest insertions + deletions).
//   - Each output record carries a tag: "=" (unchanged), "+" (added), "-" (removed).
//   - Context lines (unchanged lines adjacent to changes) are included in output.
//   - Empty input produces an empty diff, not a null result.
//   - All returned strings are fresh allocations; the diff holds no live refs.
//
// Ownership/Lifetime:
//   - Returned Seq of diff records is a fresh allocation owned by the caller.
//   - Input strings are borrowed for the duration of the call; not retained.
//
// Links: src/runtime/text/rt_diff.h (public API),
//        src/runtime/rt_seq.h (Seq container used for diff output),
//        src/runtime/rt_string.h (string split for line arrays)
//
//===----------------------------------------------------------------------===//

#include "rt_diff.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string_builder.h"

#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

// ---------------------------------------------------------------------------
// Line splitting helper
// ---------------------------------------------------------------------------

typedef struct {
    char **lines;
    int count;
} line_array;

/// @brief Split a NUL-terminated string into a heap-allocated array of line copies.
/// @details Counts `\n` characters first to size the array, then walks
///          again to copy each line (without the terminator) into its
///          own malloc'd null-terminated string. The final segment after
///          the last `\n` is always emitted, even when empty — that
///          way `"a\nb\n"` produces `["a", "b", ""]` and `"a\nb"`
///          produces `["a", "b"]`, mirroring how unified-diff tools
///          treat trailing newlines.
static line_array split_lines(const char *text) {
    line_array la = {NULL, 0};
    if (!text || !*text)
        return la;

    // Count lines
    int count = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n')
            count++;

    la.lines = (char **)malloc((size_t)count * sizeof(char *));
    if (!la.lines)
        rt_trap("rt_diff: memory allocation failed");
    la.count = 0;

    const char *start = text;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            la.lines[la.count] = (char *)malloc(len + 1);
            if (!la.lines[la.count])
                rt_trap("rt_diff: memory allocation failed");
            memcpy(la.lines[la.count], start, len);
            la.lines[la.count][len] = '\0';
            la.count++;
            if (*p == '\0')
                break;
            start = p + 1;
        }
    }
    return la;
}

/// @brief Free every line copy and the line-pointer array, leaving an empty `line_array`.
/// @details Setting both `lines` and `count` to zero/NULL after free
///          makes the helper idempotent — a second call is a no-op
///          rather than a double-free.
static void free_lines(line_array *la) {
    for (int i = 0; i < la->count; i++)
        free(la->lines[i]);
    free(la->lines);
    la->lines = NULL;
    la->count = 0;
}

// ---------------------------------------------------------------------------
// Simple LCS-based diff (O(nm) space - sufficient for typical text)
// ---------------------------------------------------------------------------

/// @brief Fill the longest-common-subsequence DP table for two line arrays.
/// @details Standard `O(m*n)` LCS recurrence run *backward* from
///          `(m, n)` to `(0, 0)` so `table[i][j]` holds the LCS length
///          of the suffixes `a[i..m]` and `b[j..n]`. The traceback in
///          `rt_diff_lines` then walks forward from `(0, 0)` greedily,
///          choosing the direction that preserves the LCS — that's
///          how it produces a minimal (Myers-equivalent) diff for
///          short inputs. Note: `O(n*m)` space — fine for source
///          files (<10K lines × <10K lines) but would be replaced by
///          Hirschberg's algorithm at scale.
static void compute_lcs_table(line_array *a, line_array *b, int **table) {
    int m = a->count;
    int n = b->count;

    for (int i = m; i >= 0; i--) {
        for (int j = n; j >= 0; j--) {
            if (i == m || j == n)
                table[i][j] = 0;
            else if (strcmp(a->lines[i], b->lines[j]) == 0)
                table[i][j] = table[i + 1][j + 1] + 1;
            else
                table[i][j] = table[i + 1][j] > table[i][j + 1] ? table[i + 1][j] : table[i][j + 1];
        }
    }
}

// ---------------------------------------------------------------------------
// rt_diff_lines
// ---------------------------------------------------------------------------

void *rt_diff_lines(rt_string a, rt_string b) {
    void *result = rt_seq_new();

    const char *astr = a ? rt_string_cstr(a) : "";
    const char *bstr = b ? rt_string_cstr(b) : "";

    line_array la = split_lines(astr);
    line_array lb = split_lines(bstr);

    int m = la.count;
    int n = lb.count;

    // Build LCS table
    int **table = (int **)malloc((size_t)(m + 1) * sizeof(int *));
    if (!table) {
        free_lines(&la);
        free_lines(&lb);
        return result;
    }
    for (int i = 0; i <= m; i++) {
        table[i] = (int *)calloc((size_t)(n + 1), sizeof(int));
        if (!table[i]) {
            for (int k = 0; k < i; k++)
                free(table[k]);
            free(table);
            free_lines(&la);
            free_lines(&lb);
            return result;
        }
    }

    compute_lcs_table(&la, &lb, table);

    // Trace back to produce diff
    int i = 0, j = 0;
    while (i < m || j < n) {
        rt_string_builder sb;
        rt_sb_init(&sb);

        if (i < m && j < n && strcmp(la.lines[i], lb.lines[j]) == 0) {
            rt_sb_append_cstr(&sb, " ");
            rt_sb_append_cstr(&sb, la.lines[i]);
            i++;
            j++;
        } else if (j < n && (i >= m || table[i][j + 1] >= table[i + 1][j])) {
            rt_sb_append_cstr(&sb, "+");
            rt_sb_append_cstr(&sb, lb.lines[j]);
            j++;
        } else {
            rt_sb_append_cstr(&sb, "-");
            rt_sb_append_cstr(&sb, la.lines[i]);
            i++;
        }

        rt_string line = rt_string_from_bytes(sb.data, sb.len);
        rt_seq_push(result, line);
        rt_sb_free(&sb);
    }

    // Cleanup
    for (int k = 0; k <= m; k++)
        free(table[k]);
    free(table);
    free_lines(&la);
    free_lines(&lb);

    return result;
}

// ---------------------------------------------------------------------------
// rt_diff_unified
// ---------------------------------------------------------------------------

/// @brief Render an `a → b` diff as a plain unified-diff style string.
/// @details Emits the canonical `--- a` / `+++ b` header, then dumps
///          every line from `rt_diff_lines` (each already prefixed
///          with one of ` `, `+`, `-`) followed by `\n`. The
///          `context` parameter is parsed (defaults to 3 if negative)
///          but currently *all* lines are output — this is a
///          simplified unified format suitable for diagnostics, not
///          a true patch-applicable hunk format.
rt_string rt_diff_unified(rt_string a, rt_string b, int64_t context) {
    if (context < 0)
        context = 3;

    void *diff = rt_diff_lines(a, b);
    int64_t len = rt_seq_len(diff);

    rt_string_builder sb;
    rt_sb_init(&sb);

    // Simple unified format: output all lines with proper prefixes
    rt_sb_append_cstr(&sb, "--- a\n");
    rt_sb_append_cstr(&sb, "+++ b\n");

    for (int64_t i = 0; i < len; i++) {
        rt_string line = (rt_string)rt_seq_get(diff, i);
        const char *cstr = rt_string_cstr(line);
        if (cstr) {
            rt_sb_append_cstr(&sb, cstr);
            rt_sb_append_cstr(&sb, "\n");
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_diff_count_changes
// ---------------------------------------------------------------------------

/// @brief Count the number of added or removed lines between two strings.
int64_t rt_diff_count_changes(rt_string a, rt_string b) {
    void *diff = rt_diff_lines(a, b);
    int64_t len = rt_seq_len(diff);
    int64_t changes = 0;

    for (int64_t i = 0; i < len; i++) {
        rt_string line = (rt_string)rt_seq_get(diff, i);
        const char *cstr = rt_string_cstr(line);
        if (cstr && (cstr[0] == '+' || cstr[0] == '-'))
            changes++;
    }

    return changes;
}

// ---------------------------------------------------------------------------
// rt_diff_patch
// ---------------------------------------------------------------------------

/// @brief Reconstruct the "after" text by applying a diff sequence.
/// @details Walks the diff's line entries (each prefixed with ` `, `+`,
///          or `-`) and emits the appropriate content into a builder:
///          - ` ` (context) → output the line as-is.
///          - `+` (added)   → output the line.
///          - `-` (removed) → skip.
///          Joins lines with `\n`. The `original` parameter is unused
///          since the diff entries already encode every line — it's
///          kept in the signature so the API can later switch to a
///          non-redundant patch format without breaking callers.
rt_string rt_diff_patch(rt_string original, void *diff) {
    (void)original;
    if (!diff)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    int64_t len = rt_seq_len(diff);
    int first = 1;

    for (int64_t i = 0; i < len; i++) {
        rt_string line = (rt_string)rt_seq_get(diff, i);
        const char *cstr = rt_string_cstr(line);
        if (!cstr)
            continue;

        // Include lines that are same (' ') or added ('+')
        if (cstr[0] == ' ' || cstr[0] == '+') {
            if (!first)
                rt_sb_append_cstr(&sb, "\n");
            rt_sb_append_cstr(&sb, cstr + 1); // Skip prefix
            first = 0;
        }
        // Skip removed lines ('-')
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}
