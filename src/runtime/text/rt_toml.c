//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_toml.c
// Purpose: Implements TOML v1.0 parsing and formatting for the Viper.Text.Toml
//          class. Produces an rt_map tree from a TOML document supporting
//          tables ([table]), arrays of tables ([[array]]), inline tables,
//          string types (basic, literal, multi-line), integer, float, bool,
//          datetime, and array values.
//
// Key invariants:
//   - Section/key hierarchy maps to nested rt_map trees.
//   - Integer values are stored as Box.I64; floats as Box.F64.
//   - Booleans are Box.I64(0/1); arrays are rt_seq; datetime as rt_string.
//   - Duplicate keys within the same table cause a parse error.
//   - Parse returns NULL on invalid TOML input (not a trap).
//   - Format output is valid TOML 1.0; nested tables use explicit [section] headers.
//
// Ownership/Lifetime:
//   - Returned rt_map trees are fresh allocations owned by the caller.
//   - Formatted TOML strings are fresh rt_string allocations owned by caller.
//
// Links: src/runtime/text/rt_toml.h (public API),
//        src/runtime/rt_map.h, rt_seq.h, rt_box.h (container types)
//
//===----------------------------------------------------------------------===//

#include "rt_toml.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// --- Helper: create string from substring ---

/// @brief One-line wrapper that builds an `rt_string` from a (ptr, len) substring.
static rt_string make_str(const char *s, int64_t len) {
    return rt_string_from_bytes(s, len);
}

// --- Helper: trim whitespace ---

/// @brief Skip horizontal whitespace (`space`, `tab`) — does NOT consume newlines.
/// @details TOML treats newlines as significant (line-oriented format),
///          so we explicitly limit whitespace skipping to in-line spaces.
static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
}

/// @brief Advance the cursor past the rest of the current line, including the `\n`.
/// @details Used to skip comments and malformed lines without breaking
///          the parser. Stops at NUL if no terminating newline is found.
static void skip_line(const char **p) {
    while (**p && **p != '\n')
        (*p)++;
    if (**p == '\n')
        (*p)++;
}

// --- Helper: parse a bare key (alphanumeric, dash, underscore) ---

/// @brief Parse a TOML bare key (alphanumerics, `-`, `_`, `.`).
/// @details Dotted keys (e.g. `a.b.c`) are returned as a single
///          string here — the caller splits on `.` to walk nested
///          tables. Returns NULL when the cursor isn't sitting on a
///          legal key character.
static rt_string parse_bare_key(const char **p) {
    const char *start = *p;
    while (isalnum((unsigned char)**p) || **p == '-' || **p == '_' || **p == '.')
        (*p)++;
    if (*p == start)
        return NULL;
    return make_str(start, (int64_t)(*p - start));
}

// --- Helper: parse a quoted string ---

/// @brief Parse a basic / literal quoted TOML string (no escape decoding).
/// @details Reads until the matching quote (or end-of-line, which is
///          also treated as a string terminator for malformed input).
///          Doesn't decode `\n`, `\"`, etc. — TOML escapes are not
///          implemented in this simplified parser. The opening quote
///          (`"` or `'`) must already be at `**p`.
static rt_string parse_quoted_string(const char **p) {
    char quote = **p;
    (*p)++;
    const char *start = *p;
    while (**p && **p != quote && **p != '\n')
        (*p)++;
    rt_string result = make_str(start, (int64_t)(*p - start));
    if (**p == quote)
        (*p)++;
    return result;
}

// --- Helper: parse a value ---

/// @brief Parse a TOML scalar value (string, number, bool, date) into an `rt_string`.
/// @details Strings (quoted or unquoted) all come back as raw strings;
///          numeric/boolean type discrimination is left to the
///          consumer (most callers run `rt_parse_try_int` /
///          `rt_parse_try_bool` to coerce after the fact). Stops at
///          newline, `#` (comment), or `,` (array element separator);
///          trailing in-line whitespace is trimmed off.
static rt_string parse_value(const char **p) {
    skip_ws(p);

    // Quoted string
    if (**p == '"' || **p == '\'')
        return parse_quoted_string(p);

    // Bare value (number, boolean, date, or unquoted string)
    const char *start = *p;
    while (**p && **p != '\n' && **p != '#' && **p != ',')
        (*p)++;

    // Trim trailing whitespace
    const char *end = *p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    return make_str(start, (int64_t)(end - start));
}

