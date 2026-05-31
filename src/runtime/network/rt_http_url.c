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
//   - Public getters return owned rt_string handles; internal char* helpers return heap buffers.
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
extern void rt_trap_net(const char *msg, int err_code);

typedef struct rt_url rt_url_t;
static void free_url(rt_url_t *url);

// ---------------------------------------------------------------------------
// Trap + allocation helpers — these centralize the boilerplate that
// every URL accessor would otherwise repeat: NULL-check, raise a
// typed trap with a useful message, allocate-or-trap, and clone-or-
// rollback on alloc failure (with cleanup of the partially-built URL).
// ---------------------------------------------------------------------------

/// @brief Raise an `InvalidOperation` trap (e.g. operation on NULL Url).
/// @brief Trap with `Err_InvalidOperation` (typed: caller did the wrong thing).
static void rt_url_trap_invalid_operation(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, 0, msg);
}

/// @brief Raise a generic Runtime trap (e.g. memory allocation failure).
/// @brief Trap with `Err_RuntimeError` (untyped runtime failure — usually OOM).
static void rt_url_trap_runtime(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR, Err_RuntimeError, 0, msg);
}

/// @brief Cast `obj` to `rt_url_t*`, trapping with `context` if NULL.
/// @brief Validate + cast a Url handle. Traps with INVALID_OPERATION + the supplied context
/// message on null input — the message lets callers see which method had the bad receiver.
static rt_url_t *rt_url_require_obj(void *obj, const char *context) {
    if (!obj)
        rt_url_trap_invalid_operation(context);
    return (rt_url_t *)obj;
}

/// @brief `malloc(size)` or trap with `context` (returns a non-NULL buffer).
/// @brief malloc-or-trap: allocate `size` bytes; trap with RUNTIME_ERROR on failure.
static char *rt_url_alloc_or_trap(size_t size, const char *context) {
    char *buffer = (char *)malloc(size);
    if (!buffer)
        rt_url_trap_runtime(context);
    return buffer;
}

static void rt_url_size_add_or_trap(size_t *total, size_t value, const char *context) {
    if (!total || value == SIZE_MAX || *total > SIZE_MAX - value)
        rt_url_trap_runtime(context);
    *total += value;
}

/// @brief Duplicate `begin[0..len)` into a NUL-terminated string; trap-with-cleanup on OOM.
///
/// If allocation fails and `url` is non-NULL, the half-built URL is
/// freed before raising the trap so the caller doesn't leak a
/// partially-populated object.
static char *rt_url_dup_slice_or_trap_cleanup(rt_url_t *url,
                                              const char *begin,
                                              size_t len,
                                              const char *context) {
    if ((!begin && len > 0) || len == SIZE_MAX) {
        if (url)
            free_url(url);
        rt_url_trap_runtime(context);
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        if (url)
            free_url(url);
        rt_url_trap_runtime(context);
    }
    memcpy(copy, begin, len);
    copy[len] = '\0';
    return copy;
}

/// @brief Duplicate a NUL-terminated string with cleanup-on-OOM (NULL `str` returns NULL).
/// @brief strdup with cleanup: on OOM, calls `free_url(url)` first to avoid leaking a partially-
/// populated URL before trapping. Use during incremental URL construction.
static char *rt_url_strdup_or_trap_cleanup(rt_url_t *url, const char *str, const char *context) {
    if (!str)
        return NULL;
    return rt_url_dup_slice_or_trap_cleanup(url, str, strlen(str), context);
}

static int rt_url_string_has_embedded_nul(rt_string value) {
    if (!value)
        return 0;
    const char *str = rt_string_cstr(value);
    int64_t len64 = rt_str_len(value);
    if (!str || len64 <= 0)
        return 0;
    return memchr(str, '\0', (size_t)len64) != NULL;
}

static size_t rt_url_string_len_or_trap(rt_string value, const char *context) {
    if (!value)
        return 0;
    int64_t len64 = rt_str_len(value);
    if (len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX)
        rt_url_trap_runtime(context);
    return (size_t)len64;
}

/// @brief Duplicate the C-string content of a Viper rt_string into a heap C buffer.
/// @brief Duplicate an rt_string argument as a heap-owned C string; trap on OOM.
static char *rt_url_dup_string_arg(rt_string value, const char *context) {
    const char *str = value ? rt_string_cstr(value) : NULL;
    size_t len = rt_url_string_len_or_trap(value, context);
    if (str && len > 0 && memchr(str, '\0', len))
        rt_trap_net("URL: embedded NUL in component", Err_InvalidUrl);
    return str ? rt_url_dup_slice_or_trap_cleanup(NULL, str, len, context) : NULL;
}

/// @brief Wrap raw bytes in an rt_string or trap on alloc failure.
/// @brief Build an rt_string from raw bytes; trap with RUNTIME_ERROR if string allocation fails.
static rt_string rt_url_string_from_bytes_or_trap(const char *bytes,
                                                  size_t len,
                                                  const char *context) {
    rt_string str = rt_string_from_bytes(bytes, len);
    if (!str)
        rt_url_trap_runtime(context);
    return str;
}

/// @brief Validate a URI scheme per RFC 3986 §3.1: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ).
/// @return 1 if valid, 0 otherwise.
static int rt_url_scheme_is_valid(const char *scheme, size_t len) {
    if (!scheme || len == 0)
        return 0;
    if (!((scheme[0] >= 'a' && scheme[0] <= 'z') || (scheme[0] >= 'A' && scheme[0] <= 'Z')))
        return 0;
    for (size_t i = 0; i < len; ++i) {
        char c = scheme[i];
        int valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '+' || c == '-' || c == '.';
        if (!valid)
            return 0;
    }
    return 1;
}

//=============================================================================
// URL Parsing and Construction Implementation
//=============================================================================

