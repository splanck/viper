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
//   - Router operations are unsynchronized; callers must serialize adds and matches.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "rt_trap.h"

//=============================================================================
// Internal Structures
//=============================================================================

#define INITIAL_ROUTE_CAPACITY 16
#define INITIAL_ROUTE_SEGMENT_CAPACITY 8

typedef enum {
    SEG_LITERAL, // Exact match: "users"
    SEG_PARAM,   // Parameter capture: ":id"
    SEG_WILDCARD // Wildcard capture: "*path"
} segment_type_t;

typedef struct {
    segment_type_t type;
    char *value; // Literal text, or param name (without : or *)
} segment_t;

typedef struct {
    char *method;  // "GET", "POST", etc.
    char *pattern; // Original pattern string
    segment_t *segments;
    int segment_count;
} route_t;

typedef struct {
    route_t *routes;
    int route_count;
    int route_capacity;
} rt_http_router_impl;

typedef struct {
    int route_index;
    char *pattern;
    void *params; // Map of param name -> value
} rt_route_match_impl;

//=============================================================================
// Parsing
//=============================================================================

/// @brief Ensure a route segment array can hold at least @p needed entries.
/// @details Route patterns are user-facing input, so this helper grows parsed
///          segment storage geometrically with overflow checks instead of
///          imposing an arbitrary fixed segment ceiling. Existing segment
///          contents remain valid if allocation fails.
/// @param segments In/out segment array pointer.
/// @param capacity In/out segment capacity in entries.
/// @param needed Minimum required entry count.
/// @return 1 when capacity is available; 0 on overflow or OOM.
static int reserve_route_segments(segment_t **segments, int *capacity, int needed) {
    if (!segments || !capacity || needed < 0)
        return 0;
    if (needed <= *capacity)
        return 1;

    int new_capacity = *capacity > 0 ? *capacity : INITIAL_ROUTE_SEGMENT_CAPACITY;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > (uint64_t)SIZE_MAX / sizeof(**segments))
        return 0;

    segment_t *grown = (segment_t *)realloc(*segments, (size_t)new_capacity * sizeof(**segments));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(new_capacity - *capacity) * sizeof(*grown));
    *segments = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Ensure the router can hold at least @p needed registered routes.
/// @details Keeps route registration append-only and first-match order stable
///          while removing the previous fixed 256-route table. Reallocation
///          failure leaves the router unchanged so callers can trap cleanly.
/// @param router Router implementation object.
/// @param needed Minimum required route count.
/// @return 1 when capacity is available; 0 on overflow or OOM.
static int reserve_routes(rt_http_router_impl *router, int needed) {
    if (!router || needed < 0)
        return 0;
    if (needed <= router->route_capacity)
        return 1;

    int new_capacity = router->route_capacity > 0 ? router->route_capacity : INITIAL_ROUTE_CAPACITY;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > (uint64_t)SIZE_MAX / sizeof(*router->routes))
        return 0;

    route_t *grown = (route_t *)realloc(router->routes, (size_t)new_capacity * sizeof(*grown));
    if (!grown)
        return 0;
    memset(grown + router->route_capacity,
           0,
           (size_t)(new_capacity - router->route_capacity) * sizeof(*grown));
    router->routes = grown;
    router->route_capacity = new_capacity;
    return 1;
}

