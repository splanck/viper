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

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
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
    int8_t persistent;
    int64_t expires_at;
    struct rt_http_cookie *next;
} rt_http_cookie;

typedef struct {
    void *default_headers; // Map<String, String>
    rt_http_cookie *cookies;
    int64_t timeout_ms;
    int64_t max_redirects;
    int8_t follow_redirects;
    void *connection_pool;
    int64_t pool_size;
    int8_t keep_alive;
} rt_http_client_impl;

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
    if (c->default_headers && rt_obj_release_check0(c->default_headers))
        rt_obj_free(c->default_headers);
    if (c->connection_pool && rt_obj_release_check0(c->connection_pool))
        rt_obj_free(c->connection_pool);
    free_cookie_list(c->cookies);
}

//=============================================================================
// Helpers
//=============================================================================

/// @brief Identify request headers that must be stripped on a cross-origin redirect.
/// @details The HTTP client follows redirects by default, but a redirect to a
///          different origin may be attacker-controlled (a server you trust
///          redirecting you to a server you don't). Forwarding credential-bearing
///          headers across that boundary leaks bearer tokens, session cookies,
///          and API keys to whoever the attacker pointed the redirect at.
///
///          This function lists the names that must be dropped before reissuing
///          the request to the new origin: standard HTTP auth (`Authorization`,
///          `Proxy-Authorization`), all cookie variants (`Cookie`, `Cookie2`),
///          and the common ad-hoc API-key / bearer-token header names that
///          modern services use (`X-API-Key`, `Api-Key`, `ApiKey`,
///          `X-Auth-Token`, `X-Access-Token`).
///
///          The list is comparison-only — case-insensitive via `strcasecmp`,
///          since HTTP header names are case-insensitive. Same-origin redirects
///          retain all headers because they're equivalent to the caller making
///          the new request directly.
/// @param name Header name (case-insensitive comparison).
/// @return Non-zero if the header is sensitive and should be stripped.
static int header_is_sensitive_for_cross_origin_redirect(const char *name) {
    if (!name)
        return 0;
    return strcasecmp(name, "Authorization") == 0 ||
           strcasecmp(name, "Proxy-Authorization") == 0 ||
           strcasecmp(name, "Cookie") == 0 || strcasecmp(name, "Cookie2") == 0 ||
           strcasecmp(name, "X-API-Key") == 0 || strcasecmp(name, "Api-Key") == 0 ||
           strcasecmp(name, "ApiKey") == 0 || strcasecmp(name, "X-Auth-Token") == 0 ||
           strcasecmp(name, "X-Access-Token") == 0;
}

static int url_has_same_origin(rt_string lhs, rt_string rhs) {
    int same_origin = 0;
    void *lhs_parsed = rt_url_parse(lhs);
    void *rhs_parsed = rt_url_parse(rhs);
    rt_string lhs_scheme = rt_url_scheme(lhs_parsed);
    rt_string rhs_scheme = rt_url_scheme(rhs_parsed);
    rt_string lhs_host = rt_url_host(lhs_parsed);
    rt_string rhs_host = rt_url_host(rhs_parsed);
    const char *lhs_scheme_cstr = rt_string_cstr(lhs_scheme);
    const char *rhs_scheme_cstr = rt_string_cstr(rhs_scheme);
    const char *lhs_host_cstr = rt_string_cstr(lhs_host);
    const char *rhs_host_cstr = rt_string_cstr(rhs_host);
    const int64_t lhs_port = rt_url_port(lhs_parsed);
    const int64_t rhs_port = rt_url_port(rhs_parsed);

    if (lhs_scheme_cstr && rhs_scheme_cstr && lhs_host_cstr && rhs_host_cstr &&
        strcasecmp(lhs_scheme_cstr, rhs_scheme_cstr) == 0 &&
        strcasecmp(lhs_host_cstr, rhs_host_cstr) == 0 && lhs_port == rhs_port) {
        same_origin = 1;
    }

    rt_string_unref(rhs_host);
    rt_string_unref(lhs_host);
    rt_string_unref(rhs_scheme);
    rt_string_unref(lhs_scheme);
    if (rhs_parsed && rt_obj_release_check0(rhs_parsed))
        rt_obj_free(rhs_parsed);
    if (lhs_parsed && rt_obj_release_check0(lhs_parsed))
        rt_obj_free(lhs_parsed);
    return same_origin;
}

