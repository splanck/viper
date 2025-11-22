//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArrayStrTests.cpp
// Purpose: Verify basic behavior of the string runtime array helpers. 
// Key invariants: String elements are properly reference-counted on get/put/release.
// Ownership/Lifetime: Tests own allocated arrays and release them via rt_arr_str_release().
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
#include <cstring>

int main()
{
    // Test 1: Allocate empty array
    rt_string *arr = rt_arr_str_alloc(0);
    assert(arr != nullptr);
    assert(rt_arr_str_len(arr) == 0);
    rt_arr_str_release(arr, 0);

    // Test 2: Allocate array with 3 elements
    arr = rt_arr_str_alloc(3);
    assert(arr != nullptr);
    assert(rt_arr_str_len(arr) == 3);

    // All slots should be initialized to NULL
    for (size_t i = 0; i < 3; ++i)
    {
        rt_string s = rt_arr_str_get(arr, i);
        assert(s == nullptr);
        // Note: rt_arr_str_get retains, so we need to release even if NULL
        rt_str_release_maybe(s);
    }

    // Test 3: Put strings into array
    rt_string str1 = rt_string_from_bytes("Hello", 5);
    rt_string str2 = rt_string_from_bytes("World", 5);
    rt_string str3 = rt_string_from_bytes("Test", 4);

    rt_arr_str_put(arr, 0, str1);
    rt_arr_str_put(arr, 1, str2);
    rt_arr_str_put(arr, 2, str3);

    // rt_arr_str_put retains the values, so we can release our references
    rt_str_release_maybe(str1);
    rt_str_release_maybe(str2);
    rt_str_release_maybe(str3);

    // Test 4: Get strings from array (get returns retained handles)
    rt_string retrieved1 = rt_arr_str_get(arr, 0);
    rt_string retrieved2 = rt_arr_str_get(arr, 1);
    rt_string retrieved3 = rt_arr_str_get(arr, 2);

    assert(retrieved1 != nullptr);
    assert(retrieved2 != nullptr);
    assert(retrieved3 != nullptr);

    assert(rt_len(retrieved1) == 5);
    assert(rt_len(retrieved2) == 5);
    assert(rt_len(retrieved3) == 4);

    // Release retrieved handles (since get retains)
    rt_str_release_maybe(retrieved1);
    rt_str_release_maybe(retrieved2);
    rt_str_release_maybe(retrieved3);

    // Test 5: Overwrite a slot
    rt_string new_str = rt_string_from_bytes("Updated", 7);
    rt_arr_str_put(arr, 1, new_str);
    rt_str_release_maybe(new_str);

    rt_string check = rt_arr_str_get(arr, 1);
    assert(rt_len(check) == 7);
    rt_str_release_maybe(check);

    // Test 6: Put NULL into a slot
    rt_arr_str_put(arr, 2, nullptr);
    rt_string null_check = rt_arr_str_get(arr, 2);
    assert(null_check == nullptr);
    rt_str_release_maybe(null_check);

    // Test 7: Release array (should release all remaining strings)
    rt_arr_str_release(arr, 3);

    std::fprintf(stderr, "All string array tests passed!\n");
    return 0;
}