/// @brief URL structure.
typedef struct rt_url {
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
static int64_t default_port_for_scheme(const char *scheme) {
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
static bool is_unreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

// Note: hex_char_to_int functionality provided by rt_hex_digit_value() in rt_internal.h

/// @brief Percent-encode a byte span.
/// @return Allocated string, caller must free.
static char *percent_encode_n(const char *str, size_t len, bool encode_slash, size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!str)
        len = 0;
    // Worst case: every char becomes %XX
    if (len > SIZE_MAX / 3)
        return NULL;
    char *result = (char *)malloc(len * 3 + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (is_unreserved(c) || (!encode_slash && c == '/')) {
            *p++ = c;
        } else {
            *p++ = '%';
            *p++ = rt_hex_chars_upper[(unsigned char)c >> 4];
            *p++ = rt_hex_chars_upper[(unsigned char)c & 0x0F];
        }
    }
    *p = '\0';
    if (out_len)
        *out_len = (size_t)(p - result);
    return result;
}

/// @brief Percent-decode a byte span.
/// @return Allocated string, caller must free.
static char *percent_decode_internal_n(const char *str,
                                       size_t len,
                                       bool plus_as_space,
                                       size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!str)
        len = 0;
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            int high = rt_hex_digit_value(str[i + 1]);
            int low = rt_hex_digit_value(str[i + 2]);
            if (high >= 0 && low >= 0) {
                *p++ = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        } else if (plus_as_space && str[i] == '+') {
            *p++ = ' ';
            continue;
        }
        *p++ = str[i];
    }
    *p = '\0';
    if (out_len)
        *out_len = (size_t)(p - result);
    return result;
}

/// @brief Internal URL parsing.
/// @return 0 on success, -1 on error.
static int parse_url_full(const char *url_str, rt_url_t *result) {
    memset(result, 0, sizeof(*result));

    if (!url_str || *url_str == '\0')
        return -1;

    const char *p = url_str;

    // Parse scheme (if present)
    const char *scheme_end = strstr(p, "://");
    bool has_authority = false;
    if (scheme_end) {
        size_t scheme_len = scheme_end - p;
        if (!rt_url_scheme_is_valid(p, scheme_len))
            return -1;
        result->scheme = rt_url_dup_slice_or_trap_cleanup(
            result, p, scheme_len, "URL.Parse: scheme allocation failed");

        // Convert scheme to lowercase
        for (char *s = result->scheme; *s; s++) {
            if (*s >= 'A' && *s <= 'Z')
                *s = *s + ('a' - 'A');
        }

        p = scheme_end + 3; // Skip "://"
        has_authority = true;
    } else if (p[0] == '/' && p[1] == '/') {
        // Network-path reference (starts with //)
        p += 2;
        has_authority = true;
    }

    // Parse authority (userinfo@host:port) - only if we have a scheme or //
    if (has_authority && *p && *p != '/' && *p != '?' && *p != '#') {
        // Find end of authority
        const char *auth_end = p;
        while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
            auth_end++;

        // Check for userinfo (@)
        const char *at_sign = NULL;
        for (const char *s = p; s < auth_end; s++) {
            if (*s == '@') {
                at_sign = s;
                break;
            }
        }

        const char *host_start = p;
        if (at_sign) {
            // Parse userinfo
            const char *colon = NULL;
            for (const char *s = p; s < at_sign; s++) {
                if (*s == ':') {
                    colon = s;
                    break;
                }
            }

            if (colon) {
                // user:pass
                size_t user_len = colon - p;
                result->user = rt_url_dup_slice_or_trap_cleanup(
                    result, p, user_len, "URL.Parse: user allocation failed");

                size_t pass_len = at_sign - colon - 1;
                result->pass = rt_url_dup_slice_or_trap_cleanup(
                    result, colon + 1, pass_len, "URL.Parse: password allocation failed");
            } else {
                // Just user
                size_t user_len = at_sign - p;
                result->user = rt_url_dup_slice_or_trap_cleanup(
                    result, p, user_len, "URL.Parse: user allocation failed");
            }
            host_start = at_sign + 1;
        }

        // Parse host:port
        // Check for IPv6 literal [...]
        const char *port_colon = NULL;
        if (*host_start == '[') {
            // IPv6 literal
            const char *bracket_end = strchr(host_start, ']');
            if (bracket_end && bracket_end < auth_end) {
                if (bracket_end + 1 < auth_end && *(bracket_end + 1) != ':') {
                    free_url(result);
                    return -1;
                }
                size_t host_len = bracket_end - host_start + 1;
                result->host = rt_url_dup_slice_or_trap_cleanup(
                    result, host_start, host_len, "URL.Parse: host allocation failed");
                if (bracket_end + 1 < auth_end && *(bracket_end + 1) == ':')
                    port_colon = bracket_end + 1;
            } else {
                free_url(result);
                return -1;
            }
        } else {
            // Regular host
            for (const char *s = host_start; s < auth_end; s++) {
                if (*s == ':') {
                    port_colon = s;
                    break;
                }
            }

            const char *host_end = port_colon ? port_colon : auth_end;
            size_t host_len = host_end - host_start;
            result->host = rt_url_dup_slice_or_trap_cleanup(
                result, host_start, host_len, "URL.Parse: host allocation failed");
        }

        // Parse port
        if (port_colon && port_colon + 1 >= auth_end) {
            free_url(result);
            return -1;
        }
        if (port_colon) {
            result->port = 0;
            const char *s = port_colon + 1;
            if (*s < '0' || *s > '9') {
                free_url(result);
                return -1;
            }
            for (; s < auth_end && *s >= '0' && *s <= '9'; s++) {
                int digit = *s - '0';
                if (result->port > (INT64_MAX - digit) / 10) {
                    free_url(result);
                    return -1;
                }
                result->port = result->port * 10 + (*s - '0');
            }
            if (result->port > 65535) {
                free_url(result);
                return -1;
            }
            if (s != auth_end) {
                free_url(result);
                return -1;
            }
        }

        p = auth_end;
    } else if (has_authority) {
        free_url(result);
        return -1;
    }

    if (has_authority && (!result->host || result->host[0] == '\0')) {
        free_url(result);
        return -1;
    }

    // Parse path
    const char *path_start = p;
    const char *path_end = p;
    while (*path_end && *path_end != '?' && *path_end != '#')
        path_end++;

    if (path_end > path_start) {
        size_t path_len = path_end - path_start;
        result->path = rt_url_dup_slice_or_trap_cleanup(
            result, path_start, path_len, "URL.Parse: path allocation failed");
    }

    p = path_end;

    // Parse query
    if (*p == '?') {
        p++;
        const char *query_end = p;
        while (*query_end && *query_end != '#')
            query_end++;

        size_t query_len = query_end - p;
        result->query = rt_url_dup_slice_or_trap_cleanup(
            result, p, query_len, "URL.Parse: query allocation failed");

        p = query_end;
    }

    // Parse fragment
    if (*p == '#') {
        p++;
        size_t frag_len = strlen(p);
        result->fragment = rt_url_dup_slice_or_trap_cleanup(
            result, p, frag_len, "URL.Parse: fragment allocation failed");
    }

    return 0;
}

