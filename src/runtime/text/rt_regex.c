//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_regex.c
// Purpose: Implements regular expression pattern matching for the Viper.Text.Regex
//          class using a backtracking NFA approach. Supports literals, '.', '^',
//          '$', character classes '[...]', shorthand classes (\d \w \s),
//          quantifiers (*, +, ?, {n,m}), non-greedy quantifiers (*?, +?, ??),
//          groups '()', and alternation '|'.
//
// Key invariants:
//   - Backreferences, lookahead, lookbehind, and named groups are NOT supported.
//   - Pattern compilation is cached (lock-protected) to amortize repeat use.
//   - FindAll returns all non-overlapping matches left-to-right.
//   - Replace replaces all non-overlapping matches with the replacement string.
//   - Anchors (^ $) are applied relative to the full input string.
//   - Character classes are byte-level; Unicode codepoints are not decomposed.
//
// Ownership/Lifetime:
//   - Compiled patterns are cached in a bounded LRU table (max 16 entries);
//     least-recently-used entries are freed on eviction.
//   - Returned match strings and sequences are fresh allocations owned by caller.
//
// Links: src/runtime/text/rt_regex.h (public API),
//        src/runtime/text/rt_regex_internal.h (compiled NFA node definitions),
//        src/runtime/text/rt_compiled_pattern.h (cached pre-compiled wrapper)
//
//===----------------------------------------------------------------------===//

#include "rt_regex.h"
#include "rt_regex_internal.h"

#include "rt_internal.h"
#include "rt_option.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S-12: Pattern cache lock — protect concurrent access */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static CRITICAL_SECTION g_pattern_cache_cs;
static INIT_ONCE g_pattern_cache_cs_once = INIT_ONCE_STATIC_INIT;

/// @brief One-shot initializer for the Win32 critical section guarding
///        the compiled-pattern cache. Called via `InitOnceExecuteOnce`.
static BOOL WINAPI init_pattern_cache_cs(PINIT_ONCE o, PVOID p, PVOID *ctx) {
    (void)o;
    (void)p;
    (void)ctx;
    InitializeCriticalSection(&g_pattern_cache_cs);
    return TRUE;
}

/// @brief Acquire the pattern-cache mutex (Windows path).
///
/// Lazily initializes the critical section on first call. Required
/// per S-12: concurrent regex calls must not race on the LRU cache.
static void pattern_cache_lock(void) {
    InitOnceExecuteOnce(&g_pattern_cache_cs_once, init_pattern_cache_cs, NULL, NULL);
    EnterCriticalSection(&g_pattern_cache_cs);
}

/// @brief Release the pattern-cache mutex (Windows path).
static void pattern_cache_unlock(void) {
    LeaveCriticalSection(&g_pattern_cache_cs);
}
#else
#include <pthread.h>
static pthread_mutex_t g_pattern_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/// @brief Acquire the pattern-cache mutex (POSIX path).
///
/// Uses a statically-initialized mutex — no init step needed.
static void pattern_cache_lock(void) {
    pthread_mutex_lock(&g_pattern_cache_mutex);
}

/// @brief Release the pattern-cache mutex (POSIX path).
static void pattern_cache_unlock(void) {
    pthread_mutex_unlock(&g_pattern_cache_mutex);
}
#endif

#include "rt_trap.h"

/// @brief Safely cast strlen() result to int, trapping on overflow.
static int safe_strlen_int(const char *s) {
    if (!s) {
        rt_trap("Pattern: null string");
        return 0;
    }
    size_t n = strlen(s);
    if (n > (size_t)INT_MAX) {
        rt_trap("Pattern: string too long for regex engine");
        return 0;
    }
    return (int)n;
}

static int safe_rt_string_len_int(rt_string s) {
    if (!s)
        return 0;
    int64_t n = rt_str_len(s);
    if (n < 0 || (uint64_t)n > (uint64_t)INT_MAX) {
        rt_trap("Pattern: string too long for regex engine");
        return 0;
    }
    return (int)n;
}

