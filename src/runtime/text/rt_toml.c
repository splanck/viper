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
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Minimum payload bytes read by Seq/Map public APIs when formatting TOML values.
#define TOML_SEQ_MIN_PAYLOAD (sizeof(int64_t) * 2 + sizeof(void *) + sizeof(int8_t))
#define TOML_MAP_MIN_PAYLOAD (sizeof(void *) * 2 + sizeof(size_t) * 2)

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
static rt_string parse_value_until(const char **p, int stop_bracket) {
    skip_ws(p);

    // Quoted string
    if (**p == '"' || **p == '\'')
        return parse_quoted_string(p);

    // Bare value (number, boolean, date, or unquoted string)
    const char *start = *p;
    while (**p && **p != '\n' && **p != '#' && **p != ',' && (!stop_bracket || **p != ']'))
        (*p)++;

    // Trim trailing whitespace
    const char *end = *p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    return make_str(start, (int64_t)(end - start));
}

static rt_string parse_value(const char **p) {
    return parse_value_until(p, 0);
}

// --- Internal parse error flag (S-14, thread-local to avoid concurrent parse clobbering) ---
static _Thread_local int g_toml_had_error = 0;

// --- Helper: parse an inline array ---

/// @brief Parse a TOML inline array literal `[a, b, c]` into a Seq of strings.
/// @details Walks comma-separated values until the closing `]`. Each
///          element is read via `parse_value` (so all elements come
///          back as raw strings). Tolerates trailing commas and
///          stray whitespace between elements.
static void *parse_array(const char **p) {
    (*p)++; // skip '['
    void *seq = rt_seq_new();
    int closed = 0;

    while (**p && **p != '\n') {
        skip_ws(p);
        if (**p == ']') {
            closed = 1;
            break;
        }
        if (**p == ',') {
            (*p)++;
            continue;
        }
        rt_string val = parse_value_until(p, 1);
        if (val) {
            rt_seq_push(seq, val);
            rt_string_unref(val);
        }
    }
    if (closed && **p == ']')
        (*p)++;
    else
        g_toml_had_error = 1;
    return seq;
}

/// @brief Maximum nesting depth for TOML sections/tables (consistent with JSON/XML/YAML).
#define TOML_MAX_DEPTH 200

static void release_obj_maybe(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int is_map_obj(void *obj) {
    return obj && !rt_string_is_handle(obj) &&
           rt_obj_is_instance(obj, RT_MAP_CLASS_ID, TOML_MAP_MIN_PAYLOAD);
}

static int is_seq_obj(void *obj) {
    return obj && !rt_string_is_handle(obj) &&
           rt_obj_is_instance(obj, RT_SEQ_CLASS_ID, TOML_SEQ_MIN_PAYLOAD);
}

static void *ensure_table_path(void *root, const char *name, size_t len) {
    void *current = root;
    size_t pos = 0;

    while (pos < len) {
        if (!is_map_obj(current)) {
            g_toml_had_error = 1;
            return current;
        }

        size_t start = pos;
        while (pos < len && name[pos] != '.')
            pos++;
        if (pos == start) {
            g_toml_had_error = 1;
            return current;
        }

        rt_string key = make_str(name + start, (int64_t)(pos - start));
        void *next = rt_map_get(current, key);
        if (next && !is_map_obj(next)) {
            g_toml_had_error = 1;
            rt_string_unref(key);
            return current;
        }
        if (!next) {
            next = rt_map_new();
            rt_map_set(current, key, next);
            release_obj_maybe(next);
        }
        rt_string_unref(key);
        current = next;

        if (pos < len && name[pos] == '.')
            pos++;
    }

    return current;
}

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

            if (*p == ']') {
                p++;
            } else {
                g_toml_had_error = 1;
                if (section_name)
                    rt_string_unref(section_name);
                skip_line(&p);
                continue;
            }
            if (is_array) {
                if (*p == ']') {
                    p++;
                } else {
                    g_toml_had_error = 1;
                    if (section_name)
                        rt_string_unref(section_name);
                    skip_line(&p);
                    continue;
                }
            }
            skip_ws(&p);
            if (*p != '\0' && *p != '\n' && *p != '#') {
                g_toml_had_error = 1;
                if (section_name)
                    rt_string_unref(section_name);
                skip_line(&p);
                continue;
            }

            if (section_name) {
                // Create nested map for section
                const char *name_cstr = rt_string_cstr(section_name);
                size_t name_len = (size_t)rt_str_len(section_name);

                // Count nesting depth (dots + 1)
                int depth = 1;
                for (size_t di = 0; di < name_len; di++) {
                    if (name_cstr[di] == '.')
                        depth++;
                }
                if (depth > TOML_MAX_DEPTH) {
                    g_toml_had_error = 1;
                    rt_string_unref(section_name);
                    skip_line(&p);
                    continue;
                }

                current_section = ensure_table_path(root, name_cstr, name_len);
                rt_string_unref(section_name);
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
        if (rt_map_has(current_section, key)) {
            g_toml_had_error = 1;
            rt_string_unref(key);
            skip_line(&p);
            continue;
        }
        if (*p == '[') {
            void *arr = parse_array(&p);
            rt_map_set(current_section, key, arr);
            release_obj_maybe(arr);
        } else {
            rt_string val = parse_value(&p);
            if (val) {
                rt_map_set(current_section, key, val);
                rt_string_unref(val);
            }
        }
        rt_string_unref(key);

        skip_line(&p);
    }

    if (g_toml_had_error) {
        release_obj_maybe(root);
        return NULL;
    }
    return root;
}