/// @brief Free URL structure contents.
static void free_url(rt_url_t *url) {
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

/// @brief Replace a string field in the URL with a duplicate of `value` (optionally lowercased).
///
/// Frees the prior value, dups the new one, and applies ASCII
/// lowercasing if `lowercase != 0`. Used by every `set_*` accessor
/// that updates a single component.
static void rt_url_replace_field(char **slot, rt_string value, const char *context, int lowercase) {
    char *dup = rt_url_dup_string_arg(value, context);
    free(*slot);
    *slot = dup;
    if (lowercase && *slot) {
        for (char *p = *slot; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z')
                *p = (char)(*p + ('a' - 'A'));
        }
    }
}

/// @brief Resolve `.` and `..` segments and collapse double-slashes per RFC 3986 §5.2.4.
///
/// Walks the segments left-to-right, pushing onto a stack;
/// `..` pops the previous segment, `.` is dropped, others
/// accumulate. The result is a freshly-allocated path string
/// (caller `free`s).
static char *normalize_path(const char *path) {
    if (!path || *path == '\0')
        return rt_url_strdup_or_trap_cleanup(NULL, "/", "URL.NormalizePath: allocation failed");

    size_t input_len = strlen(path);
    if (input_len == SIZE_MAX || input_len + 1 > SIZE_MAX / sizeof(char *))
        rt_url_trap_runtime("URL.NormalizePath: length overflow");
    char **segments = (char **)calloc(input_len + 1, sizeof(char *));
    if (!segments)
        rt_url_trap_runtime("URL.NormalizePath: segment allocation failed");

    int absolute = path[0] == '/';
    size_t segment_count = 0;
    const char *cursor = path;
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        const char *segment_end = cursor;
        while (*segment_end && *segment_end != '/')
            segment_end++;
        size_t segment_len = (size_t)(segment_end - cursor);
        if (segment_len == 0)
            break;
        if (segment_len == SIZE_MAX)
            goto fail;

        char *segment = (char *)malloc(segment_len + 1);
        if (!segment)
            goto fail;
        memcpy(segment, cursor, segment_len);
        segment[segment_len] = '\0';

        if (strcmp(segment, ".") == 0) {
            free(segment);
        } else if (strcmp(segment, "..") == 0) {
            free(segment);
            if (segment_count > 0)
                free(segments[--segment_count]);
        } else {
            segments[segment_count++] = segment;
        }

        cursor = segment_end;
    }

    size_t out_len = absolute ? 1 : 0;
    for (size_t i = 0; i < segment_count; i++) {
        size_t seg_len = strlen(segments[i]);
        if (out_len > SIZE_MAX - seg_len - 1)
            goto fail;
        out_len += seg_len + 1;
    }
    if (out_len == 0)
        out_len = 1;
    if (out_len == SIZE_MAX)
        goto fail;

    char *out = (char *)malloc(out_len + 1);
    if (!out)
        goto fail;

    size_t pos = 0;
    if (absolute)
        out[pos++] = '/';
    for (size_t i = 0; i < segment_count; i++) {
        size_t seg_len = strlen(segments[i]);
        memcpy(out + pos, segments[i], seg_len);
        pos += seg_len;
        if (i + 1 < segment_count)
            out[pos++] = '/';
    }
    if (pos == 0)
        out[pos++] = '/';
    out[pos] = '\0';

    for (size_t i = 0; i < segment_count; i++)
        free(segments[i]);
    free(segments);
    return out;

fail:
    for (size_t i = 0; i < segment_count; i++)
        free(segments[i]);
    free(segments);
    rt_url_trap_runtime("URL.NormalizePath: allocation failed");
    return NULL;
}

/// @brief GC finalizer — `free_url` releases every component string and the body buffer.
static void rt_url_finalize(void *obj) {
    if (!obj)
        return;
    rt_url_t *url = (rt_url_t *)obj;
    free_url(url);
}

// ===========================================================================
// Url public API
//
// Each accessor is null-safe via `rt_url_require_obj` (which traps
// with `Err_InvalidOperation` if the URL is NULL). Setters accept
// the canonical Viper rt_string and store a heap copy; getters
// return fresh rt_string objects.
// ===========================================================================

/// @brief Parse `url_str` per RFC 3986 into a Url object; traps on syntactic failure.
void *rt_url_parse(rt_string url_str) {
    const char *str = url_str ? rt_string_cstr(url_str) : NULL;
    if (!str)
        rt_trap_net("URL: Invalid URL string", Err_InvalidUrl);
    if (rt_url_string_has_embedded_nul(url_str))
        rt_trap_net("URL: Invalid URL string", Err_InvalidUrl);

    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_url_trap_runtime("URL.Parse: memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);

    if (parse_url_full(str, url) != 0) {
        if (rt_obj_release_check0(url))
            rt_obj_free(url);
        rt_trap_net("URL: Failed to parse URL", Err_InvalidUrl);
    }

    return url;
}

/// @brief Allocate an empty Url with all components NULL.
void *rt_url_new(void) {
    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_url_trap_runtime("URL.New: memory allocation failed");

    memset(url, 0, sizeof(*url));
    rt_obj_set_finalizer(url, rt_url_finalize);
    return url;
}

// ---------------------------------------------------------------------------
// Per-component getters/setters — each pair reads or writes one
// piece of the URL: scheme, host, port, path, query, fragment,
// user, pass. Setters validate (e.g. scheme syntax) and lower-case
// scheme/host (case-insensitive per RFC 3986). Getters return a
// fresh `rt_string` (or empty string for unset fields).
// ---------------------------------------------------------------------------

/// @brief Read the URL's scheme component (e.g. "https"). Empty string if unset.
rt_string rt_url_scheme(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Scheme: null receiver");
    if (!url->scheme)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->scheme, strlen(url->scheme), "URL.Scheme: string allocation failed");
}

