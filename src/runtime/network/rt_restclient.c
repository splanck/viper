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
#include "rt_result.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <limits.h>
#include <setjmp.h>
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

/// @brief Release a temporary runtime object after another owner has retained it.
/// @details Result wrappers retain the response payload; this helper drops the
///          raw response reference returned by `execute_request_result`.
/// @param obj Runtime object to release, or NULL.
static void rest_release_temp_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Return the current trap message as a Result-compatible string.
/// @details The returned string is a runtime constant and does not transfer
///          ownership to the caller.
/// @param fallback Message used when no trap text is active.
/// @return Runtime string for `rt_result_err_str`.
static rt_string rest_current_error_message(const char *fallback) {
    const char *err = rt_trap_get_error();
    if (!err || !err[0])
        err = fallback && fallback[0] ? fallback : "RestClient request failed";
    return rt_const_cstr(err);
}

/// @brief Clear receiver-scoped compatibility state after a failed request.
/// @param client RestClient receiver, or NULL.
static void rest_client_clear_last_response(rest_client *client) {
    if (!client)
        return;
    if (client->last_response && rt_obj_release_check0(client->last_response))
        rt_obj_free(client->last_response);
    client->last_response = NULL;
    client->last_status = 0;
}

/// @brief Wrap a RestClient response in `Result.Ok` or create `Result.ErrStr`.
/// @details HTTP status codes remain data on the response object. Only missing
///          transport responses become Err values.
/// @param response HttpRes object returned by the request path.
/// @param null_message Error text when @p response is NULL.
/// @return Opaque `Viper.Result`.
static void *rest_response_result(void *response, const char *null_message) {
    if (!response)
        return rt_result_err_str(
            rt_const_cstr(null_message ? null_message : "RestClient request failed"));
    void *result = rt_result_ok(response);
    rest_release_temp_object(response);
    return result;
}

/// @brief Validate and cast an opaque RestClient handle.
/// @details Public methods use this helper when a NULL receiver is a domain
///          error instead of a silent no-op. It traps with @p message and then
///          returns NULL so methods can safely stop local control flow when
///          trap recovery hooks return.
/// @param obj Opaque RestClient handle.
/// @param message Diagnostic used for the trap when @p obj is NULL.
/// @return Cast RestClient pointer, or NULL after trapping.
static rest_client *rest_client_checked(void *obj, const char *message) {
    if (!obj) {
        rt_trap(message);
        return NULL;
    }
    return (rest_client *)obj;
}

static int rest_timeout_ms_to_int(int64_t timeout_ms, int *out_timeout_ms) {
    if (timeout_ms < 0 || timeout_ms > INT_MAX)
        return 0;
    if (out_timeout_ms)
        *out_timeout_ms = (int)timeout_ms;
    return 1;
}

static const char *rest_string_bytes(rt_string text,
                                     size_t *len_out,
                                     const char *context,
                                     int reject_embedded_nul) {
    if (len_out)
        *len_out = 0;
    if (!text)
        return "";
    const char *cstr = rt_string_cstr(text);
    int64_t len64 = rt_str_len(text);
    if (!cstr || len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX) {
        rt_trap(context);
        return "";
    }
    size_t len = (size_t)len64;
    if (reject_embedded_nul && len > 0 && memchr(cstr, '\0', len)) {
        rt_trap(context);
        return "";
    }
    if (len_out)
        *len_out = len;
    return cstr;
}

static const char *rest_header_value_bytes(rt_string text, size_t *len_out, const char *context) {
    const char *cstr = rest_string_bytes(text, len_out, context, 1);
    size_t len = len_out ? *len_out : 0;
    if (len > 0 && (memchr(cstr, '\r', len) || memchr(cstr, '\n', len))) {
        rt_trap(context);
        if (len_out)
            *len_out = 0;
        return "";
    }
    return cstr;
}

