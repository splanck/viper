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

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *default_headers; // Map<String, String>
    void *cookies;         // Map<String(domain), Map<String, String>>
    int64_t timeout_ms;
    int64_t max_redirects;
    int8_t follow_redirects;
} rt_http_client_impl;

static void rt_http_client_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    if (c->default_headers && rt_obj_release_check0(c->default_headers))
        rt_obj_free(c->default_headers);
    if (c->cookies && rt_obj_release_check0(c->cookies))
        rt_obj_free(c->cookies);
}

//=============================================================================
// Helpers
//=============================================================================

static void apply_defaults(rt_http_client_impl *c, void *req) {
    // Apply default headers
    void *keys = rt_map_keys(c->default_headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(c->default_headers, key);
        if (val)
            rt_http_req_set_header(req, key, (rt_string)val);
    }
    if (rt_obj_release_check0(keys))
        rt_obj_free(keys);

    // Apply timeout
    if (c->timeout_ms > 0)
        rt_http_req_set_timeout(req, c->timeout_ms);

    rt_http_req_set_follow_redirects(req, 0);
}

static void *clone_cookie_map(void *source) {
    void *copy = rt_map_new();
    if (!source || !copy)
        return copy ? copy : rt_map_new();

    void *keys = rt_map_keys(source);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(source, key);
        if (value)
            rt_map_set(copy, key, value);
    }
    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
    return copy;
}

static void *cookies_for_domain(rt_http_client_impl *c, rt_string domain) {
    void *domain_cookies = rt_map_get(c->cookies, domain);
    return domain_cookies ? domain_cookies : NULL;
}

static void maybe_add_domain_cookies(rt_http_client_impl *c, void *merged, const char *host) {
    rt_string domain = rt_string_from_bytes(host, strlen(host));
    void *domain_cookies = cookies_for_domain(c, domain);
    rt_string_unref(domain);
    if (!domain_cookies)
        return;

    void *keys = rt_map_keys(domain_cookies);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(domain_cookies, key);
        if (value)
            rt_map_set(merged, key, value);
    }
    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
}

static void apply_cookie_header(rt_http_client_impl *c, void *req, rt_string url) {
    void *parsed = rt_url_parse(url);
    rt_string host = rt_url_host(parsed);
    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr || !*host_cstr) {
        rt_string_unref(host);
        if (parsed && rt_obj_release_check0(parsed))
            rt_obj_free(parsed);
        return;
    }

    void *merged = rt_map_new();
    maybe_add_domain_cookies(c, merged, host_cstr);

    const char *dot = strchr(host_cstr, '.');
    while (dot) {
        maybe_add_domain_cookies(c, merged, dot + 1);
        dot = strchr(dot + 1, '.');
    }

    void *keys = rt_map_keys(merged);
    int64_t count = rt_seq_len(keys);
    if (count > 0) {
        size_t total_len = 0;
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *value = rt_map_get(merged, key);
            const char *key_cstr = rt_string_cstr(key);
            const char *value_cstr = value ? rt_string_cstr((rt_string)value) : "";
            if (!key_cstr)
                continue;
            total_len += strlen(key_cstr) + 1 + strlen(value_cstr ? value_cstr : "");
            if (i + 1 < count)
                total_len += 2;
        }

        char *cookie_header = (char *)malloc(total_len + 1);
        if (cookie_header) {
            size_t pos = 0;
            for (int64_t i = 0; i < count; i++) {
                rt_string key = (rt_string)rt_seq_get(keys, i);
                void *value = rt_map_get(merged, key);
                const char *key_cstr = rt_string_cstr(key);
                const char *value_cstr = value ? rt_string_cstr((rt_string)value) : "";
                if (!key_cstr)
                    continue;
                int written = snprintf(cookie_header + pos,
                                       total_len + 1 - pos,
                                       i + 1 < count ? "%s=%s; " : "%s=%s",
                                       key_cstr,
                                       value_cstr ? value_cstr : "");
                if (written > 0)
                    pos += (size_t)written;
            }
            rt_string header_str = rt_string_from_bytes(cookie_header, strlen(cookie_header));
            rt_http_req_set_header(req, rt_const_cstr("Cookie"), header_str);
            rt_string_unref(header_str);
            free(cookie_header);
        }
    }

    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
    if (merged && rt_obj_release_check0(merged))
        rt_obj_free(merged);
    rt_string_unref(host);
    if (parsed && rt_obj_release_check0(parsed))
        rt_obj_free(parsed);
}