void rt_url_set_scheme(void *obj, rt_string scheme) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Scheme: null receiver");
    const char *scheme_str = scheme ? rt_string_cstr(scheme) : NULL;
    size_t scheme_len = rt_url_string_len_or_trap(scheme, "URL.set_Scheme: invalid length");
    if (!scheme_str || scheme_len == 0) {
        free(url->scheme);
        url->scheme = NULL;
        return;
    }
    if (memchr(scheme_str, '\0', scheme_len) ||
        !rt_url_scheme_is_valid(scheme_str, scheme_len))
        rt_trap_net("URL.set_Scheme: invalid scheme", Err_InvalidUrl);
    rt_url_replace_field(&url->scheme, scheme, "URL.set_Scheme: allocation failed", 1);
}

rt_string rt_url_host(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Host: null receiver");
    if (!url->host)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->host, strlen(url->host), "URL.Host: string allocation failed");
}

void rt_url_set_host(void *obj, rt_string host) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Host: null receiver");
    rt_url_replace_field(&url->host, host, "URL.set_Host: allocation failed", 0);
}

int64_t rt_url_port(void *obj) {
    return rt_url_require_obj(obj, "URL.Port: null receiver")->port;
}

void rt_url_set_port(void *obj, int64_t port) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Port: null receiver");

    if (port < 0 || port > 65535)
        rt_trap_net("URL.set_Port: invalid port", Err_InvalidUrl);

    url->port = port;
}

static bool rt_url_host_needs_brackets(const char *host) {
    return host && host[0] != '[' && strchr(host, ':') != NULL;
}

static size_t rt_url_formatted_host_len(const char *host) {
    if (!host)
        return 0;
    size_t len = strlen(host);
    if (rt_url_host_needs_brackets(host)) {
        if (len > SIZE_MAX - 2)
            return SIZE_MAX;
        len += 2;
    }
    return len;
}

static char *rt_url_append_formatted_host(char *p, char *end, const char *host) {
    if (!host)
        return p;
    if (rt_url_host_needs_brackets(host))
        p += snprintf(p, (size_t)(end - p), "[%s]", host);
    else
        p += snprintf(p, (size_t)(end - p), "%s", host);
    return p;
}

rt_string rt_url_path(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Path: null receiver");
    if (!url->path)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->path, strlen(url->path), "URL.Path: string allocation failed");
}

void rt_url_set_path(void *obj, rt_string path) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Path: null receiver");
    rt_url_replace_field(&url->path, path, "URL.set_Path: allocation failed", 0);
}

rt_string rt_url_query(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Query: null receiver");
    if (!url->query)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->query, strlen(url->query), "URL.Query: string allocation failed");
}

void rt_url_set_query(void *obj, rt_string query) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Query: null receiver");
    rt_url_replace_field(&url->query, query, "URL.set_Query: allocation failed", 0);
}

rt_string rt_url_fragment(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Fragment: null receiver");
    if (!url->fragment)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->fragment, strlen(url->fragment), "URL.Fragment: string allocation failed");
}

void rt_url_set_fragment(void *obj, rt_string fragment) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Fragment: null receiver");
    rt_url_replace_field(&url->fragment, fragment, "URL.set_Fragment: allocation failed", 0);
}

rt_string rt_url_user(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.User: null receiver");
    if (!url->user)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->user, strlen(url->user), "URL.User: string allocation failed");
}

void rt_url_set_user(void *obj, rt_string user) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_User: null receiver");
    rt_url_replace_field(&url->user, user, "URL.set_User: allocation failed", 0);
}

rt_string rt_url_pass(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Pass: null receiver");
    if (!url->pass)
        return rt_str_empty();

    return rt_url_string_from_bytes_or_trap(
        url->pass, strlen(url->pass), "URL.Pass: string allocation failed");
}

void rt_url_set_pass(void *obj, rt_string pass) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.set_Pass: null receiver");
    rt_url_replace_field(&url->pass, pass, "URL.set_Pass: allocation failed", 0);
}

/// @brief Compose the userinfo + host + port part of the URL (e.g. `user:pass@host:port`).
/// Each component is included only when set; returned as a fresh rt_string.
rt_string rt_url_authority(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Authority: null receiver");

    // Calculate size: user:pass@host:port
    size_t size = 0;
    if (url->user) {
        rt_url_size_add_or_trap(&size, strlen(url->user), "URL.Authority: length overflow");
        if (url->pass) {
            rt_url_size_add_or_trap(&size, 1, "URL.Authority: length overflow"); // :
            rt_url_size_add_or_trap(
                &size, strlen(url->pass), "URL.Authority: length overflow");
        }
        rt_url_size_add_or_trap(&size, 1, "URL.Authority: length overflow");     // @
    }
    if (url->host)
        rt_url_size_add_or_trap(
            &size, rt_url_formatted_host_len(url->host), "URL.Authority: length overflow");
    if (url->port > 0)
        rt_url_size_add_or_trap(
            &size, 22, "URL.Authority: length overflow"); // :PORT

    if (size == 0)
        return rt_str_empty();
    if (size == SIZE_MAX)
        rt_url_trap_runtime("URL.Authority: length overflow");

    char *result = rt_url_alloc_or_trap(size + 1, "URL.Authority: allocation failed");

    char *p = result;
    char *end = result + size + 1;
    if (url->user) {
        p += snprintf(p, (size_t)(end - p), "%s", url->user);
        if (url->pass)
            p += snprintf(p, (size_t)(end - p), ":%s", url->pass);
        *p++ = '@';
    }
    p = rt_url_append_formatted_host(p, end, url->host);
    if (url->port > 0)
        p += snprintf(p, (size_t)(end - p), ":%lld", (long long)url->port);

    rt_string str = rt_url_string_from_bytes_or_trap(
        result, (size_t)(p - result), "URL.Authority: string allocation failed");
    free(result);
    return str;
}

