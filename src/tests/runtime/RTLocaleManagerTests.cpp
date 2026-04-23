//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTLocaleManagerTests.cpp
// Purpose: Validate Viper.Localization.LocaleManager bootstrap, current/system
//          queries, registry surface, and the stub LoadFromJson / LoadFromAsset
//          surface that traps until Phase 2.
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

//=============================================================================
// Bootstrap + Current/System
//=============================================================================

static void test_bootstrap() {
    printf("Testing LocaleManager bootstrap:\n");

    // First call should register en-US and pick a sane current.
    void *cur = rt_locale_manager_current();
    test_result("Current() non-null after bootstrap", cur != nullptr);
    // Current is either the system locale (if loaded) or en-US fallback.
    // We can't assert a specific tag because the host env varies; instead,
    // verify IsLoaded(Current) is true — bootstrap must ensure this.
    test_result("IsLoaded(Current) == true", rt_locale_manager_is_loaded(cur) == 1);

    void *sys = rt_locale_manager_system();
    test_result("System() non-null", sys != nullptr);
}

//=============================================================================
// Available registry
//=============================================================================

static void test_available_includes_en_us() {
    printf("Testing LocaleManager.Available:\n");

    void *list = rt_locale_manager_available();
    test_result("Available returns non-null list", list != nullptr);

    extern int64_t rt_list_len(void *);
    extern void *rt_list_get(void *, int64_t);
    int64_t n = rt_list_len(list);
    test_result("Available length >= 1", n >= 1);

    // Scan the list for en-US presence.
    bool has_en_us = false;
    for (int64_t i = 0; i < n; ++i) {
        rt_string entry = (rt_string)rt_list_get(list, i);
        const char *cs = rt_string_cstr(entry);
        if (cs && strcmp(cs, "en-US") == 0)
            has_en_us = true;
    }
    test_result("Available includes en-US", has_en_us);
}

//=============================================================================
// IsLoaded
//=============================================================================

static void test_is_loaded() {
    printf("Testing LocaleManager.IsLoaded:\n");

    rt_string en_us = S("en-US");
    void *en = rt_locale_parse(en_us);
    rt_string_unref(en_us);
    test_result("IsLoaded(en-US) == true", rt_locale_manager_is_loaded(en) == 1);

    rt_string fr = S("fr-FR");
    void *loc_fr = rt_locale_try_parse(fr);
    rt_string_unref(fr);
    test_result("IsLoaded(fr-FR, unloaded) == false",
                rt_locale_manager_is_loaded(loc_fr) == 0);
}

//=============================================================================
// SetCurrent
//=============================================================================

static void test_set_current_to_loaded() {
    printf("Testing LocaleManager.SetCurrent:\n");

    rt_string en_us = S("en-US");
    void *en = rt_locale_parse(en_us);
    rt_string_unref(en_us);

    rt_locale_manager_set_current(en);
    void *cur = rt_locale_manager_current();
    test_result("SetCurrent(en-US) then Current() == en-US", tag_eq(cur, "en-US"));
}

static void test_set_current_to_unloaded_traps() {
    printf("Testing LocaleManager.SetCurrent trap path:\n");

    rt_string fr = S("fr-FR");
    void *loc_fr = rt_locale_try_parse(fr);
    rt_string_unref(fr);

    EXPECT_TRAP(rt_locale_manager_set_current(loc_fr));
    test_result("SetCurrent(unloaded) traps", true);
}

static void test_set_current_null_traps() {
    printf("Testing LocaleManager.SetCurrent(null) trap:\n");
    EXPECT_TRAP(rt_locale_manager_set_current(nullptr));
    test_result("SetCurrent(null) traps", true);
}

//=============================================================================
// LoadBuiltin
//=============================================================================

static void test_load_builtin_idempotent() {
    printf("Testing LocaleManager.LoadBuiltin:\n");

    rt_string tag = S("en-US");
    rt_locale_manager_load_builtin(tag);
    rt_string_unref(tag);

    // Calling again must not explode.
    rt_string tag2 = S("en-US");
    rt_locale_manager_load_builtin(tag2);
    rt_string_unref(tag2);

    test_result("LoadBuiltin(\"en-US\") idempotent", true);
}

static void test_load_builtin_unknown_traps() {
    printf("Testing LocaleManager.LoadBuiltin unknown:\n");

    rt_string tag = S("fr-FR");
    EXPECT_TRAP(rt_locale_manager_load_builtin(tag));
    rt_string_unref(tag);
    test_result("LoadBuiltin(\"fr-FR\") traps (not baked)", true);
}

//=============================================================================
// LoadFromJson / LoadFromAsset — Phase 1 stubs
//=============================================================================