static const char *pattern_text_or_empty(rt_string text) {
    const char *cstr = text ? rt_string_cstr(text) : "";
    return cstr ? cstr : "";
}

static const char *pattern_required(rt_string pattern) {
    if (!pattern) {
        rt_trap("Pattern: null pattern");
        return "";
    }
    const char *cstr = rt_string_cstr(pattern);
    if (!cstr) {
        rt_trap("Pattern: invalid pattern string");
        return "";
    }
    return cstr;
}

static int ensure_result_capacity(
    char **result, size_t *result_cap, size_t result_len, size_t add, const char *trap_msg) {
    if (add > SIZE_MAX - result_len) {
        rt_trap(trap_msg);
        return 0;
    }
    size_t needed = result_len + add;
    if (needed < *result_cap)
        return 1;
    if (needed == SIZE_MAX) {
        rt_trap(trap_msg);
        return 0;
    }
    size_t new_cap = *result_cap;
    if (new_cap == 0)
        new_cap = 64;
    while (new_cap <= needed) {
        if (new_cap > SIZE_MAX / 2) {
            if (needed == SIZE_MAX) {
                rt_trap(trap_msg);
                return 0;
            }
            new_cap = needed + 1;
            break;
        }
        new_cap *= 2;
    }
    char *tmp = (char *)realloc(*result, new_cap);
    if (!tmp) {
        rt_trap("Pattern: memory allocation failed");
        return 0;
    }
    *result = tmp;
    *result_cap = new_cap;
    return 1;
}

//=============================================================================
// Memory Management
//=============================================================================

/// @brief Allocate a zero-initialized AST node of the given type.
///
/// Uses `calloc` so the union starts cleared (important for the
/// `children` variant where `count`/`capacity` must be 0). Traps on
/// OOM — there's no recovery path during pattern compile.
re_node *node_new(re_node_type type) {
    re_node *n = (re_node *)calloc(1, sizeof(re_node));
    if (!n) {
        rt_trap("Pattern: memory allocation failed");
        return NULL;
    }
    n->type = type;
    return n;
}

/// @brief Recursively free an AST node and all its descendants.
///
/// Walks the tree depth-first: container types (concat/alt/group) free
/// each child then their `children` array; quantifier nodes free their
/// single child; leaf types just free themselves. Safe on NULL.
void node_free(re_node *n) {
    if (!n)
        return;

    switch (n->type) {
        case RE_CONCAT:
        case RE_ALT:
        case RE_GROUP:
            for (int i = 0; i < n->data.children.count; i++) {
                node_free(n->data.children.children[i]);
            }
            free(n->data.children.children);
            break;
        case RE_QUANT:
            node_free(n->data.quant.child);
            break;
        default:
            break;
    }
    free(n);
}

/// @brief Append a child node to a container (concat/alt/group).
///
/// Geometric resize (cap doubles, starting at 4) so amortized cost is
/// O(1) per add. Traps on OOM. Caller transfers ownership of `child`
/// to `n` — the parent's `node_free` will reclaim it.
void children_add(re_node *n, re_node *child) {
    if (!n || !child) {
        rt_trap("Pattern: invalid child node");
        return;
    }
    if (n->data.children.count >= n->data.children.capacity) {
        if (n->data.children.capacity > INT_MAX / 2) {
            rt_trap("Pattern: too many child nodes");
            return;
        }
        int new_cap = n->data.children.capacity == 0 ? 4 : n->data.children.capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(re_node *)) {
            rt_trap("Pattern: child node allocation overflow");
            return;
        }
        re_node **new_children =
            (re_node **)realloc(n->data.children.children, new_cap * sizeof(re_node *));
        if (!new_children) {
            rt_trap("Pattern: memory allocation failed");
            return;
        }
        n->data.children.children = new_children;
        n->data.children.capacity = new_cap;
    }
    n->data.children.children[n->data.children.count++] = child;
}

