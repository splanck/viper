//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMapTests.cpp
// Purpose: Tests for Zanna.Collections.Map runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
static int g_finalizer_calls = 0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

static void rt_release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void count_finalizer(void *) {
    ++g_finalizer_calls;
}

static rt_string make_key(const char *text) {
    assert(text != nullptr);
    return rt_string_from_bytes(text, strlen(text));
}

static rt_string make_key_bytes(const char *text, size_t len) {
    return rt_string_from_bytes(text, len);
}

static void test_remove_frees_last_reference_without_invalid_free() {
    void *map = rt_map_new();
    assert(map != nullptr);

    rt_string key = make_key("k");
    void *value = new_obj();

    rt_map_set(map, key, value);
    // Drop the creator reference; map now owns the single remaining ref.
    rt_release_obj(value);

    assert(rt_map_len(map) == 1);
    assert(rt_map_remove(map, key) == 1);
    assert(rt_map_len(map) == 0);

    rt_string_unref(key);
    rt_release_obj(map);
}

static void test_overwrite_frees_old_last_reference_without_invalid_free() {
    void *map = rt_map_new();
    assert(map != nullptr);

    rt_string key = make_key("k");
    void *value1 = new_obj();
    void *value2 = new_obj();

    rt_map_set(map, key, value1);
    rt_release_obj(value1);

    // Overwrite should release+free the old value when it was the last reference.
    rt_map_set(map, key, value2);
    rt_release_obj(value2);

    assert(rt_map_len(map) == 1);
    assert(rt_map_remove(map, key) == 1);
    assert(rt_map_len(map) == 0);

    rt_string_unref(key);
    rt_release_obj(map);
}

static void test_free_runs_map_finalizer_and_releases_values() {
    void *map = rt_map_new();
    assert(map != nullptr);

    rt_string key = make_key("k");
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_map_set(map, key, value);
    rt_release_obj(value); // map now owns the only remaining ref
    assert(g_finalizer_calls == 0);

    rt_string_unref(key);
    rt_release_obj(map);
    assert(g_finalizer_calls == 1);
}

static void test_embedded_nul_keys_are_distinct() {
    void *map = rt_map_new();
    assert(map != nullptr);

    const char bytes[] = {'a', '\0', 'b'};
    rt_string k1 = make_key_bytes(bytes, sizeof(bytes));
    rt_string k2 = make_key("a");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);

    assert(rt_map_len(map) == 2);
    assert(rt_map_get(map, k1) == v1);
    assert(rt_map_get(map, k2) == v2);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(map);
}

int main() {
    test_remove_frees_last_reference_without_invalid_free();
    test_overwrite_frees_old_last_reference_without_invalid_free();
    test_free_runs_map_finalizer_and_releases_values();
    test_embedded_nul_keys_are_distinct();
    return 0;
}
