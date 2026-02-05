//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Viper.Network.RestClient
//
//===----------------------------------------------------------------------===//

#include "rt_restclient.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static void test_result(bool cond, const char *name)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

//=============================================================================
// Creation Tests
//=============================================================================

static void test_new_client()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    test_result(client != NULL, "new_client: should create client");

    rt_string base = rt_restclient_base_url(client);
    test_result(strcmp(rt_string_cstr(base), "https://api.example.com") == 0,
                "new_client: should store base URL");
}

static void test_new_client_empty_url()
{
    void *client = rt_restclient_new(rt_const_cstr(""));

    test_result(client != NULL, "new_client_empty: should create client with empty URL");

    rt_string base = rt_restclient_base_url(client);
    test_result(strlen(rt_string_cstr(base)) == 0,
                "new_client_empty: should have empty base URL");
}

static void test_new_client_null()
{
    rt_string base = rt_restclient_base_url(NULL);
    test_result(strlen(rt_string_cstr(base)) == 0,
                "null_client: should return empty string");
}

//=============================================================================
// Header Configuration Tests
//=============================================================================

static void test_set_header()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting a header shouldn't crash
    rt_restclient_set_header(client,
                             rt_const_cstr("X-Custom-Header"),
                             rt_const_cstr("CustomValue"));

    test_result(true, "set_header: should set header without crash");
}

static void test_del_header()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_header(client,
                             rt_const_cstr("X-Custom-Header"),
                             rt_const_cstr("CustomValue"));

    // Deleting a header shouldn't crash
    rt_restclient_del_header(client, rt_const_cstr("X-Custom-Header"));

    test_result(true, "del_header: should delete header without crash");
}

static void test_null_client_headers()
{
    // Operations on NULL client should be safe (no-op)
    rt_restclient_set_header(NULL,
                             rt_const_cstr("Header"),
                             rt_const_cstr("Value"));
    rt_restclient_del_header(NULL, rt_const_cstr("Header"));

    test_result(true, "null_client_headers: should handle NULL safely");
}

//=============================================================================
// Authentication Tests
//=============================================================================

static void test_set_auth_bearer()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting bearer auth shouldn't crash
    rt_restclient_set_auth_bearer(client, rt_const_cstr("my-token-12345"));

    test_result(true, "set_auth_bearer: should set bearer auth without crash");
}

static void test_set_auth_basic()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    // Setting basic auth shouldn't crash
    rt_restclient_set_auth_basic(client,
                                 rt_const_cstr("username"),
                                 rt_const_cstr("password"));

    test_result(true, "set_auth_basic: should set basic auth without crash");
}

static void test_clear_auth()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_auth_bearer(client, rt_const_cstr("token"));
    rt_restclient_clear_auth(client);

    test_result(true, "clear_auth: should clear auth without crash");
}

static void test_null_client_auth()
{
    // Auth operations on NULL client should be safe
    rt_restclient_set_auth_bearer(NULL, rt_const_cstr("token"));
    rt_restclient_set_auth_basic(NULL,
                                 rt_const_cstr("user"),
                                 rt_const_cstr("pass"));
    rt_restclient_clear_auth(NULL);

    test_result(true, "null_client_auth: should handle NULL safely");
}

//=============================================================================
// Timeout Tests
//=============================================================================

static void test_set_timeout()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    rt_restclient_set_timeout(client, 60000); // 60 seconds

    test_result(true, "set_timeout: should set timeout without crash");
}

static void test_set_timeout_null()
{
    rt_restclient_set_timeout(NULL, 5000);

    test_result(true, "set_timeout_null: should handle NULL safely");
}

//=============================================================================
// Status Tests (without actual HTTP)
//=============================================================================

static void test_last_status_initial()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    int64_t status = rt_restclient_last_status(client);
    test_result(status == 0, "last_status_initial: should be 0 initially");
}

static void test_last_response_initial()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    void *response = rt_restclient_last_response(client);
    test_result(response == NULL, "last_response_initial: should be NULL initially");
}

static void test_last_ok_initial()
{
    void *client = rt_restclient_new(rt_const_cstr("https://api.example.com"));

    int8_t ok = rt_restclient_last_ok(client);
    test_result(ok == 0, "last_ok_initial: should be false initially");
}

static void test_last_status_null()
{
    int64_t status = rt_restclient_last_status(NULL);
    test_result(status == 0, "last_status_null: should return 0 for NULL");
}

static void test_last_response_null()
{
    void *response = rt_restclient_last_response(NULL);
    test_result(response == NULL, "last_response_null: should return NULL for NULL");
}

static void test_last_ok_null()
{
    int8_t ok = rt_restclient_last_ok(NULL);
    test_result(ok == 0, "last_ok_null: should return false for NULL");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    // Creation tests
    test_new_client();
    test_new_client_empty_url();
    test_new_client_null();

    // Header tests
    test_set_header();
    test_del_header();
    test_null_client_headers();

    // Auth tests
    test_set_auth_bearer();
    test_set_auth_basic();
    test_clear_auth();
    test_null_client_auth();

    // Timeout tests
    test_set_timeout();
    test_set_timeout_null();

    // Status tests
    test_last_status_initial();
    test_last_response_initial();
    test_last_ok_initial();
    test_last_status_null();
    test_last_response_null();
    test_last_ok_null();

    printf("All RestClient tests passed!\n");
    return 0;
}