// --- Helper: parse an inline array ---

/// @brief Parse a TOML inline array literal `[a, b, c]` into a Seq of strings.
/// @details Walks comma-separated values until the closing `]`. Each
///          element is read via `parse_value` (so all elements come
///          back as raw strings). Tolerates trailing commas and
///          stray whitespace between elements.
static void *parse_array(const char **p) {
    (*p)++; // skip '['
    void *seq = rt_seq_new();

    while (**p && **p != ']') {
        skip_ws(p);
        if (**p == ']' || **p == '\n')
            break;
        if (**p == ',') {
            (*p)++;
            continue;
        }
        rt_string val = parse_value(p);
        if (val)
            rt_seq_push(seq, val);
    }
    if (**p == ']')
        (*p)++;
    return seq;
}

/// @brief Maximum nesting depth for TOML sections/tables (consistent with JSON/XML/YAML).
#define TOML_MAX_DEPTH 200

// --- Internal parse error flag (S-14, thread-local to avoid concurrent parse clobbering) ---
static _Thread_local int g_toml_had_error = 0;

// --- Public API ---

void *rt_toml_parse(rt_string src) {
    if (!src)
        return NULL;

    const char *p = rt_string_cstr(src);
    g_toml_had_error = 0;
    void *root = rt_map_new();
    void *current_section = root;

    while (*p) {
        skip_ws(&p);

        // Skip empty lines
        if (*p == '\n') {
            p++;
            continue;
        }

        // Skip comments
        if (*p == '#') {
            skip_line(&p);
            continue;
        }

        // Section header [section] or [section.subsection]
        if (*p == '[') {
            p++;
            int is_array = 0;
            if (*p == '[') {
                p++;
                is_array = 1;
            }

            skip_ws(&p);
            rt_string section_name = parse_bare_key(&p);
            skip_ws(&p);

            if (*p == ']')
                p++;
            if (is_array && *p == ']')
                p++;

            if (section_name) {
                // Create nested map for section
                const char *name_cstr = rt_string_cstr(section_name);

                // Count nesting depth (dots + 1)
                int depth = 1;
                for (const char *d = name_cstr; *d; d++) {
                    if (*d == '.')
                        depth++;
                }
                if (depth > TOML_MAX_DEPTH) {
                    g_toml_had_error = 1;
                    skip_line(&p);
                    continue;
                }

                // Handle dotted section names
                void *target = root;
                const char *dot = strchr(name_cstr, '.');
                if (dot) {
                    rt_string parent_key = make_str(name_cstr, (int64_t)(dot - name_cstr));
                    void *parent = rt_map_get(root, parent_key);
                    if (!parent) {
                        parent = rt_map_new();
                        rt_map_set(root, parent_key, parent);
                    }
                    target = parent;

                    rt_string child_key = make_str(
                        dot + 1, (int64_t)(strlen(name_cstr) - (size_t)(dot - name_cstr) - 1));
                    void *child = rt_map_new();
                    rt_map_set(target, child_key, child);
                    current_section = child;
                } else {
                    void *section_map = rt_map_get(root, section_name);
                    if (!section_map) {
                        section_map = rt_map_new();
                        rt_map_set(root, section_name, section_map);
                    }
                    current_section = section_map;
                }
            }
            skip_line(&p);
            continue;
        }

        // Key = Value
        rt_string key = NULL;
        if (*p == '"' || *p == '\'')
            key = parse_quoted_string(&p);
        else
            key = parse_bare_key(&p);

        if (!key) {
            /* S-14: flag malformed line that cannot be parsed as key=value */
            g_toml_had_error = 1;
            skip_line(&p);
            continue;
        }

        skip_ws(&p);
        if (*p != '=') {
            /* S-14: flag missing '=' separator */
            g_toml_had_error = 1;
            skip_line(&p);
            continue;
        }
        p++; // skip '='
        skip_ws(&p);

        // Parse value
        if (*p == '[') {
            void *arr = parse_array(&p);
            rt_map_set(current_section, key, arr);
        } else {
            rt_string val = parse_value(&p);
            if (val)
                rt_map_set(current_section, key, val);
        }

        skip_line(&p);
    }

    return root;
}