static void apply_defaults(rt_http_client_impl *c, void *req, int allow_sensitive_headers) {
    // Apply default headers
    void *keys = rt_map_keys(c->default_headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(c->default_headers, key);
        if (val && (allow_sensitive_headers ||
                    !header_is_sensitive_for_cross_origin_redirect(rt_string_cstr(key)))) {
            rt_http_req_set_header(req, key, (rt_string)val);
        }
    }
    if (rt_obj_release_check0(keys))
        rt_obj_free(keys);

    // Apply timeout
    if (c->timeout_ms > 0)
        rt_http_req_set_timeout(req, c->timeout_ms);

    rt_http_req_set_follow_redirects(req, 0);
    rt_http_req_set_keep_alive(req, c->keep_alive);
    rt_http_req_set_connection_pool(req, c->keep_alive ? c->connection_pool : NULL);
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
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(month, kMonths[i], 3) == 0)
            return i;
    }
    return -1;
}

static int64_t cookie_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
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

    if (day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60 || year < 1601)
        return -1;

    *out_epoch = cookie_days_from_civil(year, (unsigned)(month_index + 1), (unsigned)day) * 86400 +
                 hour * 3600 + minute * 60 + second;
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
    return path[cookie_len] == '\0' || path[cookie_len] == '/' || cookie_path[cookie_len - 1] == '/';
}

static int cookie_is_expired(const rt_http_cookie *cookie, int64_t now) {
    return cookie && cookie->persistent && cookie->expires_at <= now;
}