/// @brief Parse a URL pattern into typed segments for route matching.
/// @details Splits `pattern` at each `/` and classifies each non-empty
///          segment into one of three types:
///          - **`SEG_LITERAL`**: the segment matches its exact text. E.g.
///            `users` in `/users/:id`.
///          - **`SEG_PARAM`**: prefix `:` marks a single-segment capture.
///            E.g. `:id` in `/users/:id` matches one segment and binds it
///            to a route parameter named `id`. The captured segment cannot
///            be empty and cannot span `/` boundaries.
///          - **`SEG_WILDCARD`**: prefix `*` marks a multi-segment capture
///            that consumes the remainder of the path. E.g. `*path` in
///            `/static/*path` matches `assets/img/foo.png` and binds the
///            full remainder to `path`. Wildcards should be last; neither the
///            parser nor matcher rejects suffix segments, and the matcher
///            returns immediately when it reaches the wildcard.
///
///          A leading `/` is skipped before parsing. Empty segments
///          (consecutive slashes like `/users//profile`) are ignored.
///          Segment storage is allocated and grown on demand; on failure all
///          partially parsed segment values are freed and no truncated route is
///          registered.
/// @param pattern Source URL pattern (with or without leading `/`).
/// @param segments_out Output segment array; caller owns and frees via `free_route`.
/// @return Number of segments parsed (0 if pattern is empty), or -1 on invalid input/OOM.
static int parse_pattern(const char *pattern, segment_t **segments_out) {
    if (!pattern || !segments_out)
        return -1;
    *segments_out = NULL;
    if (*pattern == '\0')
        return 0;

    const char *p = pattern;
    if (*p == '/')
        p++; // Skip leading /

    int count = 0;
    int capacity = 0;
    segment_t *segments = NULL;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == 0) {
            p = end ? end + 1 : p + strlen(p);
            continue;
        }

        if (!reserve_route_segments(&segments, &capacity, count + 1))
            goto fail;

        segment_t *segment = &segments[count];
        memset(segment, 0, sizeof(*segment));
        if (*p == ':' && len > 1) {
            segment->type = SEG_PARAM;
            segment->value = (char *)malloc(len);
            if (!segment->value)
                goto fail;
            memcpy(segment->value, p + 1, len - 1);
            segment->value[len - 1] = '\0';
        } else if (*p == '*' && len > 1) {
            segment->type = SEG_WILDCARD;
            segment->value = (char *)malloc(len);
            if (!segment->value)
                goto fail;
            memcpy(segment->value, p + 1, len - 1);
            segment->value[len - 1] = '\0';
        } else {
            segment->type = SEG_LITERAL;
            if (len == SIZE_MAX)
                goto fail;
            segment->value = (char *)malloc(len + 1);
            if (!segment->value)
                goto fail;
            memcpy(segment->value, p, len);
            segment->value[len] = '\0';
        }

        count++;
        p = end ? end + 1 : p + strlen(p);
    }

    *segments_out = segments;
    return count;

fail:
    for (int i = 0; i <= count && i < capacity; i++) {
        free(segments[i].value);
        segments[i].value = NULL;
    }
    free(segments);
    return -1;
}

//=============================================================================
// Matching
//=============================================================================

