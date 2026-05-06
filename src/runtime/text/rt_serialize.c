//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_serialize.c
// Purpose: Implements a unified serialization facade for the Viper.Data.Serialize
//          class. Dispatches Parse/Format/Convert calls to format-specific
//          implementations based on the requested format enum.
//
// Key invariants:
//   - Supported formats: JSON, XML, YAML, TOML, and CSV.
//   - Unknown format enums return an empty string/NULL and set rt_serialize_error().
//   - Serialization produces a string; deserialization parses a string into
//     Viper maps, sequences, XML nodes, strings, boxed primitives, or NULL.
//   - Cross-format conversion uses the native backends plus generic projections.
//   - Error state is thread-local.
//
// Ownership/Lifetime:
//   - Returned serialized strings and deserialized values are owned by caller.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_serialize.h (public API),
//        src/runtime/text/rt_json.h, rt_xml.h, rt_toml.h, rt_yaml.h (backends)
//
//===----------------------------------------------------------------------===//

#include "rt_serialize.h"

#include "rt_box.h"
#include "rt_csv.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_toml.h"
#include "rt_xml.h"
#include "rt_yaml.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "rt_trap.h"

/// Thread-local error message.
static _Thread_local rt_string g_last_error = NULL;

/// @brief Replace the thread-local error string with `msg` (refcounts the previous one off).
/// @details Stored per-thread (`_Thread_local`) so concurrent
///          serialize calls in different threads don't clobber each
///          other's diagnostics. Released via `clear_error` at the
///          start of every public entry point so stale errors from
///          a prior call don't leak across operations.
static void set_error(const char *msg) {
    if (g_last_error)
        rt_string_unref(g_last_error);
    g_last_error = rt_string_from_bytes(msg, strlen(msg));
}

/// @brief Drop the current thread-local error string (called at the start of each entry point).
static void clear_error(void) {
    if (g_last_error)
        rt_string_unref(g_last_error);
    g_last_error = NULL;
}

static int has_error(void) {
    return g_last_error && rt_str_len(g_last_error) > 0;
}

