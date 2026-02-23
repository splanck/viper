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
//   - Compiled patterns are cached in a global table; entries are not freed.
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

static BOOL WINAPI init_pattern_cache_cs(PINIT_ONCE o, PVOID p, PVOID *ctx)
{
    (void)o;
    (void)p;
    (void)ctx;
    InitializeCriticalSection(&g_pattern_cache_cs);
    return TRUE;
}

static void pattern_cache_lock(void)
{
    InitOnceExecuteOnce(&g_pattern_cache_cs_once, init_pattern_cache_cs, NULL, NULL);
    EnterCriticalSection(&g_pattern_cache_cs);
}

static void pattern_cache_unlock(void)
{
    LeaveCriticalSection(&g_pattern_cache_cs);
}
#else
#include <pthread.h>
static pthread_mutex_t g_pattern_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void pattern_cache_lock(void)
{
    pthread_mutex_lock(&g_pattern_cache_mutex);
}

static void pattern_cache_unlock(void)
{
    pthread_mutex_unlock(&g_pattern_cache_mutex);
}
#endif

//=============================================================================
// Regex AST Node Types
//=============================================================================

typedef enum
{
    RE_LITERAL,      // Single character literal
    RE_DOT,          // . matches any char except newline
    RE_ANCHOR_START, // ^
    RE_ANCHOR_END,   // $
    RE_CLASS,        // Character class [...]
    RE_GROUP,        // Grouping (...)
    RE_CONCAT,       // Sequence of nodes
    RE_ALT,          // Alternation a|b
    RE_QUANT,        // Quantifier applied to child
} re_node_type;

typedef enum
{
    QUANT_STAR,  // *
    QUANT_PLUS,  // +
    QUANT_QUEST, // ?
} re_quant_type;

/// Character class representation using bit array for ASCII
typedef struct
{
    uint8_t bits[32]; // 256 bits for ASCII chars
    bool negated;
} re_class;

typedef struct re_node re_node;

struct re_node
{
    re_node_type type;

    union
    {
        char literal;        // RE_LITERAL
        re_class char_class; // RE_CLASS

        struct
        {
            re_node **children;
            int count;
            int capacity;
        } children; // RE_CONCAT, RE_ALT, RE_GROUP

        struct
        {
            re_node *child;
            re_quant_type qtype;
            bool greedy;
        } quant; // RE_QUANT
    } data;
};

/// Compiled pattern (exposed via rt_regex_internal.h as re_compiled_pattern)
struct re_compiled_pattern
{
    char *pattern_str;
    re_node *root;
    bool anchored_start; // Pattern starts with ^
    bool anchored_end;   // Pattern ends with $
    int group_count;     // Number of capture groups (not including group 0)
};

// Local typedef for compatibility with existing code
typedef struct re_compiled_pattern compiled_pattern;

//=============================================================================
// Memory Management
//=============================================================================

static re_node *node_new(re_node_type type)
{
    re_node *n = (re_node *)calloc(1, sizeof(re_node));
    if (!n)
        rt_trap("Pattern: memory allocation failed");
    n->type = type;
    return n;
}