/// @brief Free a compiled pattern and its AST.
///
/// Releases the duplicated pattern string, recursively frees the AST
/// root, then frees the wrapper. Safe on NULL — used both by the
/// cache eviction path and by error-recovery paths during compile.
static void pattern_free(compiled_pattern *p) {
    if (!p)
        return;
    free(p->pattern_str);
    node_free(p->root);
    free(p);
}

/// @brief Public free entry point exposed via `rt_regex_internal.h`.
///
/// Thin wrapper around `pattern_free` so external callers (e.g., the
/// cached-pattern wrapper) don't need to see the static helper.
void re_free(re_compiled_pattern *cp) {
    pattern_free(cp);
}

//=============================================================================
// Character Class Helpers
//=============================================================================

/// @brief Set bit `ch` in a character-class bitset (no-op out of range).
///
/// The bitset is 256 bits (32 bytes), one per ASCII byte value. Bytes
/// outside [0, 255] are ignored — matching of multibyte/Unicode chars
/// happens via the negation flag in `class_test`.
void class_set(re_class *c, int ch) {
    if (ch >= 0 && ch < 256) {
        c->bits[ch / 8] |= (1 << (ch % 8));
    }
}

/// @brief Test whether `ch` is in the class (after applying negation).
///
/// Bytes outside [0, 255] match if and only if the class is negated —
/// preserves the "negated class accepts everything not explicitly listed"
/// semantics for arbitrary code units.
bool class_test(const re_class *c, int ch) {
    if (ch < 0 || ch >= 256)
        return c->negated;
    bool in_class = (c->bits[ch / 8] & (1 << (ch % 8))) != 0;
    return c->negated ? !in_class : in_class;
}

/// @brief Set every bit in the inclusive range `[from, to]`.
void class_add_range(re_class *c, int from, int to) {
    for (int ch = from; ch <= to && ch < 256; ch++) {
        class_set(c, ch);
    }
}

/// @brief Apply a `\\d`/`\\D`/`\\w`/`\\W`/`\\s`/`\\S` shorthand to a class.
///
/// Lowercase shorthands set the matching characters; uppercase variants
/// also flip the `negated` flag so the class matches the complement.
/// Used both inside `[...]` brackets and as standalone atoms.
void class_add_shorthand(re_class *c, char shorthand) {
    switch (shorthand) {
        case 'd': // digits
            class_add_range(c, '0', '9');
            break;
        case 'D': // non-digits
            class_add_range(c, '0', '9');
            c->negated = !c->negated;
            break;
        case 'w': // word chars
            class_add_range(c, 'a', 'z');
            class_add_range(c, 'A', 'Z');
            class_add_range(c, '0', '9');
            class_set(c, '_');
            break;
        case 'W': // non-word chars
            class_add_range(c, 'a', 'z');
            class_add_range(c, 'A', 'Z');
            class_add_range(c, '0', '9');
            class_set(c, '_');
            c->negated = !c->negated;
            break;
        case 's': // whitespace
            class_set(c, ' ');
            class_set(c, '\t');
            class_set(c, '\n');
            class_set(c, '\r');
            class_set(c, '\f');
            class_set(c, '\v');
            break;
        case 'S': // non-whitespace
            class_set(c, ' ');
            class_set(c, '\t');
            class_set(c, '\n');
            class_set(c, '\r');
            class_set(c, '\f');
            class_set(c, '\v');
            c->negated = !c->negated;
            break;
    }
}

/// @brief Count `RE_GROUP` nodes in a subtree (excluding the implicit group 0).
///
/// Used after parse to populate `cp->group_count` so callers can size
/// match-result arrays correctly. Recurses through every container
/// kind so nested groups are tallied.
static int count_groups(re_node *n) {
    if (!n)
        return 0;

    int count = 0;
    switch (n->type) {
        case RE_GROUP:
            count = 1; // This group
            for (int i = 0; i < n->data.children.count; i++) {
                count += count_groups(n->data.children.children[i]);
            }
            break;
        case RE_CONCAT:
        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++) {
                count += count_groups(n->data.children.children[i]);
            }
            break;
        case RE_QUANT:
            count = count_groups(n->data.quant.child);
            break;
        default:
            break;
    }
    return count;
}

