//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_client.c
// Purpose: Session-based HTTP client with cookie jar, auto-redirect, and
//          persistent default headers.
// Key invariants:
//   - Cookies stored per-domain in an internal map.
//   - Redirects followed automatically up to max_redirects.
//   - Uses the existing rt_http_req/res infrastructure.
// Ownership/Lifetime:
//   - Client objects are GC-managed.
// Links: rt_http_client.h (API), rt_network_http.c (underlying HTTP)
//
//===----------------------------------------------------------------------===//

#include "rt_http_client.h"
#include "rt_network_http_internal.h"
#include "rt_network_time.inc"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
typedef CRITICAL_SECTION http_client_mutex_t;
#define HTTP_CLIENT_MUTEX_INIT(m) InitializeCriticalSection(m)
#define HTTP_CLIENT_MUTEX_LOCK(m) EnterCriticalSection(m)
#define HTTP_CLIENT_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define HTTP_CLIENT_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
#include <strings.h>
typedef pthread_mutex_t http_client_mutex_t;
#define HTTP_CLIENT_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define HTTP_CLIENT_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define HTTP_CLIENT_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define HTTP_CLIENT_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif

#include "rt_trap.h"

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct rt_http_cookie {
    char *name;
    char *value;
    char *domain;
    char *path;
    int8_t secure;
    int8_t host_only;
    int8_t http_only;
    int8_t same_site;
    int8_t persistent;
    int64_t expires_at;
    struct rt_http_cookie *next;
} rt_http_cookie;

enum {
    HTTP_COOKIE_SAMESITE_UNSPECIFIED = 0,
    HTTP_COOKIE_SAMESITE_LAX = 1,
    HTTP_COOKIE_SAMESITE_STRICT = 2,
    HTTP_COOKIE_SAMESITE_NONE = 3
};

typedef struct {
    void *default_headers; // Map<String, String>
    rt_http_cookie *cookies;
    int64_t timeout_ms;
    int64_t max_redirects;
    int8_t follow_redirects;
    void *connection_pool;
    int64_t pool_size;
    int8_t keep_alive;
    http_client_mutex_t lock;
    int8_t lock_initialized;
} rt_http_client_impl;

static int http_client_timeout_ms_to_int(int64_t timeout_ms, int *out_timeout_ms) {
    if (timeout_ms < 0 || timeout_ms > INT_MAX)
        return 0;
    if (out_timeout_ms)
        *out_timeout_ms = (int)timeout_ms;
    return 1;
}

static int http_client_string_has_embedded_nul(rt_string value) {
    if (!value)
        return 0;
    const char *cstr = rt_string_cstr(value);
    int64_t len64 = rt_str_len(value);
    if (!cstr || len64 <= 0)
        return 0;
    return memchr(cstr, '\0', (size_t)len64) != NULL;
}

/// @brief Release a retained runtime object and free it when the reference count reaches zero.
/// @details Used for short-lived snapshots and client-owned references so lock cleanup paths do
///          not repeat the release/free sequence or accidentally skip it on early returns.
/// @param obj Runtime object pointer to release, or NULL.
static void http_client_release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Release retained default-header snapshot entries.
/// @details Header snapshots retain each key/value while the client mutex is
///          held, then apply those headers after unlocking. This helper is used
///          by both normal and trap-recovery paths so partially populated
///          snapshots do not leak.
/// @param headers Snapshot array, or NULL.
/// @param header_count Number of initialized entries in @p headers.
static void http_client_release_header_snapshot(void *headers, int64_t header_count) {
    typedef struct {
        rt_string key;
        rt_string value;
    } header_snapshot;

    header_snapshot *items = (header_snapshot *)headers;
    for (int64_t i = 0; i < header_count; i++) {
        if (items[i].value)
            rt_string_unref(items[i].value);
        if (items[i].key)
            rt_string_unref(items[i].key);
    }
}

/// @brief Copy the current thread's trap message into a fixed buffer.
/// @details Trap recovery stores the latest message in runtime TLS. Cleanup
///          paths copy it before clearing recovery and releasing resources so
///          they can re-raise the original error after the mutex is unlocked.
/// @param buffer Destination buffer.
/// @param buffer_size Size of @p buffer.
/// @param fallback Message used when the trap did not provide one.
static void http_client_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

static void free_cookie_list(rt_http_cookie *cookie) {
    while (cookie) {
        rt_http_cookie *next = cookie->next;
        free(cookie->name);
        free(cookie->value);
        free(cookie->domain);
        free(cookie->path);
        free(cookie);
        cookie = next;
    }
}

static void rt_http_client_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    http_client_release_obj(c->default_headers);
    http_client_release_obj(c->connection_pool);
    free_cookie_list(c->cookies);
    if (c->lock_initialized)
        HTTP_CLIENT_MUTEX_DESTROY(&c->lock);
}

//=============================================================================
// Helpers
//=============================================================================

