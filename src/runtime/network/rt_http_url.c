//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_url.c
// Purpose: URL parsing, encoding/decoding, and query string utilities.
// Key invariants:
//   - All returned strings are allocated; callers must free.
//   - parse_url_full zeroes the result struct before use.
// Ownership/Lifetime:
//   - rt_url_t instances are GC-managed via rt_obj_set_finalizer.
//   - Internal char* fields are heap-allocated and freed by free_url.
// Links: rt_network_http.c, rt_network.h
//
//===----------------------------------------------------------------------===//

#include "rt_network.h"

#include "rt_box.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations (defined in rt_io.c).
extern void rt_trap(const char *msg);
extern void rt_trap_net(const char *msg, int err_code);

//=============================================================================
// URL Parsing and Construction Implementation
//=============================================================================

/// @brief URL structure.
typedef struct rt_url
{
    char *scheme;   // URL scheme (e.g., "http", "https")
    char *user;     // Username (optional)
    char *pass;     // Password (optional)
    char *host;     // Hostname
    int64_t port;   // Port number (0 = not specified)
    char *path;     // Path component
    char *query;    // Query string (without leading ?)
    char *fragment; // Fragment (without leading #)
} rt_url_t;

/// @brief Get default port for a scheme.
/// @return Default port or 0 if unknown.
static int64_t default_port_for_scheme(const char *scheme)
{
    if (!scheme)
        return 0;
    if (strcmp(scheme, "http") == 0)
        return 80;
    if (strcmp(scheme, "https") == 0)
        return 443;
    if (strcmp(scheme, "ftp") == 0)
        return 21;
    if (strcmp(scheme, "ssh") == 0)
        return 22;
    if (strcmp(scheme, "telnet") == 0)
        return 23;
    if (strcmp(scheme, "smtp") == 0)
        return 25;
    if (strcmp(scheme, "dns") == 0)
        return 53;
    if (strcmp(scheme, "pop3") == 0)
        return 110;
    if (strcmp(scheme, "imap") == 0)
        return 143;
    if (strcmp(scheme, "ldap") == 0)
        return 389;
    if (strcmp(scheme, "ws") == 0)
        return 80;
    if (strcmp(scheme, "wss") == 0)
        return 443;
    return 0;
}

/// @brief Check if character is unreserved (RFC 3986).
static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

// Note: hex_char_to_int functionality provided by rt_hex_digit_value() in rt_internal.h

/// @brief Percent-encode a string.
/// @return Allocated string, caller must free.
static char *percent_encode(const char *str, bool encode_slash)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    // Worst case: every char becomes %XX
    char *result = (char *)malloc(len * 3 + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (is_unreserved(c) || (!encode_slash && c == '/'))
        {
            *p++ = c;
        }
        else
        {
            *p++ = '%';
            *p++ = rt_hex_chars_upper[(unsigned char)c >> 4];
            *p++ = rt_hex_chars_upper[(unsigned char)c & 0x0F];
        }
    }
    *p = '\0';
    return result;
}

/// @brief Percent-decode a string.
/// @return Allocated string, caller must free.
static char *percent_decode(const char *str)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] == '%' && i + 2 < len)
        {
            int high = rt_hex_digit_value(str[i + 1]);
            int low = rt_hex_digit_value(str[i + 2]);
            if (high >= 0 && low >= 0)
            {
                *p++ = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        else if (str[i] == '+')
        {
            // Plus is space in query strings
            *p++ = ' ';
            continue;
        }
        *p++ = str[i];
    }
    *p = '\0';
    return result;
}

/// @brief Duplicate a string safely (handles NULL).
static char *safe_strdup(const char *str)
{
    return str ? strdup(str) : NULL;
}

static void free_url(rt_url_t *url);

