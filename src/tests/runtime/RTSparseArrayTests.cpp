//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_sparsearray.h"
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

static void test_new()
{
    void *sa = rt_sparse_new();
    assert(sa != NULL);
    assert(rt_sparse_len(sa) == 0);
}

static void test_set_get()
{
    void *sa = rt_sparse_new();
    rt_string v1 = make_str("hello");
    rt_string v2 = make_str("world");

    rt_sparse_set(sa, 0, v1);
    rt_sparse_set(sa, 1000, v2);

    assert(rt_sparse_len(sa) == 2);
    assert(rt_sparse_get(sa, 0) == v1);
    assert(rt_sparse_get(sa, 1000) == v2);
    assert(rt_sparse_get(sa, 500) == NULL);
}

static void test_has()
{
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 42, make_str("val"));

    assert(rt_sparse_has(sa, 42) == 1);
    assert(rt_sparse_has(sa, 43) == 0);
}

static void test_remove()
{
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 10, make_str("ten"));

    assert(rt_sparse_remove(sa, 10) == 1);
    assert(rt_sparse_len(sa) == 0);
    assert(rt_sparse_get(sa, 10) == NULL);
    assert(rt_sparse_remove(sa, 10) == 0);
}

static void test_negative_indices()
{
    void *sa = rt_sparse_new();
    rt_string v = make_str("neg");
    rt_sparse_set(sa, -5, v);
    assert(rt_sparse_get(sa, -5) == v);
    assert(rt_sparse_has(sa, -5) == 1);
}

static void test_large_indices()
{
    void *sa = rt_sparse_new();
    rt_string v = make_str("big");
    rt_sparse_set(sa, 1000000, v);
    assert(rt_sparse_get(sa, 1000000) == v);
    assert(rt_sparse_len(sa) == 1);
}

static void test_overwrite()
{
    void *sa = rt_sparse_new();
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_sparse_set(sa, 5, v1);
    rt_sparse_set(sa, 5, v2);

    assert(rt_sparse_len(sa) == 1);
    assert(rt_sparse_get(sa, 5) == v2);
}

static void test_indices()
{
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 10, make_str("a"));
    rt_sparse_set(sa, 20, make_str("b"));

    void *idx = rt_sparse_indices(sa);
    assert(rt_seq_len(idx) == 2);
}

static void test_values()
{
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 1, make_str("x"));
    rt_sparse_set(sa, 2, make_str("y"));

    void *vals = rt_sparse_values(sa);
    assert(rt_seq_len(vals) == 2);
}

static void test_clear()
{
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 0, make_str("a"));
    rt_sparse_set(sa, 1, make_str("b"));

    rt_sparse_clear(sa);
    assert(rt_sparse_len(sa) == 0);
}

static void test_grow()
{
    void *sa = rt_sparse_new();
    // Insert enough elements to trigger grow (>70% of 16 = 12)
    for (int i = 0; i < 20; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "v%d", i);
        rt_sparse_set(sa, (int64_t)i, make_str(buf));
    }
    assert(rt_sparse_len(sa) == 20);

    // Verify all values survived rehash
    for (int i = 0; i < 20; i++)
    {
        assert(rt_sparse_has(sa, (int64_t)i) == 1);
    }
}

static void test_null_safety()
{
    assert(rt_sparse_len(NULL) == 0);
    assert(rt_sparse_get(NULL, 0) == NULL);
    assert(rt_sparse_has(NULL, 0) == 0);
    assert(rt_sparse_remove(NULL, 0) == 0);
}

int main()
{
    test_new();
    test_set_get();
    test_has();
    test_remove();
    test_negative_indices();
    test_large_indices();
    test_overwrite();
    test_indices();
    test_values();
    test_clear();
    test_grow();
    test_null_safety();
    return 0;
}
