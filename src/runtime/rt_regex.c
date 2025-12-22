//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_regex.c
/// @brief Regular expression pattern matching using backtracking.
///
/// Implements a subset of regex: literals, dot, anchors, character classes,
/// shorthand classes, quantifiers (greedy and non-greedy), groups, alternation.
///
/// NOT supported: backreferences, lookahead/lookbehind, named groups.
///
//===----------------------------------------------------------------------===//

#include "rt_regex.h"

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

/// Compiled pattern
typedef struct
{
    char *pattern_str;
    re_node *root;
    bool anchored_start; // Pattern starts with ^
    bool anchored_end;   // Pattern ends with $
} compiled_pattern;

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

    return cp;
}

//=============================================================================
// Pattern Matching Engine (Backtracking)
//=============================================================================

typedef struct
{
    const char *text;
    int text_len;
    int start_pos; // Start position for this match attempt
} match_context;

// Forward declaration
static bool match_node(match_context *ctx, re_node *n, int pos, int *end_pos);

/// Match a quantified node
static bool match_quant(match_context *ctx, re_node *n, int pos, int *end_pos)
{
    re_node *child = n->data.quant.child;
    re_quant_type qtype = n->data.quant.qtype;
    bool greedy = n->data.quant.greedy;

    int min_count = (qtype == QUANT_PLUS) ? 1 : 0;
    int max_count = (qtype == QUANT_QUEST) ? 1 : INT32_MAX;

    // Collect all possible match counts
    int *match_ends = (int *)malloc(sizeof(int) * (ctx->text_len - pos + 2));
    if (!match_ends)
        rt_trap("Pattern: memory allocation failed");

    int num_matches = 0;
    int cur_pos = pos;

    // First, collect positions for 0 matches
    match_ends[num_matches++] = pos;

    // Then collect all possible match extensions
    while (num_matches - 1 < max_count)
    {
        int child_end;
        if (match_node(ctx, child, cur_pos, &child_end))
        {
            // Avoid infinite loop on zero-width matches
            if (child_end == cur_pos)
            {
                // Zero-width match; only count once
                break;
            }
            cur_pos = child_end;
            match_ends[num_matches++] = cur_pos;
        }
        else
        {
            break;
        }
    }

    // Now try matches in order based on greediness
    bool found = false;
    if (greedy)
    {
        // Try longest match first
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
        // Try shortest match first
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

/// Try to match node at given position, return end position if successful
static bool match_node(match_context *ctx, re_node *n, int pos, int *end_pos)
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
                if (!match_node(ctx, n->data.children.children[i], cur_pos, &child_end))
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

/// Find a match anywhere in text, returning start and end positions
static bool find_match(compiled_pattern *cp,
                       const char *text,
                       int text_len,
                       int start_from,
                       int *match_start,
                       int *match_end)
{
    match_context ctx = {text, text_len, 0};

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
    // Look for existing pattern
    for (int i = 0; i < PATTERN_CACHE_SIZE; i++)
    {
        if (pattern_cache[i].pattern &&
            strcmp(pattern_cache[i].pattern->pattern_str, pattern_str) == 0)
        {
            pattern_cache[i].access_count = ++access_counter;
            return pattern_cache[i].pattern;
        }
    }

    // Compile new pattern
    compiled_pattern *cp = compile_pattern(pattern_str);

    // Find slot (empty or LRU)
    int slot = 0;
    unsigned long min_access = ULONG_MAX;
    for (int i = 0; i < PATTERN_CACHE_SIZE; i++)
    {
        if (!pattern_cache[i].pattern)
        {
            slot = i;
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

    return cp;
}

//=============================================================================
// Public API
//=============================================================================

bool rt_pattern_is_match(rt_string pattern, rt_string text)
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

rt_string rt_pattern_find(rt_string pattern, rt_string text)
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

rt_string rt_pattern_find_from(rt_string pattern, rt_string text, int64_t start)
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

int64_t rt_pattern_find_pos(rt_string pattern, rt_string text)
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

void *rt_pattern_find_all(rt_string pattern, rt_string text)
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

rt_string rt_pattern_replace(rt_string pattern, rt_string text, rt_string replacement)
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

rt_string rt_pattern_replace_first(rt_string pattern, rt_string text, rt_string replacement)
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

void *rt_pattern_split(rt_string pattern, rt_string text)
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