static void purge_expired_cookies(rt_http_client_impl *c) {
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

static void replace_cookie(rt_http_client_impl *c, rt_http_cookie *incoming) {
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

    purge_expired_cookies(c);

    if (!host_cstr || !*host_cstr) {
        rt_string_unref(scheme);
        rt_string_unref(path);
        rt_string_unref(host);
        if (parsed && rt_obj_release_check0(parsed))
            rt_obj_free(parsed);
        return;
    }

    for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
        if ((cookie->secure && !is_secure) ||
            !cookie_domain_matches(cookie, host_cstr) ||
            !cookie_path_matches(cookie, path_cstr)) {
            continue;
        }
        total_len += strlen(cookie->name) + 1 + strlen(cookie->value);
        if (cookie_count > 0)
            total_len += 2;
        cookie_count++;
    }

    if (cookie_count > 0)
        cookie_header = (char *)malloc(total_len + 1);
    if (cookie_header) {
        size_t pos = 0;
        size_t index = 0;
        for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
            int written;
            if ((cookie->secure && !is_secure) ||
                !cookie_domain_matches(cookie, host_cstr) ||
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

    rt_string_unref(scheme);
    rt_string_unref(path);
    rt_string_unref(host);
    if (parsed && rt_obj_release_check0(parsed))
        rt_obj_free(parsed);
}

static void store_cookie_line(rt_http_client_impl *c,
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
            while (*attr_value == '.')
                memmove(attr_value, attr_value + 1, strlen(attr_value));
            free(cookie->domain);
            cookie->domain = cookie_strdup_lower(attr_value);
            cookie->host_only = 0;
        } else if (strcasecmp(attr_name, "Path") == 0 && attr_value && attr_value[0] == '/') {
            free(cookie->path);
            cookie->path = strdup(attr_value);
        } else if (strcasecmp(attr_name, "Secure") == 0) {
            cookie->secure = 1;
        } else if (strcasecmp(attr_name, "Max-Age") == 0 && attr_value) {
            long long max_age = atoll(attr_value);
            cookie->persistent = 1;
            cookie->expires_at = cookie_now_seconds() + max_age;
            if (max_age <= 0)
                cookie->expires_at = cookie_now_seconds() - 1;
        } else if (strcasecmp(attr_name, "Expires") == 0 && attr_value) {
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

    if (!cookie->host_only) {
        rt_string host_str = rt_string_from_bytes(host, strlen(host));
        int host_is_ip = rt_dns_is_ip(host_str);
        rt_string_unref(host_str);
        if (!cookie->domain || !*cookie->domain ||
            !cookie_domain_matches(cookie, host) ||
            host_is_ip == 1) {
            free_cookie_list(cookie);
            return;
        }
    }

    replace_cookie(c, cookie);
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
            store_cookie_line(c,
                              host_cstr,
                              path_cstr && *path_cstr ? path_cstr : "/",
                              scheme_cstr && strcasecmp(scheme_cstr, "https") == 0,
                              entry);
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

static rt_string resolve_redirect_url(rt_string current_url, rt_string location) {
    const char *location_cstr = rt_string_cstr(location);
    if (!location_cstr || !*location_cstr)
        return rt_string_from_bytes("", 0);

    if (strstr(location_cstr, "://"))
        return rt_string_from_bytes(location_cstr, strlen(location_cstr));
    if (strncmp(location_cstr, "//", 2) == 0) {
        void *base = rt_url_parse(current_url);
        rt_string scheme = rt_url_scheme(base);
        const char *scheme_cstr = rt_string_cstr(scheme);
        size_t scheme_len = scheme_cstr ? strlen(scheme_cstr) : 0;
        size_t out_len = scheme_len + strlen(location_cstr) + 1;
        char *absolute = (char *)malloc(out_len + 1);
        if (!absolute) {
            rt_string_unref(scheme);
            if (base && rt_obj_release_check0(base))
                rt_obj_free(base);
            return rt_string_from_bytes(location_cstr, strlen(location_cstr));
        }
        snprintf(absolute, out_len + 1, "%s:%s", scheme_cstr ? scheme_cstr : "http", location_cstr);
        rt_string full = rt_string_from_bytes(absolute, strlen(absolute));
        free(absolute);
        rt_string_unref(scheme);
        if (base && rt_obj_release_check0(base))
            rt_obj_free(base);
        return full;
    }

    void *base = rt_url_parse(current_url);
    void *resolved = rt_url_resolve(base, location);
    rt_string full = rt_url_full(resolved);
    if (resolved && rt_obj_release_check0(resolved))
        rt_obj_free(resolved);
    if (base && rt_obj_release_check0(base))
        rt_obj_free(base);
    return full;
}

static void *do_request(rt_http_client_impl *c, const char *method, rt_string url, rt_string body) {
    const char *url_cstr = rt_string_cstr(url);
    rt_string current_url =
        rt_string_from_bytes(url_cstr ? url_cstr : "", url_cstr ? strlen(url_cstr) : 0);
    const char *current_method = method;
    rt_string current_body = NULL;
    if (body) {
        const char *body_cstr = rt_string_cstr(body);
        current_body =
            rt_string_from_bytes(body_cstr ? body_cstr : "", body_cstr ? strlen(body_cstr) : 0);
    }

    int64_t redirects_left = c->follow_redirects ? c->max_redirects : 0;
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
        if (!c->follow_redirects || redirects_left <= 0 ||
            !(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            break;
        }

        rt_string location = rt_http_res_header(final_res, rt_const_cstr("location"));
        const char *location_cstr = rt_string_cstr(location);
        if (!location_cstr || !*location_cstr) {
            rt_string_unref(location);
            break;
        }

        rt_string next_url = resolve_redirect_url(current_url, location);
        rt_string_unref(location);
        if (!url_has_same_origin(current_url, next_url))
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
    if (!c)
        rt_trap("HttpClient: OOM");
    memset(c, 0, sizeof(*c));
    c->default_headers = rt_map_new();
    c->timeout_ms = 30000;
    c->max_redirects = 5;
    c->follow_redirects = 1;
    c->pool_size = 8;
    c->keep_alive = 1;
    c->connection_pool = rt_http_conn_pool_new(c->pool_size);
    rt_obj_set_finalizer(c, rt_http_client_finalize);
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
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    rt_map_set(c->default_headers, name, (void *)value);
}

/// @brief Set the request timeout in milliseconds for all subsequent requests.
void rt_http_client_set_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->timeout_ms = timeout_ms;
}

/// @brief Check whether this client keeps HTTP connections open for reuse.
int8_t rt_http_client_get_keep_alive(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_client_impl *)obj)->keep_alive;
}

/// @brief Enable or disable pooled keep-alive transport for this client.
void rt_http_client_set_keep_alive(void *obj, int8_t keep_alive) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    c->keep_alive = keep_alive ? 1 : 0;
    if (!c->keep_alive && c->connection_pool)
        rt_http_conn_pool_clear(c->connection_pool);
    if (c->keep_alive && !c->connection_pool)
        c->connection_pool = rt_http_conn_pool_new(c->pool_size > 0 ? c->pool_size : 8);
}