static void set_error_from_string(rt_string msg, const char *fallback) {
    if (g_last_error)
        rt_string_unref(g_last_error);
    if (msg && rt_str_len(msg) > 0)
        g_last_error = rt_string_ref(msg);
    else
        g_last_error = rt_string_from_bytes(fallback, strlen(fallback));
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int is_seq_obj(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_SEQ_CLASS_ID;
}

static int is_map_obj(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

static rt_string make_cstr(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void map_set_cstr(void *map, const char *key, void *value) {
    rt_string k = make_cstr(key);
    rt_map_set(map, k, value);
    rt_string_unref(k);
}

static rt_string scalar_to_string(void *obj) {
    if (!obj)
        return make_cstr("null");
    if (rt_string_is_handle(obj))
        return rt_string_ref((rt_string)obj);

    int64_t box_type = rt_box_type(obj);
    char buf[96];
    if (box_type == RT_BOX_I1)
        return make_cstr(rt_unbox_i1(obj) ? "true" : "false");
    if (box_type == RT_BOX_I64) {
        snprintf(buf, sizeof(buf), "%lld", (long long)rt_unbox_i64(obj));
        return rt_string_from_bytes(buf, strlen(buf));
    }
    if (box_type == RT_BOX_F64) {
        double v = rt_unbox_f64(obj);
        if (isnan(v))
            return make_cstr(".nan");
        if (isinf(v))
            return make_cstr(v > 0 ? ".inf" : "-.inf");
        snprintf(buf, sizeof(buf), "%.17g", v);
        return rt_string_from_bytes(buf, strlen(buf));
    }
    if (box_type == RT_BOX_STR)
        return rt_unbox_str(obj);

    return rt_json_format(obj);
}

static int xml_name_start(int c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

static int xml_name_char(int c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.';
}

static rt_string sanitized_xml_name(const char *name, const char *fallback) {
    const char *src = (name && *name) ? name : fallback;
    size_t len = strlen(src);
    size_t cap = len + strlen(fallback) + 8;
    char *buf = (char *)malloc(cap);
    if (!buf)
        rt_trap("Serialize: memory allocation failed");
    size_t out = 0;
    if (!xml_name_start((unsigned char)src[0])) {
        for (const char *p = fallback; *p; ++p)
            buf[out++] = *p;
        buf[out++] = '_';
    }
    for (size_t i = 0; i < len; i++)
        buf[out++] = (char)(xml_name_char((unsigned char)src[i]) ? src[i] : '_');
    rt_string result = rt_string_from_bytes(buf, out);
    free(buf);
    return result;
}

static void *generic_to_xml_element(const char *name, void *obj) {
    rt_string tag = sanitized_xml_name(name, "item");
    void *elem = rt_xml_element(tag);
    rt_string_unref(tag);
    if (!elem)
        return NULL;

    if (is_map_obj(obj)) {
        void *keys = rt_map_keys(obj);
        int64_t nkeys = rt_seq_len(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *child = generic_to_xml_element(rt_string_cstr(key), rt_map_get(obj, key));
            if (child)
                rt_xml_append(elem, child);
        }
        release_obj(keys);
        return elem;
    }

    if (is_seq_obj(obj)) {
        int64_t len = rt_seq_len(obj);
        for (int64_t i = 0; i < len; i++) {
            void *child = generic_to_xml_element("item", rt_seq_get(obj, i));
            if (child)
                rt_xml_append(elem, child);
        }
        return elem;
    }

    rt_string text = scalar_to_string(obj);
    rt_xml_set_text(elem, text);
    rt_string_unref(text);
    return elem;
}

static rt_string format_xml_from_generic(void *obj, int64_t indent) {
    if (rt_xml_is_node(obj))
        return indent > 0 ? rt_xml_format_pretty(obj, indent) : rt_xml_format(obj);

    void *root = generic_to_xml_element("root", obj);
    if (!root) {
        set_error("format XML: cannot build XML tree");
        return rt_string_from_bytes("", 0);
    }
    rt_string out = indent > 0 ? rt_xml_format_pretty(root, indent) : rt_xml_format(root);
    release_obj(root);
    return out;
}

static void add_grouped_child(void *map, rt_string key, void *value) {
    void *existing = rt_map_get(map, key);
    if (!existing) {
        rt_map_set(map, key, value);
        return;
    }

    if (is_seq_obj(existing)) {
        rt_seq_push(existing, value);
        return;
    }

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    rt_seq_push(seq, existing);
    rt_seq_push(seq, value);
    rt_map_set(map, key, seq);
    release_obj(seq);
}

static void *xml_to_generic_value(void *node);

static void *xml_document_to_generic(void *doc) {
    void *root = rt_xml_root(doc);
    if (!root)
        return NULL;
    void *map = rt_map_new();
    rt_string tag = rt_xml_tag(root);
    void *value = xml_to_generic_value(root);
    rt_map_set(map, tag, value);
    release_obj(value);
    rt_string_unref(tag);
    return map;
}

static void *xml_to_generic_value(void *node) {
    int64_t type = rt_xml_node_type(node);
    if (type == XML_NODE_DOCUMENT)
        return xml_document_to_generic(node);
    if (type == XML_NODE_TEXT || type == XML_NODE_CDATA)
        return (void *)rt_xml_content(node);
    if (type != XML_NODE_ELEMENT)
        return NULL;

    void *attrs = rt_xml_attr_names(node);
    void *children = rt_xml_children(node);
    int64_t attr_count = rt_seq_len(attrs);
    int64_t child_count = rt_seq_len(children);
    int64_t element_children = 0;
    rt_string_builder text;
    rt_sb_init(&text);

    void *map = rt_map_new();

    if (attr_count > 0) {
        void *attr_map = rt_map_new();
        for (int64_t i = 0; i < attr_count; i++) {
            rt_string name = (rt_string)rt_seq_get(attrs, i);
            rt_string val = rt_xml_attr(node, name);
            rt_map_set(attr_map, name, (void *)val);
            rt_string_unref(val);
        }
        map_set_cstr(map, "@attrs", attr_map);
        release_obj(attr_map);
    }

    for (int64_t i = 0; i < child_count; i++) {
        void *child = rt_seq_get(children, i);
        int64_t child_type = rt_xml_node_type(child);
        if (child_type == XML_NODE_ELEMENT) {
            element_children++;
            rt_string tag = rt_xml_tag(child);
            void *value = xml_to_generic_value(child);
            add_grouped_child(map, tag, value);
            release_obj(value);
            rt_string_unref(tag);
        } else if (child_type == XML_NODE_TEXT || child_type == XML_NODE_CDATA) {
            rt_string content = rt_xml_content(child);
            rt_sb_append_bytes(&text, rt_string_cstr(content), (size_t)rt_str_len(content));
            rt_string_unref(content);
        }
    }

    release_obj(attrs);
    release_obj(children);

    if (attr_count == 0 && element_children == 0) {
        rt_string scalar = rt_string_from_bytes(text.data ? text.data : "", text.len);
        rt_sb_free(&text);
        release_obj(map);
        return scalar;
    }

    if (text.len > 0) {
        rt_string scalar = rt_string_from_bytes(text.data, text.len);
        map_set_cstr(map, "#text", (void *)scalar);
        rt_string_unref(scalar);
    }
    rt_sb_free(&text);
    return map;
}

static rt_string format_toml_from_generic(void *obj) {
    if (is_map_obj(obj))
        return rt_toml_format(obj);
    void *map = rt_map_new();
    map_set_cstr(map, is_seq_obj(obj) ? "items" : "value", obj);
    rt_string out = rt_toml_format(map);
    release_obj(map);
    return out;
}

static int seq_rows_are_sequences(void *obj) {
    if (!is_seq_obj(obj))
        return 0;
    int64_t len = rt_seq_len(obj);
    if (len == 0)
        return 1;
    for (int64_t i = 0; i < len; i++) {
        void *row = rt_seq_get(obj, i);
        if (row && !is_seq_obj(row))
            return 0;
    }
    return 1;
}

static void seq_push_string_cell(void *row, void *value) {
    rt_string s = scalar_to_string(value);
    rt_seq_push(row, (void *)s);
    rt_string_unref(s);
}

static rt_string format_csv_from_generic(void *obj) {
    if (seq_rows_are_sequences(obj))
        return rt_csv_format(obj);

    void *rows = rt_seq_new();
    rt_seq_set_owns_elements(rows, 1);

    if (is_map_obj(obj)) {
        void *keys = rt_map_keys(obj);
        int64_t nkeys = rt_seq_len(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *row = rt_seq_new();
            rt_seq_set_owns_elements(row, 1);
            rt_seq_push(row, (void *)key);
            seq_push_string_cell(row, rt_map_get(obj, key));
            rt_seq_push(rows, row);
            release_obj(row);
        }
        release_obj(keys);
    } else if (is_seq_obj(obj)) {
        int64_t len = rt_seq_len(obj);
        for (int64_t i = 0; i < len; i++) {
            void *row = rt_seq_new();
            rt_seq_set_owns_elements(row, 1);
            seq_push_string_cell(row, rt_seq_get(obj, i));
            rt_seq_push(rows, row);
            release_obj(row);
        }
    } else {
        void *row = rt_seq_new();
        rt_seq_set_owns_elements(row, 1);
        seq_push_string_cell(row, obj);
        rt_seq_push(rows, row);
        release_obj(row);
    }

    rt_string out = rt_csv_format(rows);
    release_obj(rows);
    return out;
}

//=============================================================================
// Unified Parse
//=============================================================================

/// @brief Parse `text` according to `format` (JSON/YAML/TOML/INI), returning the resulting tree.
/// Returns NULL on parse error — call `_error()` for the diagnostic message.
void *rt_serialize_parse(rt_string text, int64_t format) {
    clear_error();
    if (!text) {
        set_error("parse: nil input");
        return NULL;
    }

    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            if (!rt_json_is_valid(text)) {
                set_error("JSON parse error");
                return NULL;
            }
            return rt_json_parse(text);

        case RT_FORMAT_XML: {
            void *result = rt_xml_parse(text);
            if (!result) {
                rt_string err = rt_xml_error();
                if (err && rt_str_len(err) > 0)
                    g_last_error = err;
                else
                    set_error("XML parse error");
            }
            return result;
        }

        case RT_FORMAT_YAML: {
            void *result = rt_yaml_parse(text);
            if (!result) {
                rt_string err = rt_yaml_error();
                if (err && rt_str_len(err) > 0)
                    set_error_from_string(err, "YAML parse error");
                rt_string_unref(err);
            }
            return result;
        }

        case RT_FORMAT_TOML:
            if (!rt_toml_is_valid(text)) {
                set_error("TOML parse error");
                return NULL;
            }
            return rt_toml_parse(text);

        case RT_FORMAT_CSV:
            return rt_csv_parse(text);

        default:
            set_error("parse: unknown format");
            return NULL;
    }
}

//=============================================================================
// Unified Format
//=============================================================================

/// @brief Serialize `obj` into the requested text format (compact output).
/// @details Dispatches to the per-format formatter (`rt_json_format`,
///          `rt_xml_format`, etc.) based on the `format` enum.
///          Returns an empty string on unknown format. Use
///          `rt_serialize_format_pretty` for indented output.
rt_string rt_serialize_format(void *obj, int64_t format) {
    clear_error();

    if (rt_xml_is_node(obj) && format != RT_FORMAT_XML) {
        void *generic = xml_to_generic_value(obj);
        rt_string out = rt_serialize_format(generic, format);
        release_obj(generic);
        return out;
    }

    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            return rt_json_format(obj);

        case RT_FORMAT_XML:
            return format_xml_from_generic(obj, 0);

        case RT_FORMAT_YAML:
            return rt_yaml_format(obj);

        case RT_FORMAT_TOML:
            return format_toml_from_generic(obj);

        case RT_FORMAT_CSV:
            return format_csv_from_generic(obj);

        default:
            set_error("format: unknown format");
            return rt_string_from_bytes("", 0);
    }
}

/// @brief Serialize an object to a pretty-printed string in the specified format (JSON/XML/YAML).
rt_string rt_serialize_format_pretty(void *obj, int64_t format, int64_t indent) {
    clear_error();

    if (indent < 1)
        indent = 2;

    if (rt_xml_is_node(obj) && format != RT_FORMAT_XML) {
        void *generic = xml_to_generic_value(obj);
        rt_string out = rt_serialize_format_pretty(generic, format, indent);
        release_obj(generic);
        return out;
    }

    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            return rt_json_format_pretty(obj, indent);

        case RT_FORMAT_XML:
            return format_xml_from_generic(obj, indent);

        case RT_FORMAT_YAML:
            return rt_yaml_format_indent(obj, indent);

        case RT_FORMAT_TOML:
            return format_toml_from_generic(obj); /* TOML has no indent option */

        case RT_FORMAT_CSV:
            return format_csv_from_generic(obj); /* CSV has no indent option */

        default:
            set_error("format_pretty: unknown format");
            return rt_string_from_bytes("", 0);
    }
}

//=============================================================================
// Validation
//=============================================================================

/// @brief Check whether a string is valid in the specified format (JSON/XML/YAML).
int8_t rt_serialize_is_valid(rt_string text, int64_t format) {
    clear_error();
    if (!text) {
        set_error("is_valid: nil input");
        return 0;
    }

    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            return rt_json_is_valid(text);

        case RT_FORMAT_XML:
            return rt_xml_is_valid(text);

        case RT_FORMAT_YAML:
            return rt_yaml_is_valid(text);

        case RT_FORMAT_TOML:
            return rt_toml_is_valid(text);

        case RT_FORMAT_CSV:
            /* CSV is always parseable (any text is valid CSV) */
            return 1;

        default:
            return 0;
    }
}

