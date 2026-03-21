//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http_router.h
// Purpose: HTTP URL pattern matching with parameter extraction and middleware.
// Key invariants:
//   - Routes are matched in registration order (first match wins).
//   - Pattern parameters use :name syntax (e.g., "/users/:id").
//   - Wildcard *name matches the rest of the path.
// Ownership/Lifetime:
//   - Router objects are GC-managed via rt_obj_set_finalizer.
// Links: rt_http_server.h (consumer), rt_network.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new HTTP router.
    void *rt_http_router_new(void);

    /// @brief Add a route for a specific HTTP method and pattern.
    /// @param router Router object.
    /// @param method HTTP method (GET, POST, PUT, DELETE, etc.).
    /// @param pattern URL pattern (e.g., "/users/:id").
    void *rt_http_router_add(void *router, rt_string method, rt_string pattern);

    /// @brief Add a GET route.
    void *rt_http_router_get(void *router, rt_string pattern);

    /// @brief Add a POST route.
    void *rt_http_router_post(void *router, rt_string pattern);

    /// @brief Add a PUT route.
    void *rt_http_router_put(void *router, rt_string pattern);

    /// @brief Add a DELETE route.
    void *rt_http_router_delete(void *router, rt_string pattern);

    /// @brief Match a request method and path against registered routes.
    /// @param router Router object.
    /// @param method HTTP method.
    /// @param path Request path.
    /// @return Route match object with params, or NULL if no match.
    void *rt_http_router_match(void *router, rt_string method, rt_string path);

    /// @brief Get number of registered routes.
    int64_t rt_http_router_count(void *router);

    /// @brief Get a parameter value from a route match.
    rt_string rt_route_match_param(void *match, rt_string name);

    /// @brief Get the route index from a match.
    int64_t rt_route_match_index(void *match);

    /// @brief Get the matched path pattern.
    rt_string rt_route_match_pattern(void *match);

#ifdef __cplusplus
}
#endif
