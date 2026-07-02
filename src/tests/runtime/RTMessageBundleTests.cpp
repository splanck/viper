//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMessageBundleTests.cpp
// Purpose: Validate Viper.Localization.MessageBundle — FromMap construction,
//          Get/TryGet/Has lookup, Format ({name}) and FormatWith ({0})
//          placeholder interpolation, Plural key resolution, fallback
//          chain walking, and trap/cycle detection paths.
//
//===----------------------------------------------------------------------===//

#include "rt_list.h"
#include "rt_locale.h"
#include "rt_map.h"
#include "rt_message_bundle.h"
#include "rt_option.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_GETPID() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir(path, 0700)
#define TEST_GETPID() getpid()
#endif

static jmp_buf g_trap_env;
static int g_expect_trap = 0;

extern "C" void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_expect_trap = 1;                                                                         \
        if (setjmp(g_trap_env) == 0) {                                                             \
            (void)(expr);                                                                          \
            g_expect_trap = 0;                                                                     \
            assert(!"expected runtime trap");                                                      \
        } else {                                                                                   \
            g_expect_trap = 0;                                                                     \
        }                                                                                          \
    } while (0)

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

static void *en_locale() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return loc;
}

static std::string temp_dir(const char *name) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/viper_msg_%ld_%s", base, (long)TEST_GETPID(), name);
    TEST_MKDIR(buf);
    return std::string(buf);
}

static void write_text_file(const std::string &path, const char *text) {
    FILE *f = fopen(path.c_str(), "wb");
    assert(f && "failed to create temp message JSON");
    size_t len = strlen(text);
    assert(fwrite(text, 1, len, f) == len);
    fclose(f);
}

// Build a Map<str, str> from pairs (alternating key, value). Terminated by NULL.
static void *build_map(const char **pairs) {
    void *m = rt_map_new();
    while (pairs[0] && pairs[1]) {
        rt_string k = S(pairs[0]);
        rt_string v = S(pairs[1]);
        rt_map_set_str(m, k, v);
        rt_string_unref(k);
        rt_string_unref(v);
        pairs += 2;
    }
    return m;
}

//=============================================================================
// FromMap / Get / TryGet
//=============================================================================

static void test_from_map_basic() {
    printf("Testing MessageBundle.FromMap + Get:\n");
    const char *pairs[] = {"hello", "Hello, world!", "bye", "Goodbye.", nullptr};
    void *m = build_map(pairs);
    void *b = rt_message_bundle_from_map(en_locale(), m);

    rt_string k1 = S("hello");
    test_result("Get(hello) = \"Hello, world!\"",
                eq(rt_message_bundle_get(b, k1), "Hello, world!"));
    rt_string_unref(k1);

    rt_string k2 = S("bye");
    test_result("Get(bye) = \"Goodbye.\"", eq(rt_message_bundle_get(b, k2), "Goodbye."));
    rt_string_unref(k2);
}

static void test_try_get_missing() {
    printf("Testing MessageBundle.TryGet on missing key:\n");
    const char *pairs[] = {"hello", "hi", "empty", "", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    rt_string missing = S("gone");
    test_result("TryGet(missing) = \"\"", eq(rt_message_bundle_try_get(b, missing), ""));
    void *missing_option = rt_message_bundle_try_get_option(b, missing);
    test_result("TryGetOption(missing) = None", rt_option_is_none(missing_option) == 1);
    rt_string fallback = S("fallback");
    test_result("GetOr(missing) = fallback",
                eq(rt_message_bundle_get_or(b, missing, fallback), "fallback"));
    rt_string_unref(fallback);
    rt_string_unref(missing);

    rt_string existing = S("hello");
    test_result("TryGet(existing) = \"hi\"", eq(rt_message_bundle_try_get(b, existing), "hi"));
    void *existing_option = rt_message_bundle_try_get_option(b, existing);
    test_result("TryGetOption(existing) = Some(\"hi\")",
                rt_option_is_some(existing_option) == 1 &&
                    strcmp(rt_string_cstr(rt_option_unwrap_str(existing_option)), "hi") == 0);
    rt_string_unref(existing);

    rt_string empty = S("empty");
    void *empty_option = rt_message_bundle_try_get_option(b, empty);
    test_result("TryGetOption(empty translation) = Some(\"\")",
                rt_option_is_some(empty_option) == 1 &&
                    rt_str_len(rt_option_unwrap_str(empty_option)) == 0);
    rt_string_unref(empty);
}

static void test_has() {
    printf("Testing MessageBundle.Has:\n");
    const char *pairs[] = {"alpha", "1", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));
    rt_string k = S("alpha");
    test_result("Has(alpha) = true", rt_message_bundle_has(b, k) == 1);
    rt_string_unref(k);
    rt_string k2 = S("beta");
    test_result("Has(beta) = false", rt_message_bundle_has(b, k2) == 0);
    rt_string_unref(k2);
}

//=============================================================================
// Format — named and positional
//=============================================================================