//=============================================================================
// Auto-Detection
//=============================================================================

/// Skip leading whitespace and return pointer to first non-space char.
static const char *skip_ws(const char *s) {
    while (*s && ((unsigned char)*s <= ' '))
        s++;
    return s;
}

/// @brief Sniff a text blob and return the most likely serialization format.
/// @details Heuristic dispatch on the first non-whitespace character(s):
///          - `{` or `[` → JSON.
///          - `<` → XML.
///          - `---` → YAML document marker.
///          - `[name]` on first line → TOML section.
///          - `key = ...` on first line → TOML key/value.
///          - `key: ...` on first line → YAML.
///          - first-line comma → CSV.
///          Returns the matching `RT_FORMAT_*` constant, or -1 for
///          NULL/empty/unknown input. Never throws — this is meant to be a
///          conservative best-effort guess used as a fallback.
int64_t rt_serialize_detect(rt_string text) {
    const char *s;
    const char *line;

    if (!text)
        return -1;

    s = rt_string_cstr(text);
    if (!s || *s == '\0')
        return -1;

    s = skip_ws(s);
    if (*s == '\0')
        return -1;

    /* TOML: [section] on first line. Check before JSON array. */
    if (*s == '[' && s[1] != '\0') {
        const char *p = s + 1;
        int has_comma = 0;
        while (*p && *p != '\n' && *p != ']') {
            if (*p == ',')
                has_comma = 1;
            p++;
        }
        if (*p == ']' && p > s + 1 && !has_comma &&
            (isalpha((unsigned char)s[1]) || s[1] == '_' || s[1] == '"' || s[1] == '\'')) {
            const char *after = p + 1;
            while (*after == ' ' || *after == '\t')
                after++;
            if (*after == '\0' || *after == '\n' || *after == '#')
                return RT_FORMAT_TOML;
        }
    }

    /* JSON: starts with { or [ */
    if (*s == '{' || *s == '[')
        return RT_FORMAT_JSON;

    /* XML: starts with < */
    if (*s == '<')
        return RT_FORMAT_XML;

    /* YAML: starts with --- */
    if (s[0] == '-' && s[1] == '-' && s[2] == '-')
        return RT_FORMAT_YAML;

    /* TOML: look for key = value on first line */
    line = s;
    {
        const char *eq = line;
        while (*eq && *eq != '\n' && *eq != '=')
            eq++;
        if (*eq == '=' && eq > line)
            return RT_FORMAT_TOML;
    }

    /* YAML: indentation-based with colons */
    {
        const char *colon = line;
        while (*colon && *colon != '\n' && *colon != ':')
            colon++;
        if (*colon == ':')
            return RT_FORMAT_YAML;
    }

    /* CSV: comma-separated first line */
    {
        const char *comma = line;
        while (*comma && *comma != '\n' && *comma != ',')
            comma++;
        if (*comma == ',')
            return RT_FORMAT_CSV;
    }

    return -1;
}

