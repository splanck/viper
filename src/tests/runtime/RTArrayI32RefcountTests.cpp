//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArrayI32RefcountTests.cpp
// Purpose: Validate rt_arr_i32 reference-counting and copy-on-resize semantics. 
// Key invariants: Shared handles must observe consistent refcounts and aliasing guarantees.
// Ownership/Lifetime: Tests manage retains/releases explicitly and ensure all arrays are freed.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

static size_t refcount(int32_t *arr)
{
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    assert(hdr != nullptr);
    return hdr->refcnt;
}

static void resize_or_abort(int32_t **arr, size_t new_len)
{
    if (rt_arr_i32_resize(arr, new_len) != 0)
    {
        std::fprintf(stderr, "rt_arr_i32_resize failed for len=%zu\n", new_len);
        std::abort();
    }
}

static void test_refcount_lifecycle()
{
    int32_t *arr = rt_arr_i32_new(3);
    assert(arr != nullptr);
    assert(refcount(arr) == 1);

    rt_arr_i32_retain(arr);
    assert(refcount(arr) == 2);

    rt_arr_i32_release(arr);
    assert(refcount(arr) == 1);

    size_t remaining = rt_heap_release(arr);
    assert(remaining == 0);
}

static void test_aliasing_visibility()
{
    int32_t *a = rt_arr_i32_new(2);
    assert(a != nullptr);
    rt_arr_i32_set(a, 0, 11);

    int32_t *b = a;
    rt_arr_i32_retain(b);
    assert(refcount(a) == 2);

    rt_arr_i32_set(a, 1, -7);
    assert(rt_arr_i32_get(b, 0) == 11);
    assert(rt_arr_i32_get(b, 1) == -7);

    rt_arr_i32_release(b);
    assert(refcount(a) == 1);
    rt_arr_i32_release(a);
}

static void test_copy_on_resize()
{
    int32_t *a = rt_arr_i32_new(2);
    assert(a != nullptr);
    rt_arr_i32_set(a, 0, 5);
    rt_arr_i32_set(a, 1, 8);

    int32_t *b = a;
    rt_arr_i32_retain(b);
    assert(refcount(a) == 2);

    int32_t *original = a;
    resize_or_abort(&a, 4);
    assert(a != nullptr);
    assert(rt_arr_i32_len(a) == 4);
    assert(rt_arr_i32_get(a, 0) == 5);
    assert(rt_arr_i32_get(a, 1) == 8);
    assert(rt_arr_i32_get(a, 2) == 0);
    assert(rt_arr_i32_get(a, 3) == 0);

    assert(a != b);
    assert(original == b);

    assert(refcount(a) == 1);
    assert(refcount(b) == 1);
    assert(rt_arr_i32_len(b) == 2);
    assert(rt_arr_i32_get(b, 0) == 5);
    assert(rt_arr_i32_get(b, 1) == 8);

    rt_arr_i32_release(b);
    rt_arr_i32_release(a);
}

static void test_self_assignment_no_refcount_change()
{
    int32_t *arr = rt_arr_i32_new(1);
    assert(arr != nullptr);
    assert(refcount(arr) == 1);

    int32_t *alias = arr; // self-assignment/aliasing without retain should not change refcount.
    (void)alias;
    assert(refcount(arr) == 1);

    rt_arr_i32_release(arr);
}

int main()
{
    test_refcount_lifecycle();
    test_aliasing_visibility();
    test_copy_on_resize();
    test_self_assignment_no_refcount_change();
    return 0;
}