/// @brief Compose just `host:port` (no userinfo, no scheme). IPv6 hosts are bracketed.
/// @brief Format the `host:port` portion only (no scheme, no path). Default ports for the scheme
/// (80 for http, 443 for https, etc.) are omitted. Useful for SNI / Host header construction.
rt_string rt_url_host_port(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.HostPort: null receiver");
    if (!url->host)
        return rt_str_empty();

    // Check if port is default for scheme
    int64_t default_port = default_port_for_scheme(url->scheme);
    bool show_port = url->port > 0 && url->port != default_port;

    size_t size = 0;
    rt_url_size_add_or_trap(
        &size, rt_url_formatted_host_len(url->host), "URL.HostPort: length overflow");
    if (show_port)
        rt_url_size_add_or_trap(&size, 22, "URL.HostPort: length overflow");
    if (size == SIZE_MAX)
        rt_url_trap_runtime("URL.HostPort: length overflow");
    char *result = rt_url_alloc_or_trap(size + 1, "URL.HostPort: allocation failed");

    char *p = result;
    char *end = result + size + 1;
    p = rt_url_append_formatted_host(p, end, url->host);
    if (show_port)
        snprintf(p, (size_t)(end - p), ":%lld", (long long)url->port);

    rt_string str = rt_url_string_from_bytes_or_trap(
        result, strlen(result), "URL.HostPort: string allocation failed");
    free(result);
    return str;
}

/// @brief Reconstruct the full URL string `scheme://authority/path?query#fragment`.
/// Each component is included only if set; the result round-trips through `rt_url_parse`.
rt_string rt_url_full(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Full: null receiver");

    // Calculate total size
    size_t size = 0;
    if (url->scheme) {
        rt_url_size_add_or_trap(&size, strlen(url->scheme), "URL.Full: length overflow");
        rt_url_size_add_or_trap(&size, 3, "URL.Full: length overflow"); // ://
    }
    if (url->user) {
        rt_url_size_add_or_trap(&size, strlen(url->user), "URL.Full: length overflow");
        if (url->pass) {
            rt_url_size_add_or_trap(&size, 1, "URL.Full: length overflow"); // :
            rt_url_size_add_or_trap(&size, strlen(url->pass), "URL.Full: length overflow");
        }
        rt_url_size_add_or_trap(&size, 1, "URL.Full: length overflow"); // @
    }
    if (url->host)
        rt_url_size_add_or_trap(
            &size, rt_url_formatted_host_len(url->host), "URL.Full: length overflow");
    if (url->port > 0)
        rt_url_size_add_or_trap(&size, 22, "URL.Full: length overflow"); // :PORT
    if (url->path)
        rt_url_size_add_or_trap(&size, strlen(url->path), "URL.Full: length overflow");
    if (url->query) {
        rt_url_size_add_or_trap(&size, 1, "URL.Full: length overflow"); // ?
        rt_url_size_add_or_trap(&size, strlen(url->query), "URL.Full: length overflow");
    }
    if (url->fragment) {
        rt_url_size_add_or_trap(&size, 1, "URL.Full: length overflow"); // #
        rt_url_size_add_or_trap(&size, strlen(url->fragment), "URL.Full: length overflow");
    }

    if (size == 0)
        return rt_str_empty();
    if (size == SIZE_MAX)
        rt_url_trap_runtime("URL.Full: length overflow");

    char *result = rt_url_alloc_or_trap(size + 1, "URL.Full: allocation failed");

    char *p = result;
    char *end = result + size + 1;
    if (url->scheme)
        p += snprintf(p, (size_t)(end - p), "%s://", url->scheme);
    if (url->user) {
        p += snprintf(p, (size_t)(end - p), "%s", url->user);
        if (url->pass)
            p += snprintf(p, (size_t)(end - p), ":%s", url->pass);
        *p++ = '@';
    }
    p = rt_url_append_formatted_host(p, end, url->host);
    if (url->port > 0) {
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

    rt_string str = rt_url_string_from_bytes_or_trap(
        result, (size_t)(p - result), "URL.Full: string allocation failed");
    free(result);
    return str;
}

/// @brief Set a query parameter (`?name=value`); replaces if it already exists.
/// Re-encodes the URL's query string after mutation. Returns `obj` for chaining.
void *rt_url_set_query_param(void *obj, rt_string name, rt_string value) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.SetQueryParam: null receiver");
    const char *name_str = name ? rt_string_cstr(name) : NULL;

    if (!name_str)
        rt_url_trap_invalid_operation("URL.SetQueryParam: null query name");

    // Parse existing query into map
    rt_string tmp_query =
        rt_url_string_from_bytes_or_trap(url->query ? url->query : "",
                                         url->query ? strlen(url->query) : 0,
                                         "URL.SetQueryParam: string allocation failed");
    void *map = rt_url_decode_query(tmp_query);
    rt_string_unref(tmp_query);

    // Set the new param
    rt_map_set_str(map, name, value ? value : rt_str_empty());

    // Rebuild query string
    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);
    const char *new_query_str = rt_string_cstr(new_query);
    url->query = (new_query_str && *new_query_str)
                     ? rt_url_strdup_or_trap_cleanup(
                           NULL, new_query_str, "URL.SetQueryParam: allocation failed")
                     : NULL;
    rt_string_unref(new_query);

    // Release temporary map
    if (map && rt_obj_release_check0(map))
        rt_obj_free(map);
    return obj;
}

/// @brief Read the value of one query parameter (URL-decoded). Empty string if missing.
rt_string rt_url_get_query_param(void *obj, rt_string name) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.GetQueryParam: null receiver");
    if (!url->query)
        return rt_str_empty();

    rt_string tmp_query = rt_url_string_from_bytes_or_trap(
        url->query, strlen(url->query), "URL.GetQueryParam: string allocation failed");
    void *map = rt_url_decode_query(tmp_query);
    rt_string_unref(tmp_query);

    void *stored = rt_map_get(map, name);
    rt_string result = stored ? (rt_string)stored : rt_str_empty();
    if (stored)
        rt_string_ref(result);

    if (map && rt_obj_release_check0(map))
        rt_obj_free(map);

    return result;
}

