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
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Minimum payload bytes read by Seq/Map public APIs when formatting TOML values.
#define TOML_SEQ_MIN_PAYLOAD (sizeof(int64_t) * 2 + sizeof(void *) + sizeof(int8_t))
#define TOML_MAP_MIN_PAYLOAD (sizeof(void *) * 2 + sizeof(size_t) * 2)

// --- Internal parse error flag (S-14, thread-local to avoid concurrent parse clobbering) ---
static _Thread_local int g_toml_had_error = 0;

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

/// @brief Skip whitespace, newlines, and comments inside multiline TOML arrays.
/// @details Unlike `skip_ws`, this consumes line boundaries because TOML arrays
///          may span multiple lines and may contain comments between elements.
/// @param p Cursor to advance.
static void skip_array_ws(const char **p) {
    for (;;) {
        while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
            (*p)++;
        if (**p == '#') {
            skip_line(p);
            continue;
        }
        return;
    }
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

/// @brief Decode one hexadecimal digit used by TOML `\u`/`\U` escapes.
/// @param c Input byte.
/// @return Value in [0, 15], or -1 when @p c is not hexadecimal.
static int toml_hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// @brief Append a Unicode scalar value as UTF-8 to a string builder.
/// @details Rejects surrogate halves and values outside Unicode range by
///          setting the TOML parse-error flag. Valid values append between
///          one and four bytes.
/// @param sb Destination builder.
/// @param codepoint Unicode scalar value.
static void toml_append_utf8(rt_string_builder *sb, uint32_t codepoint) {
    char out[4];
    size_t len = 0;
    if (codepoint <= 0x7Fu) {
        out[len++] = (char)codepoint;
    } else if (codepoint <= 0x7FFu) {
        out[len++] = (char)(0xC0u | (codepoint >> 6));
        out[len++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) {
        g_toml_had_error = 1;
        return;
    } else if (codepoint <= 0xFFFFu) {
        out[len++] = (char)(0xE0u | (codepoint >> 12));
        out[len++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[len++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint <= 0x10FFFFu) {
        out[len++] = (char)(0xF0u | (codepoint >> 18));
        out[len++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        out[len++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[len++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else {
        g_toml_had_error = 1;
        return;
    }
    rt_sb_append_bytes(sb, out, len);
}

/// @brief Parse a basic/literal TOML string, including multiline variants.
/// @details Basic strings decode TOML escapes (`\n`, `\t`, `\"`, `\\`,
///          `\uXXXX`, and `\UXXXXXXXX`). Literal strings preserve bytes.
///          Triple-quoted strings may span lines and terminate only at the
///          matching triple quote. The opening quote must be at `**p`.
static rt_string parse_quoted_string(const char **p) {
    char quote = **p;
    int multiline = ((*p)[1] == quote && (*p)[2] == quote);
    *p += multiline ? 3 : 1;

    rt_string_builder sb;
    rt_sb_init(&sb);
    while (**p) {
        if (multiline && (*p)[0] == quote && (*p)[1] == quote && (*p)[2] == quote) {
            *p += 3;
            rt_string result = rt_string_from_bytes(sb.data, sb.len);
            rt_sb_free(&sb);
            return result;
        }
        if (!multiline && **p == quote) {
            (*p)++;
            rt_string result = rt_string_from_bytes(sb.data, sb.len);
            rt_sb_free(&sb);
            return result;
        }
        if (!multiline && **p == '\n') {
            g_toml_had_error = 1;
            break;
        }
        if (quote == '"' && **p == '\\') {
            (*p)++;
            char esc = **p;
            if (!esc) {
                g_toml_had_error = 1;
                break;
            }
            (*p)++;
            switch (esc) {
                case 'b':
                    rt_sb_append_bytes(&sb, "\b", 1);
                    break;
                case 't':
                    rt_sb_append_bytes(&sb, "\t", 1);
                    break;
                case 'n':
                    rt_sb_append_bytes(&sb, "\n", 1);
                    break;
                case 'f':
                    rt_sb_append_bytes(&sb, "\f", 1);
                    break;
                case 'r':
                    rt_sb_append_bytes(&sb, "\r", 1);
                    break;
                case '"':
                    rt_sb_append_bytes(&sb, "\"", 1);
                    break;
                case '\\':
                    rt_sb_append_bytes(&sb, "\\", 1);
                    break;
                case 'u':
                case 'U': {
                    int digits = esc == 'u' ? 4 : 8;
                    uint32_t cp = 0;
                    for (int i = 0; i < digits; i++) {
                        int hv = toml_hex_value((*p)[i]);
                        if (hv < 0) {
                            g_toml_had_error = 1;
                            break;
                        }
                        cp = (cp << 4) | (uint32_t)hv;
                    }
                    if (g_toml_had_error)
                        break;
                    *p += digits;
                    toml_append_utf8(&sb, cp);
                    break;
                }
                default:
                    g_toml_had_error = 1;
                    break;
            }
            if (g_toml_had_error)
                break;
            continue;
        }
        rt_sb_append_bytes(&sb, *p, 1);
        (*p)++;
    }
    if (!g_toml_had_error)
        g_toml_had_error = 1;
    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
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
static rt_string parse_value_until(const char **p, int stop_bracket, int stop_brace) {
    skip_ws(p);

    // Quoted string
    if (**p == '"' || **p == '\'')
        return parse_quoted_string(p);

    // Bare value (number, boolean, date, or unquoted string)
    const char *start = *p;
    while (**p && **p != '\n' && **p != '#' && **p != ',' && (!stop_bracket || **p != ']') &&
           (!stop_brace || **p != '}'))
        (*p)++;

    // Trim trailing whitespace
    const char *end = *p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    return make_str(start, (int64_t)(end - start));
}

// --- Helper: parse an inline array ---

/// @brief Release a TOML value after storing it in an owning container.
/// @details `rt_map_set` and `rt_seq_push` retain runtime values. This helper
///          drops the parser's temporary reference for both raw strings and
///          object-backed values.
/// @param obj TOML value object, raw `rt_string`, or NULL.
static void release_obj_maybe(void *obj) {
    if (!obj)
        return;
    if (rt_string_is_handle(obj)) {
        rt_string_unref((rt_string)obj);
        return;
    }
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void *parse_value_object(const char **p, int stop_bracket, int stop_brace);

/// @brief Parse a TOML inline table literal `{ key = value, ... }`.
/// @details Values may be strings, arrays, or nested inline tables. Duplicate
///          keys and malformed separators mark the parse as failed.
/// @param p Cursor positioned at the opening `{`; advanced past `}` on success.
/// @return Fresh rt_map containing the inline table entries.
static void *parse_inline_table(const char **p) {
    (*p)++;
    void *map = rt_map_new();
    if (!map) {
        g_toml_had_error = 1;
        return NULL;
    }

    while (**p) {
        skip_ws(p);
        if (**p == '}') {
            (*p)++;
            return map;
        }
        rt_string key = NULL;
        if (**p == '"' || **p == '\'')
            key = parse_quoted_string(p);
        else
            key = parse_bare_key(p);
        if (!key) {
            g_toml_had_error = 1;
            return map;
        }
        skip_ws(p);
        if (**p != '=') {
            g_toml_had_error = 1;
            rt_string_unref(key);
            return map;
        }
        (*p)++;
        if (rt_map_has(map, key)) {
            g_toml_had_error = 1;
            rt_string_unref(key);
            return map;
        }
        void *value = parse_value_object(p, 0, 1);
        if (value) {
            rt_map_set(map, key, value);
            release_obj_maybe(value);
        }
        rt_string_unref(key);
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            return map;
        }
        g_toml_had_error = 1;
        return map;
    }

    if (!g_toml_had_error)
        g_toml_had_error = 1;
    return map;
}

/// @brief Parse a TOML inline array literal `[a, b, c]` into a Seq of TOML values.
/// @details Walks comma-separated values until the closing `]`. Each
///          element is read via `parse_value_object`, so nested arrays and
///          inline tables are preserved as runtime containers. Tolerates
///          trailing commas and stray whitespace between elements.
static void *parse_array(const char **p) {
    (*p)++; // skip '['
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);

    while (**p) {
        skip_array_ws(p);
        if (**p == ']') {
            (*p)++;
            return seq;
        }
        if (**p == ',') {
            g_toml_had_error = 1;
            break;
        }
        void *val = parse_value_object(p, 1, 0);
        if (val) {
            rt_seq_push(seq, val);
            release_obj_maybe(val);
        }
        skip_array_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return seq;
        }
        break;
    }
    if (!g_toml_had_error)
        g_toml_had_error = 1;
    return seq;
}

/// @brief Parse one TOML value as the appropriate runtime object.
/// @details Arrays become Seq, inline tables become Map, and scalar values
///          become rt_string. Numeric/bool/datetime coercion remains in the
///          existing typed getters.
/// @param p Cursor at the value start.
/// @param stop_bracket Whether `]` terminates a bare value.
/// @param stop_brace Whether `}` terminates a bare value.
/// @return Fresh value object or NULL on allocation failure.
static void *parse_value_object(const char **p, int stop_bracket, int stop_brace) {
    skip_ws(p);
    if (**p == '[')
        return parse_array(p);
    if (**p == '{')
        return parse_inline_table(p);
    return parse_value_until(p, stop_bracket, stop_brace);
}

/// @brief Maximum nesting depth for TOML sections/tables (consistent with JSON/XML/YAML).
#define TOML_MAX_DEPTH 200

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

/// @brief Ensure an array-of-tables path exists and append a fresh table.
/// @details For `[[a.b]]`, prefix segments are regular maps and the final
///          segment is a Seq that owns map entries. The appended map becomes
///          the current section for subsequent key/value lines.
/// @param root Root TOML map.
/// @param name Dotted array-of-tables name.
/// @param len Byte length of @p name.
/// @return Borrowed pointer to the appended table, or @p root after an error.
static void *ensure_array_table_path(void *root, const char *name, size_t len) {
    if (!root || !name || len == 0) {
        g_toml_had_error = 1;
        return root;
    }

    size_t last_start = 0;
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '.')
            last_start = i + 1;
    }
    if (last_start >= len) {
        g_toml_had_error = 1;
        return root;
    }

    void *parent = root;
    if (last_start > 0)
        parent = ensure_table_path(root, name, last_start - 1);
    if (!is_map_obj(parent)) {
        g_toml_had_error = 1;
        return root;
    }

    rt_string key = make_str(name + last_start, (int64_t)(len - last_start));
    void *seq = rt_map_get(parent, key);
    if (seq && !is_seq_obj(seq)) {
        g_toml_had_error = 1;
        rt_string_unref(key);
        return root;
    }
    if (!seq) {
        seq = rt_seq_new();
        if (!seq) {
            g_toml_had_error = 1;
            rt_string_unref(key);
            return root;
        }
        rt_seq_set_owns_elements(seq, 1);
        rt_map_set(parent, key, seq);
        release_obj_maybe(seq);
        seq = rt_map_get(parent, key);
    }

    void *table = rt_map_new();
    if (!table) {
        g_toml_had_error = 1;
        rt_string_unref(key);
        return root;
    }
    rt_seq_push(seq, table);
    release_obj_maybe(table);
    rt_string_unref(key);
    return table;
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

                current_section = is_array ? ensure_array_table_path(root, name_cstr, name_len)
                                           : ensure_table_path(root, name_cstr, name_len);
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
            rt_string_unref(key);
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
        void *val = parse_value_object(&p, 0, 0);
        if (val) {
            rt_map_set(current_section, key, val);
            release_obj_maybe(val);
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