static void apply_defaults(rt_http_client_impl *c, void *req, int allow_sensitive_headers) {
    typedef struct {
        rt_string key;
        rt_string value;
    } header_snapshot;

    int64_t timeout_ms = 0;
    int8_t keep_alive = 0;
    void *volatile keys = NULL;
    void *volatile connection_pool = NULL;
    header_snapshot *volatile headers = NULL;
    volatile int64_t header_count = 0;
    int snapshot_failed = 0;
    volatile int mutex_locked = 0;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        http_client_save_trap_error(
            saved_error, sizeof(saved_error), "HttpClient: failed to apply defaults");
        rt_trap_clear_recovery();
        if (mutex_locked)
            HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
        http_client_release_obj((void *)keys);
        http_client_release_header_snapshot((void *)headers, (int64_t)header_count);
        free((void *)headers);
        http_client_release_obj((void *)connection_pool);
        rt_trap(saved_error);
        return;
    }

    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    mutex_locked = 1;
    keys = rt_map_keys(c->default_headers);
    int64_t count = rt_seq_len(keys);
    if (count > 0) {
        if ((uint64_t)count > (uint64_t)SIZE_MAX / sizeof(*headers)) {
            snapshot_failed = 1;
        } else {
            headers = (header_snapshot *)calloc((size_t)count, sizeof(*headers));
            if (!headers)
                snapshot_failed = 1;
        }
    }
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(c->default_headers, key);
        if (!snapshot_failed && val && key &&
            (allow_sensitive_headers ||
             !rt_http_header_is_sensitive_for_cross_origin_redirect(rt_string_cstr(key)))) {
            rt_string_ref(key);
            rt_string_ref((rt_string)val);
            headers[(int64_t)header_count].key = key;
            headers[(int64_t)header_count].value = (rt_string)val;
            header_count++;
        }
    }
    http_client_release_obj((void *)keys);
    keys = NULL;

    timeout_ms = c->timeout_ms;
    keep_alive = c->keep_alive;
    connection_pool = keep_alive ? c->connection_pool : NULL;
    if (connection_pool)
        rt_obj_retain_maybe(connection_pool);
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    mutex_locked = 0;
    rt_trap_clear_recovery();

    if (snapshot_failed) {
        http_client_release_obj((void *)connection_pool);
        http_client_release_header_snapshot((void *)headers, (int64_t)header_count);
        free((void *)headers);
        rt_trap("HttpClient: memory allocation failed");
        return;
    }

    for (int64_t i = 0; i < (int64_t)header_count; i++) {
        rt_http_req_set_header(req, headers[i].key, headers[i].value);
    }
    http_client_release_header_snapshot((void *)headers, (int64_t)header_count);
    free((void *)headers);

    if (timeout_ms > 0)
        rt_http_req_set_timeout(req, timeout_ms);
    rt_http_req_set_follow_redirects(req, 0);
    rt_http_req_set_keep_alive(req, keep_alive);
    rt_http_req_set_connection_pool(req, (void *)connection_pool);
    http_client_release_obj((void *)connection_pool);
}

static int64_t cookie_now_seconds(void) {
    return (int64_t)time(NULL);
}

static char *cookie_strdup_range_trim(const char *start, size_t len) {
    while (len > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        len--;
    }
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'))
        len--;

    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *cookie_strdup_lower(const char *text) {
    size_t len = text ? strlen(text) : 0;
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    out[len] = '\0';
    return out;
}

static char *cookie_strdup_manual_domain(const char *text) {
    char *out = cookie_strdup_lower(text);
    size_t len;
    if (!out)
        return NULL;
    char *trimmed = out;
    while (*trimmed == '.' || *trimmed == ' ' || *trimmed == '\t')
        trimmed++;
    if (trimmed != out)
        memmove(out, trimmed, strlen(trimmed) + 1);
    len = strlen(out);
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t' || out[len - 1] == '.' ||
                       out[len - 1] == '/')) {
        out[--len] = '\0';
    }
    if (len >= 2 && out[0] == '[' && out[len - 1] == ']') {
        memmove(out, out + 1, len - 2);
        out[len - 2] = '\0';
    }
    return out;
}

/// @brief Validate a cookie name against the HTTP token character set.
/// @details Rejects empty names, ASCII controls, DEL, whitespace, and RFC token separators so a
///          Set-Cookie header cannot smuggle additional cookie attributes or response syntax.
/// @param name NUL-terminated cookie name.
/// @return 1 if the name is syntactically valid; otherwise 0.
static int cookie_name_is_valid(const char *name) {
    static const char *const separators = "()<>@,;:\\\"/[]?={} \t";
    if (!name || !*name)
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 0x21 || *p == 0x7f || strchr(separators, (int)*p))
            return 0;
    }
    return 1;
}

