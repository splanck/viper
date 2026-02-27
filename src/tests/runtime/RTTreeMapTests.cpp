//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTTreeMapTests.cpp
// Purpose: Tests for Viper.Collections.TreeMap sorted key-value storage.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_treemap.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

static const char *str_cstr(rt_string s)
{
    return rt_string_cstr(s);
}

/// Create a simple test object with 8 bytes payload
static void *new_test_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

// ============================================================================
// Basic Creation Tests
// ============================================================================

static void test_new_treemap()
{
    void *tm = rt_treemap_new();
    assert(tm != nullptr);
    assert(rt_treemap_len(tm) == 0);
    assert(rt_treemap_is_empty(tm) == 1);

    printf("test_new_treemap: PASSED\n");
}

// ============================================================================
// Set/Get/Has Tests
// ============================================================================

static void test_set_get()
{
    void *tm = rt_treemap_new();

    // Create some test values
    void *val1 = new_test_obj();
    void *val2 = new_test_obj();
    void *val3 = new_test_obj();

    // Set values
    rt_treemap_set(tm, make_str("banana"), val1);
    rt_treemap_set(tm, make_str("apple"), val2);
    rt_treemap_set(tm, make_str("cherry"), val3);

    assert(rt_treemap_len(tm) == 3);
    assert(rt_treemap_is_empty(tm) == 0);

    // Get values
    void *got1 = rt_treemap_get(tm, make_str("banana"));
    void *got2 = rt_treemap_get(tm, make_str("apple"));
    void *got3 = rt_treemap_get(tm, make_str("cherry"));
    void *got4 = rt_treemap_get(tm, make_str("durian")); // not found

    assert(got1 == val1);
    assert(got2 == val2);
    assert(got3 == val3);
    assert(got4 == nullptr);

    printf("test_set_get: PASSED\n");
}

static void test_has()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("key1"), new_test_obj());
    rt_treemap_set(tm, make_str("key2"), new_test_obj());

    assert(rt_treemap_has(tm, make_str("key1")) == 1);
    assert(rt_treemap_has(tm, make_str("key2")) == 1);
    assert(rt_treemap_has(tm, make_str("key3")) == 0);

    printf("test_has: PASSED\n");
}

static void test_update()
{
    void *tm = rt_treemap_new();

    void *val1 = new_test_obj();
    void *val2 = new_test_obj();

    rt_treemap_set(tm, make_str("key"), val1);
    assert(rt_treemap_get(tm, make_str("key")) == val1);
    assert(rt_treemap_len(tm) == 1);

    // Update same key
    rt_treemap_set(tm, make_str("key"), val2);
    assert(rt_treemap_get(tm, make_str("key")) == val2);
    assert(rt_treemap_len(tm) == 1); // Still 1

    printf("test_update: PASSED\n");
}

// ============================================================================
// Drop/Clear Tests
// ============================================================================

static void test_drop()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("a"), new_test_obj());
    rt_treemap_set(tm, make_str("b"), new_test_obj());
    rt_treemap_set(tm, make_str("c"), new_test_obj());

    assert(rt_treemap_len(tm) == 3);

    // Remove existing key
    assert(rt_treemap_remove(tm, make_str("b")) == 1);
    assert(rt_treemap_len(tm) == 2);
    assert(rt_treemap_has(tm, make_str("b")) == 0);

    // Remove non-existing key
    assert(rt_treemap_remove(tm, make_str("x")) == 0);
    assert(rt_treemap_len(tm) == 2);

    printf("test_remove: PASSED\n");
}

static void test_clear()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("a"), new_test_obj());
    rt_treemap_set(tm, make_str("b"), new_test_obj());
    rt_treemap_set(tm, make_str("c"), new_test_obj());

    assert(rt_treemap_len(tm) == 3);

    rt_treemap_clear(tm);

    assert(rt_treemap_len(tm) == 0);
    assert(rt_treemap_is_empty(tm) == 1);

    printf("test_clear: PASSED\n");
}

// ============================================================================
// Keys/Values Tests (sorted order)
// ============================================================================

static void test_keys_sorted()
{
    void *tm = rt_treemap_new();

    // Insert in non-sorted order
    rt_treemap_set(tm, make_str("cherry"), new_test_obj());
    rt_treemap_set(tm, make_str("apple"), new_test_obj());
    rt_treemap_set(tm, make_str("banana"), new_test_obj());
    rt_treemap_set(tm, make_str("date"), new_test_obj());

    void *keys = rt_treemap_keys(tm);
    assert(rt_seq_len(keys) == 4);

    // Keys should be in sorted order
    // Keys() pushes rt_string directly to Seq
    rt_string k0 = (rt_string)rt_seq_get(keys, 0);
    rt_string k1 = (rt_string)rt_seq_get(keys, 1);
    rt_string k2 = (rt_string)rt_seq_get(keys, 2);
    rt_string k3 = (rt_string)rt_seq_get(keys, 3);

    assert(strcmp(str_cstr(k0), "apple") == 0);
    assert(strcmp(str_cstr(k1), "banana") == 0);
    assert(strcmp(str_cstr(k2), "cherry") == 0);
    assert(strcmp(str_cstr(k3), "date") == 0);

    printf("test_keys_sorted: PASSED\n");
}