static int rest_header_name_is_token(rt_string text) {
    if (!text)
        return 0;
    const char *cstr = rt_string_cstr(text);
    int64_t len64 = rt_str_len(text);
    if (!cstr || len64 <= 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX)
        return 0;
    size_t len = (size_t)len64;
    if (memchr(cstr, '\0', len))
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)cstr[i];
        if (ch <= 32 || ch >= 127 || strchr("()<>@,;:\\\"/[]?={}", ch))
            return 0;
    }
    return 1;
}

static int rest_header_value_is_safe(rt_string text) {
    if (!text)
        return 0;
    const char *cstr = rt_string_cstr(text);
    int64_t len64 = rt_str_len(text);
    if (!cstr || len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX)
        return 0;
    size_t len = (size_t)len64;
    return !memchr(cstr, '\0', len) && !memchr(cstr, '\r', len) && !memchr(cstr, '\n', len);
}

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
    size_t base_len = 0;
    size_t path_len = 0;
    const char *base_str = rest_string_bytes(base, &base_len, "RestClient: invalid base URL", 1);
    const char *path_str = rest_string_bytes(path, &path_len, "RestClient: invalid path", 1);

    // Remove trailing slash from base if present
    while (base_len > 0 && base_str[base_len - 1] == '/')
        base_len--;

    // Remove leading slash from path if present
    while (path_len > 0 && path_str[0] == '/') {
        path_str++;
        path_len--;
    }

    if (base_len > SIZE_MAX - path_len - 1) {
        rt_trap("RestClient: URL length overflow");
        return NULL;
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
        return NULL;
    }

    rt_string out = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    if (!out) {
        rt_trap("RestClient: memory allocation failed");
        return NULL;
    }
    return out;
}