/// @brief Validate a cookie value against the cookie-octet byte ranges.
/// @details Allows the visible ASCII ranges permitted for unquoted cookie values while rejecting
///          quotes, semicolons, commas, backslashes, controls, and non-ASCII bytes that could
///          alter header parsing.
/// @param value NUL-terminated cookie value.
/// @return 1 if the value is safe to store and later emit in a Cookie header; otherwise 0.
static int cookie_value_is_valid(const char *value) {
    if (!value)
        return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        unsigned char c = *p;
        if (!(c == 0x21 || (c >= 0x23 && c <= 0x2b) || (c >= 0x2d && c <= 0x3a) ||
              (c >= 0x3c && c <= 0x5b) || (c >= 0x5d && c <= 0x7e))) {
            return 0;
        }
    }
    return 1;
}

/// @brief Validate a cookie Path attribute before storing it in the jar.
/// @details Requires an absolute path and rejects controls, DEL, and semicolons so path matching
///          cannot be confused with additional Set-Cookie attributes.
/// @param path NUL-terminated path attribute.
/// @return 1 when @p path is a safe cookie path; otherwise 0.
static int cookie_path_is_valid(const char *path) {
    if (!path || path[0] != '/')
        return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == ';')
            return 0;
    }
    return 1;
}

/// @brief Validate one DNS label from a cookie Domain attribute.
/// @details Labels must be 1..63 bytes, alphanumeric or hyphen, and may not begin or end with a
///          hyphen. The caller supplies lowercase text after leading/trailing dots are stripped.
/// @param start Pointer to the first byte of the label.
/// @param len Number of bytes in the label.
/// @return 1 if the label is valid for cookie domain matching; otherwise 0.
static int cookie_domain_label_is_valid(const char *start, size_t len) {
    if (len == 0 || len > 63 || start[0] == '-' || start[len - 1] == '-')
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)start[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return 1;
}

/// @brief Validate the full normalized cookie Domain attribute.
/// @details Enforces DNS length and label syntax before the domain is compared with the request
///          host. IP literals and public-suffix-like domains are handled by separate checks.
/// @param domain Lowercase, NUL-terminated domain string.
/// @return 1 if the domain is syntactically valid; otherwise 0.
static int cookie_domain_is_valid(const char *domain) {
    const char *label = domain;
    size_t total_len;
    if (!domain || !*domain)
        return 0;
    total_len = strlen(domain);
    if (total_len > 253 || domain[0] == '.' || domain[total_len - 1] == '.')
        return 0;
    for (const char *p = domain;; p++) {
        if (*p == '.' || *p == '\0') {
            if (!cookie_domain_label_is_valid(label, (size_t)(p - label)))
                return 0;
            if (*p == '\0')
                break;
            label = p + 1;
        }
    }
    return 1;
}

/// @brief Reject domains that are too broad to be accepted into the local cookie jar.
/// @details This is a conservative built-in deny list for common registry-controlled suffixes,
///          plus a no-dot guard. It is not a complete Public Suffix List implementation, but it
///          prevents obvious super-cookie scopes when no PSL dependency is available.
/// @param domain Lowercase, normalized cookie domain.
/// @return 1 if the domain should be treated as public-suffix-like; otherwise 0.
static int cookie_domain_is_public_suffix_like(const char *domain) {
    static const char *const public_suffixes[] = {
        "ac.uk", "co.jp", "co.uk", "com", "com.au", "com.br", "com.cn", "com.mx", "com.sg", "de",
        "edu",   "fr",    "gov",   "io",  "jp",     "net",    "org",    "ru",     "uk",     "us"};
    if (!domain || !strchr(domain, '.'))
        return 1;
    for (size_t i = 0; i < sizeof(public_suffixes) / sizeof(public_suffixes[0]); i++) {
        if (strcasecmp(domain, public_suffixes[i]) == 0)
            return 1;
    }
    return 0;
}

/// @brief Parse a SameSite attribute value into the cookie jar enum.
/// @details Unknown or empty values intentionally map to UNSPECIFIED so callers can ignore them
///          while still enforcing the Secure requirement for explicit SameSite=None.
/// @param value Attribute value following SameSite=, or NULL.
/// @return One of HTTP_COOKIE_SAMESITE_*.
static int cookie_parse_same_site(const char *value) {
    if (!value || !*value)
        return HTTP_COOKIE_SAMESITE_UNSPECIFIED;
    if (strcasecmp(value, "Lax") == 0)
        return HTTP_COOKIE_SAMESITE_LAX;
    if (strcasecmp(value, "Strict") == 0)
        return HTTP_COOKIE_SAMESITE_STRICT;
    if (strcasecmp(value, "None") == 0)
        return HTTP_COOKIE_SAMESITE_NONE;
    return HTTP_COOKIE_SAMESITE_UNSPECIFIED;
}

static char *cookie_default_path(const char *request_path) {
    if (!request_path || request_path[0] != '/')
        return strdup("/");
    const char *last_slash = strrchr(request_path, '/');
    if (!last_slash || last_slash == request_path)
        return strdup("/");
    size_t len = (size_t)(last_slash - request_path);
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, request_path, len);
    out[len] = '\0';
    return out;
}

static int cookie_month_index(const char *month) {
    static const char *const kMonths[] = {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(month, kMonths[i], 3) == 0)
            return i;
    }
    return -1;
}