/// @brief Match a request path against a parsed route.
/// @return params Map on match, NULL on no match.
static void *match_route(const route_t *route, const char *path) {
    if (!path || *path == '\0')
        path = "/";

    const char *p = path;
    if (*p == '/')
        p++;

    void *params = rt_map_new();
    int seg_idx = 0;

    while (seg_idx < route->segment_count) {
        const segment_t *seg = &route->segments[seg_idx];
        if (!seg->value) {
            if (rt_obj_release_check0(params))
                rt_obj_free(params);
            return NULL;
        }

        if (seg->type == SEG_WILDCARD) {
            // Capture the rest of the path
            size_t rest_len = strlen(p);
            rt_string name = rt_string_from_bytes(seg->value, strlen(seg->value));
            // Strip trailing / from captured path
            while (rest_len > 0 && p[rest_len - 1] == '/')
                rest_len--;
            rt_string val = rt_string_from_bytes(p, rest_len);
            if (!name || !val) {
                rt_string_unref(name);
                rt_string_unref(val);
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
            rt_map_set(params, name, (void *)val);
            rt_string_unref(name);
            rt_string_unref(val);
            return params;
        }

        // Find end of current path segment
        const char *seg_end = strchr(p, '/');
        size_t seg_len = seg_end ? (size_t)(seg_end - p) : strlen(p);

        if (seg_len == 0 && *p == '\0') {
            // Path exhausted but route has more segments
            if (rt_obj_release_check0(params))
                rt_obj_free(params);
            return NULL;
        }

        if (seg->type == SEG_LITERAL) {
            // Exact match required
            if (strlen(seg->value) != seg_len || strncmp(seg->value, p, seg_len) != 0) {
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
        } else if (seg->type == SEG_PARAM) {
            // Capture parameter
            rt_string name = rt_string_from_bytes(seg->value, strlen(seg->value));
            rt_string val = rt_string_from_bytes(p, seg_len);
            if (!name || !val) {
                rt_string_unref(name);
                rt_string_unref(val);
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
            rt_map_set(params, name, (void *)val);
            rt_string_unref(name);
            rt_string_unref(val);
        }

        p = seg_end ? seg_end + 1 : p + seg_len;
        seg_idx++;
    }

    // Check that path is fully consumed (allow trailing /)
    while (*p == '/')
        p++;
    if (*p != '\0') {
        if (rt_obj_release_check0(params))
            rt_obj_free(params);
        return NULL;
    }

    return params;
}

//=============================================================================
// Finalizers
//=============================================================================

/// @brief Free a route's heap-owned strings (method, pattern, per-segment values) and zero it.
static void free_route(route_t *route) {
    free(route->method);
    free(route->pattern);
    for (int i = 0; i < route->segment_count; i++)
        free(route->segments[i].value);
    free(route->segments);
    memset(route, 0, sizeof(*route));
}

/// @brief GC finalizer for the router: free every parsed route's heap allocations.
static void rt_http_router_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    for (int i = 0; i < router->route_count; i++)
        free_route(&router->routes[i]);
    free(router->routes);
    router->routes = NULL;
    router->route_count = 0;
    router->route_capacity = 0;
}

/// @brief GC finalizer for a route-match: free the pattern string and release the params Map.
static void rt_route_match_finalize(void *obj) {
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

/// @brief Construct an empty HTTP router with growable route storage.
/// @details Routes are matched in registration order, so register more-specific
///          patterns first. The route and per-route segment arrays grow as
///          routes are added and are released by the GC finalizer.
void *rt_http_router_new(void) {
    rt_http_router_impl *router =
        (rt_http_router_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_router_impl));
    if (!router) {
        rt_trap("HttpRouter: memory allocation failed");
        return NULL;
    }
    memset(router, 0, sizeof(*router));
    rt_obj_set_finalizer(router, rt_http_router_finalize);
    return router;
}

/// @brief Internal: register a `(method, pattern)` route. Parses the pattern into segments
/// (literal/param/wildcard), allocates per-segment storage, and appends to the routes array.
/// Traps on null input, invalid pattern, or allocation failure.
static void *add_route(void *obj, const char *method, const char *pattern) {
    if (!obj) {
        rt_trap("HttpRouter: NULL router");
        return NULL;
    }
    if (!method || !pattern) {
        rt_trap("HttpRouter: NULL method or pattern");
        return obj;
    }

    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    if (!reserve_routes(router, router->route_count + 1)) {
        rt_trap("HttpRouter: route storage allocation failed");
        return obj;
    }

    route_t *route = &router->routes[router->route_count];
    memset(route, 0, sizeof(*route));
    route->method = strdup(method);
    route->pattern = strdup(pattern);
    if (!route->method || !route->pattern) {
        free_route(route);
        rt_trap("HttpRouter: memory allocation failed");
        return obj;
    }
    route->segment_count = parse_pattern(pattern, &route->segments);
    if (route->segment_count < 0) {
        free_route(route);
        rt_trap("HttpRouter: invalid route pattern");
        return obj;
    }

    router->route_count++;
    return obj;
}

/// @brief Register a route for an arbitrary HTTP method (e.g. PATCH, OPTIONS, custom verbs).
/// Returns the router for fluent chaining: `router.add("GET", "/x").add("POST", "/y")`.
void *rt_http_router_add(void *router, rt_string method, rt_string pattern) {
    const char *m = rt_string_cstr(method);
    const char *p = rt_string_cstr(pattern);
    if (!m || !p)
        rt_trap("HttpRouter: NULL method or pattern");
    return add_route(router, m, p);
}

/// @brief Convenience: register a GET route. Equivalent to `_add(router, "GET", pattern)`.
void *rt_http_router_get(void *router, rt_string pattern) {
    return add_route(router, "GET", rt_string_cstr(pattern));
}

/// @brief Convenience: register a POST route.
void *rt_http_router_post(void *router, rt_string pattern) {
    return add_route(router, "POST", rt_string_cstr(pattern));
}

/// @brief Convenience: register a PUT route.
void *rt_http_router_put(void *router, rt_string pattern) {
    return add_route(router, "PUT", rt_string_cstr(pattern));
}

/// @brief Convenience: register a DELETE route.
void *rt_http_router_delete(void *router, rt_string pattern) {
    return add_route(router, "DELETE", rt_string_cstr(pattern));
}

/// @brief Find the first registered route that matches `(method, path)` and return a Match
/// object with extracted params. Method comparison is **case-insensitive** (a runtime behavior,
/// even though HTTP method tokens are ordinarily case-sensitive).
/// Walks routes in registration order — earlier wins on ties. Returns NULL if no route matches
/// (the server can then return 404). The returned Match object is GC-managed; caller releases.
void *rt_http_router_match(void *obj, rt_string method, rt_string path) {
    if (!obj)
        return NULL;

    rt_http_router_impl *router = (rt_http_router_impl *)obj;
    const char *m = rt_string_cstr(method);
    const char *p = rt_string_cstr(path);
    if (!m || !p)
        return NULL;

    for (int i = 0; i < router->route_count; i++) {
        route_t *route = &router->routes[i];

        // Check method match
        if (strcasecmp(route->method, m) != 0)
            continue;

        // Try path match
        void *params = match_route(route, p);
        if (params) {
            // Create match object
            rt_route_match_impl *match =
                (rt_route_match_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_route_match_impl));
            if (!match) {
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                return NULL;
            }
            memset(match, 0, sizeof(*match));
            rt_obj_set_finalizer(match, rt_route_match_finalize);
            match->route_index = i;
            match->pattern = strdup(route->pattern);
            if (!match->pattern) {
                if (rt_obj_release_check0(params))
                    rt_obj_free(params);
                if (rt_obj_release_check0(match))
                    rt_obj_free(match);
                return NULL;
            }
            match->params = params;
            return match;
        }
    }

    return NULL; // No match
}

/// @brief Number of routes currently registered (for diagnostics / capacity checks).
int64_t rt_http_router_count(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_router_impl *)obj)->route_count;
}

/// @brief Look up a captured parameter from a Match object (e.g. for `/users/:id` with input
/// `/users/42`, `_param("id")` returns "42"). Returns empty string if the param wasn't captured.
rt_string rt_route_match_param(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_route_match_impl *match = (rt_route_match_impl *)obj;
    if (!match->params)
        return rt_string_from_bytes("", 0);

    void *val = rt_map_get(match->params, name);
    if (!val)
        return rt_string_from_bytes("", 0);
    return rt_string_ref((rt_string)val);
}

/// @brief Index of the route that matched (registration order). -1 for null Match. Useful for
/// dispatching to a parallel handler array indexed by the same numbers.
int64_t rt_route_match_index(void *obj) {
    if (!obj)
        return -1;
    return ((rt_route_match_impl *)obj)->route_index;
}

/// @brief Read the original pattern string of the matched route (for logging / debug output).
rt_string rt_route_match_pattern(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_route_match_impl *match = (rt_route_match_impl *)obj;
    if (!match->pattern)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(match->pattern, strlen(match->pattern));
}