/// @brief Predicate: is the named query parameter present at all?
int8_t rt_url_has_query_param(void *obj, rt_string name) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.HasQueryParam: null receiver");
    if (!url->query)
        return 0;

    rt_string tmp_query = rt_url_string_from_bytes_or_trap(
        url->query, strlen(url->query), "URL.HasQueryParam: string allocation failed");
    void *map = rt_url_decode_query(tmp_query);
    rt_string_unref(tmp_query);

    int8_t result = rt_map_has(map, name);

    if (map && rt_obj_release_check0(map))
        rt_obj_free(map);

    return result;
}

/// @brief Remove a query parameter (no-op if missing). Returns `obj` for chaining.
void *rt_url_del_query_param(void *obj, rt_string name) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.DelQueryParam: null receiver");
    if (!url->query)
        return obj;

    rt_string tmp_query = rt_url_string_from_bytes_or_trap(
        url->query, strlen(url->query), "URL.DelQueryParam: string allocation failed");
    void *map = rt_url_decode_query(tmp_query);
    rt_string_unref(tmp_query);

    rt_map_remove(map, name);

    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);

    const char *query_str = rt_string_cstr(new_query);
    url->query =
        (query_str && *query_str)
            ? rt_url_strdup_or_trap_cleanup(NULL, query_str, "URL.DelQueryParam: allocation failed")
            : NULL;
    rt_string_unref(new_query);

    if (map && rt_obj_release_check0(map))
        rt_obj_free(map);

    return obj;
}

/// @brief Decode the URL's query string into a fresh `Map[String, String]`.
/// Repeated keys collapse to the last-occurring value.
void *rt_url_query_map(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.QueryMap: null receiver");
    if (!url->query)
        return rt_map_new();

    rt_string query = rt_url_string_from_bytes_or_trap(
        url->query, strlen(url->query), "URL.QueryMap: string allocation failed");
    void *map = rt_url_decode_query(query);
    rt_string_unref(query);
    return map;
}

/// @brief Resolve a relative URL against this base URL per RFC 3986 §5.2 (Reference Resolution).
///
/// Handles all the standard cases: schemed (use as-is), authority-
/// only (`//host/path`), absolute-path (`/path`), relative-path
/// (`path`), and same-document (`#fragment`). Returns a fresh Url.
void *rt_url_resolve(void *obj, rt_string relative) {
    rt_url_t *base = rt_url_require_obj(obj, "URL.Resolve: null receiver");
    const char *rel_str = relative ? rt_string_cstr(relative) : NULL;

    if (!rel_str || *rel_str == '\0')
        return rt_url_clone(obj);
    if (rt_url_string_has_embedded_nul(relative))
        rt_trap_net("URL.Resolve: invalid relative URL", Err_InvalidUrl);

    // Parse relative URL.
    rt_url_t rel;
    memset(&rel, 0, sizeof(rel));
    if (parse_url_full(rel_str, &rel) != 0) {
        free_url(&rel);
        rt_trap_net("URL.Resolve: invalid relative URL", Err_InvalidUrl);
    }

    // Create new URL
    rt_url_t *result = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!result)
        rt_url_trap_runtime("URL.Resolve: memory allocation failed");
    memset(result, 0, sizeof(*result));
    rt_obj_set_finalizer(result, rt_url_finalize);

    // RFC 3986 resolution algorithm
    if (rel.scheme) {
        // Relative has scheme - use as-is
        result->scheme = rt_url_strdup_or_trap_cleanup(
            result, rel.scheme, "URL.Resolve: scheme allocation failed");
        result->user =
            rt_url_strdup_or_trap_cleanup(result, rel.user, "URL.Resolve: user allocation failed");
        result->pass = rt_url_strdup_or_trap_cleanup(
            result, rel.pass, "URL.Resolve: password allocation failed");
        result->host =
            rt_url_strdup_or_trap_cleanup(result, rel.host, "URL.Resolve: host allocation failed");
        result->port = rel.port;
        result->path =
            rt_url_strdup_or_trap_cleanup(result, rel.path, "URL.Resolve: path allocation failed");
        result->query = rt_url_strdup_or_trap_cleanup(
            result, rel.query, "URL.Resolve: query allocation failed");
    } else {
        if (rel.host) {
            // Relative has authority
            result->scheme = rt_url_strdup_or_trap_cleanup(
                result, base->scheme, "URL.Resolve: scheme allocation failed");
            result->user = rt_url_strdup_or_trap_cleanup(
                result, rel.user, "URL.Resolve: user allocation failed");
            result->pass = rt_url_strdup_or_trap_cleanup(
                result, rel.pass, "URL.Resolve: password allocation failed");
            result->host = rt_url_strdup_or_trap_cleanup(
                result, rel.host, "URL.Resolve: host allocation failed");
            result->port = rel.port;
            result->path = rt_url_strdup_or_trap_cleanup(
                result, rel.path, "URL.Resolve: path allocation failed");
            result->query = rt_url_strdup_or_trap_cleanup(
                result, rel.query, "URL.Resolve: query allocation failed");
        } else {
            result->scheme = rt_url_strdup_or_trap_cleanup(
                result, base->scheme, "URL.Resolve: scheme allocation failed");
            result->user = rt_url_strdup_or_trap_cleanup(
                result, base->user, "URL.Resolve: user allocation failed");
            result->pass = rt_url_strdup_or_trap_cleanup(
                result, base->pass, "URL.Resolve: password allocation failed");
            result->host = rt_url_strdup_or_trap_cleanup(
                result, base->host, "URL.Resolve: host allocation failed");
            result->port = base->port;

            if (!rel.path || *rel.path == '\0') {
                result->path = rt_url_strdup_or_trap_cleanup(
                    result, base->path, "URL.Resolve: path allocation failed");
                if (rel.query)
                    result->query = rt_url_strdup_or_trap_cleanup(
                        result, rel.query, "URL.Resolve: query allocation failed");
                else
                    result->query = rt_url_strdup_or_trap_cleanup(
                        result, base->query, "URL.Resolve: query allocation failed");
            } else {
                if (rel.path[0] == '/') {
                    result->path = normalize_path(rel.path);
                } else {
                    // Merge paths
                    if (!base->host || !base->path || *base->path == '\0') {
                        // No base authority or empty base path
                        size_t rel_len = strlen(rel.path);
                        if (rel_len > SIZE_MAX - 2) {
                            free_url(&rel);
                            if (rt_obj_release_check0(result))
                                rt_obj_free(result);
                            rt_url_trap_runtime("URL.Resolve: path length overflow");
                        }
                        size_t len = rel_len + 2;
                        result->path =
                            rt_url_alloc_or_trap(len, "URL.Resolve: path allocation failed");
                        snprintf(result->path, len, "/%s", rel.path);
                    } else {
                        // Remove last segment of base path
                        const char *last_slash = strrchr(base->path, '/');
                        if (last_slash) {
                            size_t base_len = last_slash - base->path + 1;
                            size_t rel_len = strlen(rel.path);
                            if (base_len > SIZE_MAX - rel_len - 1) {
                                free_url(&rel);
                                if (rt_obj_release_check0(result))
                                    rt_obj_free(result);
                                rt_url_trap_runtime("URL.Resolve: path length overflow");
                            }
                            size_t len = base_len + rel_len + 1;
                            result->path =
                                rt_url_alloc_or_trap(len, "URL.Resolve: path allocation failed");
                            memcpy(result->path, base->path, base_len);
                            memcpy(result->path + base_len, rel.path, rel_len + 1);
                        } else {
                            result->path = rt_url_strdup_or_trap_cleanup(
                                result, rel.path, "URL.Resolve: path allocation failed");
                        }
                    }
                }
                if (result->path) {
                    char *normalized = normalize_path(result->path);
                    free(result->path);
                    result->path = normalized;
                }
                result->query = rt_url_strdup_or_trap_cleanup(
                    result, rel.query, "URL.Resolve: query allocation failed");
            }
        }
    }

    result->fragment = rt_url_strdup_or_trap_cleanup(
        result, rel.fragment, "URL.Resolve: fragment allocation failed");

    // Clean up relative URL
    free_url(&rel);

    return result;
}

