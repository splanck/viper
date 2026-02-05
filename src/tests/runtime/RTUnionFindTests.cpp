//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_unionfind.h"

#include <cassert>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_new()
{
    void *uf = rt_unionfind_new(10);
    assert(uf != NULL);
    assert(rt_unionfind_count(uf) == 10); // 10 disjoint sets
}

static void test_find()
{
    void *uf = rt_unionfind_new(5);
    // Each element is its own representative
    assert(rt_unionfind_find(uf, 0) == 0);
    assert(rt_unionfind_find(uf, 4) == 4);
}

static void test_union()
{
    void *uf = rt_unionfind_new(5);

    int64_t merged = rt_unionfind_union(uf, 0, 1);
    assert(merged == 1);
    assert(rt_unionfind_count(uf) == 4);

    // 0 and 1 should now share a representative
    assert(rt_unionfind_find(uf, 0) == rt_unionfind_find(uf, 1));
}

static void test_already_connected()
{
    void *uf = rt_unionfind_new(5);
    rt_unionfind_union(uf, 0, 1);

    int64_t merged = rt_unionfind_union(uf, 0, 1);
    assert(merged == 0); // Already connected
    assert(rt_unionfind_count(uf) == 4);
}

static void test_connected()
{
    void *uf = rt_unionfind_new(5);
    assert(rt_unionfind_connected(uf, 0, 1) == 0);

    rt_unionfind_union(uf, 0, 1);
    assert(rt_unionfind_connected(uf, 0, 1) == 1);
    assert(rt_unionfind_connected(uf, 0, 2) == 0);
}

static void test_transitive()
{
    void *uf = rt_unionfind_new(5);
    rt_unionfind_union(uf, 0, 1);
    rt_unionfind_union(uf, 1, 2);

    // 0 and 2 should be connected through 1
    assert(rt_unionfind_connected(uf, 0, 2) == 1);
    assert(rt_unionfind_count(uf) == 3);
}

static void test_set_size()
{
    void *uf = rt_unionfind_new(5);
    assert(rt_unionfind_set_size(uf, 0) == 1);

    rt_unionfind_union(uf, 0, 1);
    assert(rt_unionfind_set_size(uf, 0) == 2);
    assert(rt_unionfind_set_size(uf, 1) == 2);

    rt_unionfind_union(uf, 1, 2);
    assert(rt_unionfind_set_size(uf, 0) == 3);
}

static void test_reset()
{
    void *uf = rt_unionfind_new(5);
    rt_unionfind_union(uf, 0, 1);
    rt_unionfind_union(uf, 2, 3);
    assert(rt_unionfind_count(uf) == 3);

    rt_unionfind_reset(uf);
    assert(rt_unionfind_count(uf) == 5);
    assert(rt_unionfind_connected(uf, 0, 1) == 0);
}

static void test_many_unions()
{
    void *uf = rt_unionfind_new(100);

    // Connect all even numbers
    for (int64_t i = 2; i < 100; i += 2)
        rt_unionfind_union(uf, 0, i);

    // Connect all odd numbers
    for (int64_t i = 3; i < 100; i += 2)
        rt_unionfind_union(uf, 1, i);

    assert(rt_unionfind_count(uf) == 2);
    assert(rt_unionfind_set_size(uf, 0) == 50);
    assert(rt_unionfind_set_size(uf, 1) == 50);
    assert(rt_unionfind_connected(uf, 0, 98) == 1);
    assert(rt_unionfind_connected(uf, 1, 99) == 1);
    assert(rt_unionfind_connected(uf, 0, 1) == 0);
}

static void test_out_of_range()
{
    void *uf = rt_unionfind_new(5);
    assert(rt_unionfind_find(uf, -1) == -1);
    assert(rt_unionfind_find(uf, 5) == -1);
    assert(rt_unionfind_union(uf, 0, 10) == 0);
}

static void test_null_safety()
{
    assert(rt_unionfind_find(NULL, 0) == -1);
    assert(rt_unionfind_union(NULL, 0, 1) == 0);
    assert(rt_unionfind_connected(NULL, 0, 1) == 0);
    assert(rt_unionfind_count(NULL) == 0);
    assert(rt_unionfind_set_size(NULL, 0) == 0);
}

int main()
{
    test_new();
    test_find();
    test_union();
    test_already_connected();
    test_connected();
    test_transitive();
    test_set_size();
    test_reset();
    test_many_unions();
    test_out_of_range();
    test_null_safety();

    return 0;
}
