//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/network/rt_restclient.h
// Purpose: REST API client with session management, persistent headers, base URL support, JSON
// convenience methods, and configurable timeouts.
//
// Key invariants:
//   - Persistent headers (e.g., Authorization) are sent with every request.
//   - Base URL and request path are joined by slash normalization, not RFC reference resolution.
//   - JSON helper methods append Content-Type/Accept application/json fields.
//   - Timeout is reused for connection attempts and socket-I/O phases.
//
// Ownership/Lifetime:
//   - RestClient objects and returned response/string handles are runtime managed.
//
// Links: src/runtime/network/rt_restclient.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
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

/// @brief True if the client reuses keep-alive connections.
int8_t rt_restclient_get_keep_alive(void *obj);

/// @brief Enable or disable keep-alive connection reuse.
void rt_restclient_set_keep_alive(void *obj, int8_t keep_alive);

/// @brief Resize the internal keep-alive connection pool.
void rt_restclient_set_pool_size(void *obj, int64_t max_size);

//=============================================================================
// HTTP Methods - Raw
//=============================================================================

/// @brief Perform GET request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return HttpRes response object.
void *rt_restclient_get(void *obj, rt_string path);

/// @brief Perform GET request and return a Result-wrapped HttpRes.
/// @details Transport/setup failures become `Result.ErrStr`; any received HTTP
///          response, including 4xx/5xx, is returned as `Result.Ok(HttpRes)`.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_get_result(void *obj, rt_string path);

/// @brief Perform POST request with body.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return HttpRes response object.
void *rt_restclient_post(void *obj, rt_string path, rt_string body);

/// @brief Perform POST request with body and return a Result-wrapped HttpRes.
/// @details Transport/setup failures become `Result.ErrStr`; HTTP status stays
///          on the response object for explicit caller handling.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_post_result(void *obj, rt_string path, rt_string body);

/// @brief Perform PUT request with body.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return HttpRes response object.
void *rt_restclient_put(void *obj, rt_string path, rt_string body);

/// @brief Perform PUT request with body and return a Result-wrapped HttpRes.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_put_result(void *obj, rt_string path, rt_string body);

/// @brief Perform PATCH request with body.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return HttpRes response object.
void *rt_restclient_patch(void *obj, rt_string path, rt_string body);

/// @brief Perform PATCH request with body and return a Result-wrapped HttpRes.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param body Request body as string.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_patch_result(void *obj, rt_string path, rt_string body);

/// @brief Perform DELETE request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return HttpRes response object.
void *rt_restclient_delete(void *obj, rt_string path);

/// @brief Perform DELETE request and return a Result-wrapped HttpRes.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_delete_result(void *obj, rt_string path);

/// @brief Perform HEAD request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return HttpRes response object.
void *rt_restclient_head(void *obj, rt_string path);

/// @brief Perform HEAD request and return a Result-wrapped HttpRes.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return Opaque `Zanna.Result` containing `Ok(HttpRes)` or `Err(String)`.
void *rt_restclient_head_result(void *obj, rt_string path);

//=============================================================================
// HTTP Methods - JSON Convenience
//=============================================================================

/// @brief GET request, return parsed JSON.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return Any parsed JSON value, or null for a non-2xx response.
/// @note Sets Accept: application/json header.
void *rt_restclient_get_json(void *obj, rt_string path);

/// @brief POST JSON request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param json_body JSON object (Map or Seq) to serialize.
/// @return Parsed JSON response, or null for non-2xx/empty response.
/// @note Sets Content-Type: application/json header.
void *rt_restclient_post_json(void *obj, rt_string path, void *json_body);

/// @brief PUT JSON request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param json_body JSON object (Map or Seq) to serialize.
/// @return Parsed JSON response, or null for non-2xx/empty response.
void *rt_restclient_put_json(void *obj, rt_string path, void *json_body);

/// @brief PATCH JSON request.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @param json_body JSON object (Map or Seq) to serialize.
/// @return Parsed JSON response, or null for non-2xx/empty response.
void *rt_restclient_patch_json(void *obj, rt_string path, void *json_body);

/// @brief DELETE with JSON response.
/// @param obj RestClient object.
/// @param path Path relative to base URL.
/// @return Parsed JSON response, or null for non-2xx/empty response.
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
