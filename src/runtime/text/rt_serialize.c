//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_serialize.c
// Purpose: Implements a unified serialization facade for the Viper.Text.Serialize
//          class. Dispatches Serialize/Deserialize calls to format-specific
//          implementations based on the requested format tag (json, xml, toml, yaml).
//
// Key invariants:
//   - Supported formats: "json", "xml", "toml", "yaml" (case-insensitive).
//   - Unknown format tags cause a trap with a descriptive error message.
//   - Serialization produces a string; deserialization parses a string into
//     an rt_map tree.
//   - The facade validates format names before dispatching; callers never see
//     partial state from a failed dispatch.
//   - All dispatched functions are thread-safe.
//
// Ownership/Lifetime:
//   - Returned serialized strings and deserialized rt_map trees are owned by caller.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_serialize.h (public API),
//        src/runtime/text/rt_json.h, rt_xml.h, rt_toml.h, rt_yaml.h (backends)
//
//===----------------------------------------------------------------------===//

#include "rt_serialize.h"

#include "rt_csv.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_string.h"
#include "rt_toml.h"
#include "rt_xml.h"
#include "rt_yaml.h"

#include <ctype.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

extern void rt_trap(const char *msg);

/// Thread-local error message.
static _Thread_local rt_string g_last_error = NULL;

static void set_error(const char *msg)
{
    g_last_error = rt_string_from_bytes(msg, strlen(msg));
}

static void clear_error(void)
{
    g_last_error = NULL;
}

//=============================================================================
// Unified Parse
//=============================================================================

void *rt_serialize_parse(rt_string text, int64_t format)
{
    clear_error();
    if (!text)
    {
        set_error("parse: nil input");
        return NULL;
    }

    switch ((rt_format_t)format)
    {
        case RT_FORMAT_JSON:
            return rt_json_parse(text);

        case RT_FORMAT_XML:
        {
            void *result = rt_xml_parse(text);
            if (!result)
            {
                rt_string err = rt_xml_error();
                if (err && rt_str_len(err) > 0)
                    g_last_error = err;
                else
                    set_error("XML parse error");
            }
            return result;
        }

        case RT_FORMAT_YAML:
            return rt_yaml_parse(text);

        case RT_FORMAT_TOML:
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

rt_string rt_serialize_format(void *obj, int64_t format)
{
    clear_error();

    switch ((rt_format_t)format)
    {
        case RT_FORMAT_JSON:
            return rt_json_format(obj);

        case RT_FORMAT_XML:
            return rt_xml_format(obj);

        case RT_FORMAT_YAML:
            return rt_yaml_format(obj);

        case RT_FORMAT_TOML:
            return rt_toml_format(obj);

        case RT_FORMAT_CSV:
            return rt_csv_format(obj);

        default:
            set_error("format: unknown format");
            return rt_string_from_bytes("", 0);
    }
}

rt_string rt_serialize_format_pretty(void *obj, int64_t format, int64_t indent)
{
    clear_error();

    if (indent < 1)
        indent = 2;

    switch ((rt_format_t)format)
    {
        case RT_FORMAT_JSON:
            return rt_json_format_pretty(obj, indent);

        case RT_FORMAT_XML:
            return rt_xml_format_pretty(obj, indent);

        case RT_FORMAT_YAML:
            return rt_yaml_format_indent(obj, indent);

        case RT_FORMAT_TOML:
            return rt_toml_format(obj); /* TOML has no indent option */

        case RT_FORMAT_CSV:
            return rt_csv_format(obj); /* CSV has no indent option */

        default:
            set_error("format_pretty: unknown format");
            return rt_string_from_bytes("", 0);
    }
}

//=============================================================================
// Validation
//=============================================================================

int8_t rt_serialize_is_valid(rt_string text, int64_t format)
{
    if (!text)
        return 0;

    switch ((rt_format_t)format)
    {
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
static const char *skip_ws(const char *s)
{
    while (*s && ((unsigned char)*s <= ' '))
        s++;
    return s;
}

int64_t rt_serialize_detect(rt_string text)
{
    const char *s;
    const char *line;

    if (!text)
        return -1;

    s = rt_string_cstr(text);
    if (!s || *s == '\0')
        return -1;

    s = skip_ws(s);

    /* JSON: starts with { or [ */
    if (*s == '{' || *s == '[')
        return RT_FORMAT_JSON;

    /* XML: starts with < */
    if (*s == '<')
        return RT_FORMAT_XML;

    /* YAML: starts with --- */
    if (s[0] == '-' && s[1] == '-' && s[2] == '-')
        return RT_FORMAT_YAML;

    /* TOML: look for [section] or key = value on first line */
    line = s;
    if (*line == '[' && line[1] != '\0')
    {
        /* Could be TOML section or JSON array - check for ] before newline */
        const char *p = line + 1;
        while (*p && *p != '\n' && *p != ']')
            p++;
        if (*p == ']')
            return RT_FORMAT_TOML;
    }

    /* TOML: look for key = value */
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

    /* Default to CSV */
    return RT_FORMAT_CSV;
}

void *rt_serialize_auto_parse(rt_string text)
{
    int64_t format;
    if (!text)
        return NULL;

    format = rt_serialize_detect(text);
    if (format < 0)
    {
        set_error("auto_parse: cannot detect format");
        return NULL;
    }

    return rt_serialize_parse(text, format);
}

//=============================================================================
// Round-Trip Conversion
//=============================================================================

rt_string rt_serialize_convert(rt_string text, int64_t from_format, int64_t to_format)
{
    void *parsed;

    clear_error();
    if (!text)
    {
        set_error("convert: nil input");
        return rt_string_from_bytes("", 0);
    }

    parsed = rt_serialize_parse(text, from_format);
    if (!parsed)
        return rt_string_from_bytes("", 0);

    return rt_serialize_format(parsed, to_format);
}

//=============================================================================
// Format Metadata
//=============================================================================

rt_string rt_serialize_format_name(int64_t format)
{
    switch ((rt_format_t)format)
    {
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

rt_string rt_serialize_mime_type(int64_t format)
{
    switch ((rt_format_t)format)
    {
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

int64_t rt_serialize_format_from_name(rt_string name)
{
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

rt_string rt_serialize_error(void)
{
    if (g_last_error)
        return g_last_error;
    return rt_string_from_bytes("", 0);
}