/// @brief Top-level compile: parse `pattern` into a `compiled_pattern`.
///
/// Allocates the wrapper, duplicates the pattern source for diagnostics
/// and cache lookups, runs the parser, and counts capture groups. Traps
/// on syntax error (via `parse_error`) or OOM. Empty patterns are
/// represented as an empty concat (matches everywhere with zero width).
static compiled_pattern *compile_pattern(const char *pattern) {
    if (!pattern) {
        rt_trap("Pattern: null pattern");
        return NULL;
    }

    compiled_pattern *cp = (compiled_pattern *)calloc(1, sizeof(compiled_pattern));
    if (!cp) {
        rt_trap("Pattern: memory allocation failed");
        return NULL;
    }

    cp->pattern_str = strdup(pattern);
    if (!cp->pattern_str) {
        free(cp);
        rt_trap("Pattern: memory allocation failed");
        return NULL;
    }

    parser_state p = {pattern, 0, safe_strlen_int(pattern)};

    cp->root = parse_alternation(&p);

    if (!at_end(&p)) {
        pattern_free(cp);
        parse_error(&p, "unexpected character");
        return NULL;
    }

    // Handle empty pattern
    if (!cp->root) {
        cp->root = node_new(RE_CONCAT);
    }

    // Count capture groups
    cp->group_count = count_groups(cp->root);

    return cp;
}

/// @brief Public compile entry point — exposed via `rt_regex_internal.h`.
///
/// Wraps the static `compile_pattern` so external callers (the cached
/// pattern wrapper) don't need access to the static helper.
re_compiled_pattern *re_compile(const char *pattern) {
    return compile_pattern(pattern);
}

/// @brief Return the source pattern string a compiled pattern was built from.
///
/// Returns "" for NULL. Useful for cache lookup and diagnostics.
const char *re_get_pattern(re_compiled_pattern *cp) {
    return cp ? cp->pattern_str : "";
}

/// @brief Return the number of capture groups in the compiled pattern.
///
/// Counts only explicit `(...)` groups; group 0 (the whole match) is
/// not included. Returns 0 for NULL.
int re_group_count(re_compiled_pattern *cp) {
    return cp ? cp->group_count : 0;
}

//=============================================================================
// Pattern Cache (Simple LRU)
//=============================================================================

#define PATTERN_CACHE_SIZE 16

typedef struct cache_entry {
    compiled_pattern *pattern;
    unsigned long access_count;
} cache_entry;

static cache_entry pattern_cache[PATTERN_CACHE_SIZE];
static unsigned long access_counter = 0;

static compiled_pattern *get_cached_pattern(const char *pattern_str) {
    pattern_cache_lock();

    for (int i = 0; i < PATTERN_CACHE_SIZE; i++) {
        if (pattern_cache[i].pattern &&
            strcmp(pattern_cache[i].pattern->pattern_str, pattern_str) == 0) {
            pattern_cache[i].access_count = ++access_counter;
            compiled_pattern *found = pattern_cache[i].pattern;
            found->cache_refs++;
            pattern_cache_unlock();
            return found;
        }
    }

    pattern_cache_unlock();
    compiled_pattern *cp = compile_pattern(pattern_str);
    if (!cp)
        return NULL;
    cp->cache_refs = 1;
    cp->cache_linked = false;

    pattern_cache_lock();

    for (int i = 0; i < PATTERN_CACHE_SIZE; i++) {
        if (pattern_cache[i].pattern &&
            strcmp(pattern_cache[i].pattern->pattern_str, pattern_str) == 0) {
            pattern_cache[i].access_count = ++access_counter;
            compiled_pattern *found = pattern_cache[i].pattern;
            found->cache_refs++;
            pattern_cache_unlock();
            pattern_free(cp);
            return found;
        }
    }

    int slot = -1;
    unsigned long min_access = ULONG_MAX;
    for (int i = 0; i < PATTERN_CACHE_SIZE; i++) {
        if (!pattern_cache[i].pattern) {
            slot = i;
            break;
        }
        if (pattern_cache[i].pattern->cache_refs == 0 &&
            pattern_cache[i].access_count < min_access) {
            min_access = pattern_cache[i].access_count;
            slot = i;
        }
    }

    if (slot < 0) {
        pattern_cache_unlock();
        return cp;
    }

    if (pattern_cache[slot].pattern) {
        pattern_cache[slot].pattern->cache_linked = false;
        pattern_free(pattern_cache[slot].pattern);
    }

    cp->cache_linked = true;
    pattern_cache[slot].pattern = cp;
    pattern_cache[slot].access_count = ++access_counter;

    pattern_cache_unlock();
    return cp;
}

