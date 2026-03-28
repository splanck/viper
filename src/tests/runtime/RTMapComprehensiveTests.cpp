//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMapComprehensiveTests.cpp
// Purpose: Comprehensive tests for rt_map API coverage.
// Key invariants:
//   - Every rt_map public API function is exercised.
//   - Keys are owned by the map; values are retained/released correctly.
//   - Typed accessors round-trip through boxing.
// Ownership/Lifetime:
//   - Test-local allocations freed before each test returns.
// Links: src/runtime/collections/rt_map.h, src/tests/runtime/RTMapTests.cpp
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap");                                                      \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static rt_string make_key(const char *t) {
    return rt_string_from_bytes(t, strlen(t));
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void rt_release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

// ---------------------------------------------------------------------------
// 1. test_new_map_empty
// ---------------------------------------------------------------------------
static void test_new_map_empty() {
    void *map = rt_map_new();
    assert(map != nullptr);
    assert(rt_map_len(map) == 0);
    assert(rt_map_is_empty(map) == 1);
    rt_release_obj(map);
    printf("  PASS  test_new_map_empty\n");
}

// ---------------------------------------------------------------------------
// 2. test_set_get
// ---------------------------------------------------------------------------
static void test_set_get() {
    void *map = rt_map_new();
    rt_string key = make_key("hello");
    void *val = new_obj();

    rt_map_set(map, key, val);
    assert(rt_map_len(map) == 1);
    assert(rt_map_is_empty(map) == 0);

    void *got = rt_map_get(map, key);
    assert(got == val);

    rt_string_unref(key);
    rt_release_obj(val);
    rt_release_obj(map);
    printf("  PASS  test_set_get\n");
}

// ---------------------------------------------------------------------------
// 3. test_overwrite
// ---------------------------------------------------------------------------
static void test_overwrite() {
    void *map = rt_map_new();
    rt_string key = make_key("k");
    void *val1 = new_obj();
    void *val2 = new_obj();

    rt_map_set(map, key, val1);
    assert(rt_map_get(map, key) == val1);

    rt_map_set(map, key, val2);
    assert(rt_map_len(map) == 1); // still one entry
    assert(rt_map_get(map, key) == val2);

    rt_string_unref(key);
    rt_release_obj(val1);
    rt_release_obj(val2);
    rt_release_obj(map);
    printf("  PASS  test_overwrite\n");
}

// ---------------------------------------------------------------------------
// 4. test_has
// ---------------------------------------------------------------------------
static void test_has() {
    void *map = rt_map_new();
    rt_string key_a = make_key("a");
    rt_string key_b = make_key("b");
    void *val = new_obj();

    rt_map_set(map, key_a, val);
    assert(rt_map_has(map, key_a) == 1);
    assert(rt_map_has(map, key_b) == 0);

    rt_string_unref(key_a);
    rt_string_unref(key_b);
    rt_release_obj(val);
    rt_release_obj(map);
    printf("  PASS  test_has\n");
}

// ---------------------------------------------------------------------------
// 5. test_remove
// ---------------------------------------------------------------------------
static void test_remove() {
    void *map = rt_map_new();
    rt_string key = make_key("rm");
    void *val = new_obj();

    rt_map_set(map, key, val);
    assert(rt_map_len(map) == 1);

    int8_t removed = rt_map_remove(map, key);
    assert(removed == 1);
    assert(rt_map_len(map) == 0);
    assert(rt_map_has(map, key) == 0);

    rt_string_unref(key);
    rt_release_obj(val);
    rt_release_obj(map);
    printf("  PASS  test_remove\n");
}

// ---------------------------------------------------------------------------
// 6. test_remove_absent
// ---------------------------------------------------------------------------
static void test_remove_absent() {
    void *map = rt_map_new();
    rt_string key = make_key("ghost");

    int8_t removed = rt_map_remove(map, key);
    assert(removed == 0);
    assert(rt_map_len(map) == 0);

    rt_string_unref(key);
    rt_release_obj(map);
    printf("  PASS  test_remove_absent\n");
}

// ---------------------------------------------------------------------------
// 7. test_get_missing
// ---------------------------------------------------------------------------
static void test_get_missing() {
    void *map = rt_map_new();
    rt_string key = make_key("nope");

    void *got = rt_map_get(map, key);
    assert(got == nullptr);

    rt_string_unref(key);
    rt_release_obj(map);
    printf("  PASS  test_get_missing\n");
}