/// @brief Internal URL parsing.
/// @return 0 on success, -1 on error.
static int parse_url_full(const char *url_str, rt_url_t *result)
{
    memset(result, 0, sizeof(*result));

    if (!url_str || *url_str == '\0')
        return -1;

    const char *p = url_str;

    // Parse scheme (if present)
    const char *scheme_end = strstr(p, "://");
    bool has_authority = false;
    if (scheme_end)
    {
        size_t scheme_len = scheme_end - p;
        result->scheme = (char *)malloc(scheme_len + 1);
        if (!result->scheme)
            return -1;
        memcpy(result->scheme, p, scheme_len);
        result->scheme[scheme_len] = '\0';

        // Convert scheme to lowercase
        for (char *s = result->scheme; *s; s++)
        {
            if (*s >= 'A' && *s <= 'Z')
                *s = *s + ('a' - 'A');
        }

        p = scheme_end + 3; // Skip "://"
        has_authority = true;
    }
    else if (p[0] == '/' && p[1] == '/')
    {
        // Network-path reference (starts with //)
        p += 2;
        has_authority = true;
    }

    // Parse authority (userinfo@host:port) - only if we have a scheme or //
    if (has_authority && *p && *p != '/' && *p != '?' && *p != '#')
    {
        // Find end of authority
        const char *auth_end = p;
        while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
            auth_end++;

        // Check for userinfo (@)
        const char *at_sign = NULL;
        for (const char *s = p; s < auth_end; s++)
        {
            if (*s == '@')
            {
                at_sign = s;
                break;
            }
        }

        const char *host_start = p;
        if (at_sign)
        {
            // Parse userinfo
            const char *colon = NULL;
            for (const char *s = p; s < at_sign; s++)
            {
                if (*s == ':')
                {
                    colon = s;
                    break;
                }
            }

            if (colon)
            {
                // user:pass
                size_t user_len = colon - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }

                size_t pass_len = at_sign - colon - 1;
                result->pass = (char *)malloc(pass_len + 1);
                if (result->pass)
                {
                    memcpy(result->pass, colon + 1, pass_len);
                    result->pass[pass_len] = '\0';
                }
            }
            else
            {
                // Just user
                size_t user_len = at_sign - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }
            }
            host_start = at_sign + 1;
        }

        // Parse host:port
        // Check for IPv6 literal [...]
        const char *port_colon = NULL;
        if (*host_start == '[')
        {
            // IPv6 literal
            const char *bracket_end = strchr(host_start, ']');
            if (bracket_end && bracket_end < auth_end)
            {
                size_t host_len = bracket_end - host_start + 1;
                result->host = (char *)malloc(host_len + 1);
                if (result->host)
                {
                    memcpy(result->host, host_start, host_len);
                    result->host[host_len] = '\0';
                }
                if (bracket_end + 1 < auth_end && *(bracket_end + 1) == ':')
                    port_colon = bracket_end + 1;
            }
        }
        else
        {
            // Regular host
            for (const char *s = host_start; s < auth_end; s++)
            {
                if (*s == ':')
                {
                    port_colon = s;
                    break;
                }
            }

            const char *host_end = port_colon ? port_colon : auth_end;
            size_t host_len = host_end - host_start;
            result->host = (char *)malloc(host_len + 1);
            if (result->host)
            {
                memcpy(result->host, host_start, host_len);
                result->host[host_len] = '\0';
            }
        }

        // Parse port
        if (port_colon && port_colon + 1 < auth_end)
        {
            result->port = 0;
            for (const char *s = port_colon + 1; s < auth_end && *s >= '0' && *s <= '9'; s++)
            {
                result->port = result->port * 10 + (*s - '0');
            }
        }

        p = auth_end;
    }
    else if (has_authority)
    {
        free_url(result);
        return -1;
    }

    if (has_authority && (!result->host || result->host[0] == '\0'))
    {
        free_url(result);
        return -1;
    }

    // Parse path
    const char *path_start = p;
    const char *path_end = p;
    while (*path_end && *path_end != '?' && *path_end != '#')
        path_end++;

    if (path_end > path_start)
    {
        size_t path_len = path_end - path_start;
        result->path = (char *)malloc(path_len + 1);
        if (result->path)
        {
            memcpy(result->path, path_start, path_len);
            result->path[path_len] = '\0';
        }
    }

    p = path_end;

    // Parse query
    if (*p == '?')
    {
        p++;
        const char *query_end = p;
        while (*query_end && *query_end != '#')
            query_end++;

        size_t query_len = query_end - p;
        result->query = (char *)malloc(query_len + 1);
        if (result->query)
        {
            memcpy(result->query, p, query_len);
            result->query[query_len] = '\0';
        }

        p = query_end;
    }

    // Parse fragment
    if (*p == '#')
    {
        p++;
        size_t frag_len = strlen(p);
        result->fragment = (char *)malloc(frag_len + 1);
        if (result->fragment)
        {
            memcpy(result->fragment, p, frag_len);
            result->fragment[frag_len] = '\0';
        }
    }

    return 0;
}

