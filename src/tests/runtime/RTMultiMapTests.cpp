//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMultiMapTests.cpp
// Purpose: Tests for Viper.Collections.MultiMap runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_multimap.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg) { rt_abort(msg); }

static void rt_release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static rt_string make_key(const char *text)
{
    return rt_string_from_bytes(text, strlen(text));
}

static void test_new()
{
    void *mm = rt_multimap_new();
    assert(mm != nullptr);
    assert(rt_multimap_len(mm) == 0);
    assert(rt_multimap_key_count(mm) == 0);
    assert(rt_multimap_is_empty(mm) == 1);
    rt_release_obj(mm);
}

static void test_put_and_get()
{
    void *mm = rt_multimap_new();
    rt_string k = make_key("color");
    void *v1 = new_obj();
    void *v2 = new_obj();
    void *v3 = new_obj();

    rt_multimap_put(mm, k, v1);
    rt_multimap_put(mm, k, v2);
    rt_multimap_put(mm, k, v3);

    assert(rt_multimap_len(mm) == 3);
    assert(rt_multimap_key_count(mm) == 1);
    assert(rt_multimap_has(mm, k) == 1);
    assert(rt_multimap_count_for(mm, k) == 3);

    void *vals = rt_multimap_get(mm, k);
    assert(rt_seq_len(vals) == 3);
    assert(rt_seq_get(vals, 0) == v1);
    assert(rt_seq_get(vals, 1) == v2);
    assert(rt_seq_get(vals, 2) == v3);

    assert(rt_multimap_get_first(mm, k) == v1);

    rt_release_obj(vals);
    rt_string_unref(k);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(v3);
    rt_release_obj(mm);
}

static void test_multiple_keys()
{
    void *mm = rt_multimap_new();
    rt_string k1 = make_key("fruit");
    rt_string k2 = make_key("veggie");
    void *v1 = new_obj();
    void *v2 = new_obj();
    void *v3 = new_obj();

    rt_multimap_put(mm, k1, v1);
    rt_multimap_put(mm, k1, v2);
    rt_multimap_put(mm, k2, v3);

    assert(rt_multimap_len(mm) == 3);
    assert(rt_multimap_key_count(mm) == 2);
    assert(rt_multimap_count_for(mm, k1) == 2);
    assert(rt_multimap_count_for(mm, k2) == 1);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(v3);
    rt_release_obj(mm);
}

static void test_remove_all()
{
    void *mm = rt_multimap_new();
    rt_string k = make_key("key");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_multimap_put(mm, k, v1);
    rt_multimap_put(mm, k, v2);
    assert(rt_multimap_len(mm) == 2);

    assert(rt_multimap_remove_all(mm, k) == 1);
    assert(rt_multimap_len(mm) == 0);
    assert(rt_multimap_key_count(mm) == 0);
    assert(rt_multimap_has(mm, k) == 0);

    // Remove non-existent
    assert(rt_multimap_remove_all(mm, k) == 0);

    rt_string_unref(k);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(mm);
}

static void test_clear()
{
    void *mm = rt_multimap_new();
    rt_string k1 = make_key("a");
    rt_string k2 = make_key("b");
    void *v = new_obj();

    rt_multimap_put(mm, k1, v);
    rt_multimap_put(mm, k2, v);
    rt_multimap_clear(mm);

    assert(rt_multimap_len(mm) == 0);
    assert(rt_multimap_key_count(mm) == 0);
    assert(rt_multimap_is_empty(mm) == 1);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v);
    rt_release_obj(mm);
}

static void test_keys()
{
    void *mm = rt_multimap_new();
    rt_string k1 = make_key("x");
    rt_string k2 = make_key("y");
    void *v = new_obj();

    rt_multimap_put(mm, k1, v);
    rt_multimap_put(mm, k2, v);

    void *keys = rt_multimap_keys(mm);
    assert(rt_seq_len(keys) == 2);

    rt_release_obj(keys);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v);
    rt_release_obj(mm);
}

static void test_get_missing_returns_empty_seq()
{
    void *mm = rt_multimap_new();
    rt_string k = make_key("missing");
    void *vals = rt_multimap_get(mm, k);
    assert(rt_seq_len(vals) == 0);
    assert(rt_multimap_get_first(mm, k) == NULL);

    rt_release_obj(vals);
    rt_string_unref(k);
    rt_release_obj(mm);
}

static void test_null_safety()
{
    rt_string k = make_key("test");
    assert(rt_multimap_len(NULL) == 0);
    assert(rt_multimap_key_count(NULL) == 0);
    assert(rt_multimap_is_empty(NULL) == 1);
    assert(rt_multimap_has(NULL, k) == 0);
    assert(rt_multimap_count_for(NULL, k) == 0);
    assert(rt_multimap_get_first(NULL, k) == NULL);
    assert(rt_multimap_remove_all(NULL, k) == 0);
    rt_multimap_put(NULL, k, NULL);
    rt_multimap_clear(NULL);
    rt_string_unref(k);
}

int main()
{
    test_new();
    test_put_and_get();
    test_multiple_keys();
    test_remove_all();
    test_clear();
    test_keys();
    test_get_missing_returns_empty_seq();
    test_null_safety();
    return 0;
}
