//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_restclient.c
/// @brief REST API client implementation.
///
//===----------------------------------------------------------------------===//

#include "rt_restclient.h"
#include "rt_codec.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_network_http_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    rt_string base_url;
    void *headers; // Map of default headers
    int64_t timeout_ms;
    void *last_response; // Last HttpRes
    int64_t last_status;
    void *connection_pool;
    int64_t pool_size;
    int8_t keep_alive;
} rest_client;

//=============================================================================
// Finalizer
//=============================================================================

/// @brief GC finalizer: release the base URL string, headers map (which holds string K/V pairs),
/// and any cached HTTP response.
static void rest_client_finalize(void *obj) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    // Release base_url string
    if (client->base_url)
        rt_string_unref(client->base_url);
    // Release headers map
    if (client->headers && rt_obj_release_check0(client->headers))
        rt_obj_free(client->headers);
    // Release last response
    if (client->last_response && rt_obj_release_check0(client->last_response))
        rt_obj_free(client->last_response);
    if (client->connection_pool && rt_obj_release_check0(client->connection_pool))
        rt_obj_free(client->connection_pool);
}

//=============================================================================
// Helper Functions
//=============================================================================

/// @brief Concatenate a base URL and a path with a single `/` separator. Strips trailing slashes
/// from `base` and leading slashes from `path` so callers can mix-and-match conventions without
/// producing `//` artefacts. Always inserts exactly one separator.
static rt_string join_url(rt_string base, rt_string path) {
    const char *base_str = rt_string_cstr(base);
    const char *path_str = rt_string_cstr(path);

    if (!base_str)
        base_str = "";
    if (!path_str)
        path_str = "";

    size_t base_len = strlen(base_str);
    size_t path_len = strlen(path_str);

    // Remove trailing slash from base if present
    while (base_len > 0 && base_str[base_len - 1] == '/')
        base_len--;

    // Remove leading slash from path if present
    while (path_len > 0 && path_str[0] == '/') {
        path_str++;
        path_len--;
    }

    size_t total = base_len + 1 + path_len;
    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_status_t status = rt_sb_reserve(&sb, total + 1);
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&sb, base_str, base_len);
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&sb, "/", 1);
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&sb, path_str, path_len);
    if (status != RT_SB_OK) {
        rt_sb_free(&sb);
        rt_trap("RestClient: memory allocation failed");
    }

    rt_string out = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return out;
}

/// @brief Build an HttpReq pre-populated with the client's defaults: full URL = `base+path`,
/// every default header copied in, and the configured timeout applied. Returned req is owned by
/// the caller (will be released by `execute_request`).
static void *create_request(rest_client *client, rt_string method, rt_string path) {
    rt_string url = join_url(client->base_url, path);
    void *req = rt_http_req_new(method, url);
    rt_string_unref(url); // Release after use — req copies the C string

    // Apply default headers
    void *keys = rt_map_keys(client->headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(client->headers, key);
        if (val) {
            rt_http_req_set_header(req, key, (rt_string)val);
        }
    }
    // Release the keys Seq (its owns_elements finalizer releases the strings)
    if (rt_obj_release_check0(keys))
        rt_obj_free(keys);

    // Apply timeout
    if (client->timeout_ms > 0) {
        rt_http_req_set_timeout(req, client->timeout_ms);
    }
    rt_http_req_set_keep_alive(req, client->keep_alive);
    rt_http_req_set_connection_pool(req, client->keep_alive ? client->connection_pool : NULL);

    return req;
}

/// @brief Send the HttpReq, cache the resulting HttpRes on the client (releasing any prior cache),
/// and update `last_status` for ergonomic post-call checks. Releases `req` regardless of outcome.
/// Returns the HttpRes (a separate retained reference is stored in `client->last_response`).
static void *execute_request(rest_client *client, void *req) {
    void *res = rt_http_req_send(req);
    if (req && rt_obj_release_check0(req))
        rt_obj_free(req);

    // Release the previous response before taking ownership of the new one (RC-2 fix)
    if (client->last_response && rt_obj_release_check0(client->last_response))
        rt_obj_free(client->last_response);

    rt_obj_retain_maybe(res);
    client->last_response = res;
    client->last_status = rt_http_res_status(res);
    return res;
}

//=============================================================================
// Creation and Configuration
//=============================================================================

/// @brief Construct a REST client targeting `base_url`. Defaults: empty headers map, 30-second
/// timeout, no auth. The base URL is duplicated so the caller can release the original. Returns
/// a GC-managed handle wired to `rest_client_finalize`.
void *rt_restclient_new(rt_string base_url) {
    rest_client *client = (rest_client *)rt_obj_new_i64(0, (int64_t)sizeof(rest_client));
    memset(client, 0, sizeof(rest_client));

    const char *url_str = rt_string_cstr(base_url);
    client->base_url = url_str ? rt_string_from_bytes(url_str, strlen(url_str)) : rt_const_cstr("");
    client->headers = rt_map_new();
    client->timeout_ms = 30000; // 30 second default
    client->last_response = NULL;
    client->last_status = 0;
    client->pool_size = 8;
    client->keep_alive = 1;
    client->connection_pool = rt_http_conn_pool_new(client->pool_size);

    rt_obj_set_finalizer(client, rest_client_finalize);
    return client;
}