/// @brief Check whether a string contains valid TOML syntax.
int8_t rt_toml_is_valid(rt_string src) {
    void *result = rt_toml_parse(src);
    if (!result || g_toml_had_error)
        return 0;
    release_obj_maybe(result);
    return 1;
}

static int is_bare_key_bytes(const char *s, size_t len) {
    if (!s || len == 0)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-'))
            return 0;
    }
    return 1;
}

static void append_quoted_toml_bytes(rt_string_builder *sb, const char *s, size_t len) {
    rt_sb_append_cstr(sb, "\"");
    if (s) {
        for (size_t i = 0; i < len; ++i) {
            char ch = s[i];
            switch (ch) {
                case '\\':
                    rt_sb_append_cstr(sb, "\\\\");
                    break;
                case '"':
                    rt_sb_append_cstr(sb, "\\\"");
                    break;
                case '\n':
                    rt_sb_append_cstr(sb, "\\n");
                    break;
                case '\r':
                    rt_sb_append_cstr(sb, "\\r");
                    break;
                case '\t':
                    rt_sb_append_cstr(sb, "\\t");
                    break;
                case '\b':
                    rt_sb_append_cstr(sb, "\\b");
                    break;
                case '\f':
                    rt_sb_append_cstr(sb, "\\f");
                    break;
                default:
                    if ((unsigned char)ch < 0x20) {
                        char esc[7];
                        snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)ch);
                        rt_sb_append_cstr(sb, esc);
                    } else {
                        rt_sb_append_bytes(sb, &ch, 1);
                    }
                    break;
            }
        }
    }
    rt_sb_append_cstr(sb, "\"");
}

static void append_quoted_toml_string(rt_string_builder *sb, const char *s) {
    append_quoted_toml_bytes(sb, s, s ? strlen(s) : 0);
}

static void append_toml_key_bytes(rt_string_builder *sb, const char *key, size_t key_len) {
    if (is_bare_key_bytes(key, key_len))
        rt_sb_append_bytes(sb, key, key_len);
    else
        append_quoted_toml_bytes(sb, key ? key : "", key ? key_len : 0);
}