/// @brief Deep-copy a Url — every component string is duplicated so the clone is fully independent.
void *rt_url_clone(void *obj) {
    rt_url_t *url = rt_url_require_obj(obj, "URL.Clone: null receiver");
    rt_url_t *clone = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!clone)
        rt_url_trap_runtime("URL.Clone: memory allocation failed");
    memset(clone, 0, sizeof(*clone));
    rt_obj_set_finalizer(clone, rt_url_finalize);

    clone->scheme =
        rt_url_strdup_or_trap_cleanup(clone, url->scheme, "URL.Clone: scheme allocation failed");
    clone->user =
        rt_url_strdup_or_trap_cleanup(clone, url->user, "URL.Clone: user allocation failed");
    clone->pass =
        rt_url_strdup_or_trap_cleanup(clone, url->pass, "URL.Clone: password allocation failed");
    clone->host =
        rt_url_strdup_or_trap_cleanup(clone, url->host, "URL.Clone: host allocation failed");
    clone->port = url->port;
    clone->path =
        rt_url_strdup_or_trap_cleanup(clone, url->path, "URL.Clone: path allocation failed");
    clone->query =
        rt_url_strdup_or_trap_cleanup(clone, url->query, "URL.Clone: query allocation failed");
    clone->fragment = rt_url_strdup_or_trap_cleanup(
        clone, url->fragment, "URL.Clone: fragment allocation failed");

    return clone;
}

/// @brief URL-encode (percent-escape) a string per RFC 3986 unreserved-characters rule.
/// Reserved + non-ASCII bytes become `%XX` triples.
rt_string rt_url_encode(rt_string text) {
    const char *str = text ? rt_string_cstr(text) : "";
    size_t text_len = rt_url_string_len_or_trap(text, "URL.Encode: invalid input length");
    size_t encoded_len = 0;
    char *encoded = percent_encode_n(str, text_len, true, &encoded_len);
    if (!encoded)
        rt_url_trap_runtime("URL.Encode: allocation failed");

    rt_string result = rt_url_string_from_bytes_or_trap(
        encoded, encoded_len, "URL.Encode: string allocation failed");
    free(encoded);
    return result;
}

/// @brief URL-decode (unescape) `%XX` triples in `text`. Invalid escapes pass through verbatim.
rt_string rt_url_decode(rt_string text) {
    const char *str = text ? rt_string_cstr(text) : "";
    size_t text_len = rt_url_string_len_or_trap(text, "URL.Decode: invalid input length");
    size_t decoded_len = 0;
    char *decoded = percent_decode_internal_n(str, text_len, false, &decoded_len);
    if (!decoded)
        rt_url_trap_runtime("URL.Decode: allocation failed");

    rt_string result = rt_url_string_from_bytes_or_trap(
        decoded, decoded_len, "URL.Decode: string allocation failed");
    free(decoded);
    return result;
}