/// @brief Detect the format from the text content (via `_detect`) and parse with that format.
/// Convenience for "load this file, figure out what it is" workflows.
void *rt_serialize_auto_parse(rt_string text) {
    int64_t format;
    clear_error();
    if (!text) {
        set_error("auto_parse: nil input");
        return NULL;
    }

    format = rt_serialize_detect(text);
    if (format < 0) {
        set_error("auto_parse: cannot detect format");
        return NULL;
    }

    return rt_serialize_parse(text, format);
}

//=============================================================================
// Round-Trip Conversion
//=============================================================================

/// @brief Round-trip text through a parse → re-format cycle to convert between formats.
/// @details Implementation: parse `text` as `from_format` to a tree,
///          then re-serialize that tree as `to_format`. Lossy in both
///          directions for some pairs (XML attributes ↔ JSON keys,
///          TOML datetimes ↔ JSON strings) — caller should expect
///          structural fidelity, not byte-for-byte equivalence.
rt_string rt_serialize_convert(rt_string text, int64_t from_format, int64_t to_format) {
    void *parsed;
    void *value;
    int parsed_is_xml;

    clear_error();
    if (!text) {
        set_error("convert: nil input");
        return rt_string_from_bytes("", 0);
    }

    if (!rt_serialize_is_valid(text, from_format)) {
        if (!has_error())
            set_error("convert: source text is not valid for format");
        return rt_string_from_bytes("", 0);
    }

    parsed = rt_serialize_parse(text, from_format);
    if (!parsed && has_error())
        return rt_string_from_bytes("", 0);

    parsed_is_xml = rt_xml_is_node(parsed);
    value = parsed;
    if (parsed_is_xml && to_format != RT_FORMAT_XML)
        value = xml_to_generic_value(parsed);

    rt_string out = rt_serialize_format(value, to_format);

    if (value != parsed)
        release_obj(value);
    release_obj(parsed);
    return out;
}

