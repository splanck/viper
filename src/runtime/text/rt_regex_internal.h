//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_regex_internal.h
// Purpose: Internal regex engine types and NFA/DFA state structures shared between rt_regex.c and
// rt_compiled_pattern.c, not part of the public API.
//
// Key invariants:
//   - This header is internal; it must not be included by code outside the text/ directory.
//   - Defines the compiled NFA state representation and matching engine entry points.
//   - rt_regex_compile_internal is the shared compilation entry point.
//   - rt_regex_exec_internal runs the NFA on a subject string.
//
// Ownership/Lifetime:
//   - Compiled regex objects are owned by their enclosing rt_regex or rt_compiled_pattern.
//   - No direct public ownership semantics; accessed only through the wrapper APIs.
//
// Links: src/runtime/text/rt_regex.c, src/runtime/text/rt_compiled_pattern.c (internal users)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Forward declaration of internal compiled pattern
typedef struct re_compiled_pattern re_compiled_pattern;

/// @brief Compile a pattern string into an internal representation.
/// @param pattern The regex pattern string.
/// @return Compiled pattern, or traps on error.
re_compiled_pattern *re_compile(const char *pattern);

/// @brief Free a compiled pattern.
/// @param cp The compiled pattern to free.
void re_free(re_compiled_pattern *cp);

/// @brief Get the pattern string from a compiled pattern.
/// @param cp The compiled pattern.
/// @return The original pattern string.
const char *re_get_pattern(re_compiled_pattern *cp);

/// @brief Find a match in text, returning start and end positions.
/// @param cp Compiled pattern.
/// @param text Text to search.
/// @param text_len Length of text.
/// @param start_from Position to start searching from.
/// @param match_start Output: start position of match.
/// @param match_end Output: end position of match.
/// @return true if match found.
bool re_find_match(re_compiled_pattern *cp,
                   const char *text,
                   int text_len,
                   int start_from,
                   int *match_start,
                   int *match_end);

/// @brief Find a match and capture groups.
/// @param cp Compiled pattern.
/// @param text Text to search.
/// @param text_len Length of text.
/// @param start_from Position to start searching from.
/// @param match_start Output: start position of full match.
/// @param match_end Output: end position of full match.
/// @param group_starts Output array for group start positions (must be pre-allocated).
/// @param group_ends Output array for group end positions (must be pre-allocated).
/// @param max_groups Maximum number of groups to capture.
/// @param num_groups Output: actual number of groups captured.
/// @return true if match found.
bool re_find_match_with_groups(re_compiled_pattern *cp,
                               const char *text,
                               int text_len,
                               int start_from,
                               int *match_start,
                               int *match_end,
                               int *group_starts,
                               int *group_ends,
                               int max_groups,
                               int *num_groups);

/// @brief Get number of capture groups in pattern.
/// @param cp Compiled pattern.
/// @return Number of capture groups (not including group 0).
int re_group_count(re_compiled_pattern *cp);

//=============================================================================
// Engine internals
//
// AST node types and primitive constructors shared between the core
// (rt_regex.c), the parser (rt_regex_parse.c), and the matcher
// (rt_regex_match.c). These are engine-private; rt_compiled_pattern.c
// includes this header for the re_* API above and simply ignores them.
//=============================================================================

typedef enum {
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

typedef enum {
    QUANT_STAR,  // *
    QUANT_PLUS,  // +
    QUANT_QUEST, // ?
} re_quant_type;

/// Character class representation using bit array for ASCII
typedef struct {
    uint8_t bits[32]; // 256 bits for ASCII chars
    bool negated;
} re_class;

typedef struct re_node re_node;

struct re_node {
    re_node_type type;
    int group_index; // RE_GROUP: lexical capture index (order of '('), else -1

    union {
        char literal;        // RE_LITERAL
        re_class char_class; // RE_CLASS

        struct {
            re_node **children;
            int count;
            int capacity;
        } children; // RE_CONCAT, RE_ALT, RE_GROUP

        struct {
            re_node *child;
            re_quant_type qtype;
            bool greedy;
        } quant; // RE_QUANT
    } data;
};

/// Compiled pattern (forward-declared above as re_compiled_pattern)
struct re_compiled_pattern {
    char *pattern_str;
    re_node *root;
    bool anchored_start; // Pattern starts with ^
    bool anchored_end;   // Pattern ends with $
    int group_count;     // Number of capture groups (not including group 0)
    unsigned int cache_refs;
    bool cache_linked;
};

// Local typedef for compatibility with existing code
typedef struct re_compiled_pattern compiled_pattern;

/// Parser cursor over a pattern source string.
typedef struct {
    const char *src;
    int pos;
    int len;
    int group_counter; // next lexical capture-group index
} parser_state;

// AST node/class primitives (defined in rt_regex.c).
re_node *node_new(re_node_type type);
void node_free(re_node *n);
void children_add(re_node *n, re_node *child);
void class_set(re_class *c, int ch);
bool class_test(const re_class *c, int ch);
void class_add_range(re_class *c, int from, int to);
void class_add_shorthand(re_class *c, char shorthand);

// Parser entry points (defined in rt_regex_parse.c).
re_node *parse_alternation(parser_state *p);
bool at_end(parser_state *p);
void parse_error(parser_state *p, const char *msg);

#ifdef __cplusplus
}
#endif
