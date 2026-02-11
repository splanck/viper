//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTJsonStreamTests.cpp
// Purpose: Tests for Viper.Text.JsonStream SAX-style streaming JSON parser.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_json_stream.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// Basic token tests
// ============================================================================

static void test_empty_object()
{
    void *p = rt_json_stream_new(make_str("{}"));
    assert(p != nullptr);

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_START);
    assert(rt_json_stream_depth(p) == 1);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_END);
    assert(rt_json_stream_depth(p) == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_END);

    printf("test_empty_object: PASSED\n");
}

static void test_empty_array()
{
    void *p = rt_json_stream_new(make_str("[]"));

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_START);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_END);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_END);

    printf("test_empty_array: PASSED\n");
}

static void test_string_value()
{
    void *p = rt_json_stream_new(make_str("{\"name\": \"Alice\"}"));

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_START);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_KEY);
    rt_string key = rt_json_stream_string_value(p);
    assert(strcmp(rt_string_cstr(key), "name") == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_STRING);
    rt_string val = rt_json_stream_string_value(p);
    assert(strcmp(rt_string_cstr(val), "Alice") == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_END);

    printf("test_string_value: PASSED\n");
}

static void test_number_value()
{
    void *p = rt_json_stream_new(make_str("[42, 3.14, -7, 1e3]"));

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_START);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 42.0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(fabs(rt_json_stream_number_value(p) - 3.14) < 1e-9);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == -7.0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 1000.0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_END);

    printf("test_number_value: PASSED\n");
}

static void test_bool_value()
{
    void *p = rt_json_stream_new(make_str("[true, false]"));

    rt_json_stream_next(p); /* [ */

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_BOOL);
    assert(rt_json_stream_bool_value(p) == 1);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_BOOL);
    assert(rt_json_stream_bool_value(p) == 0);

    printf("test_bool_value: PASSED\n");
}

static void test_null_value()
{
    void *p = rt_json_stream_new(make_str("[null]"));

    rt_json_stream_next(p); /* [ */

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NULL);

    printf("test_null_value: PASSED\n");
}

static void test_nested_object()
{
    void *p = rt_json_stream_new(make_str("{\"a\": {\"b\": 1}}"));

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_START);
    assert(rt_json_stream_depth(p) == 1);

    tok = rt_json_stream_next(p); /* key "a" */
    assert(tok == RT_JSON_TOK_KEY);

    tok = rt_json_stream_next(p); /* { inner */
    assert(tok == RT_JSON_TOK_OBJECT_START);
    assert(rt_json_stream_depth(p) == 2);

    tok = rt_json_stream_next(p); /* key "b" */
    assert(tok == RT_JSON_TOK_KEY);

    tok = rt_json_stream_next(p); /* 1 */
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 1.0);

    tok = rt_json_stream_next(p); /* } inner */
    assert(tok == RT_JSON_TOK_OBJECT_END);
    assert(rt_json_stream_depth(p) == 1);

    tok = rt_json_stream_next(p); /* } outer */
    assert(tok == RT_JSON_TOK_OBJECT_END);
    assert(rt_json_stream_depth(p) == 0);

    printf("test_nested_object: PASSED\n");
}

static void test_array_of_objects()
{
    void *p = rt_json_stream_new(make_str("[{\"x\":1},{\"x\":2}]"));

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_START);

    /* First object */
    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_START);
    tok = rt_json_stream_next(p); /* key "x" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* 1 */
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 1.0);
    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_END);

    /* Second object */
    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_START);
    tok = rt_json_stream_next(p); /* key "x" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* 2 */
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 2.0);
    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_OBJECT_END);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ARRAY_END);

    printf("test_array_of_objects: PASSED\n");
}

// ============================================================================
// Escape handling
// ============================================================================

static void test_escape_sequences()
{
    void *p =
        rt_json_stream_new(make_str("[\"line1\\nline2\", \"tab\\there\", \"quote\\\"inside\"]"));

    rt_json_stream_next(p); /* [ */

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_STRING);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "line1\nline2") == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_STRING);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "tab\there") == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_STRING);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "quote\"inside") == 0);

    printf("test_escape_sequences: PASSED\n");
}

static void test_unicode_escape()
{
    /* \u0041 = 'A' */
    void *p = rt_json_stream_new(make_str("[\"\\u0041\"]"));

    rt_json_stream_next(p); /* [ */
    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_STRING);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "A") == 0);

    printf("test_unicode_escape: PASSED\n");
}

// ============================================================================
// Skip functionality
// ============================================================================

static void test_skip_object()
{
    void *p = rt_json_stream_new(make_str("[{\"a\":1,\"b\":{\"c\":2}}, 99]"));

    rt_json_stream_next(p); /* [ */
    rt_json_stream_next(p); /* { */
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_OBJECT_START);

    rt_json_stream_skip(p);
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_OBJECT_END);
    assert(rt_json_stream_depth(p) == 1);

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 99.0);

    printf("test_skip_object: PASSED\n");
}

static void test_skip_array()
{
    void *p = rt_json_stream_new(make_str("{\"data\":[1,2,3],\"done\":true}"));

    rt_json_stream_next(p); /* { */
    rt_json_stream_next(p); /* key "data" */
    rt_json_stream_next(p); /* [ */
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_ARRAY_START);

    rt_json_stream_skip(p);
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_ARRAY_END);

    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_KEY);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "done") == 0);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_BOOL);
    assert(rt_json_stream_bool_value(p) == 1);

    printf("test_skip_array: PASSED\n");
}