//=============================================================================
// Format Metadata
//=============================================================================

/// @brief Return the human-readable name of a format constant (e.g., "JSON", "XML").
rt_string rt_serialize_format_name(int64_t format) {
    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            return rt_string_from_bytes("json", 4);
        case RT_FORMAT_XML:
            return rt_string_from_bytes("xml", 3);
        case RT_FORMAT_YAML:
            return rt_string_from_bytes("yaml", 4);
        case RT_FORMAT_TOML:
            return rt_string_from_bytes("toml", 4);
        case RT_FORMAT_CSV:
            return rt_string_from_bytes("csv", 3);
        default:
            return rt_string_from_bytes("unknown", 7);
    }
}

/// @brief Return the MIME content-type string for a format (e.g., "application/json").
rt_string rt_serialize_mime_type(int64_t format) {
    switch ((rt_format_t)format) {
        case RT_FORMAT_JSON:
            return rt_string_from_bytes("application/json", 16);
        case RT_FORMAT_XML:
            return rt_string_from_bytes("application/xml", 15);
        case RT_FORMAT_YAML:
            return rt_string_from_bytes("application/yaml", 16);
        case RT_FORMAT_TOML:
            return rt_string_from_bytes("application/toml", 16);
        case RT_FORMAT_CSV:
            return rt_string_from_bytes("text/csv", 8);
        default:
            return rt_string_from_bytes("application/octet-stream", 24);
    }
}