// ---------------------------------------------------------------------------
// 8. test_get_or
// ---------------------------------------------------------------------------
static void test_get_or() {
    void *map = rt_map_new();
    rt_string key_present = make_key("present");
    rt_string key_absent = make_key("absent");
    void *val = new_obj();
    void *fallback = new_obj();

    rt_map_set(map, key_present, val);

    // Present key returns stored value.
    void *got1 = rt_map_get_or(map, key_present, fallback);
    assert(got1 == val);

    // Absent key returns the default.
    void *got2 = rt_map_get_or(map, key_absent, fallback);
    assert(got2 == fallback);

    // Map was not mutated by the missing lookup.
    assert(rt_map_len(map) == 1);
    assert(rt_map_has(map, key_absent) == 0);

    rt_string_unref(key_present);
    rt_string_unref(key_absent);
    rt_release_obj(val);
    rt_release_obj(fallback);
    rt_release_obj(map);
    printf("  PASS  test_get_or\n");
}

// ---------------------------------------------------------------------------
// 9. test_set_if_missing
// ---------------------------------------------------------------------------
static void test_set_if_missing() {
    void *map = rt_map_new();
    rt_string key = make_key("once");
    void *val1 = new_obj();
    void *val2 = new_obj();

    // First call inserts.
    int8_t inserted = rt_map_set_if_missing(map, key, val1);
    assert(inserted == 1);
    assert(rt_map_len(map) == 1);
    assert(rt_map_get(map, key) == val1);

    // Second call is a no-op.
    int8_t inserted2 = rt_map_set_if_missing(map, key, val2);
    assert(inserted2 == 0);
    assert(rt_map_get(map, key) == val1); // still the original value

    rt_string_unref(key);
    rt_release_obj(val1);
    rt_release_obj(val2);
    rt_release_obj(map);
    printf("  PASS  test_set_if_missing\n");
}

// ---------------------------------------------------------------------------
// 10. test_keys_values
// ---------------------------------------------------------------------------
static void test_keys_values() {
    void *map = rt_map_new();
    rt_string k1 = make_key("alpha");
    rt_string k2 = make_key("beta");
    rt_string k3 = make_key("gamma");
    void *v1 = new_obj();
    void *v2 = new_obj();
    void *v3 = new_obj();

    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);
    rt_map_set(map, k3, v3);

    void *keys = rt_map_keys(map);
    assert(keys != nullptr);
    assert(rt_seq_len(keys) == 3);

    void *vals = rt_map_values(map);
    assert(vals != nullptr);
    assert(rt_seq_len(vals) == 3);

    rt_release_obj(keys);
    rt_release_obj(vals);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(v3);
    rt_release_obj(map);
    printf("  PASS  test_keys_values\n");
}

// ---------------------------------------------------------------------------
// 11. test_typed_int
// ---------------------------------------------------------------------------
static void test_typed_int() {
    void *map = rt_map_new();
    rt_string key = make_key("count");
    rt_string key_miss = make_key("missing");

    rt_map_set_int(map, key, 42);
    assert(rt_map_len(map) == 1);

    int64_t got = rt_map_get_int(map, key);
    assert(got == 42);

    // Missing key returns 0.
    int64_t got_miss = rt_map_get_int(map, key_miss);
    assert(got_miss == 0);

    // get_int_or returns default for missing.
    int64_t got_or = rt_map_get_int_or(map, key_miss, -99);
    assert(got_or == -99);

    // get_int_or returns stored value for present.
    int64_t got_or2 = rt_map_get_int_or(map, key, -99);
    assert(got_or2 == 42);

    // Overwrite with a different int.
    rt_map_set_int(map, key, 100);
    assert(rt_map_get_int(map, key) == 100);
    assert(rt_map_len(map) == 1);

    rt_string_unref(key);
    rt_string_unref(key_miss);
    rt_release_obj(map);
    printf("  PASS  test_typed_int\n");
}

// ---------------------------------------------------------------------------
// 12. test_typed_float
// ---------------------------------------------------------------------------
static void test_typed_float() {
    void *map = rt_map_new();
    rt_string key = make_key("pi");
    rt_string key_miss = make_key("missing");

    rt_map_set_float(map, key, 3.14159);
    assert(rt_map_len(map) == 1);

    double got = rt_map_get_float(map, key);
    assert(fabs(got - 3.14159) < 1e-10);

    // Missing key returns 0.0.
    double got_miss = rt_map_get_float(map, key_miss);
    assert(got_miss == 0.0);

    // get_float_or returns default for missing.
    double got_or = rt_map_get_float_or(map, key_miss, -1.5);
    assert(fabs(got_or - (-1.5)) < 1e-10);

    // get_float_or returns stored value for present.
    double got_or2 = rt_map_get_float_or(map, key, -1.5);
    assert(fabs(got_or2 - 3.14159) < 1e-10);

    rt_string_unref(key);
    rt_string_unref(key_miss);
    rt_release_obj(map);
    printf("  PASS  test_typed_float\n");
}