/// @brief Build an HttpReq pre-populated with the client's defaults: full URL = `base+path`,
/// every default header copied in, and the configured timeout applied. Returned req is owned by
/// the caller (will be released by `execute_request`).
static void *create_request(rest_client *client, rt_string method, rt_string path) {
    if (!client) {
        rt_trap("RestClient: NULL client");
        return NULL;
    }
    rt_string url = join_url(client->base_url, path);
    if (!url)
        return NULL;
    void *req = rt_http_req_new(method, url);
    rt_string_unref(url); // Release after use — req copies the C string
    if (!req) {
        rt_trap("RestClient: request allocation failed");
        return NULL;
    }

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
    if (!client) {
        if (req && rt_obj_release_check0(req))
            rt_obj_free(req);
        rt_trap("RestClient: NULL client");
        return NULL;
    }
    if (!req) {
        rt_trap("RestClient: request allocation failed");
        return NULL;
    }
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

/// @brief Send an HttpReq and return `Result<HttpRes>`.
/// @details Converts request/transport traps into `Result.ErrStr`, releases
///          the request in all paths, and keeps the receiver-scoped
///          `LastResponse`/`LastStatus` compatibility state synchronized when
///          an HTTP response is received.
/// @param client RestClient receiver.
/// @param req Prepared HttpReq object; ownership is consumed.
/// @return Opaque `Viper.Result` containing `Ok(HttpRes)` or `Err(String)`.
static void *execute_request_result(rest_client *client, void *req) {
    if (!client) {
        rest_release_temp_object(req);
        return rt_result_err_str(rt_const_cstr("RestClient: NULL client"));
    }
    if (!req) {
        rest_client_clear_last_response(client);
        return rt_result_err_str(rt_const_cstr("RestClient: request allocation failed"));
    }

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    void *res = rt_http_req_send(req);
    rt_trap_clear_recovery();
    rest_release_temp_object(req);

    if (!res) {
        rest_client_clear_last_response(client);
        return rt_result_err_str(rt_const_cstr("RestClient request failed"));
    }

    if (client->last_response && rt_obj_release_check0(client->last_response))
        rt_obj_free(client->last_response);

    rt_obj_retain_maybe(res);
    client->last_response = res;
    client->last_status = rt_http_res_status(res);
    return rest_response_result(res, "RestClient request failed");
}

//=============================================================================
// Creation and Configuration
//=============================================================================

/// @brief Construct a REST client targeting `base_url`. Defaults: empty headers map, 30-second
/// timeout, no auth. The base URL is duplicated so the caller can release the original. Returns
/// a GC-managed handle wired to `rest_client_finalize`.
void *rt_restclient_new(rt_string base_url) {
    rest_client *client = (rest_client *)rt_obj_new_i64(0, (int64_t)sizeof(rest_client));
    if (!client) {
        rt_trap("RestClient: memory allocation failed");
        return NULL;
    }
    memset(client, 0, sizeof(rest_client));
    rt_obj_set_finalizer(client, rest_client_finalize);

    size_t url_len = 0;
    const char *url_str = rest_string_bytes(base_url, &url_len, "RestClient: invalid base URL", 1);
    client->base_url = rt_string_from_bytes(url_str, url_len);
    if (!client->base_url) {
        rt_trap("RestClient: memory allocation failed");
        if (rt_obj_release_check0(client))
            rt_obj_free(client);
        return NULL;
    }
    client->headers = rt_map_new();
    if (!client->headers) {
        rt_trap("RestClient: memory allocation failed");
        if (rt_obj_release_check0(client))
            rt_obj_free(client);
        return NULL;
    }
    client->timeout_ms = 30000; // 30 second default
    client->last_response = NULL;
    client->last_status = 0;
    client->pool_size = 8;
    client->keep_alive = 1;
    client->connection_pool = rt_http_conn_pool_new(client->pool_size);
    if (!client->connection_pool) {
        rt_trap("RestClient: memory allocation failed");
        if (rt_obj_release_check0(client))
            rt_obj_free(client);
        return NULL;
    }

    return client;
}

/// @brief Read the base URL the client is targeting (the same string passed to `_new`).
rt_string rt_restclient_base_url(void *obj) {
    if (!obj)
        return rt_const_cstr("");
    rest_client *client = (rest_client *)obj;
    return client->base_url;
}

/// @brief Set a default header sent on every subsequent request. Repeated calls overwrite,
/// including differently cased spellings of the same name (HTTP field names are
/// case-insensitive). Pair with `_del_header` to remove specific entries; map-managed
/// lifetime handles release.
void rt_restclient_set_header(void *obj, rt_string name, rt_string value) {
    if (!obj)
        return;
    if (!rest_header_name_is_token(name) || !rest_header_value_is_safe(value))
        return;
    rest_client *client = (rest_client *)obj;
    rt_http_header_map_set_ci(client->headers, name, (void *)value);
}

/// @brief Remove a default header (any case-insensitive spelling) so subsequent requests
/// don't include it. No-op on missing keys.
void rt_restclient_del_header(void *obj, rt_string name) {
    if (!obj)
        return;
    if (!rest_header_name_is_token(name))
        return;
    rest_client *client = (rest_client *)obj;
    rt_http_header_map_remove_ci(client->headers, rt_string_cstr(name));
}

/// @brief Convenience: set the `Authorization: Bearer <token>` header. The token is appended
/// directly with no encoding (caller responsible for opaque-bearer-token formatting).
void rt_restclient_set_auth_bearer(void *obj, rt_string token) {
    if (!obj)
        return;

    size_t tok_len = 0;
    const char *tok_str =
        rest_header_value_bytes(token, &tok_len, "RestClient: invalid bearer token");
    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_status_t status = rt_sb_append_cstr(&sb, "Bearer ");
    if (status == RT_SB_OK)
        status = rt_sb_append_bytes(&sb, tok_str, tok_len);
    if (status != RT_SB_OK) {
        rt_sb_free(&sb);
        rt_trap("RestClient: memory allocation failed");
        return;
    }

    rt_string auth_str = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    if (!auth_str) {
        rt_trap("RestClient: memory allocation failed");
        return;
    }
    rt_restclient_set_header(obj, rt_const_cstr("Authorization"), auth_str);
    rt_string_unref(auth_str);
}

/// @brief Convenience: set `Authorization: Basic <base64(user:pass)>`. Builds the credential
/// string `user:pass`, base64-encodes it via `rt_codec_base64_enc`, and prepends the `Basic `
/// scheme. All temporary buffers are freed before returning.
void rt_restclient_set_auth_basic(void *obj, rt_string username, rt_string password) {
    if (!obj)
        return;

    size_t user_len = 0;
    size_t pass_len = 0;
    const char *user_str =
        rest_header_value_bytes(username, &user_len, "RestClient: invalid username");
    const char *pass_str =
        rest_header_value_bytes(password, &pass_len, "RestClient: invalid password");
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
        return;
    }

    rt_string cred_str = rt_string_from_bytes(cred_sb.data, cred_sb.len);
    rt_sb_free(&cred_sb);
    if (!cred_str) {
        rt_trap("RestClient: memory allocation failed");
        return;
    }
    rt_string encoded = rt_codec_base64_enc(cred_str);
    rt_string_unref(cred_str);
    if (!encoded) {
        rt_trap("RestClient: memory allocation failed");
        return;
    }

    const char *enc_str = rt_string_cstr(encoded);
    if (!enc_str) {
        rt_string_unref(encoded);
        rt_trap("RestClient: memory allocation failed");
        return;
    }
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
        return;
    }

    rt_string auth_str = rt_string_from_bytes(auth_sb.data, auth_sb.len);
    rt_sb_free(&auth_sb);
    rt_string_unref(encoded);
    if (!auth_str) {
        rt_trap("RestClient: memory allocation failed");
        return;
    }
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
    int timeout_int = 0;
    if (!rest_timeout_ms_to_int(timeout_ms, &timeout_int)) {
        rt_trap("RestClient: invalid timeout");
        return;
    }
    rest_client *client = (rest_client *)obj;
    client->timeout_ms = timeout_int;
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
        client->connection_pool =
            rt_http_conn_pool_new(client->pool_size > 0 ? client->pool_size : 8);
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
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("GET"), path);
    return execute_request(client, req);
}