/// @brief Free URL structure contents.
static void free_url(rt_url_t *url)
{
    if (url->scheme)
        free(url->scheme);
    if (url->user)
        free(url->user);
    if (url->pass)
        free(url->pass);
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    if (url->query)
        free(url->query);
    if (url->fragment)
        free(url->fragment);
    memset(url, 0, sizeof(*url));
}

static void rt_url_finalize(void *obj)
{
    if (!obj)
        return;
    rt_url_t *url = (rt_url_t *)obj;
    free_url(url);
}

void *rt_url_parse(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str)
        rt_trap_net("URL: Invalid URL string", Err_InvalidUrl);

    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);

    if (parse_url_full(str, url) != 0)
    {
        rt_trap_net("URL: Failed to parse URL", Err_InvalidUrl);
    }

    return url;
}

void *rt_url_new(void)
{
    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);
    return url;
}

rt_string rt_url_scheme(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->scheme)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->scheme, strlen(url->scheme));
}

void rt_url_set_scheme(void *obj, rt_string scheme)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(scheme);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->scheme)
        free(url->scheme);
    url->scheme = dup;

    // Convert to lowercase
    if (url->scheme)
    {
        for (char *p = url->scheme; *p; p++)
        {
            if (*p >= 'A' && *p <= 'Z')
                *p = *p + ('a' - 'A');
        }
    }
}

rt_string rt_url_host(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->host, strlen(url->host));
}

void rt_url_set_host(void *obj, rt_string host)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(host);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->host)
        free(url->host);
    url->host = dup;
}

int64_t rt_url_port(void *obj)
{
    if (!obj)
        return 0;

    return ((rt_url_t *)obj)->port;
}

void rt_url_set_port(void *obj, int64_t port)
{
    if (!obj)
        return;

    // Clamp to valid port range (0 = unset, 1-65535 = valid).
    if (port < 0)
        port = 0;
    else if (port > 65535)
        port = 65535;

    ((rt_url_t *)obj)->port = port;
}

rt_string rt_url_path(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->path)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->path, strlen(url->path));
}

void rt_url_set_path(void *obj, rt_string path)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(path);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->path)
        free(url->path);
    url->path = dup;
}

rt_string rt_url_query(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->query, strlen(url->query));
}

void rt_url_set_query(void *obj, rt_string query)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(query);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->query)
        free(url->query);
    url->query = dup;
}

rt_string rt_url_fragment(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->fragment)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->fragment, strlen(url->fragment));
}

void rt_url_set_fragment(void *obj, rt_string fragment)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(fragment);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->fragment)
        free(url->fragment);
    url->fragment = dup;
}

rt_string rt_url_user(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->user)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->user, strlen(url->user));
}

void rt_url_set_user(void *obj, rt_string user)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(user);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->user)
        free(url->user);
    url->user = dup;
}

rt_string rt_url_pass(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->pass)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->pass, strlen(url->pass));
}

void rt_url_set_pass(void *obj, rt_string pass)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    const char *str = rt_string_cstr(pass);
    char *dup = str ? strdup(str) : NULL;
    if (str && !dup)
        return; // OOM: preserve existing value.

    if (url->pass)
        free(url->pass);
    url->pass = dup;
}