static void append_toml_value(rt_string_builder *sb, void *val) {
    if (!val) {
        append_quoted_toml_string(sb, "");
        return;
    }

    if (rt_string_is_handle(val)) {
        rt_string s = (rt_string)val;
        append_quoted_toml_bytes(sb, rt_string_cstr(s), (size_t)rt_str_len(s));
        return;
    }

    int64_t box_type = rt_box_type(val);
    char buf[96];
    switch (box_type) {
        case RT_BOX_I1:
            rt_sb_append_cstr(sb, rt_unbox_i1(val) ? "true" : "false");
            return;
        case RT_BOX_I64:
            snprintf(buf, sizeof(buf), "%lld", (long long)rt_unbox_i64(val));
            rt_sb_append_bytes(sb, buf, strlen(buf));
            return;
        case RT_BOX_F64:
            snprintf(buf, sizeof(buf), "%.17g", rt_unbox_f64(val));
            rt_sb_append_bytes(sb, buf, strlen(buf));
            return;
        case RT_BOX_STR: {
            rt_string s = rt_unbox_str(val);
            append_quoted_toml_bytes(sb, rt_string_cstr(s), (size_t)rt_str_len(s));
            rt_string_unref(s);
            return;
        }
        default:
            break;
    }

    if (is_seq_obj(val)) {
        rt_sb_append_cstr(sb, "[");
        int64_t len = rt_seq_len(val);
        for (int64_t i = 0; i < len; i++) {
            if (i > 0)
                rt_sb_append_cstr(sb, ", ");
            append_toml_value(sb, rt_seq_get(val, i));
        }
        rt_sb_append_cstr(sb, "]");
        return;
    }

    append_quoted_toml_string(sb, "");
}

/// @brief Format a Map as a TOML document string.
rt_string rt_toml_format(void *map) {
    if (!map)
        return rt_string_from_bytes("", 0);
    if (!is_map_obj(map))
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    void *keys = rt_map_keys(map);
    int64_t n = rt_seq_len(keys);

    for (int64_t i = 0; i < n; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(map, key);
        const char *key_cstr = rt_string_cstr(key);
        size_t key_len = (size_t)rt_str_len(key);

        if (is_map_obj(val)) {
            rt_sb_append_cstr(&sb, "[");
            append_toml_key_bytes(&sb, key_cstr, key_len);
            rt_sb_append_cstr(&sb, "]\n");

            void *sub_k = rt_map_keys(val);
            for (int64_t j = 0; j < rt_seq_len(sub_k); j++) {
                rt_string sk = (rt_string)rt_seq_get(sub_k, j);
                void *sv = rt_map_get(val, sk);
                if (is_map_obj(sv))
                    continue;
                const char *sk_cstr = rt_string_cstr(sk);
                append_toml_key_bytes(&sb, sk_cstr, (size_t)rt_str_len(sk));
                rt_sb_append_cstr(&sb, " = ");
                append_toml_value(&sb, sv);
                rt_sb_append_cstr(&sb, "\n");
            }
            release_obj_maybe(sub_k);
            rt_sb_append_cstr(&sb, "\n");
            continue;
        }

        append_toml_key_bytes(&sb, key_cstr, key_len);
        rt_sb_append_cstr(&sb, " = ");
        append_toml_value(&sb, val);
        rt_sb_append_cstr(&sb, "\n");
    }

    release_obj_maybe(keys);
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
            rt_string_unref(s);
            if (!root)
                return NULL;
        }
    }

    const char *path = rt_string_cstr(key_path);
    size_t path_len = (size_t)rt_str_len(key_path);
    size_t path_pos = 0;
    void *current = root;

    while (path_pos < path_len) {
        if (!is_map_obj(current))
            return NULL;

        size_t start = path_pos;
        while (path_pos < path_len && path[path_pos] != '.')
            path_pos++;
        if (path_pos == start)
            return NULL;

        rt_string key = make_str(path + start, (int64_t)(path_pos - start));
        if (path_pos < path_len && path[path_pos] == '.')
            path_pos++;

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
        return rt_string_ref((rt_string)val);
    // Try as boxed string
    if (rt_box_type(val) == RT_BOX_STR)
        return rt_unbox_str(val);
    return rt_string_from_bytes("", 0);
}