/// @brief Read the base URL the client is targeting (the same string passed to `_new`).
rt_string rt_restclient_base_url(void *obj) {
    if (!obj)
        return rt_const_cstr("");
    rest_client *client = (rest_client *)obj;
    return client->base_url;
}

/// @brief Set a default header sent on every subsequent request. Repeated calls overwrite. Pair
/// with `_del_header` to remove specific entries; map-managed lifetime handles release.
void rt_restclient_set_header(void *obj, rt_string name, rt_string value) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    rt_map_set(client->headers, name, (void *)value);
}

/// @brief Remove a default header so subsequent requests don't include it. No-op on missing keys.
void rt_restclient_del_header(void *obj, rt_string name) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    rt_map_remove(client->headers, name);
}

/// @brief Convenience: set the `Authorization: Bearer <token>` header. The token is appended
/// directly with no encoding (caller responsible for opaque-bearer-token formatting).
void rt_restclient_set_auth_bearer(void *obj, rt_string token) {
    if (!obj)
        return;

    const char *tok_str = rt_string_cstr(token);
    if (!tok_str)
        tok_str = "";

    size_t tok_len = strlen(tok_str);
    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_status_t status = rt_sb_append_cstr(&sb, "Bearer ");
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&sb, tok_str, tok_len);
    if (status != RT_SB_OK) {
        rt_sb_free(&sb);
        rt_trap("RestClient: memory allocation failed");
    }

    rt_string auth_str = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    rt_restclient_set_header(obj, rt_const_cstr("Authorization"), auth_str);
    rt_string_unref(auth_str);
}

/// @brief Convenience: set `Authorization: Basic <base64(user:pass)>`. Builds the credential
/// string `user:pass`, base64-encodes it via `rt_codec_base64_enc`, and prepends the `Basic `
/// scheme. All temporary buffers are freed before returning.
void rt_restclient_set_auth_basic(void *obj, rt_string username, rt_string password) {
    if (!obj)
        return;

    const char *user_str = rt_string_cstr(username);
    const char *pass_str = rt_string_cstr(password);
    if (!user_str)
        user_str = "";
    if (!pass_str)
        pass_str = "";

    size_t user_len = strlen(user_str);
    size_t pass_len = strlen(pass_str);
    rt_string_builder cred_sb;
    rt_sb_init(&cred_sb);

    rt_sb_status_t status = rt_sb_append_bytes(&cred_sb, user_str, user_len);
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&cred_sb, ":", 1);
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&cred_sb, pass_str, pass_len);
    if (status != RT_SB_OK) {
        rt_sb_free(&cred_sb);
        rt_trap("RestClient: memory allocation failed");
    }

    rt_string cred_str = rt_string_from_bytes(cred_sb.data, cred_sb.len);
    rt_sb_free(&cred_sb);
    rt_string encoded = rt_codec_base64_enc(cred_str);
    rt_string_unref(cred_str);

    const char *enc_str = rt_string_cstr(encoded);
    size_t enc_len = strlen(enc_str);
    rt_string_builder auth_sb;
    rt_sb_init(&auth_sb);

    status = rt_sb_append_cstr(&auth_sb, "Basic ");
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&auth_sb, enc_str, enc_len);
    if (status != RT_SB_OK) {
        rt_sb_free(&auth_sb);
        rt_string_unref(encoded);
        rt_trap("RestClient: memory allocation failed");
    }

    rt_string auth_str = rt_string_from_bytes(auth_sb.data, auth_sb.len);
    rt_sb_free(&auth_sb);
    rt_string_unref(encoded);
    rt_restclient_set_header(obj, rt_const_cstr("Authorization"), auth_str);
    rt_string_unref(auth_str);
}

/// @brief Remove the `Authorization` header (whether Bearer or Basic was set).
void rt_restclient_clear_auth(void *obj) {
    if (!obj)
        return;
    rt_restclient_del_header(obj, rt_const_cstr("Authorization"));
}

/// @brief Configure per-request timeout in milliseconds (default 30000). 0 disables the timeout.
void rt_restclient_set_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    client->timeout_ms = timeout_ms;
}

/// @brief Check whether this RestClient reuses keep-alive connections.
int8_t rt_restclient_get_keep_alive(void *obj) {
    if (!obj)
        return 0;
    return ((rest_client *)obj)->keep_alive;
}

/// @brief Enable or disable keep-alive connection reuse.
void rt_restclient_set_keep_alive(void *obj, int8_t keep_alive) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    client->keep_alive = keep_alive ? 1 : 0;
    if (!client->keep_alive && client->connection_pool)
        rt_http_conn_pool_clear(client->connection_pool);
    if (client->keep_alive && !client->connection_pool)
        client->connection_pool = rt_http_conn_pool_new(client->pool_size > 0 ? client->pool_size : 8);
}