rt_string rt_url_authority(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate size: user:pass@host:port
    size_t size = 0;
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass); // :pass
        size += 1;                         // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 22; // :PORT (max 19 digits for int64_t + colon + margin)

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    char *end = result + size + 1;
    if (url->user)
    {
        p += snprintf(p, (size_t)(end - p), "%s", url->user);
        if (url->pass)
            p += snprintf(p, (size_t)(end - p), ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += snprintf(p, (size_t)(end - p), "%s", url->host);
    if (url->port > 0)
        p += snprintf(p, (size_t)(end - p), ":%lld", (long long)url->port);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

rt_string rt_url_host_port(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    // Check if port is default for scheme
    int64_t default_port = default_port_for_scheme(url->scheme);
    bool show_port = url->port > 0 && url->port != default_port;

    size_t size = strlen(url->host) + (show_port ? 22 : 0);
    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    if (show_port)
        snprintf(result, size + 1, "%s:%lld", url->host, (long long)url->port);
    else
    {
        size_t hlen = strlen(url->host);
        memcpy(result, url->host, hlen + 1);
    }

    rt_string str = rt_string_from_bytes(result, strlen(result));
    free(result);
    return str;
}

rt_string rt_url_full(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate total size
    size_t size = 0;
    if (url->scheme)
        size += strlen(url->scheme) + 3; // scheme://
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass);
        size += 1; // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 22; // :PORT (max 19 digits for int64_t + colon + margin)
    if (url->path)
        size += strlen(url->path);
    if (url->query)
        size += 1 + strlen(url->query); // ?query
    if (url->fragment)
        size += 1 + strlen(url->fragment); // #fragment

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    char *end = result + size + 1;
    if (url->scheme)
        p += snprintf(p, (size_t)(end - p), "%s://", url->scheme);
    if (url->user)
    {
        p += snprintf(p, (size_t)(end - p), "%s", url->user);
        if (url->pass)
            p += snprintf(p, (size_t)(end - p), ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += snprintf(p, (size_t)(end - p), "%s", url->host);
    if (url->port > 0)
    {
        int64_t default_port = default_port_for_scheme(url->scheme);
        if (url->port != default_port)
            p += snprintf(p, (size_t)(end - p), ":%lld", (long long)url->port);
    }
    if (url->path)
        p += snprintf(p, (size_t)(end - p), "%s", url->path);
    if (url->query && url->query[0])
        p += snprintf(p, (size_t)(end - p), "?%s", url->query);
    if (url->fragment && url->fragment[0])
        p += snprintf(p, (size_t)(end - p), "#%s", url->fragment);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

void *rt_url_set_query_param(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (!name_str)
        return obj;

    // Encode name and value
    char *enc_name = percent_encode(name_str, true);
    char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

    if (!enc_name || !enc_value)
    {
        free(enc_name);
        free(enc_value);
        return obj;
    }

    // Parse existing query into map
    void *map = rt_url_decode_query(
        rt_string_from_bytes(url->query ? url->query : "", url->query ? strlen(url->query) : 0));

    // Set the new param
    void *boxed = rt_box_str(value);
    rt_map_set(map, name, boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Rebuild query string
    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);
    url->query = strdup(rt_string_cstr(new_query));

    free(enc_name);
    free(enc_value);
    return obj;
}

rt_string rt_url_get_query_param(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    void *boxed = rt_map_get(map, name);

    if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
        return rt_string_from_bytes("", 0);

    return rt_unbox_str(boxed);
}

int8_t rt_url_has_query_param(void *obj, rt_string name)
{
    if (!obj)
        return 0;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return 0;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    return rt_map_has(map, name);
}

void *rt_url_del_query_param(void *obj, rt_string name)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return obj;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    rt_map_remove(map, name);

    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);

    const char *query_str = rt_string_cstr(new_query);
    url->query = query_str && *query_str ? strdup(query_str) : NULL;

    return obj;
}

void *rt_url_query_map(void *obj)
{
    if (!obj)
        return rt_map_new();

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_map_new();

    return rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
}

void *rt_url_resolve(void *obj, rt_string relative)
{
    if (!obj)
        rt_trap("URL: NULL base URL");

    rt_url_t *base = (rt_url_t *)obj;
    const char *rel_str = rt_string_cstr(relative);

    if (!rel_str || *rel_str == '\0')
        return rt_url_clone(obj);

    // Parse relative URL
    rt_url_t rel;
    memset(&rel, 0, sizeof(rel));
    parse_url_full(rel_str, &rel);

    // Create new URL
    rt_url_t *result = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!result)
        rt_trap("URL: Memory allocation failed");
    memset(result, 0, sizeof(*result));
    rt_obj_set_finalizer(result, rt_url_finalize);

    // RFC 3986 resolution algorithm
    if (rel.scheme)
    {
        // Relative has scheme - use as-is
        result->scheme = safe_strdup(rel.scheme);
        result->user = safe_strdup(rel.user);
        result->pass = safe_strdup(rel.pass);
        result->host = safe_strdup(rel.host);
        result->port = rel.port;
        result->path = safe_strdup(rel.path);
        result->query = safe_strdup(rel.query);
    }
    else
    {
        if (rel.host)
        {
            // Relative has authority
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(rel.user);
            result->pass = safe_strdup(rel.pass);
            result->host = safe_strdup(rel.host);
            result->port = rel.port;
            result->path = safe_strdup(rel.path);
            result->query = safe_strdup(rel.query);
        }
        else
        {
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(base->user);
            result->pass = safe_strdup(base->pass);
            result->host = safe_strdup(base->host);
            result->port = base->port;

            if (!rel.path || *rel.path == '\0')
            {
                result->path = safe_strdup(base->path);
                if (rel.query)
                    result->query = safe_strdup(rel.query);
                else
                    result->query = safe_strdup(base->query);
            }
            else
            {
                if (rel.path[0] == '/')
                {
                    result->path = safe_strdup(rel.path);
                }
                else
                {
                    // Merge paths
                    if (!base->host || !base->path || *base->path == '\0')
                    {
                        // No base authority or empty base path
                        size_t len = strlen(rel.path) + 2;
                        result->path = (char *)malloc(len);
                        if (result->path)
                            snprintf(result->path, len, "/%s", rel.path);
                    }
                    else
                    {
                        // Remove last segment of base path
                        const char *last_slash = strrchr(base->path, '/');
                        if (last_slash)
                        {
                            size_t base_len = last_slash - base->path + 1;
                            size_t len = base_len + strlen(rel.path) + 1;
                            result->path = (char *)malloc(len);
                            if (result->path)
                            {
                                memcpy(result->path, base->path, base_len);
                                size_t rel_len = strlen(rel.path);
                                memcpy(result->path + base_len, rel.path, rel_len + 1);
                            }
                        }
                        else
                        {
                            result->path = safe_strdup(rel.path);
                        }
                    }
                }
                result->query = safe_strdup(rel.query);
            }
        }
    }

    result->fragment = safe_strdup(rel.fragment);

    // Clean up relative URL
    free_url(&rel);

    return result;
}

void *rt_url_clone(void *obj)
{
    if (!obj)
        return rt_url_new();

    rt_url_t *url = (rt_url_t *)obj;
    rt_url_t *clone = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!clone)
        rt_trap("URL: Memory allocation failed");
    memset(clone, 0, sizeof(*clone));
    rt_obj_set_finalizer(clone, rt_url_finalize);

    clone->scheme = safe_strdup(url->scheme);
    clone->user = safe_strdup(url->user);
    clone->pass = safe_strdup(url->pass);
    clone->host = safe_strdup(url->host);
    clone->port = url->port;
    clone->path = safe_strdup(url->path);
    clone->query = safe_strdup(url->query);
    clone->fragment = safe_strdup(url->fragment);

    return clone;
}

