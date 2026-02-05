//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_orderedmap.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
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
    if (!s) return false;
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

static void test_new_empty()
{
    void *m = rt_orderedmap_new();
    assert(m != NULL);
    assert(rt_orderedmap_len(m) == 0);
    assert(rt_orderedmap_is_empty(m) == 1);
}

static void test_set_and_get()
{
    void *m = rt_orderedmap_new();
    rt_string k = make_str("key1");
    rt_string v = make_str("value1");

    rt_orderedmap_set(m, k, v);
    assert(rt_orderedmap_len(m) == 1);
    assert(rt_orderedmap_is_empty(m) == 0);

    void *got = rt_orderedmap_get(m, k);
    assert(got == v);

    rt_string_unref(k);
}

static void test_overwrite()
{
    void *m = rt_orderedmap_new();
    rt_string k = make_str("key");
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_orderedmap_set(m, k, v1);
    rt_orderedmap_set(m, k, v2);

    assert(rt_orderedmap_len(m) == 1);
    assert(rt_orderedmap_get(m, k) == v2);

    rt_string_unref(k);
}

static void test_has()
{
    void *m = rt_orderedmap_new();
    rt_string k1 = make_str("exists");
    rt_string k2 = make_str("missing");
    rt_string v = make_str("val");

    rt_orderedmap_set(m, k1, v);
    assert(rt_orderedmap_has(m, k1) == 1);
    assert(rt_orderedmap_has(m, k2) == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_remove()
{
    void *m = rt_orderedmap_new();
    rt_string k = make_str("key");
    rt_string v = make_str("val");

    rt_orderedmap_set(m, k, v);
    assert(rt_orderedmap_remove(m, k) == 1);
    assert(rt_orderedmap_len(m) == 0);
    assert(rt_orderedmap_has(m, k) == 0);

    assert(rt_orderedmap_remove(m, k) == 0); // Already removed

    rt_string_unref(k);
}

static void test_insertion_order()
{
    void *m = rt_orderedmap_new();
    rt_string ka = make_str("alpha");
    rt_string kb = make_str("beta");
    rt_string kc = make_str("gamma");
    rt_string va = make_str("a");
    rt_string vb = make_str("b");
    rt_string vc = make_str("c");

    rt_orderedmap_set(m, ka, va);
    rt_orderedmap_set(m, kb, vb);
    rt_orderedmap_set(m, kc, vc);

    // Keys should be in insertion order
    void *keys = rt_orderedmap_keys(m);
    assert(rt_seq_len(keys) == 3);
    assert(str_eq((rt_string)rt_seq_get(keys, 0), "alpha"));
    assert(str_eq((rt_string)rt_seq_get(keys, 1), "beta"));
    assert(str_eq((rt_string)rt_seq_get(keys, 2), "gamma"));

    rt_string_unref(ka);
    rt_string_unref(kb);
    rt_string_unref(kc);
}

static void test_key_at()
{
    void *m = rt_orderedmap_new();
    rt_string k1 = make_str("first");
    rt_string k2 = make_str("second");
    rt_string k3 = make_str("third");
    rt_string v = make_str("v");

    rt_orderedmap_set(m, k1, v);
    rt_orderedmap_set(m, k2, v);
    rt_orderedmap_set(m, k3, v);

    rt_string at0 = rt_orderedmap_key_at(m, 0);
    rt_string at1 = rt_orderedmap_key_at(m, 1);
    rt_string at2 = rt_orderedmap_key_at(m, 2);

    assert(str_eq(at0, "first"));
    assert(str_eq(at1, "second"));
    assert(str_eq(at2, "third"));
    assert(rt_orderedmap_key_at(m, 3) == NULL);

    rt_string_unref(at0);
    rt_string_unref(at1);
    rt_string_unref(at2);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
}

static void test_clear()
{
    void *m = rt_orderedmap_new();
    rt_string k = make_str("key");
    rt_string v = make_str("val");

    rt_orderedmap_set(m, k, v);
    rt_orderedmap_clear(m);

    assert(rt_orderedmap_len(m) == 0);
    assert(rt_orderedmap_has(m, k) == 0);

    rt_string_unref(k);
}

static void test_many_entries()
{
    void *m = rt_orderedmap_new();
    char buf[32];

    for (int i = 0; i < 100; i++)
    {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        rt_string k = make_str(buf);
        rt_string v = make_str(buf);
        rt_orderedmap_set(m, k, v);
        rt_string_unref(k);
    }

    assert(rt_orderedmap_len(m) == 100);

    // Verify order is preserved
    rt_string first = rt_orderedmap_key_at(m, 0);
    rt_string last = rt_orderedmap_key_at(m, 99);
    assert(str_eq(first, "key_000"));
    assert(str_eq(last, "key_099"));
    rt_string_unref(first);
    rt_string_unref(last);
}

static void test_null_safety()
{
    assert(rt_orderedmap_len(NULL) == 0);
    assert(rt_orderedmap_is_empty(NULL) == 1);
    assert(rt_orderedmap_get(NULL, NULL) == NULL);
    assert(rt_orderedmap_has(NULL, NULL) == 0);
    assert(rt_orderedmap_remove(NULL, NULL) == 0);
    assert(rt_orderedmap_key_at(NULL, 0) == NULL);
}

int main()
{
    test_new_empty();
    test_set_and_get();
    test_overwrite();
    test_has();
    test_remove();
    test_insertion_order();
    test_key_at();
    test_clear();
    test_many_entries();
    test_null_safety();

    return 0;
}