static void store_cookie_line(rt_http_client_impl *c, const char *host, const char *line) {
    if (!host || !line || !*line)
        return;

    const char *cookie_end = strchr(line, ';');
    size_t cookie_len = cookie_end ? (size_t)(cookie_end - line) : strlen(line);
    const char *eq = memchr(line, '=', cookie_len);
    if (!eq || eq == line)
        return;

    size_t name_len = (size_t)(eq - line);
    size_t value_len = cookie_len - name_len - 1;
    if (name_len == 0)
        return;

    char domain_buf[256];
    snprintf(domain_buf, sizeof(domain_buf), "%s", host);

    const char *attr = cookie_end ? cookie_end + 1 : NULL;
    while (attr && *attr) {
        while (*attr == ' ' || *attr == '\t' || *attr == ';')
            attr++;
        if (!*attr)
            break;

        const char *attr_end = strchr(attr, ';');
        size_t attr_len = attr_end ? (size_t)(attr_end - attr) : strlen(attr);
        if (attr_len > 7 && strncasecmp(attr, "Domain=", 7) == 0) {
            const char *domain_val = attr + 7;
            while (*domain_val == '.')
                domain_val++;
            size_t domain_len = attr_len - (size_t)(domain_val - attr);
            if (domain_len > 0 && domain_len < sizeof(domain_buf)) {
                memcpy(domain_buf, domain_val, domain_len);
                domain_buf[domain_len] = '\0';
            }
        }
        attr = attr_end ? attr_end + 1 : NULL;
    }

    rt_string domain = rt_string_from_bytes(domain_buf, strlen(domain_buf));
    rt_string name = rt_string_from_bytes(line, name_len);
    rt_string value = rt_string_from_bytes(eq + 1, value_len);
    rt_http_client_set_cookie(c, domain, name, value);
    rt_string_unref(value);
    rt_string_unref(name);
    rt_string_unref(domain);
}

static void store_response_cookies(rt_http_client_impl *c, void *res, rt_string url) {
    void *parsed = rt_url_parse(url);
    rt_string host = rt_url_host(parsed);
    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr || !*host_cstr) {
        rt_string_unref(host);
        if (parsed && rt_obj_release_check0(parsed))
            rt_obj_free(parsed);
        return;
    }

    rt_string cookie_header = rt_http_res_header(res, rt_const_cstr("set-cookie"));
    const char *cookies = rt_string_cstr(cookie_header);
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
            store_cookie_line(c, host_cstr, entry);
            free(entry);
            if (!end)
                break;
            line = end + 1;
        }
    }

    rt_string_unref(cookie_header);
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
    rt_string current_url = rt_string_from_bytes(url_cstr ? url_cstr : "", url_cstr ? strlen(url_cstr) : 0);
    const char *current_method = method;
    rt_string current_body = NULL;
    if (body) {
        const char *body_cstr = rt_string_cstr(body);
        current_body = rt_string_from_bytes(body_cstr ? body_cstr : "", body_cstr ? strlen(body_cstr) : 0);
    }

    int64_t redirects_left = c->follow_redirects ? c->max_redirects : 0;
    void *final_res = NULL;

    while (1) {
        rt_string method_str = rt_string_from_bytes(current_method, strlen(current_method));
        void *req = rt_http_req_new(method_str, current_url);
        rt_string_unref(method_str);

        apply_defaults(c, req);
        apply_cookie_header(c, req, current_url);
        rt_http_req_set_max_redirects(req, 0);

        if (current_body && strcmp(current_method, "GET") != 0 && strcmp(current_method, "DELETE") != 0)
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
        rt_string_unref(current_url);
        current_url = next_url;
        redirects_left--;

        if (status == 303 || ((status == 301 || status == 302) && strcmp(current_method, "POST") == 0)) {
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

void *rt_http_client_new(void) {
    rt_http_client_impl *c =
        (rt_http_client_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_client_impl));
    if (!c)
        rt_trap("HttpClient: OOM");
    memset(c, 0, sizeof(*c));
    c->default_headers = rt_map_new();
    c->cookies = rt_map_new();
    c->timeout_ms = 30000;
    c->max_redirects = 5;
    c->follow_redirects = 1;
    rt_obj_set_finalizer(c, rt_http_client_finalize);
    return c;
}

void *rt_http_client_get(void *obj, rt_string url) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "GET", url, NULL);
}

void *rt_http_client_post(void *obj, rt_string url, rt_string body) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "POST", url, body);
}

void *rt_http_client_put(void *obj, rt_string url, rt_string body) {
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "PUT", url, body);
}

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
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;

    void *domain_cookies = rt_map_get(c->cookies, domain);
    if (!domain_cookies) {
        domain_cookies = rt_map_new();
        rt_map_set(c->cookies, domain, domain_cookies);
        if (domain_cookies && rt_obj_release_check0(domain_cookies))
            rt_obj_free(domain_cookies);
        domain_cookies = rt_map_get(c->cookies, domain);
    }
    rt_map_set(domain_cookies, name, (void *)value);
}

void *rt_http_client_get_cookies(void *obj, rt_string domain) {
    if (!obj)
        return rt_map_new();
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    void *domain_cookies = rt_map_get(c->cookies, domain);
    return domain_cookies ? clone_cookie_map(domain_cookies) : rt_map_new();
}
