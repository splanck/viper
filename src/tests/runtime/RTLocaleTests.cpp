//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTLocaleTests.cpp
// Purpose: Validate Viper.Localization.Locale parsing, canonicalization, and
//          fallback-chain behavior. Covers the positive path for common BCP-47
//          shapes and the rejection path for malformed input.
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_locale_manager.h"
#include "rt_list.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>

static jmp_buf g_trap_env;
static int g_expect_trap = 0;

extern "C" void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define EXPECT_TRAP(expr)                                                         \
    do {                                                                          \
        g_expect_trap = 1;                                                         \
        if (setjmp(g_trap_env) == 0) {                                            \
            (void)(expr);                                                          \
            g_expect_trap = 0;                                                     \
            assert(!"expected runtime trap");                                     \
        } else {                                                                   \
            g_expect_trap = 0;                                                     \
        }                                                                          \
    } while (0)

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool tag_eq(void *locale, const char *expected) {
    rt_string t = rt_locale_tag(locale);
    const char *cs = rt_string_cstr(t);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(t);
    return ok;
}

static bool field_eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

//=============================================================================
// Parse — happy path
//=============================================================================

static void test_parse_basic_tags() {
    printf("Testing Locale.Parse basic tags:\n");

    {
        rt_string in = S("en");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"en\") -> en", tag_eq(loc, "en"));
    }
    {
        rt_string in = S("en-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"en-US\") -> en-US", tag_eq(loc, "en-US"));
    }
    {
        rt_string in = S("fr-FR");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"fr-FR\") -> fr-FR", tag_eq(loc, "fr-FR"));
    }
    {
        rt_string in = S("en-Latn-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"en-Latn-US\") -> en-Latn-US", tag_eq(loc, "en-Latn-US"));
    }
    {
        rt_string in = S("zh-Hans-CN");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"zh-Hans-CN\") -> zh-Hans-CN", tag_eq(loc, "zh-Hans-CN"));
    }
}

//=============================================================================
// Parse — canonicalization
//=============================================================================

static void test_parse_canonicalization() {
    printf("Testing Locale.Parse canonicalization:\n");

    // Mixed case input → language lowercased, region uppercased.
    {
        rt_string in = S("EN_us");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"EN_us\") canonical", tag_eq(loc, "en-US"));
    }
    // Underscore separator is accepted and normalized to dash.
    {
        rt_string in = S("de_DE");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"de_DE\") canonical", tag_eq(loc, "de-DE"));
    }
    // Script case is normalized to Title-case.
    {
        rt_string in = S("ZH-hans-cn");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"ZH-hans-cn\") canonical", tag_eq(loc, "zh-Hans-CN"));
    }
    // 3-digit region (UN M.49) survives as-is.
    {
        rt_string in = S("es-419");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"es-419\") canonical", tag_eq(loc, "es-419"));
    }
    // "root" maps to invariant.
    {
        rt_string in = S("root");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("Parse(\"root\") -> root", tag_eq(loc, "root"));
    }
}

//=============================================================================
// Property accessors
//=============================================================================

static void test_property_accessors() {
    printf("Testing Locale property accessors:\n");

    {
        rt_string in = S("en-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("en-US.Language == \"en\"", field_eq(rt_locale_language(loc), "en"));
        test_result("en-US.Script == \"\"",     field_eq(rt_locale_script(loc), ""));
        test_result("en-US.Region == \"US\"",   field_eq(rt_locale_region(loc), "US"));
        test_result("en-US.Tag == \"en-US\"",   field_eq(rt_locale_tag(loc), "en-US"));
    }
    {
        rt_string in = S("en-Latn-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        test_result("en-Latn-US.Script == \"Latn\"", field_eq(rt_locale_script(loc), "Latn"));
    }
    {
        void *root = rt_locale_invariant();
        test_result("Invariant().Tag == \"root\"", field_eq(rt_locale_tag(root), "root"));
        test_result("Invariant().Language == \"\"", field_eq(rt_locale_language(root), ""));
    }
}

//=============================================================================
// FromParts
//=============================================================================

static void test_from_parts() {
    printf("Testing Locale.FromParts:\n");

    {
        rt_string lang = S("en");
        rt_string script = S("");
        rt_string region = S("US");
        void *loc = rt_locale_from_parts(lang, script, region);
        rt_string_unref(lang);
        rt_string_unref(script);
        rt_string_unref(region);
        test_result("FromParts(en,\"\",US) -> en-US", tag_eq(loc, "en-US"));
    }
    {
        rt_string lang = S("zh");
        rt_string script = S("Hans");
        rt_string region = S("CN");
        void *loc = rt_locale_from_parts(lang, script, region);
        rt_string_unref(lang);
        rt_string_unref(script);
        rt_string_unref(region);
        test_result("FromParts(zh,Hans,CN) -> zh-Hans-CN", tag_eq(loc, "zh-Hans-CN"));
    }
}

//=============================================================================
// Equals
//=============================================================================

