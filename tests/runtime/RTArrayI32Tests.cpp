// File: tests/runtime/RTArrayI32Tests.cpp
// Purpose: Verify basic behavior of the int32 runtime array helpers.
// Key invariants: Resizing zero-initializes new slots and preserves prior values.
// Ownership: Tests own allocated arrays and release them via rt_arr_i32_release().
// Links: docs/runtime-vm.md#runtime-abi

#include "rt_array.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>

static void expect_zero_range(int32_t *arr, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        assert(rt_arr_i32_get(arr, i) == 0);
}

int main()
{
    int32_t *arr = rt_arr_i32_new(0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 0);

    assert(rt_arr_i32_resize(&arr, 3) == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 3);
    expect_zero_range(arr, 0, 3);

    rt_arr_i32_set(arr, 0, 7);
    rt_arr_i32_set(arr, 1, -2);
    rt_arr_i32_set(arr, 2, 99);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);

    assert(rt_arr_i32_resize(&arr, 6) == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 6);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);
    expect_zero_range(arr, 3, 6);

    assert(rt_arr_i32_resize(&arr, 2) == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 2);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);

    assert(rt_arr_i32_resize(&arr, 5) == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 5);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    expect_zero_range(arr, 2, 5);

    int32_t *fresh = nullptr;
    assert(rt_arr_i32_resize(&fresh, 4) == 0);
    assert(fresh != nullptr);
    assert(rt_arr_i32_len(fresh) == 4);
    expect_zero_range(fresh, 0, 4);

    rt_arr_i32_release(arr);
    rt_arr_i32_release(fresh);
    return 0;
}