rt_string rt_url_encode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *encoded = percent_encode(str, true);
    if (!encoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(encoded, strlen(encoded));
    free(encoded);
    return result;
}

rt_string rt_url_decode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *decoded = percent_decode(str);
    if (!decoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(decoded, strlen(decoded));
    free(decoded);
    return result;
}

rt_string rt_url_encode_query(void *map)
{
    if (!map)
        return rt_string_from_bytes("", 0);

    void *keys = rt_map_keys(map);
    int64_t len = rt_seq_len(keys);

    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Build query string
    size_t cap = 256;
    char *result = (char *)malloc(cap);
    if (!result)
        return rt_string_from_bytes("", 0);

    size_t pos = 0;
    for (int64_t i = 0; i < len; i++)
    {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(map, key);

        const char *key_str = rt_string_cstr(key);
        rt_string value_str_handle = NULL;
        if (value && rt_box_type(value) == RT_BOX_STR)
        {
            value_str_handle = rt_unbox_str(value);
        }
        else
        {
            value_str_handle = (rt_string)value;
            if (value_str_handle)
                rt_string_ref(value_str_handle);
        }
        const char *value_str = value_str_handle ? rt_string_cstr(value_str_handle) : "";

        char *enc_key = percent_encode(key_str, true);
        char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

        if (!enc_key || !enc_value)
        {
            free(enc_key);
            free(enc_value);
            continue;
        }

        size_t needed = strlen(enc_key) + 1 + strlen(enc_value) + 2; // key=value&
        if (pos + needed >= cap)
        {
            cap = (pos + needed) * 2;
            char *new_result = (char *)realloc(result, cap);
            if (!new_result)
            {
                free(enc_key);
                free(enc_value);
                break;
            }
            result = new_result;
        }

        if (i > 0)
            result[pos++] = '&';
        pos += snprintf(result + pos, cap - pos, "%s=%s", enc_key, enc_value);

        free(enc_key);
        free(enc_value);
        if (value_str_handle)
            rt_string_unref(value_str_handle);
    }

    result[pos] = '\0';
    rt_string str = rt_string_from_bytes(result, pos);
    free(result);
    return str;
}

