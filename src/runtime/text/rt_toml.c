//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
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

static rt_string make_str(const char *s, int64_t len)
{
    return rt_string_from_bytes(s, len);
}

// --- Helper: trim whitespace ---

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t')
        (*p)++;
}

static void skip_line(const char **p)
{
    while (**p && **p != '\n')
        (*p)++;
    if (**p == '\n')
        (*p)++;
}

// --- Helper: parse a bare key (alphanumeric, dash, underscore) ---

static rt_string parse_bare_key(const char **p)
{
    const char *start = *p;
    while (isalnum((unsigned char)**p) || **p == '-' || **p == '_' || **p == '.')
        (*p)++;
    if (*p == start)
        return NULL;
    return make_str(start, (int64_t)(*p - start));
}

// --- Helper: parse a quoted string ---

static rt_string parse_quoted_string(const char **p)
{
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

static rt_string parse_value(const char **p)
{
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

static void *parse_array(const char **p)
{
    (*p)++; // skip '['
    void *seq = rt_seq_new();

    while (**p && **p != ']')
    {
        skip_ws(p);
        if (**p == ']' || **p == '\n')
            break;
        if (**p == ',')
        {
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

// --- Internal parse error flag (S-14) ---
static int g_toml_had_error = 0;

// --- Public API ---

void *rt_toml_parse(rt_string src)
{
    if (!src)
        return NULL;

    const char *p = rt_string_cstr(src);
    g_toml_had_error = 0;
    void *root = rt_map_new();
    void *current_section = root;

    while (*p)
    {
        skip_ws(&p);

        // Skip empty lines
        if (*p == '\n')
        {
            p++;
            continue;
        }

        // Skip comments
        if (*p == '#')
        {
            skip_line(&p);
            continue;
        }

        // Section header [section] or [section.subsection]
        if (*p == '[')
        {
            p++;
            int is_array = 0;
            if (*p == '[')
            {
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

            if (section_name)
            {
                // Create nested map for section
                const char *name_cstr = rt_string_cstr(section_name);

                // Handle dotted section names
                void *target = root;
                const char *dot = strchr(name_cstr, '.');
                if (dot)
                {
                    rt_string parent_key = make_str(name_cstr, (int64_t)(dot - name_cstr));
                    void *parent = rt_map_get(root, parent_key);
                    if (!parent)
                    {
                        parent = rt_map_new();
                        rt_map_set(root, parent_key, parent);
                    }
                    target = parent;

                    rt_string child_key = make_str(
                        dot + 1, (int64_t)(strlen(name_cstr) - (size_t)(dot - name_cstr) - 1));
                    void *child = rt_map_new();
                    rt_map_set(target, child_key, child);
                    current_section = child;
                }
                else
                {
                    void *section_map = rt_map_get(root, section_name);
                    if (!section_map)
                    {
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

        if (!key)
        {
            /* S-14: flag malformed line that cannot be parsed as key=value */
            g_toml_had_error = 1;
            skip_line(&p);
            continue;
        }

        skip_ws(&p);
        if (*p != '=')
        {
            /* S-14: flag missing '=' separator */
            g_toml_had_error = 1;
            skip_line(&p);
            continue;
        }
        p++; // skip '='
        skip_ws(&p);

        // Parse value
        if (*p == '[')
        {
            void *arr = parse_array(&p);
            rt_map_set(current_section, key, arr);
        }
        else
        {
            rt_string val = parse_value(&p);
            if (val)
                rt_map_set(current_section, key, val);
        }

        skip_line(&p);
    }

    return root;
}

int8_t rt_toml_is_valid(rt_string src)
{
    /* S-14: rt_toml_parse always returns a (partial) map; check error flag */
    void *result = rt_toml_parse(src);
    if (!result || g_toml_had_error)
        return 0;
    return 1;
}

rt_string rt_toml_format(void *map)
{
    if (!map)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);

    void *keys = rt_map_keys(map);
    int64_t n = rt_seq_len(keys);

    for (int64_t i = 0; i < n; i++)
    {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(map, key);
        const char *key_cstr = rt_string_cstr(key);

        // Check if value is a sub-map
        void *sub_keys = rt_map_keys(val);
        if (sub_keys && rt_seq_len(sub_keys) >= 0)
        {
            // It's a section - try to format as section
            // But first we need to tell if it's really a map
            // Simple heuristic: try rt_map_len
            int64_t sub_len = rt_map_len(val);
            if (sub_len > 0)
            {
                rt_sb_append_cstr(&sb, "[");
                rt_sb_append_bytes(&sb, key_cstr, (int64_t)strlen(key_cstr));
                rt_sb_append_cstr(&sb, "]\n");

                void *sub_k = rt_map_keys(val);
                for (int64_t j = 0; j < rt_seq_len(sub_k); j++)
                {
                    rt_string sk = (rt_string)rt_seq_get(sub_k, j);
                    void *sv = rt_map_get(val, sk);
                    const char *sk_cstr = rt_string_cstr(sk);
                    rt_sb_append_bytes(&sb, sk_cstr, (int64_t)strlen(sk_cstr));
                    rt_sb_append_cstr(&sb, " = \"");
                    const char *sv_cstr = rt_string_cstr((rt_string)sv);
                    rt_sb_append_bytes(&sb, sv_cstr, (int64_t)strlen(sv_cstr));
                    rt_sb_append_cstr(&sb, "\"\n");
                }
                rt_sb_append_cstr(&sb, "\n");
                continue;
            }
        }

        // Simple key = value
        rt_sb_append_bytes(&sb, key_cstr, (int64_t)strlen(key_cstr));
        rt_sb_append_cstr(&sb, " = \"");
        const char *val_cstr = rt_string_cstr((rt_string)val);
        rt_sb_append_bytes(&sb, val_cstr, (int64_t)strlen(val_cstr));
        rt_sb_append_cstr(&sb, "\"\n");
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

void *rt_toml_get(void *root, rt_string key_path)
{
    if (!root || !key_path)
        return NULL;

    // Auto-parse: if root is a raw TOML string, parse it first
    /* S-15: use memcpy to avoid type-punning UB */
    uint64_t magic;
    memcpy(&magic, root, sizeof(magic));
    if (magic == RT_STRING_MAGIC)
    {
        root = rt_toml_parse((rt_string)root);
        if (!root)
            return NULL;
    }
    else if (magic == RT_BOX_STR)
    {
        // Boxed string (from Zia str→ptr conversion) — unbox and parse
        rt_string s = rt_unbox_str(root);
        if (s)
        {
            root = rt_toml_parse(s);
            if (!root)
                return NULL;
        }
    }

    const char *path = rt_string_cstr(key_path);
    void *current = root;

    while (*path)
    {
        const char *dot = strchr(path, '.');
        rt_string key;
        if (dot)
        {
            key = make_str(path, (int64_t)(dot - path));
            path = dot + 1;
        }
        else
        {
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

rt_string rt_toml_get_str(void *root, rt_string key_path)
{
    void *val = rt_toml_get(root, key_path);
    if (!val)
        return rt_string_from_bytes("", 0);
    // Check if value is a raw string
    /* S-15: use memcpy to avoid type-punning UB */
    uint64_t m;
    memcpy(&m, val, sizeof(m));
    if (m == RT_STRING_MAGIC)
        return (rt_string)val;
    // Try as boxed string
    int64_t tag;
    memcpy(&tag, val, sizeof(tag));
    if (tag == RT_BOX_STR)
        return rt_unbox_str(val);
    return rt_string_from_bytes("", 0);
}
