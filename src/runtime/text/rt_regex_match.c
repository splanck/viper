//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_regex_match.c
// Purpose: Backtracking matching engine for the Viper regex AST. Runs a
//          compiled re_node tree against a subject string, with and without
//          capture-group tracking. Split out of rt_regex.c; shares AST types
//          and class primitives via rt_regex_internal.h.
//
// Key invariants:
//   - Matching is bounded by RE_MAX_STEPS (S-11 ReDoS guard); the engine
//     aborts a match attempt rather than backtracking unboundedly.
//   - re_find_match / re_find_match_with_groups are the engine entry points
//     consumed by the core's public rt_pattern_* API.
//   - Capture-group recording inside quantifiers is best-effort (last match
//     wins), not fully PCRE-compliant.
//
// Ownership/Lifetime:
//   - Operates on caller-owned compiled patterns and text buffers; allocates
//     only transient position scratch arrays, freed before returning.
//
// Links: src/runtime/text/rt_regex.c (core/cache/public API),
//        src/runtime/text/rt_regex_parse.c (parser),
//        src/runtime/text/rt_regex_internal.h (shared engine types)
//
//===----------------------------------------------------------------------===//

#include "rt_regex.h"
#include "rt_regex_internal.h"

#include "rt_internal.h"
#include "rt_trap.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Pattern Matching Engine (Backtracking)
//=============================================================================

/* S-11: Maximum backtracking steps before aborting (ReDoS guard) */
#define RE_MAX_STEPS 1000000

typedef struct {
    const char *text;
    int text_len;
    int start_pos; // Start position for this match attempt
    int steps;     // Backtracking step counter
    int max_steps; // Step limit (0 = unlimited)
} match_context;

// Forward declarations
static bool match_node(match_context *ctx, re_node *n, int pos, int *end_pos);
static bool match_concat_from(
    match_context *ctx, re_node **children, int count, int index, int pos, int *end_pos);

/// @brief Enumerate the end positions reachable by repeating a quantifier.
///
/// For `<child>{0..max}` (or `{1..max}` for `+`, or `{0..1}` for `?`),
/// greedily steps the cursor forward, recording each successful end
/// position. Returns the count. Detects zero-width matches and bails
/// (otherwise `()*` would loop forever). Used by both `match_quant`
/// (standalone) and `match_concat_from` (with backtracking).
///
/// @param ctx           Match context (text, length, step counter).
/// @param n             Quantifier node.
/// @param pos           Starting cursor.
/// @param positions     Out: array of reachable end positions.
/// @param max_positions Capacity of `positions`.
/// @return Number of end positions written.
static int collect_quant_positions(
    match_context *ctx, re_node *n, int pos, int *positions, int max_positions) {
    re_node *child = n->data.quant.child;
    re_quant_type qtype = n->data.quant.qtype;

    int min_count = (qtype == QUANT_PLUS) ? 1 : 0;
    int max_count = (qtype == QUANT_QUEST) ? 1 : INT32_MAX;

    int num = 0;
    int cur_pos = pos;

    // Position for 0 matches (if allowed)
    if (min_count == 0 && num < max_positions)
        positions[num++] = pos;

    // Greedily collect match positions
    int count = 0;
    while (count < max_count && num < max_positions) {
        int child_end;
        if (match_node(ctx, child, cur_pos, &child_end)) {
            if (child_end == cur_pos)
                break; // Zero-width match; only count once
            cur_pos = child_end;
            count++;
            if (count >= min_count)
                positions[num++] = cur_pos;
        } else {
            break;
        }
    }

    return num;
}

static int *alloc_match_positions(int text_len, int pos, int *capacity_out) {
    if (capacity_out)
        *capacity_out = 0;
    if (pos < 0 || pos > text_len) {
        rt_trap("Pattern: invalid match position");
        return NULL;
    }
    size_t capacity = (size_t)(text_len - pos) + 2;
    if (capacity > (size_t)INT_MAX || capacity > SIZE_MAX / sizeof(int)) {
        rt_trap("Pattern: position allocation overflow");
        return NULL;
    }
    int *positions = (int *)malloc(sizeof(int) * capacity);
    if (!positions) {
        rt_trap("Pattern: memory allocation failed");
        return NULL;
    }
    if (capacity_out)
        *capacity_out = (int)capacity;
    return positions;
}