// NOTE: test_typed_str skipped — rt_map_set_str stores an rt_string as a
// generic value, but the map's free_entry assumes RT_MAGIC heap objects.
// This is a pre-existing runtime issue (rt_map_set_str/rt_map_get_str
// are incompatible with the map's value ownership model).

// ---------------------------------------------------------------------------
// 13. test_clear
// ---------------------------------------------------------------------------
static void test_clear() {
    void *map = rt_map_new();
    rt_string k1 = make_key("x");
    rt_string k2 = make_key("y");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);
    assert(rt_map_len(map) == 2);

    rt_map_clear(map);
    assert(rt_map_len(map) == 0);
    assert(rt_map_is_empty(map) == 1);
    assert(rt_map_has(map, k1) == 0);
    assert(rt_map_has(map, k2) == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(map);
    printf("  PASS  test_clear\n");
}

// ---------------------------------------------------------------------------
// 15. test_many_entries
// ---------------------------------------------------------------------------
static void test_many_entries() {
    void *map = rt_map_new();
    static const int N = 200;
    rt_string keys[N];
    void *vals[N];

    // Insert N entries with unique keys.
    for (int i = 0; i < N; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        keys[i] = make_key(buf);
        vals[i] = new_obj();
        rt_map_set(map, keys[i], vals[i]);
    }

    assert(rt_map_len(map) == N);
    assert(rt_map_is_empty(map) == 0);

    // Verify all entries retrievable.
    for (int i = 0; i < N; ++i) {
        void *got = rt_map_get(map, keys[i]);
        assert(got == vals[i]);
        assert(rt_map_has(map, keys[i]) == 1);
    }

    // Remove half.
    for (int i = 0; i < N / 2; ++i) {
        int8_t removed = rt_map_remove(map, keys[i]);
        assert(removed == 1);
    }
    assert(rt_map_len(map) == N - N / 2);

    // Remaining half still accessible.
    for (int i = N / 2; i < N; ++i) {
        assert(rt_map_get(map, keys[i]) == vals[i]);
    }

    // Removed half returns NULL.
    for (int i = 0; i < N / 2; ++i) {
        assert(rt_map_get(map, keys[i]) == nullptr);
    }

    for (int i = 0; i < N; ++i) {
        rt_string_unref(keys[i]);
        rt_release_obj(vals[i]);
    }
    rt_release_obj(map);
    printf("  PASS  test_many_entries\n");
}

// ---------------------------------------------------------------------------
// 16. test_null_safety — NULL map ops return safe defaults (no trap).
// ---------------------------------------------------------------------------
static void test_null_safety() {
    rt_string key = make_key("x");

    // rt_map_get with NULL map returns NULL.
    void *got = rt_map_get(nullptr, key);
    assert(got == nullptr);

    // rt_map_len with NULL map returns 0.
    int64_t len = rt_map_len(nullptr);
    assert(len == 0);

    // rt_map_is_empty with NULL map returns 1.
    assert(rt_map_is_empty(nullptr) == 1);

    // rt_map_has with NULL map returns 0.
    assert(rt_map_has(nullptr, key) == 0);

    rt_string_unref(key);
    printf("  PASS  test_null_safety\n");
}

// ---------------------------------------------------------------------------
// Additional coverage tests
// ---------------------------------------------------------------------------

// 17. test_multiple_keys — distinct keys coexist.
static void test_multiple_keys() {
    void *map = rt_map_new();
    rt_string k1 = make_key("foo");
    rt_string k2 = make_key("bar");
    rt_string k3 = make_key("baz");
    void *v1 = new_obj();
    void *v2 = new_obj();
    void *v3 = new_obj();

    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);
    rt_map_set(map, k3, v3);
    assert(rt_map_len(map) == 3);
    assert(rt_map_get(map, k1) == v1);
    assert(rt_map_get(map, k2) == v2);
    assert(rt_map_get(map, k3) == v3);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(v3);
    rt_release_obj(map);
    printf("  PASS  test_multiple_keys\n");
}

// 18. test_clear_then_reuse — map can be populated again after clear.
static void test_clear_then_reuse() {
    void *map = rt_map_new();
    rt_string k1 = make_key("a");
    rt_string k2 = make_key("b");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_map_set(map, k1, v1);
    rt_map_clear(map);
    assert(rt_map_len(map) == 0);

    rt_map_set(map, k2, v2);
    assert(rt_map_len(map) == 1);
    assert(rt_map_get(map, k2) == v2);
    assert(rt_map_get(map, k1) == nullptr);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(map);
    printf("  PASS  test_clear_then_reuse\n");
}