/// @brief Resize the keep-alive pool. Existing idle connections are dropped.
void rt_restclient_set_pool_size(void *obj, int64_t max_size) {
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    if (max_size <= 0)
        max_size = 1;
    client->pool_size = max_size;

    void *new_pool = rt_http_conn_pool_new(max_size);
    void *old_pool = client->connection_pool;
    client->connection_pool = new_pool;
    if (old_pool && rt_obj_release_check0(old_pool))
        rt_obj_free(old_pool);
}

//=============================================================================
// HTTP Methods - Raw
//=============================================================================

/// @brief Send a `GET` to `base_url + path`. Returns the raw HttpRes for caller inspection.
void *rt_restclient_get(void *obj, rt_string path) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("GET"), path);
    return execute_request(client, req);
}

/// @brief Send a `POST` with a string body. Caller sets Content-Type via `_set_header` if needed.
void *rt_restclient_post(void *obj, rt_string path, rt_string body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("POST"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `PUT` with a string body.
void *rt_restclient_put(void *obj, rt_string path, rt_string body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PUT"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `PATCH` with a string body.
void *rt_restclient_patch(void *obj, rt_string path, rt_string body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PATCH"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `DELETE` to `base_url + path` (no body).
void *rt_restclient_delete(void *obj, rt_string path) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("DELETE"), path);
    return execute_request(client, req);
}

/// @brief Send a `HEAD` request — useful for checking resource existence/headers without body.
void *rt_restclient_head(void *obj, rt_string path) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("HEAD"), path);
    return execute_request(client, req);
}

//=============================================================================
// HTTP Methods - JSON Convenience
//=============================================================================

/// @brief Send a `GET` with `Accept: application/json` and parse the response body as JSON.
/// Returns the parsed JSON value, or NULL if the response is non-2xx (caller can re-inspect via
/// `_last_response`/`_last_status`). One-call convenience for typical REST-API consumers.
void *rt_restclient_get_json(void *obj, rt_string path) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("GET"), path);
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string body = rt_http_res_body_str(res);
    return rt_json_parse(body);
}

/// @brief Send a `POST` with `Content-Type: application/json` carrying `rt_json_format(json_body)`.
/// Sets `Accept: application/json` for response, parses the response body as JSON. NULL on
/// non-2xx OR empty response body.
void *rt_restclient_post_json(void *obj, rt_string path, void *json_body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("POST"), path);
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    const char *res_str = rt_string_cstr(res_body);
    if (!res_str || strlen(res_str) == 0)
        return NULL;

    return rt_json_parse(res_body);
}

/// @brief `PUT` JSON body and parse response. Same Accept/Content-Type handling as `_post_json`.
void *rt_restclient_put_json(void *obj, rt_string path, void *json_body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PUT"), path);
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    const char *res_str = rt_string_cstr(res_body);
    if (!res_str || strlen(res_str) == 0)
        return NULL;

    return rt_json_parse(res_body);
}

/// @brief `PATCH` JSON body and parse response. Used for partial-update REST endpoints.
void *rt_restclient_patch_json(void *obj, rt_string path, void *json_body) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PATCH"), path);
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    const char *res_str = rt_string_cstr(res_body);
    if (!res_str || strlen(res_str) == 0)
        return NULL;

    return rt_json_parse(res_body);
}

/// @brief `DELETE` (no body) and parse the response as JSON. Useful when the API returns a
/// confirmation envelope on successful deletion.
void *rt_restclient_delete_json(void *obj, rt_string path) {
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("DELETE"), path);
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    const char *res_str = rt_string_cstr(res_body);
    if (!res_str || strlen(res_str) == 0)
        return NULL;

    return rt_json_parse(res_body);
}

//=============================================================================
// Error Handling
//=============================================================================

/// @brief Read the HTTP status of the last response. Returns 0 if no request has been issued yet.
int64_t rt_restclient_last_status(void *obj) {
    if (!obj)
        return 0;
    rest_client *client = (rest_client *)obj;
    return client->last_status;
}

/// @brief Return a retained reference to the last HttpRes (NULL if none yet). Caller must
/// release. Use to inspect headers / raw body when a typed convenience method returned NULL.
void *rt_restclient_last_response(void *obj) {
    if (!obj)
        return NULL;
    rest_client *client = (rest_client *)obj;
    rt_obj_retain_maybe(client->last_response);
    return client->last_response;
}

/// @brief Returns 1 if the last status was 2xx (success). Convenience for the common
/// `if (!client.lastOk()) handle_error()` pattern.
int8_t rt_restclient_last_ok(void *obj) {
    if (!obj)
        return 0;
    rest_client *client = (rest_client *)obj;
    return (client->last_status >= 200 && client->last_status < 300) ? 1 : 0;
}