static int parse_cookie_expires(const char *text, int64_t *out_epoch) {
    char weekday[4] = {0};
    char month[4] = {0};
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;

    if (sscanf(text,
               " %3[^,], %d %3s %d %d:%d:%d GMT",
               weekday,
               &day,
               month,
               &year,
               &hour,
               &minute,
               &second) != 7) {
        return -1;
    }

    int month_index = cookie_month_index(month);
    if (month_index < 0)
        return -1;

    if (day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60 || year < 1601)
        return -1;

    *out_epoch =
        rt_network_days_from_civil(year, (unsigned)(month_index + 1), (unsigned)day) * 86400 +
        hour * 3600 + minute * 60 + second;
    return 0;
}

static int parse_cookie_max_age(const char *text, int64_t *out_delta) {
    const char *p = text;
    int negative = 0;
    uint64_t value = 0;
    uint64_t limit = 0;

    if (!text || !*text || !out_delta)
        return -1;
    if (*p == '-') {
        negative = 1;
        p++;
        if (!*p)
            return -1;
    } else if (*p == '+') {
        return -1;
    }

    limit = negative ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
    for (; *p; p++) {
        uint64_t digit = 0;
        if (*p < '0' || *p > '9')
            return -1;
        digit = (uint64_t)(*p - '0');
        if (value > (limit - digit) / 10u) {
            *out_delta = negative ? INT64_MIN : INT64_MAX;
            return 0;
        }
        value = value * 10u + digit;
    }

    if (negative) {
        if (value == (uint64_t)INT64_MAX + 1u)
            *out_delta = INT64_MIN;
        else
            *out_delta = -(int64_t)value;
    } else {
        *out_delta = (int64_t)value;
    }
    return 0;
}

static int cookie_domain_matches(const rt_http_cookie *cookie, const char *host) {
    size_t host_len, domain_len;
    if (!cookie || !cookie->domain || !host)
        return 0;

    if (cookie->host_only)
        return strcasecmp(cookie->domain, host) == 0;

    if (strcasecmp(cookie->domain, host) == 0)
        return 1;

    host_len = strlen(host);
    domain_len = strlen(cookie->domain);
    if (host_len <= domain_len)
        return 0;
    return host[host_len - domain_len - 1] == '.' &&
           strcasecmp(host + host_len - domain_len, cookie->domain) == 0;
}

static int cookie_path_matches(const rt_http_cookie *cookie, const char *request_path) {
    const char *cookie_path = (cookie && cookie->path && *cookie->path) ? cookie->path : "/";
    const char *path = (request_path && *request_path) ? request_path : "/";
    size_t cookie_len = strlen(cookie_path);

    if (strncmp(path, cookie_path, cookie_len) != 0)
        return 0;
    if (cookie_len == 1)
        return 1;
    return path[cookie_len] == '\0' || path[cookie_len] == '/' ||
           cookie_path[cookie_len - 1] == '/';
}

static int cookie_is_expired(const rt_http_cookie *cookie, int64_t now) {
    return cookie && cookie->persistent && cookie->expires_at <= now;
}

static void purge_expired_cookies_locked(rt_http_client_impl *c) {
    rt_http_cookie **link = &c->cookies;
    const int64_t now = cookie_now_seconds();
    while (*link) {
        if (cookie_is_expired(*link, now)) {
            rt_http_cookie *dead = *link;
            *link = dead->next;
            dead->next = NULL;
            free_cookie_list(dead);
        } else {
            link = &(*link)->next;
        }
    }
}

static void replace_cookie_locked(rt_http_client_impl *c, rt_http_cookie *incoming) {
    rt_http_cookie **link = &c->cookies;
    while (*link) {
        rt_http_cookie *cookie = *link;
        if (strcasecmp(cookie->name, incoming->name) == 0 &&
            strcasecmp(cookie->domain, incoming->domain) == 0 &&
            strcmp(cookie->path, incoming->path) == 0) {
            *link = cookie->next;
            cookie->next = NULL;
            free_cookie_list(cookie);
            break;
        }
        link = &cookie->next;
    }

    if ((incoming->persistent && incoming->expires_at <= cookie_now_seconds()) ||
        incoming->value[0] == '\0') {
        free_cookie_list(incoming);
        return;
    }

    incoming->next = c->cookies;
    c->cookies = incoming;
}

