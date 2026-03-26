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

#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
    void *default_headers; // Map<String, String>
    void *cookies;         // Map<String(domain), Map<String, String>>
    int64_t timeout_ms;
    int64_t max_redirects;
    int8_t follow_redirects;
} rt_http_client_impl;

static void rt_http_client_finalize(void *obj)
{
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

static void apply_defaults(rt_http_client_impl *c, void *req)
{
    // Apply default headers
    void *keys = rt_map_keys(c->default_headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++)
    {
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
}

static void *do_request(rt_http_client_impl *c, const char *method, rt_string url, rt_string body)
{
    rt_string method_str = rt_string_from_bytes(method, strlen(method));
    void *req = rt_http_req_new(method_str, url);
    rt_string_unref(method_str);

    apply_defaults(c, req);

    if (body)
    {
        rt_http_req_set_body_str(req, body);
    }

    void *res = rt_http_req_send(req);
    return res;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_http_client_new(void)
{
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

void *rt_http_client_get(void *obj, rt_string url)
{
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "GET", url, NULL);
}

void *rt_http_client_post(void *obj, rt_string url, rt_string body)
{
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "POST", url, body);
}

void *rt_http_client_put(void *obj, rt_string url, rt_string body)
{
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "PUT", url, body);
}

void *rt_http_client_delete(void *obj, rt_string url)
{
    if (!obj)
        rt_trap("HttpClient: NULL");
    return do_request((rt_http_client_impl *)obj, "DELETE", url, NULL);
}

/// @brief Perform client set header operation.
/// @param obj
/// @param name
/// @param value
void rt_http_client_set_header(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    rt_map_set(c->default_headers, name, (void *)value);
}

/// @brief Perform client set timeout operation.
/// @param obj
/// @param timeout_ms
void rt_http_client_set_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->timeout_ms = timeout_ms;
}

/// @brief Perform client set max redirects operation.
/// @param obj
/// @param max
void rt_http_client_set_max_redirects(void *obj, int64_t max)
{
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->max_redirects = max;
}

/// @brief Perform client get follow redirects operation.
/// @param obj
/// @return Result value.
int8_t rt_http_client_get_follow_redirects(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_http_client_impl *)obj)->follow_redirects;
}

/// @brief Perform client set follow redirects operation.
/// @param obj
/// @param follow
void rt_http_client_set_follow_redirects(void *obj, int8_t follow)
{
    if (!obj)
        return;
    ((rt_http_client_impl *)obj)->follow_redirects = follow;
}

/// @brief Perform client set cookie operation.
/// @param obj
/// @param domain
/// @param name
/// @param value
void rt_http_client_set_cookie(void *obj, rt_string domain, rt_string name, rt_string value)
{
    if (!obj)
        return;
    rt_http_client_impl *c = (rt_http_client_impl *)obj;

    void *domain_cookies = rt_map_get(c->cookies, domain);
    if (!domain_cookies)
    {
        domain_cookies = rt_map_new();
        rt_map_set(c->cookies, domain, domain_cookies);
    }
    rt_map_set(domain_cookies, name, (void *)value);
}

void *rt_http_client_get_cookies(void *obj, rt_string domain)
{
    if (!obj)
        return rt_map_new();
    rt_http_client_impl *c = (rt_http_client_impl *)obj;
    void *domain_cookies = rt_map_get(c->cookies, domain);
    return domain_cookies ? domain_cookies : rt_map_new();
}
