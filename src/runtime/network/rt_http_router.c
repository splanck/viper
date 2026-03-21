//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_router.c
// Purpose: HTTP URL pattern matching with parameter extraction.
// Key invariants:
//   - Routes matched in registration order (first match wins).
//   - :name captures a single path segment; *name captures the rest.
//   - Thread-safe for concurrent matching (routes are immutable after start).
// Ownership/Lifetime:
//   - Router and match objects are GC-managed.
// Links: rt_http_router.h (API), rt_http_server.c (consumer)
//
//===----------------------------------------------------------------------===//

#include "rt_http_router.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structures
//=============================================================================

#define MAX_ROUTE_SEGMENTS 32
#define MAX_ROUTES 256

typedef enum
{
    SEG_LITERAL, // Exact match: "users"
    SEG_PARAM,   // Parameter capture: ":id"
    SEG_WILDCARD // Wildcard capture: "*path"
} segment_type_t;

typedef struct
{
    segment_type_t type;
    char *value; // Literal text, or param name (without : or *)
} segment_t;

typedef struct
{
    char *method;                     // "GET", "POST", etc.
    char *pattern;                    // Original pattern string
    segment_t segments[MAX_ROUTE_SEGMENTS];
    int segment_count;
} route_t;

typedef struct
{
    route_t routes[MAX_ROUTES];
    int route_count;
} rt_http_router_impl;

typedef struct
{
    int route_index;
    char *pattern;
    void *params; // Map of param name -> value
} rt_route_match_impl;

//=============================================================================
// Parsing
//=============================================================================

/// @brief Parse a URL pattern into segments.
static int parse_pattern(const char *pattern, segment_t *segments, int max_segments)
{
    if (!pattern || *pattern == '\0')
        return 0;

    const char *p = pattern;
    if (*p == '/')
        p++; // Skip leading /

    int count = 0;
    while (*p && count < max_segments)
    {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == 0)
        {
            p = end ? end + 1 : p + strlen(p);
            continue;
        }

        if (*p == ':' && len > 1)
        {
            segments[count].type = SEG_PARAM;
            segments[count].value = (char *)malloc(len);
            if (segments[count].value)
            {
                memcpy(segments[count].value, p + 1, len - 1);
                segments[count].value[len - 1] = '\0';
            }
        }
        else if (*p == '*' && len > 1)
        {
            segments[count].type = SEG_WILDCARD;
            segments[count].value = (char *)malloc(len);
            if (segments[count].value)
            {
                memcpy(segments[count].value, p + 1, len - 1);
                segments[count].value[len - 1] = '\0';
            }
        }
        else
        {
            segments[count].type = SEG_LITERAL;
            segments[count].value = (char *)malloc(len + 1);
            if (segments[count].value)
            {
                memcpy(segments[count].value, p, len);
                segments[count].value[len] = '\0';
            }
        }

        count++;
        p = end ? end + 1 : p + strlen(p);
    }

    return count;
}

//=============================================================================
// Matching
//=============================================================================