/// @brief Send a `GET` to `base_url + path` and return `Result<HttpRes>`.
void *rt_restclient_get_result(void *obj, rt_string path) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("GET"), path);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

/// @brief Send a `POST` with a string body. Caller sets Content-Type via `_set_header` if needed.
void *rt_restclient_post(void *obj, rt_string path, rt_string body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("POST"), path);
    if (!req)
        return NULL;
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `POST` with a string body and return `Result<HttpRes>`.
void *rt_restclient_post_result(void *obj, rt_string path, rt_string body) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("POST"), path);
    rt_http_req_set_body_str(req, body);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

/// @brief Send a `PUT` with a string body.
void *rt_restclient_put(void *obj, rt_string path, rt_string body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("PUT"), path);
    if (!req)
        return NULL;
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `PUT` with a string body and return `Result<HttpRes>`.
void *rt_restclient_put_result(void *obj, rt_string path, rt_string body) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("PUT"), path);
    rt_http_req_set_body_str(req, body);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

/// @brief Send a `PATCH` with a string body.
void *rt_restclient_patch(void *obj, rt_string path, rt_string body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("PATCH"), path);
    if (!req)
        return NULL;
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

/// @brief Send a `PATCH` with a string body and return `Result<HttpRes>`.
void *rt_restclient_patch_result(void *obj, rt_string path, rt_string body) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("PATCH"), path);
    rt_http_req_set_body_str(req, body);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

/// @brief Send a `DELETE` to `base_url + path` (no body).
void *rt_restclient_delete(void *obj, rt_string path) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("DELETE"), path);
    return execute_request(client, req);
}

/// @brief Send a `DELETE` to `base_url + path` and return `Result<HttpRes>`.
void *rt_restclient_delete_result(void *obj, rt_string path) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("DELETE"), path);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

/// @brief Send a `HEAD` request — useful for checking resource existence/headers without body.
void *rt_restclient_head(void *obj, rt_string path) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("HEAD"), path);
    return execute_request(client, req);
}

