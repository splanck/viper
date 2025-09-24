// File: tests/runtime/RTArrayI32Tests.cpp
// Purpose: Verify basic behavior of the int32 runtime array helpers.
// Key invariants: Resizing zero-initializes new slots and preserves prior values.
// Ownership: Tests own allocated arrays and release them via free().
// Links: docs/runtime-vm.md#runtime-abi

#include "rt_array.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>

static void expect_zero_range(void *arr, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        assert(rt_arr_i32_get(arr, i) == 0);
}

int main()
{
    void *arr = rt_arr_i32_new(0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 0);

    arr = rt_arr_i32_resize(arr, 3);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 3);
    expect_zero_range(arr, 0, 3);

    rt_arr_i32_set(arr, 0, 7);
    rt_arr_i32_set(arr, 1, -2);
    rt_arr_i32_set(arr, 2, 99);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);

    arr = rt_arr_i32_resize(arr, 6);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 6);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);
    expect_zero_range(arr, 3, 6);

    arr = rt_arr_i32_resize(arr, 2);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 2);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);

    arr = rt_arr_i32_resize(arr, 5);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 5);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    expect_zero_range(arr, 2, 5);

    void *fresh = rt_arr_i32_resize(nullptr, 4);
    assert(fresh != nullptr);
    assert(rt_arr_i32_len(fresh) == 4);
    expect_zero_range(fresh, 0, 4);

    free(arr);
    free(fresh);
    return 0;
}