static void test_values_sorted()
{
    void *tm = rt_treemap_new();

    // Insert with known values to track order
    void *valA = new_test_obj();
    void *valB = new_test_obj();
    void *valC = new_test_obj();

    // Insert in non-sorted key order
    rt_treemap_set(tm, make_str("cherry"), valC);
    rt_treemap_set(tm, make_str("apple"), valA);
    rt_treemap_set(tm, make_str("banana"), valB);

    void *values = rt_treemap_values(tm);
    assert(rt_seq_len(values) == 3);

    // Values should be in key-sorted order: apple, banana, cherry
    void *v0 = rt_seq_get(values, 0);
    void *v1 = rt_seq_get(values, 1);
    void *v2 = rt_seq_get(values, 2);

    assert(v0 == valA);
    assert(v1 == valB);
    assert(v2 == valC);

    printf("test_values_sorted: PASSED\n");
}

// ============================================================================
// First/Last Tests
// ============================================================================

static void test_first_last()
{
    void *tm = rt_treemap_new();

    // Empty map
    rt_string first_empty = rt_treemap_first(tm);
    rt_string last_empty = rt_treemap_last(tm);
    assert(strcmp(str_cstr(first_empty), "") == 0);
    assert(strcmp(str_cstr(last_empty), "") == 0);

    // Add entries
    rt_treemap_set(tm, make_str("cherry"), new_test_obj());
    rt_treemap_set(tm, make_str("apple"), new_test_obj());
    rt_treemap_set(tm, make_str("banana"), new_test_obj());

    rt_string first = rt_treemap_first(tm);
    rt_string last = rt_treemap_last(tm);

    assert(strcmp(str_cstr(first), "apple") == 0);
    assert(strcmp(str_cstr(last), "cherry") == 0);

    printf("test_first_last: PASSED\n");
}

// ============================================================================
// Floor/Ceil Tests
// ============================================================================

static void test_floor()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("apple"), new_test_obj());
    rt_treemap_set(tm, make_str("cherry"), new_test_obj());
    rt_treemap_set(tm, make_str("elderberry"), new_test_obj());

    // Exact match
    rt_string f1 = rt_treemap_floor(tm, make_str("cherry"));
    assert(strcmp(str_cstr(f1), "cherry") == 0);

    // Between keys - should get lower key
    rt_string f2 = rt_treemap_floor(tm, make_str("banana"));
    assert(strcmp(str_cstr(f2), "apple") == 0);

    rt_string f3 = rt_treemap_floor(tm, make_str("date"));
    assert(strcmp(str_cstr(f3), "cherry") == 0);

    // Higher than all keys
    rt_string f4 = rt_treemap_floor(tm, make_str("zebra"));
    assert(strcmp(str_cstr(f4), "elderberry") == 0);

    // Lower than all keys - no floor
    rt_string f5 = rt_treemap_floor(tm, make_str("aardvark"));
    assert(strcmp(str_cstr(f5), "") == 0);

    printf("test_floor: PASSED\n");
}

static void test_ceil()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("apple"), new_test_obj());
    rt_treemap_set(tm, make_str("cherry"), new_test_obj());
    rt_treemap_set(tm, make_str("elderberry"), new_test_obj());

    // Exact match
    rt_string c1 = rt_treemap_ceil(tm, make_str("cherry"));
    assert(strcmp(str_cstr(c1), "cherry") == 0);

    // Between keys - should get higher key
    rt_string c2 = rt_treemap_ceil(tm, make_str("banana"));
    assert(strcmp(str_cstr(c2), "cherry") == 0);

    rt_string c3 = rt_treemap_ceil(tm, make_str("date"));
    assert(strcmp(str_cstr(c3), "elderberry") == 0);

    // Lower than all keys - should get first key
    rt_string c4 = rt_treemap_ceil(tm, make_str("aardvark"));
    assert(strcmp(str_cstr(c4), "apple") == 0);

    // Higher than all keys - no ceiling
    rt_string c5 = rt_treemap_ceil(tm, make_str("zebra"));
    assert(strcmp(str_cstr(c5), "") == 0);

    printf("test_ceil: PASSED\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_empty_key()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str(""), new_test_obj());
    assert(rt_treemap_has(tm, make_str("")) == 1);
    assert(rt_treemap_len(tm) == 1);

    printf("test_empty_key: PASSED\n");
}

static void test_null_value()
{
    void *tm = rt_treemap_new();

    rt_treemap_set(tm, make_str("key"), nullptr);
    assert(rt_treemap_has(tm, make_str("key")) == 1);
    assert(rt_treemap_get(tm, make_str("key")) == nullptr);

    printf("test_null_value: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Collections.TreeMap Tests ===\n\n");

    // Basic creation
    test_new_treemap();

    // Set/Get/Has
    test_set_get();
    test_has();
    test_update();

    // Drop/Clear
    test_drop();
    test_clear();

    // Keys/Values (sorted)
    test_keys_sorted();
    test_values_sorted();

    // First/Last
    test_first_last();

    // Floor/Ceil
    test_floor();
    test_ceil();

    // Edge cases
    test_empty_key();
    test_null_value();

    printf("\nAll RTTreeMapTests passed!\n");
    return 0;
}