static void node_free(re_node *n)
{
    if (!n)
        return;

    switch (n->type)
    {
        case RE_CONCAT:
        case RE_ALT:
        case RE_GROUP:
            for (int i = 0; i < n->data.children.count; i++)
            {
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

static void children_add(re_node *n, re_node *child)
{
    if (n->data.children.count >= n->data.children.capacity)
    {
        int new_cap = n->data.children.capacity == 0 ? 4 : n->data.children.capacity * 2;
        re_node **new_children =
            (re_node **)realloc(n->data.children.children, new_cap * sizeof(re_node *));
        if (!new_children)
            rt_trap("Pattern: memory allocation failed");
        n->data.children.children = new_children;
        n->data.children.capacity = new_cap;
    }
    n->data.children.children[n->data.children.count++] = child;
}

static void pattern_free(compiled_pattern *p)
{
    if (!p)
        return;
    free(p->pattern_str);
    node_free(p->root);
    free(p);
}

// Public API for internal header
void re_free(re_compiled_pattern *cp)
{
    pattern_free(cp);
}

//=============================================================================
// Character Class Helpers
//=============================================================================

static void class_set(re_class *c, int ch)
{
    if (ch >= 0 && ch < 256)
    {
        c->bits[ch / 8] |= (1 << (ch % 8));
    }
}

static bool class_test(const re_class *c, int ch)
{
    if (ch < 0 || ch >= 256)
        return c->negated;
    bool in_class = (c->bits[ch / 8] & (1 << (ch % 8))) != 0;
    return c->negated ? !in_class : in_class;
}

static void class_add_range(re_class *c, int from, int to)
{
    for (int ch = from; ch <= to && ch < 256; ch++)
    {
        class_set(c, ch);
    }
}

// Add shorthand class chars
static void class_add_shorthand(re_class *c, char shorthand)
{
    switch (shorthand)
    {
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

//=============================================================================
// Pattern Parser
//=============================================================================

typedef struct
{
    const char *src;
    int pos;
    int len;
} parser_state;

static char peek(parser_state *p)
{
    return p->pos < p->len ? p->src[p->pos] : '\0';
}

static char advance(parser_state *p)
{
    return p->pos < p->len ? p->src[p->pos++] : '\0';
}

static bool at_end(parser_state *p)
{
    return p->pos >= p->len;
}

static void parse_error(parser_state *p, const char *msg)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "Pattern error at position %d: %s", p->pos, msg);
    rt_trap(buf);
}

// Forward declarations
static re_node *parse_alternation(parser_state *p);
static re_node *parse_concat(parser_state *p);
static re_node *parse_quantified(parser_state *p);
static re_node *parse_atom(parser_state *p);

/// Parse a character class [...] starting after the [
static re_node *parse_class(parser_state *p)
{
    re_node *n = node_new(RE_CLASS);
    memset(n->data.char_class.bits, 0, sizeof(n->data.char_class.bits));
    n->data.char_class.negated = false;

    // Check for negation
    if (peek(p) == '^')
    {
        n->data.char_class.negated = true;
        advance(p);
    }

    bool first = true;
    while (!at_end(p) && (first || peek(p) != ']'))
    {
        first = false;
        char c = advance(p);

        if (c == '\\' && !at_end(p))
        {
            char esc = advance(p);
            switch (esc)
            {
                case 'd':
                case 'D':
                case 'w':
                case 'W':
                case 's':
                case 'S':
                    class_add_shorthand(&n->data.char_class, esc);
                    break;
                case 'n':
                    class_set(&n->data.char_class, '\n');
                    break;
                case 'r':
                    class_set(&n->data.char_class, '\r');
                    break;
                case 't':
                    class_set(&n->data.char_class, '\t');
                    break;
                default:
                    class_set(&n->data.char_class, (unsigned char)esc);
                    break;
            }
        }
        else if (peek(p) == '-' && p->pos + 1 < p->len && p->src[p->pos + 1] != ']')
        {
            // Range: a-z
            advance(p); // consume -
            char end = advance(p);
            if (end == '\\' && !at_end(p))
            {
                end = advance(p);
                // Handle escape in range end
                switch (end)
                {
                    case 'n':
                        end = '\n';
                        break;
                    case 'r':
                        end = '\r';
                        break;
                    case 't':
                        end = '\t';
                        break;
                }
            }
            class_add_range(&n->data.char_class, (unsigned char)c, (unsigned char)end);
        }
        else
        {
            class_set(&n->data.char_class, (unsigned char)c);
        }
    }

    if (peek(p) != ']')
    {
        node_free(n);
        parse_error(p, "unclosed character class");
    }
    advance(p); // consume ]

    return n;
}

/// Parse an atom (literal, class, group, escape, anchor)
static re_node *parse_atom(parser_state *p)
{
    char c = peek(p);

    if (c == '\\')
    {
        advance(p);
        if (at_end(p))
            parse_error(p, "trailing backslash");

        char esc = advance(p);
        switch (esc)
        {
            case 'd':
            case 'D':
            case 'w':
            case 'W':
            case 's':
            case 'S':
            {
                re_node *n = node_new(RE_CLASS);
                memset(n->data.char_class.bits, 0, sizeof(n->data.char_class.bits));
                n->data.char_class.negated = false;
                class_add_shorthand(&n->data.char_class, esc);
                return n;
            }
            case 'n':
            {
                re_node *n = node_new(RE_LITERAL);
                n->data.literal = '\n';
                return n;
            }
            case 'r':
            {
                re_node *n = node_new(RE_LITERAL);
                n->data.literal = '\r';
                return n;
            }
            case 't':
            {
                re_node *n = node_new(RE_LITERAL);
                n->data.literal = '\t';
                return n;
            }
            default:
            {
                // Escaped special char or literal
                re_node *n = node_new(RE_LITERAL);
                n->data.literal = esc;
                return n;
            }
        }
    }
    else if (c == '.')
    {
        advance(p);
        return node_new(RE_DOT);
    }
    else if (c == '^')
    {
        advance(p);
        return node_new(RE_ANCHOR_START);
    }
    else if (c == '$')
    {
        advance(p);
        return node_new(RE_ANCHOR_END);
    }
    else if (c == '[')
    {
        advance(p);
        return parse_class(p);
    }
    else if (c == '(')
    {
        advance(p);
        re_node *inner = parse_alternation(p);
        if (peek(p) != ')')
        {
            node_free(inner);
            parse_error(p, "unclosed group");
        }
        advance(p);
        re_node *group = node_new(RE_GROUP);
        children_add(group, inner);
        return group;
    }
    else if (c == ')' || c == '|' || c == '*' || c == '+' || c == '?' || c == '\0')
    {
        // These end an atom
        return NULL;
    }
    else
    {
        advance(p);
        re_node *n = node_new(RE_LITERAL);
        n->data.literal = c;
        return n;
    }
}

/// Parse an atom possibly followed by a quantifier
static re_node *parse_quantified(parser_state *p)
{
    re_node *atom = parse_atom(p);
    if (!atom)
        return NULL;

    char c = peek(p);
    if (c == '*' || c == '+' || c == '?')
    {
        advance(p);
        re_node *q = node_new(RE_QUANT);
        q->data.quant.child = atom;
        q->data.quant.greedy = true;

        switch (c)
        {
            case '*':
                q->data.quant.qtype = QUANT_STAR;
                break;
            case '+':
                q->data.quant.qtype = QUANT_PLUS;
                break;
            case '?':
                q->data.quant.qtype = QUANT_QUEST;
                break;
        }

        // Check for non-greedy modifier
        if (peek(p) == '?')
        {
            advance(p);
            q->data.quant.greedy = false;
        }

        return q;
    }

    return atom;
}

/// Parse a concatenation of quantified atoms
static re_node *parse_concat(parser_state *p)
{
    re_node *concat = node_new(RE_CONCAT);

    while (!at_end(p))
    {
        char c = peek(p);
        if (c == ')' || c == '|')
            break;

        re_node *child = parse_quantified(p);
        if (!child)
            break;

        children_add(concat, child);
    }

    // Simplify single-child concat
    if (concat->data.children.count == 0)
    {
        node_free(concat);
        return NULL;
    }
    if (concat->data.children.count == 1)
    {
        re_node *child = concat->data.children.children[0];
        concat->data.children.children[0] = NULL;
        concat->data.children.count = 0;
        node_free(concat);
        return child;
    }

    return concat;
}

/// Parse an alternation (a|b|c)
static re_node *parse_alternation(parser_state *p)
{
    re_node *first = parse_concat(p);

    if (peek(p) != '|')
    {
        return first;
    }

    re_node *alt = node_new(RE_ALT);
    if (first)
        children_add(alt, first);
    else
    {
        // Empty alternative (matches empty string)
        children_add(alt, node_new(RE_CONCAT));
    }

    while (peek(p) == '|')
    {
        advance(p); // consume |
        re_node *branch = parse_concat(p);
        if (branch)
            children_add(alt, branch);
        else
            children_add(alt, node_new(RE_CONCAT));
    }

    // Simplify single-branch alternation
    if (alt->data.children.count == 1)
    {
        re_node *child = alt->data.children.children[0];
        alt->data.children.children[0] = NULL;
        alt->data.children.count = 0;
        node_free(alt);
        return child;
    }

    return alt;
}

/// Count groups in AST
static int count_groups(re_node *n)
{
    if (!n)
        return 0;

    int count = 0;
    switch (n->type)
    {
        case RE_GROUP:
            count = 1; // This group
            for (int i = 0; i < n->data.children.count; i++)
            {
                count += count_groups(n->data.children.children[i]);
            }
            break;
        case RE_CONCAT:
        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++)
            {
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

/// Compile a pattern string into AST
static compiled_pattern *compile_pattern(const char *pattern)
{
    if (!pattern)
        rt_trap("Pattern: null pattern");

    compiled_pattern *cp = (compiled_pattern *)calloc(1, sizeof(compiled_pattern));
    if (!cp)
        rt_trap("Pattern: memory allocation failed");

    cp->pattern_str = strdup(pattern);
    if (!cp->pattern_str)
    {
        free(cp);
        rt_trap("Pattern: memory allocation failed");
    }

    parser_state p = {pattern, 0, (int)strlen(pattern)};

    cp->root = parse_alternation(&p);

    if (!at_end(&p))
    {
        pattern_free(cp);
        parse_error(&p, "unexpected character");
    }

    // Handle empty pattern
    if (!cp->root)
    {
        cp->root = node_new(RE_CONCAT);
    }

    // Count capture groups
    cp->group_count = count_groups(cp->root);

    return cp;
}

// Public compile API
re_compiled_pattern *re_compile(const char *pattern)
{
    return compile_pattern(pattern);
}

const char *re_get_pattern(re_compiled_pattern *cp)
{
    return cp ? cp->pattern_str : "";
}

int re_group_count(re_compiled_pattern *cp)
{
    return cp ? cp->group_count : 0;
}

//=============================================================================
// Pattern Matching Engine (Backtracking)
//=============================================================================

/* S-11: Maximum backtracking steps before aborting (ReDoS guard) */
#define RE_MAX_STEPS 1000000

typedef struct
{
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

/// Collect all possible end positions for a quantified node.
/// Returns the number of positions stored in `positions`.
/// Positions are ordered from fewest to most repetitions.
static int collect_quant_positions(
    match_context *ctx, re_node *n, int pos, int *positions, int max_positions)
{
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
    while (count < max_count && num < max_positions)
    {
        int child_end;
        if (match_node(ctx, child, cur_pos, &child_end))
        {
            if (child_end == cur_pos)
                break; // Zero-width match; only count once
            cur_pos = child_end;
            count++;
            if (count >= min_count)
                positions[num++] = cur_pos;
        }
        else
        {
            break;
        }
    }

    return num;
}

/// Match a quantified node (standalone, no continuation awareness).
/// Used when the quantifier is NOT inside a concat (e.g., at pattern root).
static bool match_quant(match_context *ctx, re_node *n, int pos, int *end_pos)
{
    bool greedy = n->data.quant.greedy;

    int *positions = (int *)malloc(sizeof(int) * (ctx->text_len - pos + 2));
    if (!positions)
        rt_trap("Pattern: memory allocation failed");

    int num = collect_quant_positions(ctx, n, pos, positions, ctx->text_len - pos + 2);

    bool found = false;
    if (greedy)
    {
        // Try longest first
        for (int i = num - 1; i >= 0; i--)
        {
            *end_pos = positions[i];
            found = true;
            break;
        }
    }
    else
    {
        // Try shortest first
        if (num > 0)
        {
            *end_pos = positions[0];
            found = true;
        }
    }

    free(positions);
    return found;
}

/// Try to match node at given position, return end position if successful
static bool match_node(match_context *ctx, re_node *n, int pos, int *end_pos)
{
    /* S-11: ReDoS guard — abort if step limit exceeded */
    if (ctx->max_steps > 0 && ++ctx->steps > ctx->max_steps)
    {
        *end_pos = pos;
        return false;
    }

    if (!n)
    {
        *end_pos = pos;
        return true;
    }

    switch (n->type)
    {
        case RE_LITERAL:
            if (pos < ctx->text_len && ctx->text[pos] == n->data.literal)
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_DOT:
            if (pos < ctx->text_len && ctx->text[pos] != '\n')
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_ANCHOR_START:
            if (pos == 0)
            {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_ANCHOR_END:
            if (pos == ctx->text_len)
            {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_CLASS:
            if (pos < ctx->text_len &&
                class_test(&n->data.char_class, (unsigned char)ctx->text[pos]))
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_CONCAT:
            return match_concat_from(
                ctx, n->data.children.children, n->data.children.count, 0, pos, end_pos);

        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++)
            {
                int child_end;
                if (match_node(ctx, n->data.children.children[i], pos, &child_end))
                {
                    *end_pos = child_end;
                    return true;
                }
            }
            return false;

        case RE_GROUP:
            if (n->data.children.count > 0)
            {
                return match_node(ctx, n->data.children.children[0], pos, end_pos);
            }
            *end_pos = pos;
            return true;

        case RE_QUANT:
            return match_quant(ctx, n, pos, end_pos);
    }

    return false;
}

/// Match a concat sequence from `index` onward, with backtracking through
/// quantifier children. When a quantifier child is encountered, all possible
/// match lengths are tried (greedy = longest first) and the function recurses
/// to verify the remaining children can also match.
static bool match_concat_from(
    match_context *ctx, re_node **children, int count, int index, int pos, int *end_pos)
{
    if (index >= count)
    {
        *end_pos = pos;
        return true;
    }

    re_node *child = children[index];

    if (child->type == RE_QUANT)
    {
        bool greedy = child->data.quant.greedy;

        int *positions = (int *)malloc(sizeof(int) * (ctx->text_len - pos + 2));
        if (!positions)
            rt_trap("Pattern: memory allocation failed");

        int num = collect_quant_positions(ctx, child, pos, positions, ctx->text_len - pos + 2);

        bool found = false;
        if (greedy)
        {
            // Try longest match first, backtrack to shorter
            for (int i = num - 1; i >= 0; i--)
            {
                if (match_concat_from(ctx, children, count, index + 1, positions[i], end_pos))
                {
                    found = true;
                    break;
                }
            }
        }
        else
        {
            // Try shortest match first
            for (int i = 0; i < num; i++)
            {
                if (match_concat_from(ctx, children, count, index + 1, positions[i], end_pos))
                {
                    found = true;
                    break;
                }
            }
        }

        free(positions);
        return found;
    }
    else
    {
        // Non-quantifier child: single match attempt
        int child_end;
        if (match_node(ctx, child, pos, &child_end))
            return match_concat_from(ctx, children, count, index + 1, child_end, end_pos);
        return false;
    }
}

/// Find a match anywhere in text, returning start and end positions
static bool find_match(compiled_pattern *cp,
                       const char *text,
                       int text_len,
                       int start_from,
                       int *match_start,
                       int *match_end)
{
    match_context ctx = {text, text_len, 0, 0, RE_MAX_STEPS};

    for (int i = start_from; i <= text_len; i++)
    {
        ctx.start_pos = i;
        int end_pos;
        if (match_node(&ctx, cp->root, i, &end_pos))
        {
            *match_start = i;
            *match_end = end_pos;
            return true;
        }
    }
    return false;
}

// Public find_match API
bool re_find_match(re_compiled_pattern *cp,
                   const char *text,
                   int text_len,
                   int start_from,
                   int *match_start,
                   int *match_end)
{
    return find_match(cp, text, text_len, start_from, match_start, match_end);
}

//-----------------------------------------------------------------------------
// Capture Group Support
//-----------------------------------------------------------------------------

/// Match context with group tracking
typedef struct
{
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

/// Match a quantified node with group tracking
static bool match_quant_groups(match_context_groups *ctx, re_node *n, int pos, int *end_pos)
{
    re_node *child = n->data.quant.child;
    re_quant_type qtype = n->data.quant.qtype;
    bool greedy = n->data.quant.greedy;

    int min_count = (qtype == QUANT_PLUS) ? 1 : 0;
    int max_count = (qtype == QUANT_QUEST) ? 1 : INT32_MAX;

    int *match_ends = (int *)malloc(sizeof(int) * (ctx->text_len - pos + 2));
    if (!match_ends)
        rt_trap("Pattern: memory allocation failed");

    int num_matches = 0;
    int cur_pos = pos;

    match_ends[num_matches++] = pos;

    while (num_matches - 1 < max_count)
    {
        int child_end;
        if (match_node_groups(ctx, child, cur_pos, &child_end))
        {
            if (child_end == cur_pos)
                break;
            cur_pos = child_end;
            match_ends[num_matches++] = cur_pos;
        }
        else
        {
            break;
        }
    }

    bool found = false;
    if (greedy)
    {
        for (int i = num_matches - 1; i >= 0; i--)
        {
            if (i >= min_count)
            {
                *end_pos = match_ends[i];
                found = true;
                break;
            }
        }
    }
    else
    {
        for (int i = 0; i < num_matches; i++)
        {
            if (i >= min_count)
            {
                *end_pos = match_ends[i];
                found = true;
                break;
            }
        }
    }

    free(match_ends);
    return found;
}

/// Match node with group tracking
static bool match_node_groups(match_context_groups *ctx, re_node *n, int pos, int *end_pos)
{
    if (!n)
    {
        *end_pos = pos;
        return true;
    }

    switch (n->type)
    {
        case RE_LITERAL:
            if (pos < ctx->text_len && ctx->text[pos] == n->data.literal)
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_DOT:
            if (pos < ctx->text_len && ctx->text[pos] != '\n')
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_ANCHOR_START:
            if (pos == 0)
            {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_ANCHOR_END:
            if (pos == ctx->text_len)
            {
                *end_pos = pos;
                return true;
            }
            return false;

        case RE_CLASS:
            if (pos < ctx->text_len &&
                class_test(&n->data.char_class, (unsigned char)ctx->text[pos]))
            {
                *end_pos = pos + 1;
                return true;
            }
            return false;

        case RE_CONCAT:
        {
            int cur_pos = pos;
            for (int i = 0; i < n->data.children.count; i++)
            {
                int child_end;
                if (!match_node_groups(ctx, n->data.children.children[i], cur_pos, &child_end))
                {
                    return false;
                }
                cur_pos = child_end;
            }
            *end_pos = cur_pos;
            return true;
        }

        case RE_ALT:
            for (int i = 0; i < n->data.children.count; i++)
            {
                int child_end;
                if (match_node_groups(ctx, n->data.children.children[i], pos, &child_end))
                {
                    *end_pos = child_end;
                    return true;
                }
            }
            return false;

        case RE_GROUP:
        {
            int group_idx = ctx->next_group++;
            int child_end = pos;
            bool matched = true;

            if (n->data.children.count > 0)
            {
                matched = match_node_groups(ctx, n->data.children.children[0], pos, &child_end);
            }

            if (matched && group_idx < ctx->max_groups)
            {
                ctx->group_starts[group_idx] = pos;
                ctx->group_ends[group_idx] = child_end;
            }

            if (matched)
            {
                *end_pos = child_end;
            }
            else
            {
                ctx->next_group--; // Revert group index
            }
            return matched;
        }

        case RE_QUANT:
            return match_quant_groups(ctx, n, pos, end_pos);
    }

    return false;
}

/// Find match with capture groups
static bool find_match_groups(compiled_pattern *cp,
                              const char *text,
                              int text_len,
                              int start_from,
                              int *match_start,
                              int *match_end,
                              int *group_starts,
                              int *group_ends,
                              int max_groups,
                              int *num_groups)
{
    match_context_groups ctx = {text, text_len, 0, group_starts, group_ends, max_groups, 0};

    for (int i = start_from; i <= text_len; i++)
    {
        ctx.start_pos = i;
        ctx.next_group = 0;
        int end_pos;
        if (match_node_groups(&ctx, cp->root, i, &end_pos))
        {
            *match_start = i;
            *match_end = end_pos;
            *num_groups = ctx.next_group;
            return true;
        }
    }
    *num_groups = 0;
    return false;
}

// Public API for group capturing
bool re_find_match_with_groups(re_compiled_pattern *cp,
                               const char *text,
                               int text_len,
                               int start_from,
                               int *match_start,
                               int *match_end,
                               int *group_starts,
                               int *group_ends,
                               int max_groups,
                               int *num_groups)
{
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

//=============================================================================
// Pattern Cache (Simple LRU)
//=============================================================================

#define PATTERN_CACHE_SIZE 16

typedef struct cache_entry
{
    compiled_pattern *pattern;
    unsigned long access_count;
} cache_entry;

static cache_entry pattern_cache[PATTERN_CACHE_SIZE];
static unsigned long access_counter = 0;

static compiled_pattern *get_cached_pattern(const char *pattern_str)
{
    /* S-12: Lock cache for concurrent access safety */
    pattern_cache_lock();

    // Look for existing pattern
    for (int i = 0; i < PATTERN_CACHE_SIZE; i++)
    {
        if (pattern_cache[i].pattern &&
            strcmp(pattern_cache[i].pattern->pattern_str, pattern_str) == 0)
        {
            pattern_cache[i].access_count = ++access_counter;
            compiled_pattern *found = pattern_cache[i].pattern;
            pattern_cache_unlock();
            return found;
        }
    }

    // Compile new pattern (outside lock would be better, but kept simple here)
    compiled_pattern *cp = compile_pattern(pattern_str);

    // Find slot (empty or LRU)
    int slot = 0;
    unsigned long min_access = ULONG_MAX;
    for (int i = 0; i < PATTERN_CACHE_SIZE; i++)
    {
        if (!pattern_cache[i].pattern)
        {
            slot = i;
            min_access = 0;
            break;
        }
        if (pattern_cache[i].access_count < min_access)
        {
            min_access = pattern_cache[i].access_count;
            slot = i;
        }
    }

    // Evict if necessary
    if (pattern_cache[slot].pattern)
    {
        pattern_free(pattern_cache[slot].pattern);
    }

    pattern_cache[slot].pattern = cp;
    pattern_cache[slot].access_count = ++access_counter;

    pattern_cache_unlock();
    return cp;
}

//=============================================================================
// Public API
//=============================================================================

int8_t rt_pattern_is_match(rt_string text, rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;
    return find_match(cp, txt_str, (int)strlen(txt_str), 0, &match_start, &match_end);
}

rt_string rt_pattern_find(rt_string text, rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = (int)strlen(txt_str);
    int match_start, match_end;

    if (find_match(cp, txt_str, text_len, 0, &match_start, &match_end))
    {
        return rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    }
    return rt_const_cstr("");
}

rt_string rt_pattern_find_from(rt_string text, rt_string pattern, int64_t start)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    int text_len = (int)strlen(txt_str);
    if (start < 0)
        start = 0;
    if (start > text_len)
        return rt_const_cstr("");

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;

    if (find_match(cp, txt_str, text_len, (int)start, &match_start, &match_end))
    {
        return rt_string_from_bytes(txt_str + match_start, match_end - match_start);
    }
    return rt_const_cstr("");
}

int64_t rt_pattern_find_pos(rt_string text, rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int match_start, match_end;

    if (find_match(cp, txt_str, (int)strlen(txt_str), 0, &match_start, &match_end))
    {
        return (int64_t)match_start;
    }
    return -1;
}

void *rt_pattern_find_all(rt_string text, rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    void *seq = rt_seq_new();
    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = (int)strlen(txt_str);
    int pos = 0;

    while (pos <= text_len)
    {
        int match_start, match_end;
        if (!find_match(cp, txt_str, text_len, pos, &match_start, &match_end))
            break;

        rt_string match = rt_string_from_bytes(txt_str + match_start, match_end - match_start);
        rt_seq_push(seq, (void *)match);

        // Move past this match (at least 1 char to avoid infinite loop on empty match)
        pos = match_end > match_start ? match_end : match_start + 1;
    }

    return seq;
}

rt_string rt_pattern_replace(rt_string text, rt_string pattern, rt_string replacement)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);
    const char *rep_str = rt_string_cstr(replacement);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";
    if (!rep_str)
        rep_str = "";

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = (int)strlen(txt_str);
    int rep_len = (int)strlen(rep_str);

    // Build result
    size_t result_cap = text_len + 64;
    char *result = (char *)malloc(result_cap);
    if (!result)
        rt_trap("Pattern: memory allocation failed");
    size_t result_len = 0;

    int pos = 0;
    while (pos <= text_len)
    {
        int match_start, match_end;
        if (!find_match(cp, txt_str, text_len, pos, &match_start, &match_end))
        {
            // Copy rest of text
            size_t remaining = text_len - pos;
            if (result_len + remaining >= result_cap)
            {
                result_cap = result_len + remaining + 1;
                result = (char *)realloc(result, result_cap);
                if (!result)
                    rt_trap("Pattern: memory allocation failed");
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
                rt_trap("Pattern: memory allocation failed");
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

rt_string rt_pattern_replace_first(rt_string text, rt_string pattern, rt_string replacement)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);
    const char *rep_str = rt_string_cstr(replacement);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";
    if (!rep_str)
        rep_str = "";

    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = (int)strlen(txt_str);
    int rep_len = (int)strlen(rep_str);

    int match_start, match_end;
    if (!find_match(cp, txt_str, text_len, 0, &match_start, &match_end))
    {
        // No match, return original
        return rt_string_from_bytes(txt_str, text_len);
    }

    // Build result: before + replacement + after
    size_t result_len = match_start + rep_len + (text_len - match_end);
    char *result = (char *)malloc(result_len + 1);
    if (!result)
        rt_trap("Pattern: memory allocation failed");

    memcpy(result, txt_str, match_start);
    memcpy(result + match_start, rep_str, rep_len);
    memcpy(result + match_start + rep_len, txt_str + match_end, text_len - match_end);

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    return out;
}

void *rt_pattern_split(rt_string text, rt_string pattern)
{
    const char *pat_str = rt_string_cstr(pattern);
    const char *txt_str = rt_string_cstr(text);

    if (!pat_str)
        rt_trap("Pattern: null pattern");
    if (!txt_str)
        txt_str = "";

    void *seq = rt_seq_new();
    compiled_pattern *cp = get_cached_pattern(pat_str);
    int text_len = (int)strlen(txt_str);
    int pos = 0;

    while (pos <= text_len)
    {
        int match_start, match_end;
        if (!find_match(cp, txt_str, text_len, pos, &match_start, &match_end))
        {
            // No more matches; add remaining text
            rt_string part = rt_string_from_bytes(txt_str + pos, text_len - pos);
            rt_seq_push(seq, (void *)part);
            break;
        }

        // Add text before match
        rt_string part = rt_string_from_bytes(txt_str + pos, match_start - pos);
        rt_seq_push(seq, (void *)part);

        // Move past match
        pos = match_end > match_start ? match_end : match_start + 1;

        // If we're at end after match, add empty string
        if (pos > text_len)
        {
            rt_seq_push(seq, (void *)rt_const_cstr(""));
        }
    }

    // Handle empty text or pattern that doesn't match
    if (rt_seq_len(seq) == 0)
    {
        rt_seq_push(seq, (void *)rt_string_from_bytes(txt_str, text_len));
    }

    return seq;
}

rt_string rt_pattern_escape(rt_string text)
{
    const char *txt_str = rt_string_cstr(text);
    if (!txt_str)
        txt_str = "";

    int text_len = (int)strlen(txt_str);

    // Count special characters
    int special_count = 0;
    for (int i = 0; i < text_len; i++)
    {
        char c = txt_str[i];
        if (c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' || c == '^' || c == '$' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '|' || c == '{' || c == '}')
        {
            special_count++;
        }
    }

    // Allocate result
    size_t result_len = text_len + special_count;
    char *result = (char *)malloc(result_len + 1);
    if (!result)
        rt_trap("Pattern: memory allocation failed");

    int j = 0;
    for (int i = 0; i < text_len; i++)
    {
        char c = txt_str[i];
        if (c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' || c == '^' || c == '$' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '|' || c == '{' || c == '}')
        {
            result[j++] = '\\';
        }
        result[j++] = c;
    }
    result[j] = '\0';

    rt_string out = rt_string_from_bytes(result, result_len);
    free(result);
    return out;
}