/// @brief Build a `name=value&…` query string from a `Map[String,String]`, URL-encoding each piece.
rt_string rt_url_encode_query(void *map) {
    if (!map)
        return rt_str_empty();

    void *keys = rt_map_keys(map);
    int64_t len = rt_seq_len(keys);

    if (len == 0) {
        if (keys && rt_obj_release_check0(keys))
            rt_obj_free(keys);
        return rt_str_empty();
    }

    // Build query string
    size_t cap = 256;
    char *result = rt_url_alloc_or_trap(cap, "URL.EncodeQuery: allocation failed");

    size_t pos = 0;
    for (int64_t i = 0; i < len; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(map, key);

        const char *key_str = key ? rt_string_cstr(key) : "";
        size_t key_len = rt_url_string_len_or_trap(key, "URL.EncodeQuery: invalid key length");
        rt_string value_str_handle = NULL;
        if (value && rt_box_type(value) == RT_BOX_STR) {
            value_str_handle = rt_unbox_str(value);
        } else {
            value_str_handle = (rt_string)value;
            if (value_str_handle)
                rt_string_ref(value_str_handle);
        }
        const char *value_str = value_str_handle ? rt_string_cstr(value_str_handle) : "";
        size_t value_len = rt_url_string_len_or_trap(value_str_handle,
                                                     "URL.EncodeQuery: invalid value length");

        size_t enc_key_len = 0;
        size_t enc_value_len = 0;
        char *enc_key = percent_encode_n(key_str, key_len, true, &enc_key_len);
        char *enc_value = percent_encode_n(value_str, value_len, true, &enc_value_len);

        if (!enc_key || !enc_value) {
            if (value_str_handle)
                rt_string_unref(value_str_handle);
            free(enc_key);
            free(enc_value);
            free(result);
            if (keys && rt_obj_release_check0(keys))
                rt_obj_free(keys);
            rt_url_trap_runtime("URL.EncodeQuery: allocation failed");
        }

        if (enc_value_len > SIZE_MAX - 2 ||
            enc_key_len > SIZE_MAX - enc_value_len - 2) {
            if (value_str_handle)
                rt_string_unref(value_str_handle);
            free(enc_key);
            free(enc_value);
            free(result);
            if (keys && rt_obj_release_check0(keys))
                rt_obj_free(keys);
            rt_url_trap_runtime("URL.EncodeQuery: length overflow");
        }
        size_t max_needed = enc_key_len + enc_value_len + 2;
        if (pos > SIZE_MAX - max_needed) {
            if (value_str_handle)
                rt_string_unref(value_str_handle);
            free(enc_key);
            free(enc_value);
            free(result);
            if (keys && rt_obj_release_check0(keys))
                rt_obj_free(keys);
            rt_url_trap_runtime("URL.EncodeQuery: length overflow");
        }
        size_t needed = enc_key_len + 1 + enc_value_len + (i > 0 ? 1 : 0);
        if (pos + needed + 1 > cap) {
            size_t target = pos + needed + 1;
            while (cap < target) {
                if (cap > SIZE_MAX / 2) {
                    cap = target;
                    break;
                }
                cap *= 2;
            }
            char *new_result = (char *)realloc(result, cap);
            if (!new_result) {
                if (value_str_handle)
                    rt_string_unref(value_str_handle);
                free(enc_key);
                free(enc_value);
                free(result);
                if (keys && rt_obj_release_check0(keys))
                    rt_obj_free(keys);
                rt_url_trap_runtime("URL.EncodeQuery: allocation failed");
            }
            result = new_result;
        }

        if (i > 0)
            result[pos++] = '&';
        memcpy(result + pos, enc_key, enc_key_len);
        pos += enc_key_len;
        result[pos++] = '=';
        memcpy(result + pos, enc_value, enc_value_len);
        pos += enc_value_len;

        free(enc_key);
        free(enc_value);
        if (value_str_handle)
            rt_string_unref(value_str_handle);
    }

    result[pos] = '\0';
    rt_string str =
        rt_url_string_from_bytes_or_trap(result, pos, "URL.EncodeQuery: string allocation failed");
    free(result);
    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
    return str;
}

/// @brief Parse a `name=value&…` query string into a `Map[String,String]`. Inverse of
/// `encode_query`.
void *rt_url_decode_query(rt_string query) {
    void *map = rt_map_new();
    const char *str = query ? rt_string_cstr(query) : NULL;
    size_t query_len = rt_url_string_len_or_trap(query, "URL.DecodeQuery: invalid query length");

    if (!str || query_len == 0)
        return map;

    const char *p = str;
    const char *end = str + query_len;
    while (p < end) {
        const char *amp = (const char *)memchr(p, '&', (size_t)(end - p));
        const char *part_end = amp ? amp : end;
        if (part_end > p) {
            const char *eq = (const char *)memchr(p, '=', (size_t)(part_end - p));
            const char *val_start = eq ? eq + 1 : part_end;
            size_t key_len = eq ? (size_t)(eq - p) : (size_t)(part_end - p);
            size_t val_len = eq ? (size_t)(part_end - val_start) : 0;
            size_t dec_key_len = 0;
            size_t dec_val_len = 0;
            char *dec_key = percent_decode_internal_n(p, key_len, true, &dec_key_len);
            char *dec_val = percent_decode_internal_n(val_start, val_len, true, &dec_val_len);
            if (!dec_key || !dec_val) {
                free(dec_key);
                free(dec_val);
                rt_url_trap_runtime("URL.DecodeQuery: decode allocation failed");
            }
            rt_string key_str = rt_url_string_from_bytes_or_trap(
                dec_key, dec_key_len, "URL.DecodeQuery: key string allocation failed");
            rt_string val_str = rt_url_string_from_bytes_or_trap(
                dec_val, dec_val_len, "URL.DecodeQuery: value string allocation failed");
            rt_map_set_str(map, key_str, val_str);
            rt_string_unref(key_str);
            rt_string_unref(val_str);
            free(dec_key);
            free(dec_val);
        }
        p = amp ? amp + 1 : end;
    }

    return map;
}

/// @brief Quick syntactic check: is `url_str` parseable as a URL? Returns 1 if yes, 0 if no.
/// Trap-guarded — won't propagate parse errors out to the caller.
int8_t rt_url_is_valid(rt_string url_str) {
    const char *str = url_str ? rt_string_cstr(url_str) : NULL;
    if (!str || *str == '\0')
        return 0;
    if (rt_url_string_has_embedded_nul(url_str))
        return 0;

    // Reject strings with unencoded spaces (common non-URL indicator)
    for (const char *p = str; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            return 0;
    }

    // Reject URLs starting with :// (missing scheme)
    if (str[0] == ':' && str[1] == '/' && str[2] == '/')
        return 0;

    // Check for scheme - must have letters before ://
    const char *scheme_sep = strstr(str, "://");
    if (scheme_sep) {
        // Scheme must be at least 1 character and only contain [a-zA-Z0-9+.-]
        if (scheme_sep == str)
            return 0; // Empty scheme
        for (const char *p = str; p < scheme_sep; p++) {
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
