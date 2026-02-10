//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBoolStdTests.cpp
// Purpose: Verify that all bool-standardised functions return int8_t 0/1.
//
//===----------------------------------------------------------------------===//

#include "rt_bits.h"
#include "rt_compiled_pattern.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_log.h"
#include "rt_parse.h"
#include "rt_regex.h"
#include "rt_string.h"
#include "rt_template.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// rt_parse return type tests
// ============================================================================

static void test_parse_try_int_returns()
{
    int64_t val = 0;
    int8_t r = rt_parse_try_int(make_str("42"), &val);
    assert(r == 1);
    assert(val == 42);

    r = rt_parse_try_int(make_str("abc"), &val);
    assert(r == 0);

    printf("test_parse_try_int_returns: PASSED\n");
}

static void test_parse_try_num_returns()
{
    double val = 0.0;
    int8_t r = rt_parse_try_num(make_str("3.14"), &val);
    assert(r == 1);

    r = rt_parse_try_num(make_str("abc"), &val);
    assert(r == 0);

    printf("test_parse_try_num_returns: PASSED\n");
}

static void test_parse_try_bool_returns()
{
    int8_t val = 0;
    int8_t r = rt_parse_try_bool(make_str("true"), &val);
    assert(r == 1);
    assert(val == 1);

    r = rt_parse_try_bool(make_str("false"), &val);
    assert(r == 1);
    assert(val == 0);

    r = rt_parse_try_bool(make_str("maybe"), &val);
    assert(r == 0);

    printf("test_parse_try_bool_returns: PASSED\n");
}

static void test_parse_bool_or_returns()
{
    int8_t r = rt_parse_bool_or(make_str("yes"), 0);
    assert(r == 1);

    r = rt_parse_bool_or(make_str("no"), 1);
    assert(r == 0);

    r = rt_parse_bool_or(make_str("invalid"), 1);
    assert(r == 1);

    r = rt_parse_bool_or(make_str("invalid"), 0);
    assert(r == 0);

    printf("test_parse_bool_or_returns: PASSED\n");
}

static void test_parse_is_int_returns()
{
    int8_t r = rt_parse_is_int(make_str("42"));
    assert(r == 1);

    r = rt_parse_is_int(make_str("abc"));
    assert(r == 0);

    printf("test_parse_is_int_returns: PASSED\n");
}

static void test_parse_is_num_returns()
{
    int8_t r = rt_parse_is_num(make_str("3.14"));
    assert(r == 1);

    r = rt_parse_is_num(make_str("abc"));
    assert(r == 0);

    printf("test_parse_is_num_returns: PASSED\n");
}

// ============================================================================
// rt_regex return type test
// ============================================================================

static void test_regex_is_match_returns()
{
    int8_t r = rt_pattern_is_match(make_str("^hello"), make_str("hello world"));
    assert(r == 1);

    r = rt_pattern_is_match(make_str("^goodbye"), make_str("hello world"));
    assert(r == 0);

    printf("test_regex_is_match_returns: PASSED\n");
}

// ============================================================================
// rt_error return type test
// ============================================================================

static void test_error_ok_returns()
{
    RtError none = {Err_None, 0};
    int8_t r = rt_ok(none);
    assert(r == 1);

    RtError err = {Err_RuntimeError, 42};
    r = rt_ok(err);
    assert(r == 0);

    printf("test_error_ok_returns: PASSED\n");
}

// ============================================================================
// rt_bits return type test
// ============================================================================

static void test_bits_get_returns()
{
    int8_t r = rt_bits_get(0xFF, 0);
    assert(r == 1);

    r = rt_bits_get(0xFF, 8);
    assert(r == 0);

    printf("test_bits_get_returns: PASSED\n");
}

// ============================================================================
// rt_log return type test
// ============================================================================

static void test_log_enabled_returns()
{
    int64_t original = rt_log_level();

    rt_log_set_level(rt_log_level_debug());
    int8_t r = rt_log_enabled(rt_log_level_debug());
    assert(r == 1);

    rt_log_set_level(rt_log_level_off());
    r = rt_log_enabled(rt_log_level_debug());
    assert(r == 0);

    rt_log_set_level(original);

    printf("test_log_enabled_returns: PASSED\n");
}

// ============================================================================
// rt_template return type test
// ============================================================================

static void test_template_has_returns()
{
    int8_t r = rt_template_has(make_str("Hello {{name}}!"), make_str("name"));
    assert(r == 1);

    r = rt_template_has(make_str("Hello world!"), make_str("name"));
    assert(r == 0);

    printf("test_template_has_returns: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Bool Standardization Tests ===\n\n");

    test_parse_try_int_returns();
    test_parse_try_num_returns();
    test_parse_try_bool_returns();
    test_parse_bool_or_returns();
    test_parse_is_int_returns();
    test_parse_is_num_returns();
    test_regex_is_match_returns();
    test_error_ok_returns();
    test_bits_get_returns();
    test_log_enabled_returns();
    test_template_has_returns();

    printf("\nAll RTBoolStdTests passed!\n");
    return 0;
}