/// @brief Resize the keep-alive pool. Existing idle connections are dropped.
void rt_http_client_set_pool_size(void *obj, int64_t max_size) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    if (max_size <= 0)
        max_size = 1;
    c->pool_size = max_size;

    void *new_pool = rt_http_conn_pool_new(max_size);
    void *old_pool = c->connection_pool;
    c->connection_pool = new_pool;
    if (old_pool && rt_obj_release_check0(old_pool))
        rt_obj_free(old_pool);
}

/// @brief Set the maximum number of HTTP redirects to follow (0 = no redirects).
void rt_http_client_set_max_redirects(void *obj, int64_t max) {
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->max_redirects = max < 0 ? 0 : max;
}

/// @brief Check whether the client automatically follows HTTP redirects.
int8_t rt_http_client_get_follow_redirects(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_client_impl *)obj)->follow_redirects;
}

/// @brief Enable or disable automatic following of HTTP redirects.
void rt_http_client_set_follow_redirects(void *obj, int8_t follow) {
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->follow_redirects = follow ? 1 : 0;
}

/// @brief Store a cookie for a specific domain; sent automatically on matching requests.
void rt_http_client_set_cookie(void *obj, rt_string domain, rt_string name, rt_string value) {
    const char *domain_cstr;
    const char *name_cstr;
    const char *value_cstr;
    rt_http_cookie *cookie;
    if (!obj)
        return;
    domain_cstr = rt_string_cstr(domain);
    name_cstr = rt_string_cstr(name);
    value_cstr = rt_string_cstr(value);
    if (!domain_cstr || !*domain_cstr || !name_cstr || !*name_cstr || !value_cstr)
        return;

    cookie = (rt_http_cookie *)calloc(1, sizeof(*cookie));
    if (!cookie)
        return;
    cookie->name = strdup(name_cstr);
    cookie->value = strdup(value_cstr);
    cookie->domain = cookie_strdup_lower(domain_cstr);
    cookie->path = strdup("/");
    cookie->host_only = 0;
    if (!cookie->name || !cookie->value || !cookie->domain || !cookie->path) {
        free_cookie_list(cookie);
        return;
    }

    replace_cookie((rt_http_client_impl *)obj, cookie);
}

/// @brief Return a cloned snapshot of all cookies stored for `domain` (key→value Map). Cloned so
/// caller mutations don't affect the client's jar; empty Map if no cookies for the domain.
void *rt_http_client_get_cookies(void *obj, rt_string domain) {
    const char *domain_cstr;
    void *snapshot;
    if (!obj)
        return rt_map_new();
    domain_cstr = rt_string_cstr(domain);
    if (!domain_cstr || !*domain_cstr)
        return rt_map_new();

    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    purge_expired_cookies(c);
    snapshot = rt_map_new();
    for (rt_http_cookie *cookie = c->cookies; cookie; cookie = cookie->next) {
        if (!cookie_domain_matches(cookie, domain_cstr))
            continue;
        rt_map_set_str(snapshot, rt_const_cstr(cookie->name), rt_const_cstr(cookie->value));
    }
    return snapshot;
}