static void apply_cookie_header(rt_http_client_impl *c, void *req, rt_string url) {
    void *parsed = rt_url_parse(url);
    rt_string host = rt_url_host(parsed);
    rt_string path = rt_url_path(parsed);
    rt_string scheme = rt_url_scheme(parsed);
    const char *host_cstr = rt_string_cstr(host);
    const char *path_cstr = rt_string_cstr(path);
    const char *scheme_cstr = rt_string_cstr(scheme);
    int is_secure = scheme_cstr && strcasecmp(scheme_cstr, "https") == 0;
    size_t total_len = 0;
    size_t cookie_count = 0;
    char *cookie_header = NULL;

    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    purge_expired_cookies_locked(c);

    if (!host_cstr || !*host_cstr) {
        HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
        rt_string_unref(scheme);
        rt_string_unref(path);
        rt_string_unref(host);
        http_client_release_obj(parsed);
        return;
    }

    for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
        if ((cookie->secure && !is_secure) || !cookie_domain_matches(cookie, host_cstr) ||
            !cookie_path_matches(cookie, path_cstr)) {
            continue;
        }
        size_t name_len = strlen(cookie->name);
        size_t value_len = strlen(cookie->value);
        if (name_len > SIZE_MAX - 1 || value_len > SIZE_MAX - name_len - 1)
            goto cookie_header_done;
        size_t item_len = name_len + 1 + value_len;
        if (total_len > SIZE_MAX - item_len)
            goto cookie_header_done;
        total_len += item_len;
        if (cookie_count > 0) {
            if (total_len > SIZE_MAX - 2)
                goto cookie_header_done;
            total_len += 2;
        }
        cookie_count++;
    }

    if (cookie_count > 0 && total_len < SIZE_MAX)
        cookie_header = (char *)malloc(total_len + 1);
    if (cookie_header) {
        size_t pos = 0;
        size_t index = 0;
        for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
            int written;
            if ((cookie->secure && !is_secure) || !cookie_domain_matches(cookie, host_cstr) ||
                !cookie_path_matches(cookie, path_cstr)) {
                continue;
            }
            written = snprintf(cookie_header + pos,
                               total_len + 1 - pos,
                               index + 1 < cookie_count ? "%s=%s; " : "%s=%s",
                               cookie->name,
                               cookie->value);
            if (written > 0)
                pos += (size_t)written;
            index++;
        }

        rt_string header_str = rt_string_from_bytes(cookie_header, strlen(cookie_header));
        rt_http_req_set_header(req, rt_const_cstr("Cookie"), header_str);
        rt_string_unref(header_str);
        free(cookie_header);
    }

cookie_header_done:
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);

    rt_string_unref(scheme);
    rt_string_unref(path);
    rt_string_unref(host);
    http_client_release_obj(parsed);
}

static void store_cookie_line_locked(rt_http_client_impl *c,
                                     const char *host,
                                     const char *request_path,
                                     int is_secure,
                                     const char *line) {
    const char *cookie_end;
    const char *eq;
    size_t cookie_len;
    size_t name_len;
    size_t value_len;
    rt_http_cookie *cookie;
    int max_age_seen = 0;

    if (!host || !line || !*line)
        return;

    cookie_end = strchr(line, ';');
    cookie_len = cookie_end ? (size_t)(cookie_end - line) : strlen(line);
    eq = memchr(line, '=', cookie_len);
    if (!eq || eq == line)
        return;

    name_len = (size_t)(eq - line);
    value_len = cookie_len - name_len - 1;
    if (name_len == 0)
        return;

    cookie = (rt_http_cookie *)calloc(1, sizeof(*cookie));
    if (!cookie)
        return;

    cookie->name = cookie_strdup_range_trim(line, name_len);
    cookie->value = cookie_strdup_range_trim(eq + 1, value_len);
    cookie->domain = cookie_strdup_lower(host);
    cookie->path = cookie_default_path(request_path);
    cookie->host_only = 1;
    if (!cookie->name || !cookie->value || !cookie->domain || !cookie->path) {
        free_cookie_list(cookie);
        return;
    }
    if (!cookie_name_is_valid(cookie->name) || !cookie_value_is_valid(cookie->value)) {
        free_cookie_list(cookie);
        return;
    }

    const char *attr = cookie_end ? cookie_end + 1 : NULL;
    while (attr && *attr) {
        const char *attr_end;
        const char *attr_eq;
        size_t attr_len;
        char *attr_name;
        char *attr_value = NULL;

        while (*attr == ' ' || *attr == '\t' || *attr == ';')
            attr++;
        if (!*attr)
            break;

        attr_end = strchr(attr, ';');
        attr_len = attr_end ? (size_t)(attr_end - attr) : strlen(attr);
        attr_eq = memchr(attr, '=', attr_len);
        if (attr_eq) {
            attr_name = cookie_strdup_range_trim(attr, (size_t)(attr_eq - attr));
            attr_value =
                cookie_strdup_range_trim(attr_eq + 1, attr_len - (size_t)(attr_eq - attr) - 1);
        } else {
            attr_name = cookie_strdup_range_trim(attr, attr_len);
        }

        if (!attr_name) {
            free(attr_value);
            break;
        }

        if (strcasecmp(attr_name, "Domain") == 0 && attr_value && *attr_value) {
            char *domain = cookie_strdup_manual_domain(attr_value);
            if (!domain) {
                free(attr_value);
                free(attr_name);
                free_cookie_list(cookie);
                return;
            }
            free(cookie->domain);
            cookie->domain = domain;
            cookie->host_only = 0;
        } else if (strcasecmp(attr_name, "Path") == 0 && attr_value && attr_value[0] == '/') {
            char *path = strdup(attr_value);
            if (!path) {
                free(attr_value);
                free(attr_name);
                free_cookie_list(cookie);
                return;
            }
            free(cookie->path);
            cookie->path = path;
        } else if (strcasecmp(attr_name, "Secure") == 0) {
            cookie->secure = 1;
        } else if (strcasecmp(attr_name, "HttpOnly") == 0) {
            cookie->http_only = 1;
        } else if (strcasecmp(attr_name, "SameSite") == 0 && attr_value) {
            cookie->same_site = (int8_t)cookie_parse_same_site(attr_value);
        } else if (strcasecmp(attr_name, "Max-Age") == 0 && attr_value) {
            int64_t max_age = 0;
            if (parse_cookie_max_age(attr_value, &max_age) == 0) {
                int64_t now = cookie_now_seconds();
                cookie->persistent = 1;
                if (max_age <= 0) {
                    cookie->expires_at = now - 1;
                } else if (max_age > INT64_MAX - now) {
                    cookie->expires_at = INT64_MAX;
                } else {
                    cookie->expires_at = now + max_age;
                }
                max_age_seen = 1;
            }
        } else if (strcasecmp(attr_name, "Expires") == 0 && attr_value && !max_age_seen) {
            int64_t expires_at = 0;
            if (parse_cookie_expires(attr_value, &expires_at) == 0) {
                cookie->persistent = 1;
                cookie->expires_at = expires_at;
            }
        }

        free(attr_value);
        free(attr_name);
        attr = attr_end ? attr_end + 1 : NULL;
    }

    if (cookie->secure && !is_secure) {
        free_cookie_list(cookie);
        return;
    }
    if ((cookie->same_site == HTTP_COOKIE_SAMESITE_NONE && !cookie->secure) ||
        !cookie_path_is_valid(cookie->path)) {
        free_cookie_list(cookie);
        return;
    }

    if (!cookie->host_only) {
        rt_string host_str = rt_string_from_bytes(host, strlen(host));
        int host_is_ip = host_str ? rt_dns_is_ip(host_str) : 1;
        rt_string_unref(host_str);
        if (!cookie_domain_is_valid(cookie->domain) ||
            cookie_domain_is_public_suffix_like(cookie->domain) ||
            !cookie_domain_matches(cookie, host) || host_is_ip == 1) {
            free_cookie_list(cookie);
            return;
        }
    }

    replace_cookie_locked(c, cookie);
}