static void test_load_from_json_stub_traps() {
    printf("Testing LocaleManager.LoadFromJson stub:\n");

    rt_string path = S("nonexistent.json");
    EXPECT_TRAP(rt_locale_manager_load_from_json(path));
    rt_string_unref(path);
    test_result("LoadFromJson(\"nonexistent.json\") traps (Phase 1 stub)", true);

    // Try* variant must NOT trap and must return 0.
    rt_string path2 = S("nonexistent.json");
    int8_t ok = rt_locale_manager_try_load_from_json(path2);
    rt_string_unref(path2);
    test_result("TryLoadFromJson returns 0 without trap", ok == 0);
}

static void test_load_from_asset_stub_traps() {
    printf("Testing LocaleManager.LoadFromAsset stub:\n");

    rt_string name = S("locales/fr-FR.json");
    EXPECT_TRAP(rt_locale_manager_load_from_asset(name));
    rt_string_unref(name);
    test_result("LoadFromAsset traps (Phase 1 stub)", true);

    rt_string name2 = S("locales/fr-FR.json");
    int8_t ok = rt_locale_manager_try_load_from_asset(name2);
    rt_string_unref(name2);
    test_result("TryLoadFromAsset returns 0 without trap", ok == 0);
}

//=============================================================================
// Search path
//=============================================================================

static void test_search_path_roundtrip() {
    printf("Testing LocaleManager search paths:\n");

    // Before adding, search path is empty.
    {
        rt_locale_manager_reset();
        rt_string sp = rt_locale_manager_search_path();
        const char *cs = rt_string_cstr(sp);
        test_result("SearchPath empty after Reset", cs && *cs == '\0');
        rt_string_unref(sp);
    }
    // Add one path.
    {
        rt_string p = S("/opt/viper/locales");
        rt_locale_manager_add_search_path(p);
        rt_string_unref(p);
        rt_string sp = rt_locale_manager_search_path();
        test_result("SearchPath contains added path",
                    strcmp(rt_string_cstr(sp), "/opt/viper/locales") == 0);
        rt_string_unref(sp);
    }
    // Add a second; separator should appear.
    {
        rt_string p = S("/home/user/.viper/locales");
        rt_locale_manager_add_search_path(p);
        rt_string_unref(p);
        rt_string sp = rt_locale_manager_search_path();
        const char *cs = rt_string_cstr(sp);
        // Don't hard-code the separator; just check both sub-paths appear.
        test_result("SearchPath contains first path",
                    strstr(cs, "/opt/viper/locales") != nullptr);
        test_result("SearchPath contains second path",
                    strstr(cs, "/home/user/.viper/locales") != nullptr);
        rt_string_unref(sp);
    }
    // Reset clears.
    {
        rt_locale_manager_reset();
        rt_string sp = rt_locale_manager_search_path();
        test_result("Reset clears SearchPath",
                    strcmp(rt_string_cstr(sp), "") == 0);
        rt_string_unref(sp);
    }
}

//=============================================================================
// Load high-level
//=============================================================================

static void test_load_high_level() {
    printf("Testing LocaleManager.Load:\n");

    rt_string en_tag = S("en-US");
    void *en = rt_locale_manager_load(en_tag);
    rt_string_unref(en_tag);
    test_result("Load(\"en-US\") returns a Locale", en != nullptr);
    test_result("Loaded en-US has canonical tag", tag_eq(en, "en-US"));

    // Unregistered locale returns NULL.
    rt_string fr_tag = S("fr-FR");
    void *fr = rt_locale_manager_load(fr_tag);
    rt_string_unref(fr_tag);
    test_result("Load(\"fr-FR\") returns NULL (not registered)", fr == nullptr);
}

//=============================================================================
// Unload refusal
//=============================================================================

static void test_unload_current_refused() {
    printf("Testing LocaleManager.Unload refusal:\n");

    void *cur = rt_locale_manager_current();
    int8_t res = rt_locale_manager_unload(cur);
    test_result("Unload(current) returns 0", res == 0);

    // Unload a baked entry (en-US) should also return 0.
    rt_string en_tag = S("en-US");
    void *en = rt_locale_parse(en_tag);
    rt_string_unref(en_tag);
    int8_t res2 = rt_locale_manager_unload(en);
    test_result("Unload(baked en-US) returns 0", res2 == 0);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT LocaleManager Tests ===\n\n");
    test_bootstrap();
    test_available_includes_en_us();
    test_is_loaded();
    test_set_current_to_loaded();
    test_set_current_to_unloaded_traps();
    test_set_current_null_traps();
    test_load_builtin_idempotent();
    test_load_builtin_unknown_traps();
    test_load_from_json_stub_traps();
    test_load_from_asset_stub_traps();
    test_search_path_roundtrip();
    test_load_high_level();
    test_unload_current_refused();
    printf("\nAll LocaleManager tests passed!\n");
    return 0;
}