// ============================================================================
// has_next / token_type
// ============================================================================

static void test_has_next()
{
    void *p = rt_json_stream_new(make_str("42"));

    assert(rt_json_stream_has_next(p) == 1);

    rt_json_stream_next(p);
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_NUMBER);

    assert(rt_json_stream_has_next(p) == 0);

    printf("test_has_next: PASSED\n");
}

static void test_token_type_none()
{
    void *p = rt_json_stream_new(make_str("{}"));
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_NONE);

    printf("test_token_type_none: PASSED\n");
}

// ============================================================================
// Error handling
// ============================================================================

static void test_invalid_json()
{
    void *p = rt_json_stream_new(make_str("{invalid}"));

    rt_json_stream_next(p); /* { */
    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ERROR);

    rt_string err = rt_json_stream_error(p);
    assert(strlen(rt_string_cstr(err)) > 0);

    printf("test_invalid_json: PASSED\n");
}

static void test_unterminated_string()
{
    void *p = rt_json_stream_new(make_str("[\"no closing quote]"));

    rt_json_stream_next(p); /* [ */
    int64_t tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_ERROR);

    printf("test_unterminated_string: PASSED\n");
}

// ============================================================================
// NULL safety
// ============================================================================

static void test_null_parser()
{
    assert(rt_json_stream_next(NULL) == RT_JSON_TOK_ERROR);
    assert(rt_json_stream_token_type(NULL) == RT_JSON_TOK_ERROR);
    assert(rt_json_stream_number_value(NULL) == 0.0);
    assert(rt_json_stream_bool_value(NULL) == 0);
    assert(rt_json_stream_depth(NULL) == 0);
    assert(rt_json_stream_has_next(NULL) == 0);

    printf("test_null_parser: PASSED\n");
}

// ============================================================================
// Complex JSON
// ============================================================================

static void test_complex_json()
{
    const char *json = "{"
                       "  \"users\": ["
                       "    {\"name\": \"Alice\", \"age\": 30, \"active\": true},"
                       "    {\"name\": \"Bob\", \"age\": 25, \"active\": false}"
                       "  ],"
                       "  \"count\": 2,"
                       "  \"meta\": null"
                       "}";

    void *p = rt_json_stream_new(make_str(json));

    int64_t tok = rt_json_stream_next(p); /* { outer */
    assert(tok == RT_JSON_TOK_OBJECT_START);

    tok = rt_json_stream_next(p); /* key "users" */
    assert(tok == RT_JSON_TOK_KEY);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "users") == 0);

    tok = rt_json_stream_next(p); /* [ */
    assert(tok == RT_JSON_TOK_ARRAY_START);

    /* First user object */
    tok = rt_json_stream_next(p); /* { */
    assert(tok == RT_JSON_TOK_OBJECT_START);

    tok = rt_json_stream_next(p); /* key "name" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* "Alice" */
    assert(tok == RT_JSON_TOK_STRING);
    assert(strcmp(rt_string_cstr(rt_json_stream_string_value(p)), "Alice") == 0);

    tok = rt_json_stream_next(p); /* key "age" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* 30 */
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 30.0);

    tok = rt_json_stream_next(p); /* key "active" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* true */
    assert(tok == RT_JSON_TOK_BOOL);
    assert(rt_json_stream_bool_value(p) == 1);

    tok = rt_json_stream_next(p); /* } first user */
    assert(tok == RT_JSON_TOK_OBJECT_END);

    /* Skip second user */
    tok = rt_json_stream_next(p); /* { second user */
    assert(tok == RT_JSON_TOK_OBJECT_START);
    rt_json_stream_skip(p);
    assert(rt_json_stream_token_type(p) == RT_JSON_TOK_OBJECT_END);

    tok = rt_json_stream_next(p); /* ] */
    assert(tok == RT_JSON_TOK_ARRAY_END);

    tok = rt_json_stream_next(p); /* key "count" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* 2 */
    assert(tok == RT_JSON_TOK_NUMBER);
    assert(rt_json_stream_number_value(p) == 2.0);

    tok = rt_json_stream_next(p); /* key "meta" */
    assert(tok == RT_JSON_TOK_KEY);
    tok = rt_json_stream_next(p); /* null */
    assert(tok == RT_JSON_TOK_NULL);

    tok = rt_json_stream_next(p); /* } outer */
    assert(tok == RT_JSON_TOK_OBJECT_END);

    tok = rt_json_stream_next(p);
    assert(tok == RT_JSON_TOK_END);

    printf("test_complex_json: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== JsonStream Tests ===\n\n");

    /* Basic tokens */
    test_empty_object();
    test_empty_array();
    test_string_value();
    test_number_value();
    test_bool_value();
    test_null_value();
    test_nested_object();
    test_array_of_objects();

    /* Escape handling */
    test_escape_sequences();
    test_unicode_escape();

    /* Skip */
    test_skip_object();
    test_skip_array();

    /* State queries */
    test_has_next();
    test_token_type_none();

    /* Error handling */
    test_invalid_json();
    test_unterminated_string();

    /* NULL safety */
    test_null_parser();

    /* Complex document */
    test_complex_json();

    printf("\nAll JsonStream tests passed!\n");
    return 0;
}