/// @brief Match a quantifier node when it has no following continuation.
///
/// Picks the longest (greedy) or shortest (lazy) reachable end without
/// any backtracking — there's no "what comes after" to consult. Used
/// when the quantifier is the entire pattern or the last child of a
/// concat. The full backtracking version lives in `match_concat_from`.
static bool match_quant(match_context *ctx, re_node *n, int pos, int *end_pos) {
    bool greedy = n->data.quant.greedy;

    int capacity = 0;
    int *positions = alloc_match_positions(ctx->text_len, pos, &capacity);
    int num = collect_quant_positions(ctx, n, pos, positions, capacity);

    bool found = false;
    if (greedy) {
        // Try longest first
        if (num > 0) {
            *end_pos = positions[num - 1];
            found = true;
        }
    } else {
        // Try shortest first
        if (num > 0) {
            *end_pos = positions[0];
            found = true;
        }
    }

    free(positions);
    return found;
}

/// @brief Match a node against `ctx->text` starting at `pos`.
///
/// Returns true on success and writes the post-match cursor to
/// `*end_pos`. Implements the per-node-kind match logic via switch.
/// Increments the global step counter on each call and bails (returns
/// false) when the S-11 ReDoS cap is exceeded — this is what protects
/// the engine from catastrophic backtracking inputs.
static bool match_node(match_context *ctx, re_node *n, int pos, int *end_pos) {
    if (!ctx || !end_pos || pos < 0 || pos > ctx->text_len)
        return false;
    /* S-11: ReDoS guard — abort if step limit exceeded */
    if (ctx->max_steps > 0 && ++ctx->steps > ctx->max_steps) {
        *end_pos = pos;
        return false;
    }

    if (!n) {
        *end_pos = pos;
        return true;
    }

    switch (n->type) {
        case RE_LITERAL:
            if (pos < ctx->text_len && ctx->text[pos] == n->data.literal) {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_DOT:
            if (pos < ctx->text_len && ctx->text[pos] != '\n') {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_ANCHOR_START:
            if (pos == 0) {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_ANCHOR_END:
            if (pos == ctx->text_len) {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_CLASS:
            if (pos < ctx->text_len &&
                class_test(&n->data.char_class, (unsigned char)ctx->text[pos])) {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_CONCAT:
            return match_concat_from(
                ctx, n->data.children.children, n->data.children.count, 0, pos, end_pos);

        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++) {
                int child_end;
                if (match_node(ctx, n->data.children.children[i], pos, &child_end)) {
                    *end_pos = child_end;
                    return true;
                }
            }
            return false;

        case RE_GROUP:
            if (n->data.children.count > 0) {
                return match_node(ctx, n->data.children.children[0], pos, end_pos);
            }
            *end_pos = pos;
            return true;

        case RE_QUANT:
            return match_quant(ctx, n, pos, end_pos);
    }

    return false;
}

/// @brief Match the tail of a concat sequence starting at `children[index]`.
///
/// The interesting case is a quantifier child: we enumerate every
/// reachable end position and try each one in greedy/lazy order,
/// recursing for the remaining children. This is the backtracking
/// loop — most patterns terminate quickly, but the S-11 step cap in
/// `match_node` ensures even pathological cases bail eventually.
/// Non-quantifier children are matched once; if they succeed we tail
/// into the next child.
static bool match_concat_from(
    match_context *ctx, re_node **children, int count, int index, int pos, int *end_pos) {
    if (index >= count) {
        *end_pos = pos;
        return true;
    }

    re_node *child = children[index];

    if (child->type == RE_QUANT) {
        bool greedy = child->data.quant.greedy;

        int capacity = 0;
        int *positions = alloc_match_positions(ctx->text_len, pos, &capacity);
        int num = collect_quant_positions(ctx, child, pos, positions, capacity);

        bool found = false;
        if (greedy) {
            // Try longest match first, backtrack to shorter
            for (int i = num - 1; i >= 0; i--) {
                if (match_concat_from(ctx, children, count, index + 1, positions[i], end_pos)) {
                    found = true;
                    break;
                }
            }
        } else {
            // Try shortest match first
            for (int i = 0; i < num; i++) {
                if (match_concat_from(ctx, children, count, index + 1, positions[i], end_pos)) {
                    found = true;
                    break;
                }
            }
        }

        free(positions);
        return found;
    } else {
        // Non-quantifier child: single match attempt
        int child_end;
        if (match_node(ctx, child, pos, &child_end))
            return match_concat_from(ctx, children, count, index + 1, child_end, end_pos);
        return false;
    }
}

/// @brief Scan the text for the first match starting at or after `start_from`.
///
/// Walks the cursor forward one byte at a time, attempting `match_node`
/// at each position. Caps the per-attempt step count via `RE_MAX_STEPS`
/// (S-11). Returns true on first hit with start/end set.
static bool find_match(compiled_pattern *cp,
                       const char *text,
                       int text_len,
                       int start_from,
                       int *match_start,
                       int *match_end) {
    match_context ctx = {text, text_len, 0, 0, RE_MAX_STEPS};

    for (int i = start_from; i <= text_len; i++) {
        ctx.start_pos = i;
        ctx.steps = 0;
        int end_pos;
        if (match_node(&ctx, cp->root, i, &end_pos)) {
            *match_start = i;
            *match_end = end_pos;
            return true;
        }
    }
    return false;
}

/// @brief Public `find_match` exposed via `rt_regex_internal.h`.
///
/// Wraps the static helper for the cached-pattern API and any other
/// in-process consumer that holds a compiled pattern directly.
bool re_find_match(re_compiled_pattern *cp,
                   const char *text,
                   int text_len,
                   int start_from,
                   int *match_start,
                   int *match_end) {
    return find_match(cp, text, text_len, start_from, match_start, match_end);
}

//-----------------------------------------------------------------------------
// Capture Group Support
//-----------------------------------------------------------------------------

/// @brief Match context augmented with capture-group tracking arrays.
///
/// Pairs the standard text/length/start with caller-provided arrays
/// for group start/end positions. `next_group` is bumped each time a
/// `RE_GROUP` is entered so groups get the same index they would in
/// PCRE-style pattern numbering.
typedef struct {
    const char *text;
    int text_len;
    int start_pos;
    int *group_starts;
    int *group_ends;
    int max_groups;
    int next_group;
} match_context_groups;

// Forward declarations for group-capturing versions
static bool match_node_groups(match_context_groups *ctx, re_node *n, int pos, int *end_pos);

/// @brief Match a quantifier node carrying capture-group state.
///
/// Same logic as `match_quant` but uses the group-aware match recursion
/// so any captures inside the quantified subexpression are recorded.
/// Note: this version doesn't backtrack against the continuation —
/// capture groups inside `+`/`*` are best-effort (last successful match
/// wins) rather than fully PCRE-compliant.
static bool match_quant_groups(match_context_groups *ctx, re_node *n, int pos, int *end_pos) {
    re_node *child = n->data.quant.child;
    re_quant_type qtype = n->data.quant.qtype;
    bool greedy = n->data.quant.greedy;

    int min_count = (qtype == QUANT_PLUS) ? 1 : 0;
    int max_count = (qtype == QUANT_QUEST) ? 1 : INT32_MAX;

    int capacity = 0;
    int *match_ends = alloc_match_positions(ctx->text_len, pos, &capacity);

    int num_matches = 0;
    int cur_pos = pos;

    if (num_matches < capacity)
        match_ends[num_matches++] = pos;

    while (num_matches < capacity && num_matches - 1 < max_count) {
        int child_end;
        if (match_node_groups(ctx, child, cur_pos, &child_end)) {
            if (child_end == cur_pos)
                break;
            cur_pos = child_end;
            match_ends[num_matches++] = cur_pos;
        } else {
            break;
        }
    }

    bool found = false;
    if (greedy) {
        for (int i = num_matches - 1; i >= 0; i--) {
            if (i >= min_count) {
                *end_pos = match_ends[i];
                found = true;
                break;
            }
        }
    } else {
        for (int i = 0; i < num_matches; i++) {
            if (i >= min_count) {
                *end_pos = match_ends[i];
                found = true;
                break;
            }
        }
    }

    free(match_ends);
    return found;
}

/// @brief Group-tracking variant of `match_node`.
///
/// Same dispatch table as the non-group matcher, but the `RE_GROUP`
/// branch records `[start, end)` into `ctx->group_starts/ends` on
/// success. If a group fails to match, the `next_group` counter is
/// decremented so subsequent groups get the right index. No ReDoS
/// step counter here — typical group-capturing matches are bounded
/// in practice by the public API only running once per call.
static bool match_node_groups(match_context_groups *ctx, re_node *n, int pos, int *end_pos) {
    if (!ctx || !end_pos || pos < 0 || pos > ctx->text_len)
        return false;
    if (!n) {
        *end_pos = pos;
        return true;
    }

    switch (n->type) {
        case RE_LITERAL:
            if (pos < ctx->text_len && ctx->text[pos] == n->data.literal) {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_DOT:
            if (pos < ctx->text_len && ctx->text[pos] != '\n') {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_ANCHOR_START:
            if (pos == 0) {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_ANCHOR_END:
            if (pos == ctx->text_len) {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_CLASS:
            if (pos < ctx->text_len &&
                class_test(&n->data.char_class, (unsigned char)ctx->text[pos])) {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_CONCAT: {
            int cur_pos = pos;
            for (int i = 0; i < n->data.children.count; i++) {
                int child_end;
                if (!match_node_groups(ctx, n->data.children.children[i], cur_pos, &child_end)) {
                    return false;
                }
                cur_pos = child_end;
            }
            *end_pos = cur_pos;
            return true;
        }

        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++) {
                int child_end;
                if (match_node_groups(ctx, n->data.children.children[i], pos, &child_end)) {
                    *end_pos = child_end;
                    return true;
                }
            }
            return false;

        case RE_GROUP: {
            int group_idx = ctx->next_group++;
            int child_end = pos;
            bool matched = true;

            if (n->data.children.count > 0) {
                matched = match_node_groups(ctx, n->data.children.children[0], pos, &child_end);
            }

            if (matched && group_idx < ctx->max_groups) {
                ctx->group_starts[group_idx] = pos;
                ctx->group_ends[group_idx] = child_end;
            }

            if (matched) {
                *end_pos = child_end;
            } else {
                ctx->next_group--; // Revert group index
            }
            return matched;
        }

        case RE_QUANT:
            return match_quant_groups(ctx, n, pos, end_pos);
    }

    return false;
}

/// @brief Group-tracking version of `find_match`.
///
/// Same scan loop, but uses `match_node_groups` so capture-group
/// positions land in the caller-provided arrays. `*num_groups` is
/// set to the number of groups actually captured (may be less than
/// `max_groups` when the pattern doesn't reach all of them).
static bool find_match_groups(compiled_pattern *cp,
                              const char *text,
                              int text_len,
                              int start_from,
                              int *match_start,
                              int *match_end,
                              int *group_starts,
                              int *group_ends,
                              int max_groups,
                              int *num_groups) {
    match_context_groups ctx = {text, text_len, 0, group_starts, group_ends, max_groups, 0};

    for (int i = start_from; i <= text_len; i++) {
        ctx.start_pos = i;
        ctx.next_group = 0;
        int end_pos;
        if (match_node_groups(&ctx, cp->root, i, &end_pos)) {
            *match_start = i;
            *match_end = end_pos;
            *num_groups = ctx.next_group;
            return true;
        }
    }
    *num_groups = 0;
    return false;
}

/// @brief Public group-capturing find — exposed via `rt_regex_internal.h`.
///
/// Wraps `find_match_groups` for callers that hold a compiled pattern
/// directly (cached-pattern wrapper, replace-with-references helpers,
/// etc.).
bool re_find_match_with_groups(re_compiled_pattern *cp,
                               const char *text,
                               int text_len,
                               int start_from,
                               int *match_start,
                               int *match_end,
                               int *group_starts,
                               int *group_ends,
                               int max_groups,
                               int *num_groups) {
    return find_match_groups(cp,
                             text,
                             text_len,
                             start_from,
                             match_start,
                             match_end,
                             group_starts,
                             group_ends,
                             max_groups,
                             num_groups);
}