static void release_cached_pattern(compiled_pattern *cp) {
    if (!cp)
        return;
    bool should_free = false;
    pattern_cache_lock();
    if (cp->cache_refs > 0)
        cp->cache_refs--;
    should_free = (cp->cache_refs == 0 && !cp->cache_linked);
    pattern_cache_unlock();
    if (should_free)
        pattern_free(cp);
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Test whether a regex pattern matches anywhere in the text.
int8_t rt_pattern_is_match(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);
    int text_len = safe_rt_string_len_int(text);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    int8_t matched = re_find_match(cp, txt_str, text_len, 0, &match_start, &match_end) ? 1 : 0;
    release_cached_pattern(cp);
    return matched;
}

/// @brief Find the first match of a regex pattern in the text (empty string if no match).
rt_string rt_pattern_find(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int match_start, match_end;
    rt_string result;

    if (re_find_match(cp, txt_str, text_len, 0, &match_start, &match_end)) {
        result = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    } else {
        result = rt_const_cstr("");
    }
    release_cached_pattern(cp);
    return result;
}

/// @brief Find the first regex match and return it as an Option string.
/// @details This sentinel-free variant returns `SomeStr(match)` for any match,
///          including a valid empty-string match, and `None` when no match
///          exists. Invalid pattern syntax still traps like @ref rt_pattern_find.
/// @param text Text to search.
/// @param pattern Regex pattern string.
/// @return Opaque Viper.Option containing the first match, or None.
void *rt_pattern_find_option(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int match_start, match_end;
    void *option = NULL;

    if (re_find_match(cp, txt_str, text_len, 0, &match_start, &match_end)) {
        rt_string match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        option = rt_option_some_str(match);
        rt_str_release_maybe(match);
    } else {
        option = rt_option_none();
    }
    release_cached_pattern(cp);
    return option;
}

/// @brief Find the first match starting at or after the given byte offset.
rt_string rt_pattern_find_from(rt_string text, rt_string pattern, int64_t start) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    int text_len = safe_rt_string_len_int(text);
    if (start < 0)
        start = 0;
    if (start > text_len)
        return rt_const_cstr("");

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    rt_string result;

    if (re_find_match(cp, txt_str, text_len, (int)start, &match_start, &match_end)) {
        result = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    } else {
        result = rt_const_cstr("");
    }
    release_cached_pattern(cp);
    return result;
}

/// @brief Find the first regex match at or after a byte offset as an Option string.
/// @details Negative starts are clamped to zero. A start beyond the text length
///          returns None. Empty matches are preserved as `SomeStr("")`.
/// @param text Text to search.
/// @param pattern Regex pattern string.
/// @param start Starting byte offset.
/// @return Opaque Viper.Option containing the first match, or None.
void *rt_pattern_find_from_option(rt_string text, rt_string pattern, int64_t start) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    int text_len = safe_rt_string_len_int(text);
    if (start < 0)
        start = 0;
    if (start > text_len)
        return rt_option_none();

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    void *option = NULL;

    if (re_find_match(cp, txt_str, text_len, (int)start, &match_start, &match_end)) {
        rt_string match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        option = rt_option_some_str(match);
        rt_str_release_maybe(match);
    } else {
        option = rt_option_none();
    }
    release_cached_pattern(cp);
    return option;
}