static void test_format_named() {
    printf("Testing MessageBundle.Format (named):\n");
    const char *pairs[] = {"greet", "Hello, {name}!", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    const char *vpairs[] = {"name", "Alice", nullptr};
    void *vars = build_map(vpairs);

    rt_string key = S("greet");
    test_result("Format(greet, {name=Alice}) = \"Hello, Alice!\"",
                eq(rt_message_bundle_format(b, key, vars), "Hello, Alice!"));
    rt_string_unref(key);
}

static void test_format_missing_placeholder() {
    printf("Testing Format with missing placeholder:\n");
    const char *pairs[] = {"greet", "Hello, {name}!", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));
    void *vars = rt_map_new();

    rt_string key = S("greet");
    // Missing placeholders are preserved so translator/key mistakes remain visible.
    test_result("Format missing var preserves placeholder",
                eq(rt_message_bundle_format(b, key, vars), "Hello, {name}!"));
    rt_string_unref(key);
}

static void test_format_escaped_braces() {
    printf("Testing Format escaped braces:\n");
    const char *pairs[] = {"tmpl", "{{Hello}} {name}", "pos", "{{{0}}}", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    const char *vpairs[] = {"name", "Alice", nullptr};
    void *vars = build_map(vpairs);
    rt_string key = S("tmpl");
    test_result("Format handles {{ and }}",
                eq(rt_message_bundle_format(b, key, vars), "{Hello} Alice"));
    rt_string_unref(key);

    void *list = rt_list_new();
    rt_string x = S("X");
    rt_list_push(list, x);
    rt_string_unref(x);
    rt_string pos = S("pos");
    test_result("FormatWith handles escaped braces",
                eq(rt_message_bundle_format_with(b, pos, list), "{X}"));
    rt_string_unref(pos);
}

static void test_format_with_positional() {
    printf("Testing MessageBundle.FormatWith (positional):\n");
    const char *pairs[] = {"order", "{0}, {1}, and {2}", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    void *list = rt_list_new();
    rt_string a = S("apples"), c = S("cherries"), bn = S("bananas");
    rt_list_push(list, a);
    rt_list_push(list, bn);
    rt_list_push(list, c);

    rt_string key = S("order");
    test_result("FormatWith -> \"apples, bananas, and cherries\"",
                eq(rt_message_bundle_format_with(b, key, list), "apples, bananas, and cherries"));
    rt_string_unref(key);
}

static void test_format_with_large_index_preserved() {
    printf("Testing FormatWith positional index overflow guard:\n");
    const char *pairs[] = {"big", "{999999999999999999999999999999}", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));
    void *list = rt_list_new();

    rt_string key = S("big");
    test_result(
        "Overflowing positional index is preserved",
        eq(rt_message_bundle_format_with(b, key, list), "{999999999999999999999999999999}"));
    rt_string_unref(key);
}

//=============================================================================
// Plural
//=============================================================================

static void test_plural_basic() {
    printf("Testing MessageBundle.Plural:\n");
    const char *pairs[] = {"items.one", "1 item", "items.other", "{n} items", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    rt_string key = S("items");
    void *vars = rt_map_new();
    test_result("Plural(items, 1) = \"1 item\"",
                eq(rt_message_bundle_plural(b, key, 1, vars), "1 item"));

    rt_string key2 = S("items");
    void *vars2 = rt_map_new();
    test_result("Plural(items, 5) = \"5 items\"",
                eq(rt_message_bundle_plural(b, key2, 5, vars2), "5 items"));

    rt_string key3 = S("items");
    void *vars3 = rt_map_new();
    test_result("Plural(items, 0) = \"0 items\"",
                eq(rt_message_bundle_plural(b, key3, 0, vars3), "0 items"));

    rt_string_unref(key);
    rt_string_unref(key2);
    rt_string_unref(key3);
}

static void test_plural_other_fallback() {
    printf("Testing Plural fallback when category missing:\n");
    // Only "other" form provided; all n values should hit it.
    const char *pairs[] = {"days.other", "{n} days", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    rt_string key = S("days");
    void *vars = rt_map_new();
    test_result("Plural(days, 1) falls back to other",
                eq(rt_message_bundle_plural(b, key, 1, vars), "1 days"));
    rt_string_unref(key);
}

static void test_plural_does_not_mutate_vars() {
    printf("Testing Plural does not mutate vars:\n");
    const char *pairs[] = {"items.other", "{n} items for {name}", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));
    const char *vpairs[] = {"name", "Alice", nullptr};
    void *vars = build_map(vpairs);

    rt_string key = S("items");
    test_result("Plural formats using cloned vars",
                eq(rt_message_bundle_plural(b, key, 7, vars), "7 items for Alice"));
    rt_string_unref(key);

    rt_string n_key = S("n");
    test_result("Original vars does not gain n", rt_map_has(vars, n_key) == 0);
    rt_string_unref(n_key);
}

static void test_load_from_json_schema() {
    printf("Testing MessageBundle.LoadFromJson schema:\n");
    std::string dir = temp_dir("json");
    std::string good = dir + "/messages.json";
    write_text_file(good, "{\"hello\":\"Hello\",\"bye\":\"Bye\"}");

    rt_string good_path = S(good.c_str());
    void *b = rt_message_bundle_load_from_json(en_locale(), good_path);
    rt_string_unref(good_path);
    rt_string hello = S("hello");
    test_result("LoadFromJson string map works", eq(rt_message_bundle_get(b, hello), "Hello"));
    rt_string_unref(hello);

    std::string bad = dir + "/bad.json";
    write_text_file(bad, "{\"hello\":1}");
    rt_string bad_path = S(bad.c_str());
    EXPECT_TRAP(rt_message_bundle_load_from_json(en_locale(), bad_path));
    rt_string_unref(bad_path);
    test_result("LoadFromJson rejects non-string values", true);
}

//=============================================================================
// Fallback chain
//=============================================================================

static void test_fallback_chain() {
    printf("Testing Fallback chain walk:\n");
    const char *base_pairs[] = {"ui.save", "Save", "ui.load", "Load", nullptr};
    const char *custom_pairs[] = {"ui.save", "Save As", nullptr};

    void *base = rt_message_bundle_from_map(en_locale(), build_map(base_pairs));
    void *custom = rt_message_bundle_from_map(en_locale(), build_map(custom_pairs));
    rt_message_bundle_set_fallback(custom, base);

    rt_string k_save = S("ui.save");
    test_result("custom overrides base: ui.save",
                eq(rt_message_bundle_get(custom, k_save), "Save As"));
    rt_string_unref(k_save);

    rt_string k_load = S("ui.load");
    test_result("custom falls through to base: ui.load",
                eq(rt_message_bundle_get(custom, k_load), "Load"));
    rt_string_unref(k_load);
}

static void test_locale_qualified_fallback_keys() {
    printf("Testing locale-qualified MessageBundle fallback keys:\n");
    rt_string loc_s = S("fr-CA");
    void *loc = rt_locale_parse(loc_s);
    rt_string_unref(loc_s);
    const char *pairs[] = {"fr:greet", "Bonjour", "root:greet", "Hello", nullptr};
    void *b = rt_message_bundle_from_map(loc, build_map(pairs));

    rt_string key = S("greet");
    test_result("fr-CA falls back to fr:greet inside bundle",
                eq(rt_message_bundle_get(b, key), "Bonjour"));
    rt_string_unref(key);
}

static void test_fallback_cycle_trap() {
    printf("Testing Fallback cycle detection:\n");
    void *a = rt_message_bundle_from_map(en_locale(), rt_map_new());
    void *b = rt_message_bundle_from_map(en_locale(), rt_map_new());
    rt_message_bundle_set_fallback(a, b);
    EXPECT_TRAP(rt_message_bundle_set_fallback(b, a));
    test_result("Setting fallback that would cycle traps", true);
}

//=============================================================================
// Trap paths
//=============================================================================

static void test_get_missing_traps() {
    printf("Testing Get with no matching key traps:\n");
    void *b = rt_message_bundle_from_map(en_locale(), rt_map_new());
    rt_string k = S("nonexistent");
    EXPECT_TRAP(rt_message_bundle_get(b, k));
    rt_string_unref(k);
    test_result("Get on empty bundle traps", true);
}

static void test_format_rejects_bad_vars() {
    printf("Testing Format vars validation:\n");
    const char *pairs[] = {"greet", "Hello, {name}!", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));
    void *vars = rt_map_new();
    rt_string name = S("name");
    rt_map_set_int(vars, name, 42);
    rt_string_unref(name);

    rt_string key = S("greet");
    EXPECT_TRAP(rt_message_bundle_format(b, key, vars));
    rt_string_unref(key);
    test_result("Format rejects non-string var values", true);
}

//=============================================================================
// Keys + Count
//=============================================================================

static void test_keys_and_count() {
    printf("Testing Keys + Count:\n");
    const char *pairs[] = {"a", "1", "b", "2", "c", "3", nullptr};
    void *b = rt_message_bundle_from_map(en_locale(), build_map(pairs));

    test_result("Count = 3", rt_message_bundle_get_count(b) == 3);

    void *keys = rt_message_bundle_keys(b);
    test_result("Keys list has 3 entries", rt_list_len(keys) == 3);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT MessageBundle Tests ===\n\n");
    test_from_map_basic();
    test_try_get_missing();
    test_has();
    test_format_named();
    test_format_missing_placeholder();
    test_format_escaped_braces();
    test_format_with_positional();
    test_format_with_large_index_preserved();
    test_plural_basic();
    test_plural_other_fallback();
    test_plural_does_not_mutate_vars();
    test_load_from_json_schema();
    test_fallback_chain();
    test_locale_qualified_fallback_keys();
    test_fallback_cycle_trap();
    test_get_missing_traps();
    test_format_rejects_bad_vars();
    test_keys_and_count();
    printf("\nAll MessageBundle tests passed!\n");
    return 0;
}
