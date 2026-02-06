//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_weakmap.h"

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

static void test_basic()
{
    void *m = rt_weakmap_new();
    assert(rt_weakmap_len(m) == 0);
    assert(rt_weakmap_is_empty(m) == 1);

    rt_string k = make_str("key");
    rt_string v = make_str("value");
    rt_weakmap_set(m, k, v);

    assert(rt_weakmap_len(m) == 1);
    assert(rt_weakmap_is_empty(m) == 0);

    void *got = rt_weakmap_get(m, k);
    assert(got == v);

    rt_string_unref(k);
}

static void test_has()
{
    void *m = rt_weakmap_new();
    rt_string k1 = make_str("a");
    rt_string k2 = make_str("b");

    rt_weakmap_set(m, k1, make_str("val"));
    assert(rt_weakmap_has(m, k1) == 1);
    assert(rt_weakmap_has(m, k2) == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_remove()
{
    void *m = rt_weakmap_new();
    rt_string k = make_str("key");
    rt_weakmap_set(m, k, make_str("val"));
    assert(rt_weakmap_len(m) == 1);

    assert(rt_weakmap_remove(m, k) == 1);
    assert(rt_weakmap_len(m) == 0);
    assert(rt_weakmap_has(m, k) == 0);

    // Remove non-existent
    assert(rt_weakmap_remove(m, k) == 0);

    rt_string_unref(k);
}

static void test_update()
{
    void *m = rt_weakmap_new();
    rt_string k = make_str("key");
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_weakmap_set(m, k, v1);
    rt_weakmap_set(m, k, v2);
    assert(rt_weakmap_len(m) == 1);

    void *got = rt_weakmap_get(m, k);
    assert(got == v2);

    rt_string_unref(k);
}

static void test_keys()
{
    void *m = rt_weakmap_new();
    rt_weakmap_set(m, make_str("x"), make_str("1"));
    rt_weakmap_set(m, make_str("y"), make_str("2"));

    void *keys = rt_weakmap_keys(m);
    assert(rt_seq_len(keys) == 2);
}

static void test_clear()
{
    void *m = rt_weakmap_new();
    rt_weakmap_set(m, make_str("a"), make_str("1"));
    rt_weakmap_set(m, make_str("b"), make_str("2"));
    assert(rt_weakmap_len(m) == 2);

    rt_weakmap_clear(m);
    assert(rt_weakmap_len(m) == 0);
}

static void test_compact()
{
    void *m = rt_weakmap_new();
    rt_string k1 = make_str("alive");
    rt_string k2 = make_str("dead");

    rt_weakmap_set(m, k1, make_str("val"));
    rt_weakmap_set(m, k2, NULL); // Simulate collected value

    assert(rt_weakmap_len(m) == 2);
    int64_t removed = rt_weakmap_compact(m);
    assert(removed == 1);
    assert(rt_weakmap_len(m) == 1);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_many_entries()
{
    void *m = rt_weakmap_new();
    for (int i = 0; i < 100; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "key_%d", i);
        rt_weakmap_set(m, make_str(buf), make_str(buf));
    }
    assert(rt_weakmap_len(m) == 100);

    // Verify all retrievable
    for (int i = 0; i < 100; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "key_%d", i);
        rt_string k = make_str(buf);
        assert(rt_weakmap_has(m, k) == 1);
        rt_string_unref(k);
    }
}

static void test_null_safety()
{
    assert(rt_weakmap_len(NULL) == 0);
    assert(rt_weakmap_is_empty(NULL) == 1);
    assert(rt_weakmap_get(NULL, NULL) == NULL);
    assert(rt_weakmap_has(NULL, NULL) == 0);
    assert(rt_weakmap_remove(NULL, NULL) == 0);
    rt_weakmap_set(NULL, NULL, NULL);
    rt_weakmap_clear(NULL);
    assert(rt_weakmap_compact(NULL) == 0);
}

int main()
{
    test_basic();
    test_has();
    test_remove();
    test_update();
    test_keys();
    test_clear();
    test_compact();
    test_many_entries();
    test_null_safety();
    return 0;
}