static void store_response_cookies(rt_http_client_impl *c, void *res, rt_string url) {
    void *parsed = rt_url_parse(url);
    rt_string host = rt_url_host(parsed);
    rt_string path = rt_url_path(parsed);
    rt_string scheme = rt_url_scheme(parsed);
    const char *host_cstr = rt_string_cstr(host);
    const char *path_cstr = rt_string_cstr(path);
    const char *scheme_cstr = rt_string_cstr(scheme);
    rt_string cookie_header;
    const char *cookies;

    if (!host_cstr || !*host_cstr) {
        rt_string_unref(scheme);
        rt_string_unref(path);
        rt_string_unref(host);
        if (parsed && rt_obj_release_check0(parsed))
            rt_obj_free(parsed);
        return;
    }

    cookie_header = rt_http_res_header(res, rt_const_cstr("set-cookie"));
    cookies = rt_string_cstr(cookie_header);
    if (cookies && *cookies) {
        const char *line = cookies;
        while (*line) {
            const char *end = strchr(line, '\n');
            size_t len = end ? (size_t)(end - line) : strlen(line);
            char *entry = (char *)malloc(len + 1);
            if (!entry)
                break;
            memcpy(entry, line, len);
            entry[len] = '\0';
            HTTP_CLIENT_MUTEX_LOCK(&c->lock);
            store_cookie_line_locked(c,
                                     host_cstr,
                                     path_cstr && *path_cstr ? path_cstr : "/",
                                     scheme_cstr && strcasecmp(scheme_cstr, "https") == 0,
                                     entry);
            HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
            free(entry);
            if (!end)
                break;
            line = end + 1;
        }
    }

    rt_string_unref(cookie_header);
    rt_string_unref(scheme);
    rt_string_unref(path);
    rt_string_unref(host);
    if (parsed && rt_obj_release_check0(parsed))
        rt_obj_free(parsed);
}

