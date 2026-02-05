//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCaseConvertTests.cpp
// Purpose: Tests for case conversion string functions (CamelCase, PascalCase,
//          SnakeCase, KebabCase, ScreamingSnake).
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// ---------------------------------------------------------------------------
// CamelCase tests
// ---------------------------------------------------------------------------

static void test_camel_from_spaces()
{
    rt_string s = make_str("hello world");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "helloWorld"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_camel_from_snake()
{
    rt_string s = make_str("hello_world_test");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "helloWorldTest"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_camel_from_kebab()
{
    rt_string s = make_str("hello-world-test");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "helloWorldTest"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_camel_from_pascal()
{
    rt_string s = make_str("HelloWorld");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "helloWorld"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_camel_single_word()
{
    rt_string s = make_str("hello");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "hello"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_camel_empty()
{
    rt_string s = make_str("");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, ""));
    rt_string_unref(r);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// PascalCase tests
// ---------------------------------------------------------------------------

static void test_pascal_from_spaces()
{
    rt_string s = make_str("hello world");
    rt_string r = rt_str_pascal_case(s);
    assert(str_eq(r, "HelloWorld"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_pascal_from_snake()
{
    rt_string s = make_str("hello_world_test");
    rt_string r = rt_str_pascal_case(s);
    assert(str_eq(r, "HelloWorldTest"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_pascal_from_camel()
{
    rt_string s = make_str("helloWorld");
    rt_string r = rt_str_pascal_case(s);
    assert(str_eq(r, "HelloWorld"));
    rt_string_unref(r);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// SnakeCase tests
// ---------------------------------------------------------------------------

static void test_snake_from_camel()
{
    rt_string s = make_str("helloWorld");
    rt_string r = rt_str_snake_case(s);
    assert(str_eq(r, "hello_world"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_snake_from_pascal()
{
    rt_string s = make_str("HelloWorldTest");
    rt_string r = rt_str_snake_case(s);
    assert(str_eq(r, "hello_world_test"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_snake_from_spaces()
{
    rt_string s = make_str("hello world test");
    rt_string r = rt_str_snake_case(s);
    assert(str_eq(r, "hello_world_test"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_snake_from_kebab()
{
    rt_string s = make_str("hello-world");
    rt_string r = rt_str_snake_case(s);
    assert(str_eq(r, "hello_world"));
    rt_string_unref(r);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// KebabCase tests
// ---------------------------------------------------------------------------

static void test_kebab_from_camel()
{
    rt_string s = make_str("helloWorld");
    rt_string r = rt_str_kebab_case(s);
    assert(str_eq(r, "hello-world"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_kebab_from_snake()
{
    rt_string s = make_str("hello_world_test");
    rt_string r = rt_str_kebab_case(s);
    assert(str_eq(r, "hello-world-test"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_kebab_from_pascal()
{
    rt_string s = make_str("HelloWorld");
    rt_string r = rt_str_kebab_case(s);
    assert(str_eq(r, "hello-world"));
    rt_string_unref(r);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// ScreamingSnake tests
// ---------------------------------------------------------------------------

static void test_screaming_from_camel()
{
    rt_string s = make_str("helloWorld");
    rt_string r = rt_str_screaming_snake(s);
    assert(str_eq(r, "HELLO_WORLD"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_screaming_from_snake()
{
    rt_string s = make_str("hello_world");
    rt_string r = rt_str_screaming_snake(s);
    assert(str_eq(r, "HELLO_WORLD"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_screaming_from_spaces()
{
    rt_string s = make_str("hello world test");
    rt_string r = rt_str_screaming_snake(s);
    assert(str_eq(r, "HELLO_WORLD_TEST"));
    rt_string_unref(r);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// Mixed / edge cases
// ---------------------------------------------------------------------------

static void test_null_safety()
{
    rt_string r = rt_str_camel_case(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_str_pascal_case(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_str_snake_case(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_str_kebab_case(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_str_screaming_snake(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);
}

static void test_mixed_separators()
{
    rt_string s = make_str("hello_world-test case");
    rt_string r = rt_str_camel_case(s);
    assert(str_eq(r, "helloWorldTestCase"));
    rt_string_unref(r);
    rt_string_unref(s);
}

static void test_acronym_handling()
{
    rt_string s = make_str("XMLParser");
    rt_string r = rt_str_snake_case(s);
    // "XML" should be split as "XM" + "LParser" -> "xm_l_parser"
    // or handled as one unit depending on the algorithm
    // Our split_words treats runs of uppercase: when it sees uppercase+uppercase+lowercase
    // it breaks before the last uppercase, so XML -> XM + LParser -> xm + lparser? No...
    // Actually: X(upper), M(upper), L(upper), P(upper)... wait P is uppercase too
    // XMLParser: X-M-L-P-a-r-s-e-r
    // split_words at i=0: X is upper, i+1=M is upper, i+2=L is upper -> acronym boundary at i=0
    //   so we get "X" then continue from i=1
    //   at i=1: M upper, L upper, P upper -> acronym boundary, get "M"
    //   at i=2: L upper, P upper, a lower -> acronym boundary, get "L"
    //   at i=3: P upper, a lower -> not acronym (only 2 chars), collect "Parser"
    // Result: x_m_l_parser
    // Actually let me re-check the algorithm... The ACRONYM check is:
    //   if i+2 < len && upper[i] && upper[i+1] && lower[i+2] -> break, emit char at i
    // So for "XMLParser":
    //   i=0: X upper, M upper, L lower? No L is upper. No break.
    //   Then camelCase check: i=0 X upper, but no lowercase before it. No break.
    //   Collect X, i=1
    //   i=1: M upper, L upper, P upper? Yes. i+2=L, but is L upper?
    //   Wait... "XMLParser" = X, M, L, P, a, r, s, e, r
    //   i=1: src[1]='M' upper, src[2]='L' upper, src[3]='P' upper -> not lower so no acronym break
    //   camelCase: src[1]='M' not lower so no camelCase break
    //   Collect M, i=2
    //   i=2: src[2]='L' upper, src[3]='P' upper, src[4]='a' lower -> ACRONYM boundary! Break.
    //   So word so far is "XML", break. Next word starts at i=3.
    //   i=3: src[3]='P' upper... collect normally until separator or camelCase
    //   src[3]='P', src[4]='a' -> P is upper, a is lower -> not camelCase (P is not preceded by lower)
    //   Actually wait: camelCase check is: islower(src[i]) && isupper(src[i+1])
    //   At i=3: P is upper, not lower, so no break
    //   At i=4: a is lower, r is lower -> no break
    //   ... collect "Parser"
    // So we get words: ["XML", "Parser"]
    // snake_case: "xml_parser"
    const char *result_cstr = rt_string_cstr(r);
    assert(str_eq(r, "xml_parser"));
    rt_string_unref(r);
    rt_string_unref(s);
}

int main()
{
    // CamelCase
    test_camel_from_spaces();
    test_camel_from_snake();
    test_camel_from_kebab();
    test_camel_from_pascal();
    test_camel_single_word();
    test_camel_empty();

    // PascalCase
    test_pascal_from_spaces();
    test_pascal_from_snake();
    test_pascal_from_camel();

    // SnakeCase
    test_snake_from_camel();
    test_snake_from_pascal();
    test_snake_from_spaces();
    test_snake_from_kebab();

    // KebabCase
    test_kebab_from_camel();
    test_kebab_from_snake();
    test_kebab_from_pascal();

    // ScreamingSnake
    test_screaming_from_camel();
    test_screaming_from_snake();
    test_screaming_from_spaces();

    // Edge cases
    test_null_safety();
    test_mixed_separators();
    test_acronym_handling();

    return 0;
}
