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
//   - Mutable defaults, pool configuration, and last-response state are mutex protected.
//
// Ownership/Lifetime:
//   - RestClient objects and returned response/string handles are runtime managed.
//   - BaseUrl and LastResponse return independent caller-owned references.
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

/// @brief Stable managed-object class identity for RestClient handles.
/// @details Every non-null public receiver is checked against this tag and the
///          complete private payload size before mutable headers, cached
///          responses, connection pools, or native synchronization state are
///          accessed.
#define RT_RESTCLIENT_CLASS_ID INT64_C(-0x72020E)

//=============================================================================
// RestClient Creation and Configuration
//=============================================================================

/// @brief Create a synchronized REST client with a copied base URL.
/// @details Construction publishes the object only after its mutex, default
///          header Map, and private keep-alive pool are initialized. NULL keeps
///          the historical empty-base behavior; invalid or embedded-NUL String
///          handles trap. Partial construction is released on every failure.
/// @param base_url Base URL for all requests (e.g., "https://api.example.com").
/// @return Caller-owned RestClient object, or NULL after a returning trap hook.
void *rt_restclient_new(rt_string base_url);

/// @brief Get an independent copy of the configured base URL.
/// @param obj RestClient object.
/// @return Caller-owned base URL String; NULL receivers return empty String.
/// @note A non-NULL wrong-class receiver traps before payload access.
rt_string rt_restclient_base_url(void *obj);

/// @brief Transactionally set a default header for all later requests.
/// @details Replacement is case-insensitive. The complete key snapshot and new
///          Map entry are allocated before differently cased aliases are
///          removed, so a caught allocation trap preserves the prior value.
/// @param obj RestClient object.
/// @param name Header name.
/// @param value Header value.
void rt_restclient_set_header(void *obj, rt_string name, rt_string value);

/// @brief Remove every case-insensitive spelling of a default header.
/// @param obj RestClient object.
/// @param name Header name.
void rt_restclient_del_header(void *obj, rt_string name);

/// @brief Transactionally set `Authorization: Bearer <token>`.
/// @details Native and managed staging is released on validation, allocation,
///          or Map-update failure; the previous Authorization value is retained.
/// @param obj RestClient object.
/// @param token Bearer token.
void rt_restclient_set_auth_bearer(void *obj, rt_string token);

/// @brief Transactionally set base64-encoded Basic authentication.
/// @details Username/password are copied as `user:password`, encoded, prefixed,
///          and published only after every intermediate allocation succeeds.
/// @param obj RestClient object.
/// @param username Username.
/// @param password Password.
void rt_restclient_set_auth_basic(void *obj, rt_string username, rt_string password);

/// @brief Clear authentication.
/// @param obj RestClient object.
void rt_restclient_clear_auth(void *obj);

/// @brief Set the synchronized default timeout for subsequent requests.
/// @param obj RestClient object.
/// @param timeout_ms Milliseconds in the inclusive range 0..INT_MAX; zero
///        disables address/socket operation deadlines.
void rt_restclient_set_timeout(void *obj, int64_t timeout_ms);

/// @brief Read whether the client reuses keep-alive connections.
/// @param obj RestClient receiver; NULL returns zero.
/// @return One when enabled, otherwise zero.
int8_t rt_restclient_get_keep_alive(void *obj);

/// @brief Enable or disable keep-alive connection reuse transactionally.
/// @details Enabling allocates a replacement pool before changing state and
///          rechecks the configured size before publication; disabling
///          atomically detaches and then clears the old pool.
/// @param obj RestClient receiver; NULL is a no-op.
/// @param keep_alive Nonzero to enable reuse.
void rt_restclient_set_keep_alive(void *obj, int8_t keep_alive);

/// @brief Replace the internal keep-alive pool with the requested capacity.
/// @details Allocation completes before the synchronized exchange, so failure
///          preserves both the previous pool and configured size. When reuse
///          is disabled only the future size changes and no idle pool remains.
/// @param obj RestClient receiver; NULL is a no-op.
/// @param max_size Requested capacity; non-positive values clamp to one and the
///        native HTTP pool applies its documented upper bound.
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
/// @note NULL and wrong-class receivers produce `Err(String)` without native
///       payload access. Result-construction OOM releases all partial values.
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

/// @brief Get a retained snapshot of the last response object.
/// @param obj RestClient object.
/// @return Caller-owned HttpRes reference, or NULL if no response was received.
void *rt_restclient_last_response(void *obj);

/// @brief Check if last request was successful (2xx status).
/// @param obj RestClient object.
/// @return 1 if last status was 200-299, 0 otherwise.
int8_t rt_restclient_last_ok(void *obj);

#ifdef __cplusplus
}
#endif