static void *do_request(rt_http_client_impl *c, const char *method, rt_string url, rt_string body) {
    int8_t follow_redirects = 0;
    int64_t redirects_left = 0;
    const char *url_cstr = url ? rt_string_cstr(url) : NULL;
    if (!url_cstr || http_client_string_has_embedded_nul(url))
        rt_trap("HttpClient: invalid URL");
    rt_string current_url =
        rt_string_from_bytes(url_cstr ? url_cstr : "", url_cstr ? strlen(url_cstr) : 0);
    if (!current_url)
        rt_trap("HttpClient: memory allocation failed");
    const char *current_method = method;
    rt_string current_body = NULL;
    if (body) {
        const char *body_cstr = rt_string_cstr(body);
        int64_t body_len64 = rt_str_len(body);
        if (!body_cstr || body_len64 < 0 || (uint64_t)body_len64 > (uint64_t)SIZE_MAX)
            rt_trap("HttpClient: invalid body");
        current_body = rt_string_from_bytes(body_cstr, (size_t)body_len64);
        if (!current_body)
            rt_trap("HttpClient: memory allocation failed");
    }

    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    follow_redirects = c->follow_redirects ? 1 : 0;
    redirects_left = follow_redirects ? c->max_redirects : 0;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);

    void *final_res = NULL;
    int allow_sensitive_defaults = 1;

    while (1) {
        rt_string method_str = rt_string_from_bytes(current_method, strlen(current_method));
        void *req = rt_http_req_new(method_str, current_url);
        rt_string_unref(method_str);

        apply_defaults(c, req, allow_sensitive_defaults);
        apply_cookie_header(c, req, current_url);
        rt_http_req_set_max_redirects(req, 0);

        if (current_body && strcmp(current_method, "GET") != 0 &&
            strcmp(current_method, "DELETE") != 0)
            rt_http_req_set_body_str(req, current_body);

        final_res = rt_http_req_send(req);
        if (req && rt_obj_release_check0(req))
            rt_obj_free(req);
        store_response_cookies(c, final_res, current_url);

        int64_t status = rt_http_res_status(final_res);
        if (!follow_redirects || redirects_left <= 0 ||
            !(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            break;
        }

        rt_string location = rt_http_res_header(final_res, rt_const_cstr("location"));
        const char *location_cstr = rt_string_cstr(location);
        if (!location_cstr || !*location_cstr) {
            rt_string_unref(location);
            break;
        }

        rt_string next_url = rt_http_resolve_redirect_url(current_url, location);
        rt_string_unref(location);
        if (!rt_http_url_has_same_origin(current_url, next_url))
            allow_sensitive_defaults = 0;
        rt_string_unref(current_url);
        current_url = next_url;
        redirects_left--;

        if (status == 303 ||
            ((status == 301 || status == 302) && strcmp(current_method, "POST") == 0)) {
            current_method = "GET";
            if (current_body) {
                rt_string_unref(current_body);
                current_body = NULL;
            }
        }

        if (final_res && rt_obj_release_check0(final_res))
            rt_obj_free(final_res);
        final_res = NULL;
    }

    rt_string_unref(current_url);
    if (current_body)
        rt_string_unref(current_body);
    return final_res;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Construct an HTTP client with sensible defaults: 30s timeout, 5 max redirects, redirect
/// following enabled, empty cookie jar, empty default-headers map. Built on top of `rt_http_req`
/// from `rt_network_http.c` — adds session state (cookies + defaults) and **per-request redirect
/// chasing** (rather than the underlying req's all-or-nothing redirect handling).
void *rt_http_client_new(void) {
    rt_http_client_impl *c =
        (rt_http_client_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_client_impl));
    if (!c) {
        rt_trap("HttpClient: OOM");
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    rt_obj_set_finalizer(c, rt_http_client_finalize);
    c->default_headers = rt_map_new();
    if (!c->default_headers) {
        rt_trap("HttpClient: OOM");
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        return NULL;
    }
    c->timeout_ms = 30000;
    c->max_redirects = 5;
    c->follow_redirects = 1;
    c->pool_size = 8;
    c->keep_alive = 1;
    c->connection_pool = rt_http_conn_pool_new(c->pool_size);
    if (!c->connection_pool) {
        rt_trap("HttpClient: OOM");
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        return NULL;
    }
    HTTP_CLIENT_MUTEX_INIT(&c->lock);
    c->lock_initialized = 1;
    return c;
}

/// @brief Send a `GET` to `url`. Applies default headers + cookies; auto-follows redirects up to
/// `max_redirects`. Returns an HttpRes for the FINAL response after any redirects.
void *rt_http_client_get(void *obj, rt_string url) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "GET", url, NULL);
}

/// @brief Send a `POST` with a string body. **Redirect semantics:** 301/302 with POST switch to
/// GET (per common browser behavior); 303 always switches to GET; 307/308 preserve method+body.
void *rt_http_client_post(void *obj, rt_string url, rt_string body) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "POST", url, body);
}

/// @brief Send a `PUT` with a string body. Redirects preserve method+body for 307/308.
void *rt_http_client_put(void *obj, rt_string url, rt_string body) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "PUT", url, body);
}

/// @brief Send a `DELETE` (no body). Same redirect handling as `_get`.
void *rt_http_client_delete(void *obj, rt_string url) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "DELETE", url, NULL);
}

/// @brief Add or replace a default header that is sent with every request.
void rt_http_client_set_header(void *obj, rt_string name, rt_string value) {
    if (!obj)
        return;
    if (!name || !value || http_client_string_has_embedded_nul(name) ||
        http_client_string_has_embedded_nul(value)) {
        rt_trap("HttpClient: invalid default header");
        return;
    }
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    rt_map_set(c->default_headers, name, (void *)value);
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
}