/// @brief Look up a format constant by its short name (case-insensitive: "json", "xml", ...).
/// @details Inverse of `rt_serialize_format_name`. Accepts both
///          "yaml" and "yml" for YAML. Returns -1 for unknown names.
int64_t rt_serialize_format_from_name(rt_string name) {
    const char *s;
    if (!name)
        return -1;

    s = rt_string_cstr(name);
    if (!s)
        return -1;

    /* Case-insensitive comparison */
    if (strcasecmp(s, "json") == 0)
        return RT_FORMAT_JSON;
    if (strcasecmp(s, "xml") == 0)
        return RT_FORMAT_XML;
    if (strcasecmp(s, "yaml") == 0)
        return RT_FORMAT_YAML;
    if (strcasecmp(s, "yml") == 0)
        return RT_FORMAT_YAML;
    if (strcasecmp(s, "toml") == 0)
        return RT_FORMAT_TOML;
    if (strcasecmp(s, "csv") == 0)
        return RT_FORMAT_CSV;

    return -1;
}

//=============================================================================
// Error Handling
//=============================================================================

/// @brief Return the most recent thread-local error message (empty string if none).
/// @details Stored per-thread, so concurrent serialize calls don't
///          step on each other's diagnostics. Cleared at the start of
///          every public entry point.
rt_string rt_serialize_error(void) {
    if (g_last_error)
        return rt_string_ref(g_last_error);
    return rt_string_from_bytes("", 0);
}