/// @brief Find the byte position of the first match (-1 if no match).
int64_t rt_pattern_find_pos(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    int64_t result = -1;

    if (re_find_match(cp, txt_str, safe_rt_string_len_int(text), 0, &match_start, &match_end))
        result = (int64_t)match_start;
    release_cached_pattern(cp);
    return result;
}

/// @brief Find the byte position of the first regex match as an Option index.
/// @param text Text to search.
/// @param pattern Regex pattern string.
/// @return Opaque Viper.Option containing the byte index, or None.
void *rt_pattern_find_pos_option(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    void *option = NULL;

    if (re_find_match(cp, txt_str, safe_rt_string_len_int(text), 0, &match_start, &match_end))
        option = rt_option_some_i64((int64_t)match_start);
    else
        option = rt_option_none();
    release_cached_pattern(cp);
    return option;
}

/// @brief Find all non-overlapping matches and return them as a sequence of strings.
void *rt_pattern_find_all(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int pos = 0;

    while (pos <= text_len) {
        int match_start, match_end;
        if (!re_find_match(cp, txt_str, text_len, pos, &match_start, &match_end))
            break;

        rt_string match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        rt_seq_push(seq, (void *)match);
        rt_string_unref(match);

        // Move past this match (at least 1 char to avoid infinite loop on empty match)
        pos = match_end > match_start ? match_end : match_start + 1;
    }

    release_cached_pattern(cp);
    return seq;
}

/// @brief Replace all matches of a regex pattern with the replacement string.
rt_string rt_pattern_replace(rt_string text, rt_string pattern, rt_string replacement) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);
    const char *rep_str = pattern_text_or_empty(replacement);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int rep_len = safe_rt_string_len_int(replacement);

    // Build result
    size_t result_cap = (size_t)text_len + 64;
    if (result_cap < (size_t)text_len) {
        rt_trap("Pattern: replacement length overflow");
        release_cached_pattern(cp);
        return rt_string_from_bytes("", 0);
    }
    char *result = (char *)malloc(result_cap);
    if (!result) {
        rt_trap("Pattern: memory allocation failed");
        release_cached_pattern(cp);
        return rt_string_from_bytes("", 0);
    }
    size_t result_len = 0;

    int pos = 0;
    while (pos <= text_len) {
        int match_start, match_end;
        if (!re_find_match(cp, txt_str, text_len, pos, &match_start, &match_end)) {
            // Copy rest of text
            size_t remaining = text_len - pos;
            if (!ensure_result_capacity(&result,
                                        &result_cap,
                                        result_len,
                                        remaining,
                                        "Pattern: replacement length overflow")) {
                free(result);
                release_cached_pattern(cp);
                return rt_string_from_bytes("", 0);
            }
            memcpy(result + result_len, txt_str + pos, remaining);
            result_len += remaining;
            break;
        }

        // Copy text before match
        size_t before_len = match_start - pos;
        if ((size_t)rep_len > SIZE_MAX - before_len) {
            free(result);
            release_cached_pattern(cp);
            rt_trap("Pattern: replacement length overflow");
            return rt_string_from_bytes("", 0);
        }
        if (!ensure_result_capacity(&result,
                                    &result_cap,
                                    result_len,
                                    before_len + (size_t)rep_len,
                                    "Pattern: replacement length overflow")) {
            free(result);
            release_cached_pattern(cp);
            return rt_string_from_bytes("", 0);
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
    release_cached_pattern(cp);
    return out;
}

/// @brief Replace only the first match of a regex pattern with the replacement string.
rt_string rt_pattern_replace_first(rt_string text, rt_string pattern, rt_string replacement) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);
    const char *rep_str = pattern_text_or_empty(replacement);

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int rep_len = safe_rt_string_len_int(replacement);

    int match_start, match_end;
    if (!re_find_match(cp, txt_str, text_len, 0, &match_start, &match_end)) {
        // No match, return original
        rt_string out = rt_string_from_bytes(txt_str, text_len);
        release_cached_pattern(cp);
        return out;
    }

    // Build result: before + replacement + after
    size_t result_len = (size_t)match_start;
    if ((size_t)rep_len > SIZE_MAX - result_len ||
        (size_t)(text_len - match_end) > SIZE_MAX - result_len - (size_t)rep_len) {
        release_cached_pattern(cp);
        rt_trap("Pattern: replacement length overflow");
        return rt_string_from_bytes("", 0);
    }
    result_len += (size_t)rep_len + (size_t)(text_len - match_end);
    if (result_len == SIZE_MAX) {
        release_cached_pattern(cp);
        rt_trap("Pattern: replacement length overflow");
        return rt_string_from_bytes("", 0);
    }
    char *result = (char *)malloc(result_len + 1);
    if (!result) {
        rt_trap("Pattern: memory allocation failed");
        release_cached_pattern(cp);
        return rt_string_from_bytes("", 0);
    }

    memcpy(result, txt_str, match_start);
    memcpy(result + match_start, rep_str, rep_len);
    memcpy(result + match_start + rep_len, txt_str + match_end, text_len - match_end);

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    release_cached_pattern(cp);
    return out;
}