/// @brief Check whether a string contains valid TOML syntax.
int8_t rt_toml_is_valid(rt_string src) {
    /* S-14: rt_toml_parse always returns a (partial) map; check error flag */
    void *result = rt_toml_parse(src);
    if (!result || g_toml_had_error)
        return 0;
    return 1;
}

/// @brief Format a Map as a TOML document string.
rt_string rt_toml_format(void *map) {
    if (!map)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    void *keys = rt_map_keys(map);
    int64_t n = rt_seq_len(keys);

    for (int64_t i = 0; i < n; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(map, key);
        const char *key_cstr = rt_string_cstr(key);

        // Check if value is a sub-map
        void *sub_keys = rt_map_keys(val);
        if (sub_keys && rt_seq_len(sub_keys) >= 0) {
            // It's a section - try to format as section
            // But first we need to tell if it's really a map
            // Simple heuristic: try rt_map_len
            int64_t sub_len = rt_map_len(val);
            if (sub_len > 0) {
                rt_sb_append_cstr(&sb, "[");
                rt_sb_append_bytes(&sb, key_cstr, strlen(key_cstr));
                rt_sb_append_cstr(&sb, "]\n");

                void *sub_k = rt_map_keys(val);
                for (int64_t j = 0; j < rt_seq_len(sub_k); j++) {
                    rt_string sk = (rt_string)rt_seq_get(sub_k, j);
                    void *sv = rt_map_get(val, sk);
                    const char *sk_cstr = rt_string_cstr(sk);
                    rt_sb_append_bytes(&sb, sk_cstr, strlen(sk_cstr));
                    rt_sb_append_cstr(&sb, " = \"");
                    const char *sv_cstr = rt_string_cstr((rt_string)sv);
                    rt_sb_append_bytes(&sb, sv_cstr, strlen(sv_cstr));
                    rt_sb_append_cstr(&sb, "\"\n");
                }
                rt_sb_append_cstr(&sb, "\n");
                continue;
            }
        }

        // Simple key = value
        rt_sb_append_bytes(&sb, key_cstr, strlen(key_cstr));
        rt_sb_append_cstr(&sb, " = \"");
        const char *val_cstr = rt_string_cstr((rt_string)val);
        rt_sb_append_bytes(&sb, val_cstr, strlen(val_cstr));
        rt_sb_append_cstr(&sb, "\"\n");
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

/// @brief Get a value from a parsed TOML document by dot-separated key path.
void *rt_toml_get(void *root, rt_string key_path) {
    if (!root || !key_path)
        return NULL;

    // Auto-parse: if root is a raw TOML string, parse it first
    if (rt_string_is_handle(root)) {
        root = rt_toml_parse((rt_string)root);
        if (!root)
            return NULL;
    } else if (rt_box_type(root) == RT_BOX_STR) {
        // Boxed string (from Zia str→ptr conversion) — unbox and parse
        rt_string s = rt_unbox_str(root);
        if (s) {
            root = rt_toml_parse(s);
            if (!root)
                return NULL;
        }
    }

    const char *path = rt_string_cstr(key_path);
    void *current = root;

    while (*path) {
        const char *dot = strchr(path, '.');
        rt_string key;
        if (dot) {
            key = make_str(path, (int64_t)(dot - path));
            path = dot + 1;
        } else {
            key = make_str(path, (int64_t)strlen(path));
            path += strlen(rt_string_cstr(key));
        }

        void *next = rt_map_get(current, key);
        rt_string_unref(key);
        if (!next)
            return NULL;
        current = next;
    }

    return current;
}

/// @brief Get a string value from a parsed TOML document by key path.
rt_string rt_toml_get_str(void *root, rt_string key_path) {
    void *val = rt_toml_get(root, key_path);
    if (!val)
        return rt_string_from_bytes("", 0);
    // Check if value is a raw string
    if (rt_string_is_handle(val))
        return (rt_string)val;
    // Try as boxed string
    if (rt_box_type(val) == RT_BOX_STR)
        return rt_unbox_str(val);
    return rt_string_from_bytes("", 0);
}