/// @brief Send a `HEAD` request and return `Result<HttpRes>`.
void *rt_restclient_head_result(void *obj, rt_string path) {
    if (!obj)
        return rt_result_err_str(rt_const_cstr("RestClient: null client"));
    rest_client *client = (rest_client *)obj;

    void *req = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = rest_current_error_message("RestClient request setup failed");
        rt_trap_clear_recovery();
        rest_release_temp_object(req);
        rest_client_clear_last_response(client);
        return rt_result_err_str(message);
    }
    req = create_request(client, rt_const_cstr("HEAD"), path);
    rt_trap_clear_recovery();
    return execute_request_result(client, req);
}

//=============================================================================
// HTTP Methods - JSON Convenience
//=============================================================================

/// @brief Send a `GET` with `Accept: application/json` and parse the response body as JSON.
/// Returns the parsed JSON value, or NULL if the response is non-2xx (caller can re-inspect via
/// `_last_response`/`_last_status`). One-call convenience for typical REST-API consumers.
void *rt_restclient_get_json(void *obj, rt_string path) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("GET"), path);
    if (!req)
        return NULL;
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string body = rt_http_res_body_str(res);
    void *parsed = rt_json_parse(body);
    rt_string_unref(body);
    return parsed;
}

/// @brief Send a `POST` with `Content-Type: application/json` carrying `rt_json_format(json_body)`.
/// Sets `Accept: application/json` for response, parses the response body as JSON. NULL on
/// non-2xx OR empty response body.
void *rt_restclient_post_json(void *obj, rt_string path, void *json_body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("POST"), path);
    if (!req)
        return NULL;
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);
    rt_string_unref(body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    if (rt_str_len(res_body) == 0) {
        rt_string_unref(res_body);
        return NULL;
    }

    void *parsed = rt_json_parse(res_body);
    rt_string_unref(res_body);
    return parsed;
}

/// @brief `PUT` JSON body and parse response. Same Accept/Content-Type handling as `_post_json`.
void *rt_restclient_put_json(void *obj, rt_string path, void *json_body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("PUT"), path);
    if (!req)
        return NULL;
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);
    rt_string_unref(body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    if (rt_str_len(res_body) == 0) {
        rt_string_unref(res_body);
        return NULL;
    }

    void *parsed = rt_json_parse(res_body);
    rt_string_unref(res_body);
    return parsed;
}

/// @brief `PATCH` JSON body and parse response. Used for partial-update REST endpoints.
void *rt_restclient_patch_json(void *obj, rt_string path, void *json_body) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("PATCH"), path);
    if (!req)
        return NULL;
    rt_http_req_set_header(req, rt_const_cstr("Content-Type"), rt_const_cstr("application/json"));
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    rt_string body = rt_json_format(json_body);
    rt_http_req_set_body_str(req, body);
    rt_string_unref(body);

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    if (rt_str_len(res_body) == 0) {
        rt_string_unref(res_body);
        return NULL;
    }

    void *parsed = rt_json_parse(res_body);
    rt_string_unref(res_body);
    return parsed;
}

/// @brief `DELETE` (no body) and parse the response as JSON. Useful when the API returns a
/// confirmation envelope on successful deletion.
void *rt_restclient_delete_json(void *obj, rt_string path) {
    rest_client *client = rest_client_checked(obj, "RestClient: null client");
    if (!client)
        return NULL;

    void *req = create_request(client, rt_const_cstr("DELETE"), path);
    if (!req)
        return NULL;
    rt_http_req_set_header(req, rt_const_cstr("Accept"), rt_const_cstr("application/json"));

    void *res = execute_request(client, req);
    if (!rt_http_res_is_ok(res))
        return NULL;

    rt_string res_body = rt_http_res_body_str(res);
    if (rt_str_len(res_body) == 0) {
        rt_string_unref(res_body);
        return NULL;
    }

    void *parsed = rt_json_parse(res_body);
    rt_string_unref(res_body);
    return parsed;
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
