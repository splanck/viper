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
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
    rt_string base_url;
    void *headers; // Map of default headers
    int64_t timeout_ms;
    void *last_response; // Last HttpRes
    int64_t last_status;
} rest_client;

//=============================================================================
// Helper Functions
//=============================================================================

static rt_string join_url(rt_string base, rt_string path)
{
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
    while (path_len > 0 && path_str[0] == '/')
    {
        path_str++;
        path_len--;
    }

    // Allocate combined string
    size_t total = base_len + 1 + path_len;
    char *result = (char *)malloc(total + 1);
    if (!result)
        rt_trap("RestClient: memory allocation failed");

    memcpy(result, base_str, base_len);
    result[base_len] = '/';
    memcpy(result + base_len + 1, path_str, path_len);
    result[total] = '\0';

    rt_string out = rt_string_from_bytes(result, total);
    free(result);
    return out;
}

static void *create_request(rest_client *client, rt_string method, rt_string path)
{
    rt_string url = join_url(client->base_url, path);
    void *req = rt_http_req_new(method, url);

    // Apply default headers
    void *keys = rt_map_keys(client->headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++)
    {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *val = rt_map_get(client->headers, key);
        if (val)
        {
            rt_http_req_set_header(req, key, (rt_string)val);
        }
    }

    // Apply timeout
    if (client->timeout_ms > 0)
    {
        rt_http_req_set_timeout(req, client->timeout_ms);
    }

    return req;
}

static void *execute_request(rest_client *client, void *req)
{
    void *res = rt_http_req_send(req);
    client->last_response = res;
    client->last_status = rt_http_res_status(res);
    return res;
}

//=============================================================================
// Creation and Configuration
//=============================================================================

void *rt_restclient_new(rt_string base_url)
{
    rest_client *client = (rest_client *)rt_obj_new_i64(0, (int64_t)sizeof(rest_client));
    memset(client, 0, sizeof(rest_client));

    const char *url_str = rt_string_cstr(base_url);
    client->base_url = url_str ? rt_string_from_bytes(url_str, strlen(url_str)) : rt_const_cstr("");
    client->headers = rt_map_new();
    client->timeout_ms = 30000; // 30 second default
    client->last_response = NULL;
    client->last_status = 0;

    return client;
}

rt_string rt_restclient_base_url(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    rest_client *client = (rest_client *)obj;
    return client->base_url;
}

void rt_restclient_set_header(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    rt_map_set(client->headers, name, (void *)value);
}

void rt_restclient_del_header(void *obj, rt_string name)
{
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    rt_map_remove(client->headers, name);
}

void rt_restclient_set_auth_bearer(void *obj, rt_string token)
{
    if (!obj)
        return;

    const char *tok_str = rt_string_cstr(token);
    if (!tok_str)
        tok_str = "";

    size_t tok_len = strlen(tok_str);
    size_t total = 7 + tok_len; // "Bearer " + token
    char *auth = (char *)malloc(total + 1);
    if (!auth)
        rt_trap("RestClient: memory allocation failed");

    memcpy(auth, "Bearer ", 7);
    memcpy(auth + 7, tok_str, tok_len);
    auth[total] = '\0';

    rt_string auth_str = rt_string_from_bytes(auth, total);
    free(auth);

    rt_restclient_set_header(obj, rt_const_cstr("Authorization"), auth_str);
}

void rt_restclient_set_auth_basic(void *obj, rt_string username, rt_string password)
{
    if (!obj)
        return;

    const char *user_str = rt_string_cstr(username);
    const char *pass_str = rt_string_cstr(password);
    if (!user_str)
        user_str = "";
    if (!pass_str)
        pass_str = "";

    // Create "username:password"
    size_t user_len = strlen(user_str);
    size_t pass_len = strlen(pass_str);
    size_t cred_len = user_len + 1 + pass_len;

    char *cred = (char *)malloc(cred_len + 1);
    if (!cred)
        rt_trap("RestClient: memory allocation failed");

    memcpy(cred, user_str, user_len);
    cred[user_len] = ':';
    memcpy(cred + user_len + 1, pass_str, pass_len);
    cred[cred_len] = '\0';

    // Base64 encode
    rt_string cred_str = rt_string_from_bytes(cred, cred_len);
    free(cred);
    rt_string encoded = rt_codec_base64_enc(cred_str);

    // Create "Basic <encoded>"
    const char *enc_str = rt_string_cstr(encoded);
    size_t enc_len = strlen(enc_str);
    size_t total = 6 + enc_len; // "Basic " + encoded

    char *auth = (char *)malloc(total + 1);
    if (!auth)
        rt_trap("RestClient: memory allocation failed");

    memcpy(auth, "Basic ", 6);
    memcpy(auth + 6, enc_str, enc_len);
    auth[total] = '\0';

    rt_string auth_str = rt_string_from_bytes(auth, total);
    free(auth);

    rt_restclient_set_header(obj, rt_const_cstr("Authorization"), auth_str);
}

void rt_restclient_clear_auth(void *obj)
{
    if (!obj)
        return;
    rt_restclient_del_header(obj, rt_const_cstr("Authorization"));
}

void rt_restclient_set_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        return;
    rest_client *client = (rest_client *)obj;
    client->timeout_ms = timeout_ms;
}

//=============================================================================
// HTTP Methods - Raw
//=============================================================================

void *rt_restclient_get(void *obj, rt_string path)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("GET"), path);
    return execute_request(client, req);
}

void *rt_restclient_post(void *obj, rt_string path, rt_string body)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("POST"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

void *rt_restclient_put(void *obj, rt_string path, rt_string body)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PUT"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

void *rt_restclient_patch(void *obj, rt_string path, rt_string body)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("PATCH"), path);
    rt_http_req_set_body_str(req, body);
    return execute_request(client, req);
}

void *rt_restclient_delete(void *obj, rt_string path)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("DELETE"), path);
    return execute_request(client, req);
}

void *rt_restclient_head(void *obj, rt_string path)
{
    if (!obj)
        rt_trap("RestClient: null client");
    rest_client *client = (rest_client *)obj;

    void *req = create_request(client, rt_const_cstr("HEAD"), path);
    return execute_request(client, req);
}

//=============================================================================
// HTTP Methods - JSON Convenience
//=============================================================================

void *rt_restclient_get_json(void *obj, rt_string path)
{
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

void *rt_restclient_post_json(void *obj, rt_string path, void *json_body)
{
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

void *rt_restclient_put_json(void *obj, rt_string path, void *json_body)
{
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

void *rt_restclient_patch_json(void *obj, rt_string path, void *json_body)
{
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

void *rt_restclient_delete_json(void *obj, rt_string path)
{
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

int64_t rt_restclient_last_status(void *obj)
{
    if (!obj)
        return 0;
    rest_client *client = (rest_client *)obj;
    return client->last_status;
}

void *rt_restclient_last_response(void *obj)
{
    if (!obj)
        return NULL;
    rest_client *client = (rest_client *)obj;
    return client->last_response;
}

int8_t rt_restclient_last_ok(void *obj)
{
    if (!obj)
        return 0;
    rest_client *client = (rest_client *)obj;
    return (client->last_status >= 200 && client->last_status < 300) ? 1 : 0;
}
