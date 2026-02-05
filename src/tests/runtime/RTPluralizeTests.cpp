//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_pluralize.h"
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
// Pluralize tests
// ---------------------------------------------------------------------------

static void test_plural_regular_s()
{
    rt_string w = make_str("cat");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "cats"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_es()
{
    rt_string w = make_str("box");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "boxes"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_ch()
{
    rt_string w = make_str("church");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "churches"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_sh()
{
    rt_string w = make_str("brush");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "brushes"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_y_to_ies()
{
    rt_string w = make_str("baby");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "babies"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_vowel_y()
{
    rt_string w = make_str("key");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "keys"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_irregular()
{
    rt_string w = make_str("child");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "children"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_man()
{
    rt_string w = make_str("man");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "men"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_plural_uncountable()
{
    rt_string w = make_str("sheep");
    rt_string r = rt_pluralize(w);
    assert(str_eq(r, "sheep"));
    rt_string_unref(r);
    rt_string_unref(w);
}

// ---------------------------------------------------------------------------
// Singularize tests
// ---------------------------------------------------------------------------

static void test_singular_regular()
{
    rt_string w = make_str("cats");
    rt_string r = rt_singularize(w);
    assert(str_eq(r, "cat"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_singular_es()
{
    rt_string w = make_str("boxes");
    rt_string r = rt_singularize(w);
    assert(str_eq(r, "box"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_singular_ies()
{
    rt_string w = make_str("babies");
    rt_string r = rt_singularize(w);
    assert(str_eq(r, "baby"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_singular_irregular()
{
    rt_string w = make_str("children");
    rt_string r = rt_singularize(w);
    assert(str_eq(r, "child"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_singular_uncountable()
{
    rt_string w = make_str("fish");
    rt_string r = rt_singularize(w);
    assert(str_eq(r, "fish"));
    rt_string_unref(r);
    rt_string_unref(w);
}

// ---------------------------------------------------------------------------
// Count tests
// ---------------------------------------------------------------------------

static void test_count_one()
{
    rt_string w = make_str("item");
    rt_string r = rt_pluralize_count(1, w);
    assert(str_eq(r, "1 item"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_count_many()
{
    rt_string w = make_str("item");
    rt_string r = rt_pluralize_count(5, w);
    assert(str_eq(r, "5 items"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_count_zero()
{
    rt_string w = make_str("item");
    rt_string r = rt_pluralize_count(0, w);
    assert(str_eq(r, "0 items"));
    rt_string_unref(r);
    rt_string_unref(w);
}

static void test_null_safety()
{
    rt_string r = rt_pluralize(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_singularize(NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);

    r = rt_pluralize_count(5, NULL);
    assert(str_eq(r, ""));
    rt_string_unref(r);
}

int main()
{
    // Pluralize
    test_plural_regular_s();
    test_plural_es();
    test_plural_ch();
    test_plural_sh();
    test_plural_y_to_ies();
    test_plural_vowel_y();
    test_plural_irregular();
    test_plural_man();
    test_plural_uncountable();

    // Singularize
    test_singular_regular();
    test_singular_es();
    test_singular_ies();
    test_singular_irregular();
    test_singular_uncountable();

    // Count
    test_count_one();
    test_count_many();
    test_count_zero();

    // Null safety
    test_null_safety();

    return 0;
}
