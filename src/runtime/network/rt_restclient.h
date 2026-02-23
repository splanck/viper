//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_restclient.h
// Purpose: REST API client with session management, persistent headers, base URL support, JSON convenience methods, and configurable timeouts.
//
// Key invariants:
//   - Persistent headers (e.g., Authorization) are sent with every request.
//   - Base URL is prepended to all relative request paths.
//   - JSON helper methods automatically set Content-Type: application/json.
//   - Timeout applies to connection establishment and data transfer.
//
// Ownership/Lifetime:
//   - RestClient objects are heap-allocated; caller is responsible for lifetime management.
//   - Returned response strings are newly allocated; caller must release.
//
// Links: src/runtime/network/rt_restclient.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_RESTCLIENT_H
#define VIPER_RT_RESTCLIENT_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // RestClient Creation and Configuration
    //=============================================================================

    /// @brief Create a new REST client with base URL.
    /// @param base_url Base URL for all requests (e.g., "https://api.example.com").
    /// @return RestClient object.
    void *rt_restclient_new(rt_string base_url);

    /// @brief Get the base URL.
    /// @param obj RestClient object.
    /// @return Base URL string.
    rt_string rt_restclient_base_url(void *obj);

    /// @brief Set a default header for all requests.
    /// @param obj RestClient object.
    /// @param name Header name.
    /// @param value Header value.
    void rt_restclient_set_header(void *obj, rt_string name, rt_string value);

    /// @brief Remove a default header.
    /// @param obj RestClient object.
    /// @param name Header name.
    void rt_restclient_del_header(void *obj, rt_string name);

    /// @brief Set authorization token (Bearer).
    /// @param obj RestClient object.
    /// @param token Bearer token.
    void rt_restclient_set_auth_bearer(void *obj, rt_string token);

    /// @brief Set basic authentication.
    /// @param obj RestClient object.
    /// @param username Username.
    /// @param password Password.
    void rt_restclient_set_auth_basic(void *obj, rt_string username, rt_string password);

    /// @brief Clear authentication.
    /// @param obj RestClient object.
    void rt_restclient_clear_auth(void *obj);

    /// @brief Set default timeout for requests.
    /// @param obj RestClient object.
    /// @param timeout_ms Timeout in milliseconds.
    void rt_restclient_set_timeout(void *obj, int64_t timeout_ms);

    //=============================================================================
    // HTTP Methods - Raw
    //=============================================================================

    /// @brief Perform GET request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @return HttpRes response object.
    void *rt_restclient_get(void *obj, rt_string path);

    /// @brief Perform POST request with body.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param body Request body as string.
    /// @return HttpRes response object.
    void *rt_restclient_post(void *obj, rt_string path, rt_string body);

    /// @brief Perform PUT request with body.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param body Request body as string.
    /// @return HttpRes response object.
    void *rt_restclient_put(void *obj, rt_string path, rt_string body);

    /// @brief Perform PATCH request with body.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param body Request body as string.
    /// @return HttpRes response object.
    void *rt_restclient_patch(void *obj, rt_string path, rt_string body);

    /// @brief Perform DELETE request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @return HttpRes response object.
    void *rt_restclient_delete(void *obj, rt_string path);

    /// @brief Perform HEAD request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @return HttpRes response object.
    void *rt_restclient_head(void *obj, rt_string path);

    //=============================================================================
    // HTTP Methods - JSON Convenience
    //=============================================================================

    /// @brief GET request, return parsed JSON.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @return Parsed JSON (Map or Seq), or null on error.
    /// @note Sets Accept: application/json header.
    void *rt_restclient_get_json(void *obj, rt_string path);

    /// @brief POST JSON request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param json_body JSON object (Map or Seq) to serialize.
    /// @return Parsed JSON response, or null on error.
    /// @note Sets Content-Type: application/json header.
    void *rt_restclient_post_json(void *obj, rt_string path, void *json_body);

    /// @brief PUT JSON request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param json_body JSON object (Map or Seq) to serialize.
    /// @return Parsed JSON response, or null on error.
    void *rt_restclient_put_json(void *obj, rt_string path, void *json_body);

    /// @brief PATCH JSON request.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @param json_body JSON object (Map or Seq) to serialize.
    /// @return Parsed JSON response, or null on error.
    void *rt_restclient_patch_json(void *obj, rt_string path, void *json_body);

    /// @brief DELETE with JSON response.
    /// @param obj RestClient object.
    /// @param path Path relative to base URL.
    /// @return Parsed JSON response, or null on error.
    void *rt_restclient_delete_json(void *obj, rt_string path);

    //=============================================================================
    // Error Handling
    //=============================================================================

    /// @brief Get last response status code.
    /// @param obj RestClient object.
    /// @return Last HTTP status code, or 0 if no request made.
    int64_t rt_restclient_last_status(void *obj);

    /// @brief Get last response object.
    /// @param obj RestClient object.
    /// @return Last HttpRes object, or NULL if no request made.
    void *rt_restclient_last_response(void *obj);

    /// @brief Check if last request was successful (2xx status).
    /// @param obj RestClient object.
    /// @return 1 if last status was 200-299, 0 otherwise.
    int8_t rt_restclient_last_ok(void *obj);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_RESTCLIENT_H