void *rt_url_decode_query(rt_string query)
{
    void *map = rt_map_new();
    const char *str = rt_string_cstr(query);

    if (!str || *str == '\0')
        return map;

    const char *p = str;
    while (*p)
    {
        // Find end of key
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');

        if (!eq || (amp && amp < eq))
        {
            // Key without value
            const char *end = amp ? amp : p + strlen(p);
            if (end > p)
            {
                char *key = (char *)malloc(end - p + 1);
                if (key)
                {
                    memcpy(key, p, end - p);
                    key[end - p] = '\0';
                    char *dec_key = percent_decode(key);
                    if (dec_key)
                    {
                        rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                        rt_string empty = rt_string_from_bytes("", 0);
                        void *boxed = rt_box_str(empty);
                        rt_map_set(map, key_str, boxed);
                        if (boxed && rt_obj_release_check0(boxed))
                            rt_obj_free(boxed);
                        rt_string_unref(empty);
                        free(dec_key);
                    }
                    free(key);
                }
            }
            p = amp ? amp + 1 : p + strlen(p);
        }
        else
        {
            // Key=Value
            size_t key_len = eq - p;
            const char *val_start = eq + 1;
            const char *val_end = amp ? amp : val_start + strlen(val_start);

            char *key = (char *)malloc(key_len + 1);
            char *val = (char *)malloc(val_end - val_start + 1);

            if (key && val)
            {
                memcpy(key, p, key_len);
                key[key_len] = '\0';
                memcpy(val, val_start, val_end - val_start);
                val[val_end - val_start] = '\0';

                char *dec_key = percent_decode(key);
                char *dec_val = percent_decode(val);

                if (dec_key && dec_val)
                {
                    rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                    rt_string val_str = rt_string_from_bytes(dec_val, strlen(dec_val));
                    void *boxed = rt_box_str(val_str);
                    rt_map_set(map, key_str, boxed);
                    if (boxed && rt_obj_release_check0(boxed))
                        rt_obj_free(boxed);
                    rt_string_unref(val_str);
                }

                free(dec_key);
                free(dec_val);
            }

            free(key);
            free(val);
            p = amp ? amp + 1 : val_end;
        }
    }

    return map;
}

int8_t rt_url_is_valid(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str || *str == '\0')
        return 0;

    // Reject strings with unencoded spaces (common non-URL indicator)
    for (const char *p = str; *p; p++)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            return 0;
    }

    // Reject URLs starting with :// (missing scheme)
    if (str[0] == ':' && str[1] == '/' && str[2] == '/')
        return 0;

    // Check for scheme - must have letters before ://
    const char *scheme_sep = strstr(str, "://");
    if (scheme_sep)
    {
        // Scheme must be at least 1 character and only contain [a-zA-Z0-9+.-]
        if (scheme_sep == str)
            return 0; // Empty scheme
        for (const char *p = str; p < scheme_sep; p++)
        {
            char c = *p;
            int valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
            if (!valid)
                return 0;
        }
        // First character of scheme must be a letter
        if (!((str[0] >= 'a' && str[0] <= 'z') || (str[0] >= 'A' && str[0] <= 'Z')))
            return 0;
    }

    rt_url_t url;
    memset(&url, 0, sizeof(url));

    int result = parse_url_full(str, &url);
    free_url(&url);

    return result == 0 ? 1 : 0;
}