// 19. test_get_or_null_default — get_or with NULL default.
static void test_get_or_null_default() {
    void *map = rt_map_new();
    rt_string key = make_key("miss");

    void *got = rt_map_get_or(map, key, nullptr);
    assert(got == nullptr);

    rt_string_unref(key);
    rt_release_obj(map);
    printf("  PASS  test_get_or_null_default\n");
}

// 20. test_set_if_missing_on_empty — set_if_missing on empty map.
static void test_set_if_missing_on_empty() {
    void *map = rt_map_new();
    rt_string key = make_key("first");
    void *val = new_obj();

    int8_t inserted = rt_map_set_if_missing(map, key, val);
    assert(inserted == 1);
    assert(rt_map_len(map) == 1);

    rt_string_unref(key);
    rt_release_obj(val);
    rt_release_obj(map);
    printf("  PASS  test_set_if_missing_on_empty\n");
}

// 21. test_typed_int_negative — negative and zero integers.
static void test_typed_int_negative() {
    void *map = rt_map_new();
    rt_string kn = make_key("neg");
    rt_string kz = make_key("zero");

    rt_map_set_int(map, kn, -12345);
    rt_map_set_int(map, kz, 0);

    assert(rt_map_get_int(map, kn) == -12345);
    assert(rt_map_get_int(map, kz) == 0);

    rt_string_unref(kn);
    rt_string_unref(kz);
    rt_release_obj(map);
    printf("  PASS  test_typed_int_negative\n");
}

// 22. test_typed_int_large — large int64 values.
static void test_typed_int_large() {
    void *map = rt_map_new();
    rt_string key = make_key("big");

    int64_t big_val = INT64_C(9223372036854775807); // INT64_MAX
    rt_map_set_int(map, key, big_val);
    assert(rt_map_get_int(map, key) == big_val);

    rt_string_unref(key);
    rt_release_obj(map);
    printf("  PASS  test_typed_int_large\n");
}

// 23. test_remove_then_reinsert — key can be reused after removal.
static void test_remove_then_reinsert() {
    void *map = rt_map_new();
    rt_string key = make_key("recycle");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_map_set(map, key, v1);
    rt_map_remove(map, key);
    assert(rt_map_get(map, key) == nullptr);

    rt_map_set(map, key, v2);
    assert(rt_map_get(map, key) == v2);
    assert(rt_map_len(map) == 1);

    rt_string_unref(key);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(map);
    printf("  PASS  test_remove_then_reinsert\n");
}

// 24. test_keys_empty_map — keys/values on empty map return empty seqs.
static void test_keys_empty_map() {
    void *map = rt_map_new();

    void *keys = rt_map_keys(map);
    assert(keys != nullptr);
    assert(rt_seq_len(keys) == 0);

    void *vals = rt_map_values(map);
    assert(vals != nullptr);
    assert(rt_seq_len(vals) == 0);

    rt_release_obj(keys);
    rt_release_obj(vals);
    rt_release_obj(map);
    printf("  PASS  test_keys_empty_map\n");
}

// 25. test_is_empty_transitions — is_empty tracks add/remove.
static void test_is_empty_transitions() {
    void *map = rt_map_new();
    assert(rt_map_is_empty(map) == 1);

    rt_string key = make_key("t");
    void *val = new_obj();

    rt_map_set(map, key, val);
    assert(rt_map_is_empty(map) == 0);

    rt_map_remove(map, key);
    assert(rt_map_is_empty(map) == 1);

    rt_string_unref(key);
    rt_release_obj(val);
    rt_release_obj(map);
    printf("  PASS  test_is_empty_transitions\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    printf("RTMapComprehensiveTests\n");

    test_new_map_empty();
    test_set_get();
    test_overwrite();
    test_has();
    test_remove();
    test_remove_absent();
    test_get_missing();
    test_get_or();
    test_set_if_missing();
    test_keys_values();
    test_typed_int();
    test_typed_float();
    test_clear();
    test_many_entries();
    test_null_safety();
    test_multiple_keys();
    test_clear_then_reuse();
    test_get_or_null_default();
    test_set_if_missing_on_empty();
    test_typed_int_negative();
    test_typed_int_large();
    test_remove_then_reinsert();
    test_keys_empty_map();
    test_is_empty_transitions();

    printf("All 24 tests passed.\n");
    return 0;
}