static void test_equals() {
    printf("Testing Locale.Equals:\n");

    rt_string a_str = S("en-US");
    rt_string b_str = S("EN_us");
    rt_string c_str = S("fr-FR");
    void *a = rt_locale_parse(a_str);
    void *b = rt_locale_parse(b_str); // canonicalizes to en-US
    void *c = rt_locale_parse(c_str);
    rt_string_unref(a_str);
    rt_string_unref(b_str);
    rt_string_unref(c_str);

    test_result("Equals(en-US, en-US) = 1",         rt_locale_equals(a, a) == 1);
    test_result("Equals(en-US, EN_us canonical)",    rt_locale_equals(a, b) == 1);
    test_result("Equals(en-US, fr-FR) = 0",         rt_locale_equals(a, c) == 0);
    test_result("Equals(null, null) = 1",            rt_locale_equals(nullptr, nullptr) == 1);
    test_result("Equals(a, null) = 0",               rt_locale_equals(a, nullptr) == 0);
}

//=============================================================================
// Fallbacks
//=============================================================================

static int64_t list_len(void *list) {
    extern int64_t rt_list_len(void *);
    return rt_list_len(list);
}

static bool list_tag_at(void *list, int64_t idx, const char *expected) {
    extern void *rt_list_get(void *, int64_t);
    void *loc = rt_list_get(list, idx);
    return tag_eq(loc, expected);
}

static void test_fallbacks() {
    printf("Testing Locale.Fallbacks:\n");

    // en-Latn-US -> [en-Latn-US, en-US, en, root]
    {
        rt_string in = S("en-Latn-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        void *chain = rt_locale_fallbacks(loc);
        test_result("en-Latn-US chain length 4", list_len(chain) == 4);
        test_result("chain[0] = en-Latn-US", list_tag_at(chain, 0, "en-Latn-US"));
        test_result("chain[1] = en-US",      list_tag_at(chain, 1, "en-US"));
        test_result("chain[2] = en",         list_tag_at(chain, 2, "en"));
        test_result("chain[3] = root",       list_tag_at(chain, 3, "root"));
    }
    // en-US -> [en-US, en, root]
    {
        rt_string in = S("en-US");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        void *chain = rt_locale_fallbacks(loc);
        test_result("en-US chain length 3", list_len(chain) == 3);
        test_result("chain[0] = en-US",   list_tag_at(chain, 0, "en-US"));
        test_result("chain[1] = en",      list_tag_at(chain, 1, "en"));
        test_result("chain[2] = root",    list_tag_at(chain, 2, "root"));
    }
    // en -> [en, root]
    {
        rt_string in = S("en");
        void *loc = rt_locale_parse(in);
        rt_string_unref(in);
        void *chain = rt_locale_fallbacks(loc);
        test_result("en chain length 2", list_len(chain) == 2);
        test_result("chain[0] = en",     list_tag_at(chain, 0, "en"));
        test_result("chain[1] = root",   list_tag_at(chain, 1, "root"));
    }
    // root -> [root] (special case: invariant-only chain)
    {
        void *root = rt_locale_invariant();
        void *chain = rt_locale_fallbacks(root);
        test_result("root chain length 1", list_len(chain) == 1);
        test_result("chain[0] = root",     list_tag_at(chain, 0, "root"));
    }
}

//=============================================================================
// Traps
//=============================================================================

static void test_trap_empty() {
    printf("Testing Locale.Parse trap paths:\n");

    rt_string empty = S("");
    EXPECT_TRAP(rt_locale_parse(empty));
    rt_string_unref(empty);
    test_result("Parse(\"\") traps", true);

    // Single-character "tag" is invalid (language min 2 chars).
    rt_string one = S("x");
    EXPECT_TRAP(rt_locale_parse(one));
    rt_string_unref(one);
    test_result("Parse(\"x\") traps", true);

    // Non-alpha input.
    rt_string bogus = S("123");
    EXPECT_TRAP(rt_locale_parse(bogus));
    rt_string_unref(bogus);
    test_result("Parse(\"123\") traps", true);

    // Subtag exceeding 8 chars.
    rt_string toolong = S("englishlang");
    EXPECT_TRAP(rt_locale_parse(toolong));
    rt_string_unref(toolong);
    test_result("Parse(\"englishlang\") traps", true);
}

static void test_try_parse_returns_null() {
    printf("Testing Locale.TryParse soft failure:\n");

    rt_string bogus = S("!@#");
    void *loc = rt_locale_try_parse(bogus);
    rt_string_unref(bogus);
    test_result("TryParse(\"!@#\") returns NULL", loc == nullptr);

    rt_string empty = S("");
    void *loc2 = rt_locale_try_parse(empty);
    rt_string_unref(empty);
    test_result("TryParse(\"\") returns NULL", loc2 == nullptr);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT Locale Tests ===\n\n");
    test_parse_basic_tags();
    test_parse_canonicalization();
    test_property_accessors();
    test_from_parts();
    test_equals();
    test_fallbacks();
    test_trap_empty();
    test_try_parse_returns_null();
    printf("\nAll Locale tests passed!\n");
    return 0;
}