/// @brief Match a request path against a parsed route.
/// @return params Map on match, NULL on no match.
static void *match_route(const route_t *route, const char *path)
{
    if (!path || *path == '\0')
        path = "/";

    const char *p = path;
    if (*p == '/')
        p++;

    void *params = rt_map_new();
    int seg_idx = 0;

    while (seg_idx < route->segment_count)
    {
        const segment_t *seg = &route->segments[seg_idx];

        if (seg->type == SEG_WILDCARD)
        {
            // Capture the rest of the path
            size_t rest_len = strlen(p);
            rt_string name = rt_string_from_bytes(seg->value, strlen(seg->value));
            // Strip trailing / from captured path
            while (rest_len > 0 && p[rest_len - 1] == '/')
                rest_len--;
            rt_string val = rt_string_from_bytes(p, rest_len);
            rt_map_set(params, name, (void *)val);
            rt_string_unref(name);
            return params;
        }

        // Find end of current path segment
        const char *seg_end = strchr(p, '/');
        size_t seg_len = seg_end ? (size_t)(seg_end - p) : strlen(p);

        if (seg_len == 0 && *p == '\0')
        {
            // Path exhausted but route has more segments
            if (rt_obj_release_check0(params))
                rt_obj_free(params);
            return NULL;
        }

        if (seg->type == SEG_LITERAL)
        {
            // Exact match required
            if (strlen(seg->value) != seg_len || strncmp(seg->value, p, seg_len) != 0)
            {
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
        }
        else if (seg->type == SEG_PARAM)
        {
            // Capture parameter
            rt_string name = rt_string_from_bytes(seg->value, strlen(seg->value));
            rt_string val = rt_string_from_bytes(p, seg_len);
            rt_map_set(params, name, (void *)val);
            rt_string_unref(name);
        }

        p = seg_end ? seg_end + 1 : p + seg_len;
        seg_idx++;
    }

    // Check that path is fully consumed (allow trailing /)
    while (*p == '/')
        p++;
    if (*p != '\0')
    {
        if (rt_obj_release_check0(params))
            rt_obj_free(params);
        return NULL;
    }

    return params;
}

//=============================================================================
// Finalizers
//=============================================================================

static void free_route(route_t *route)
{
    free(route->method);
    free(route->pattern);
    for (int i = 0; i < route->segment_count; i++)
        free(route->segments[i].value);
    memset(route, 0, sizeof(*route));
}

static void rt_http_router_finalize(void *obj)
{
    if (!obj)
        return;
    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    for (int i = 0; i < router->route_count; i++)
        free_route(&router->routes[i]);
}

static void rt_route_match_finalize(void *obj)
{
    if (!obj)
        return;
    rt_route_match_impl *match = (rt_route_match_impl *)obj;
    free(match->pattern);
    match->pattern = NULL;
    if (match->params && rt_obj_release_check0(match->params))
        rt_obj_free(match->params);
    match->params = NULL;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_http_router_new(void)
{
    rt_http_router_impl *router =
        (rt_http_router_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_router_impl));
    if (!router)
        rt_trap("HttpRouter: memory allocation failed");
    memset(router, 0, sizeof(*router));
    rt_obj_set_finalizer(router, rt_http_router_finalize);
    return router;
}

static void *add_route(void *obj, const char *method, const char *pattern)
{
    if (!obj)
        rt_trap("HttpRouter: NULL router");

    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    if (router->route_count >= MAX_ROUTES)
        rt_trap("HttpRouter: too many routes (max 256)");

    route_t *route = &router->routes[router->route_count];
    route->method = strdup(method);
    route->pattern = strdup(pattern);
    route->segment_count = parse_pattern(pattern, route->segments, MAX_ROUTE_SEGMENTS);

    router->route_count++;
    return obj;
}

void *rt_http_router_add(void *router, rt_string method, rt_string pattern)
{
    const char *m = rt_string_cstr(method);
    const char *p = rt_string_cstr(pattern);
    if (!m || !p)
        rt_trap("HttpRouter: NULL method or pattern");
    return add_route(router, m, p);
}

void *rt_http_router_get(void *router, rt_string pattern)
{
    return add_route(router, "GET", rt_string_cstr(pattern));
}

void *rt_http_router_post(void *router, rt_string pattern)
{
    return add_route(router, "POST", rt_string_cstr(pattern));
}

void *rt_http_router_put(void *router, rt_string pattern)
{
    return add_route(router, "PUT", rt_string_cstr(pattern));
}

void *rt_http_router_delete(void *router, rt_string pattern)
{
    return add_route(router, "DELETE", rt_string_cstr(pattern));
}

void *rt_http_router_match(void *obj, rt_string method, rt_string path)
{
    if (!obj)
        return NULL;

    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    const char *m = rt_string_cstr(method);
    const char *p = rt_string_cstr(path);
    if (!m || !p)
        return NULL;

    for (int i = 0; i < router->route_count; i++)
    {
        route_t *route = &router->routes[i];

        // Check method match
        if (strcasecmp(route->method, m) != 0)
            continue;

        // Try path match
        void *params = match_route(route, p);
        if (params)
        {
            // Create match object
            rt_route_match_impl *match =
                (rt_route_match_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_route_match_impl));
            if (!match)
            {
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
            memset(match, 0, sizeof(*match));
            rt_obj_set_finalizer(match, rt_route_match_finalize);
            match->route_index = i;
            match->pattern = strdup(route->pattern);
            match->params = params;
            return match;
        }
    }

    return NULL; // No match
}

int64_t rt_http_router_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_http_router_impl *)obj)->route_count;
}

rt_string rt_route_match_param(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_route_match_impl *match = (rt_route_match_impl *)obj;
    if (!match->params)
        return rt_string_from_bytes("", 0);

    void *val = rt_map_get(match->params, name);
    if (!val)
        return rt_string_from_bytes("", 0);
    return (rt_string)val;
}

int64_t rt_route_match_index(void *obj)
{
    if (!obj)
        return -1;
    return ((rt_route_match_impl *)obj)->route_index;
}

rt_string rt_route_match_pattern(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_route_match_impl *match = (rt_route_match_impl *)obj;
    if (!match->pattern)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(match->pattern, strlen(match->pattern));
}