/// @brief Split a string by a regex pattern, returning a sequence of substrings.
void *rt_pattern_split(rt_string text, rt_string pattern) {
    const char *pat_str = pattern_required(pattern);
    const char *txt_str = pattern_text_or_empty(text);

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = safe_rt_string_len_int(text);
    int pos = 0;

    while (pos <= text_len) {
        int match_start, match_end;
        if (!re_find_match(cp, txt_str, text_len, pos, &match_start, &match_end)) {
            // No more matches; add remaining text
            rt_string part = rt_string_from_bytes(txt_str + pos, text_len - pos);
            rt_seq_push(seq, (void *)part);
            rt_string_unref(part);
            break;
        }

        // Add text before match
        rt_string part = rt_string_from_bytes(txt_str + pos, match_start - pos);
        rt_seq_push(seq, (void *)part);
        rt_string_unref(part);

        // Move past match
        pos = match_end > match_start ? match_end : match_start + 1;

        // If we're at end after match, add empty string
        if (pos > text_len) {
            rt_string empty = rt_const_cstr("");
            rt_seq_push(seq, (void *)empty);
            rt_string_unref(empty);
        }
    }

    // Handle empty text or pattern that doesn't match
    if (rt_seq_len(seq) == 0) {
        rt_string part = rt_string_from_bytes(txt_str, text_len);
        rt_seq_push(seq, (void *)part);
        rt_string_unref(part);
    }

    release_cached_pattern(cp);
    return seq;
}

/// @brief Escape all regex metacharacters in a string so it matches literally.
rt_string rt_pattern_escape(rt_string text) {
    const char *txt_str = pattern_text_or_empty(text);

    int text_len = safe_rt_string_len_int(text);

    // Count special characters
    size_t special_count = 0;
    for (int i = 0; i < text_len; i++) {
        char c = txt_str[i];
        if (c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' || c == '^' || c == '$' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '|' || c == '{' || c == '}') {
            if (special_count == SIZE_MAX) {
                rt_trap("Pattern: escape length overflow");
                return rt_string_from_bytes("", 0);
            }
            special_count++;
        }
    }

    // Allocate result
    if ((size_t)text_len > SIZE_MAX - special_count) {
        rt_trap("Pattern: escape length overflow");
        return rt_string_from_bytes("", 0);
    }
    size_t result_len = (size_t)text_len + special_count;
    if (result_len == SIZE_MAX) {
        rt_trap("Pattern: escape length overflow");
        return rt_string_from_bytes("", 0);
    }
    char *result = (char *)malloc(result_len + 1);
    if (!result) {
        rt_trap("Pattern: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t j = 0;
    for (int i = 0; i < text_len; i++) {
        char c = txt_str[i];
        if (c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' || c == '^' || c == '$' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '|' || c == '{' || c == '}') {
            result[j++] = '\\';
        }
        result[j++] = c;
    }
    result[j] = '\0';

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    return out;
}