/// @brief Set the request timeout in milliseconds for all subsequent requests.
void rt_http_client_set_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    int timeout_int = 0;
    if (!http_client_timeout_ms_to_int(timeout_ms, &timeout_int))
        rt_trap("HttpClient: invalid timeout");
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    c->timeout_ms = timeout_int;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
}

/// @brief Check whether this client keeps HTTP connections open for reuse.
int8_t rt_http_client_get_keep_alive(void *obj) {
    if (!obj)
        return 0;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    int8_t keep_alive;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    keep_alive = c->keep_alive;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    return keep_alive;
}

/// @brief Enable or disable pooled keep-alive transport for this client.
void rt_http_client_set_keep_alive(void *obj, int8_t keep_alive) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    void *pool_to_clear = NULL;
    int64_t pool_size = 8;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    c->keep_alive = keep_alive ? 1 : 0;
    pool_size = c->pool_size > 0 ? c->pool_size : 8;
    if (!c->keep_alive && c->connection_pool)
        pool_to_clear = c->connection_pool;
    if (c->keep_alive && !c->connection_pool)
        c->connection_pool = rt_http_conn_pool_new(pool_size);
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    if (pool_to_clear)
        rt_http_conn_pool_clear(pool_to_clear);
}

/// @brief Resize the keep-alive pool. Existing idle connections are dropped.
void rt_http_client_set_pool_size(void *obj, int64_t max_size) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    void *old_pool;
    void *new_pool;
    if (max_size <= 0)
        max_size = 1;
    new_pool = rt_http_conn_pool_new(max_size);
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    c->pool_size = max_size;
    old_pool = c->connection_pool;
    c->connection_pool = new_pool;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    if (old_pool && rt_obj_release_check0(old_pool))
        rt_obj_free(old_pool);
}

/// @brief Set the maximum number of HTTP redirects to follow (0 = no redirects).
void rt_http_client_set_max_redirects(void *obj, int64_t max) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    c->max_redirects = max < 0 ? 0 : max;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
}

/// @brief Check whether the client automatically follows HTTP redirects.
int8_t rt_http_client_get_follow_redirects(void *obj) {
    if (!obj)
        return 0;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    int8_t follow;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    follow = c->follow_redirects;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    return follow;
}

/// @brief Enable or disable automatic following of HTTP redirects.
void rt_http_client_set_follow_redirects(void *obj, int8_t follow) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    c->follow_redirects = follow ? 1 : 0;
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
}

/// @brief Store a cookie for a specific domain; sent automatically on matching requests.
void rt_http_client_set_cookie(void *obj, rt_string domain, rt_string name, rt_string value) {
    const char *domain_cstr;
    const char *name_cstr;
    const char *value_cstr;
    rt_http_cookie *cookie;
    if (!obj)
        return;
    domain_cstr = domain ? rt_string_cstr(domain) : NULL;
    name_cstr = name ? rt_string_cstr(name) : NULL;
    value_cstr = value ? rt_string_cstr(value) : NULL;
    if (!domain_cstr || !*domain_cstr || !name_cstr || !*name_cstr || !value_cstr ||
        http_client_string_has_embedded_nul(domain) || http_client_string_has_embedded_nul(name) ||
        http_client_string_has_embedded_nul(value))
        return;

    cookie = (rt_http_cookie *)calloc(1, sizeof(*cookie));
    if (!cookie)
        return;
    cookie->name = strdup(name_cstr);
    cookie->value = strdup(value_cstr);
    cookie->domain = cookie_strdup_manual_domain(domain_cstr);
    cookie->path = strdup("/");
    cookie->host_only = 0;
    if (!cookie->name || !cookie->value || !cookie->domain || !cookie->path || !*cookie->domain) {
        free_cookie_list(cookie);
        return;
    }

    HTTP_CLIENT_MUTEX_LOCK(&((rt_http_client_impl *)obj)->lock);
    replace_cookie_locked((rt_http_client_impl *)obj, cookie);
    HTTP_CLIENT_MUTEX_UNLOCK(&((rt_http_client_impl *)obj)->lock);
}

/// @brief Return a cloned snapshot of all cookies stored for `domain` (key→value Map). Cloned so
/// caller mutations don't affect the client's jar; empty Map if no cookies for the domain.
void *rt_http_client_get_cookies(void *obj, rt_string domain) {
    const char *domain_cstr;
    void *snapshot;
    if (!obj)
        return rt_map_new();
    domain_cstr = domain ? rt_string_cstr(domain) : NULL;
    if (!domain_cstr || !*domain_cstr || http_client_string_has_embedded_nul(domain))
        return rt_map_new();

    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    HTTP_CLIENT_MUTEX_LOCK(&c->lock);
    purge_expired_cookies_locked(c);
    snapshot = rt_map_new();
    for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
        if (!cookie_domain_matches(cookie, domain_cstr))
            continue;
        rt_map_set_str(snapshot, rt_const_cstr(cookie->name), rt_const_cstr(cookie->value));
    }
    HTTP_CLIENT_MUTEX_UNLOCK(&c->lock);
    return snapshot;
}
