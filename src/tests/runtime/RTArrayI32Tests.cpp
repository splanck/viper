//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArrayI32Tests.cpp
// Purpose: Verify basic behavior of the int32 runtime array helpers.
// Key invariants: Resizing zero-initializes new slots and preserves prior values.
// Ownership/Lifetime: Tests own allocated arrays and release them via rt_arr_i32_release().
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "viper/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

static void expect_zero_range(int32_t *arr, size_t start, size_t end) {
    for (size_t i = start; i < end; ++i)
        assert(rt_arr_i32_get(arr, i) == 0);
}

static void resize_or_abort(int32_t **arr, size_t new_len) {
    if (rt_arr_i32_resize(arr, new_len) != 0) {
        std::fprintf(stderr, "rt_arr_i32_resize failed for len=%zu\n", new_len);
        std::abort();
    }
}

static void invoke_oob_get() {
    int32_t *panic_arr = rt_arr_i32_new(1);
    assert(panic_arr != nullptr);
    rt_arr_i32_get(panic_arr, 1);
}

static void invoke_oob_set() {
    int32_t *panic_arr = rt_arr_i32_new(1);
    assert(panic_arr != nullptr);
    rt_arr_i32_set(panic_arr, 1, 42);
}

static void invoke_copy_null_src() {
    int32_t *panic_dst = rt_arr_i32_new(1);
    assert(panic_dst != nullptr);
    rt_arr_i32_copy_payload(panic_dst, NULL, 1);
}

static void invoke_copy_null_dst() {
    int32_t *panic_src = rt_arr_i32_new(1);
    assert(panic_src != nullptr);
    panic_src[0] = 99;
    rt_arr_i32_copy_payload(NULL, panic_src, 1);
}

static void expect_oob_message(const std::string &stderr_output) {
    bool saw_panic = stderr_output.find("rt_arr_i32: index") != std::string::npos;
    if (!saw_panic) {
        std::fprintf(stderr, "expected panic message, got: %s\n", stderr_output.c_str());
        std::abort();
    }
}

int main(int argc, char *argv[]) {
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    int32_t *arr = rt_arr_i32_new(0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 0);

    resize_or_abort(&arr, 3);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 3);
    expect_zero_range(arr, 0, 3);

    rt_arr_i32_set(arr, 0, 7);
    rt_arr_i32_set(arr, 1, -2);
    rt_arr_i32_set(arr, 2, 99);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);

    resize_or_abort(&arr, 6);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 6);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);
    expect_zero_range(arr, 3, 6);

    resize_or_abort(&arr, 2);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 2);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);

    resize_or_abort(&arr, 5);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 5);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    expect_zero_range(arr, 2, 5);

    int32_t *fresh = nullptr;
    resize_or_abort(&fresh, 4);
    assert(fresh != nullptr);
    assert(rt_arr_i32_len(fresh) == 4);
    expect_zero_range(fresh, 0, 4);

    rt_arr_i32_release(arr);
    rt_arr_i32_release(fresh);

    // Out-of-bounds and null-pointer trap tests
    auto result = viper::tests::runIsolated(invoke_oob_get);
    expect_oob_message(result.stderrText);

    result = viper::tests::runIsolated(invoke_oob_set);
    expect_oob_message(result.stderrText);

    result = viper::tests::runIsolated(invoke_copy_null_src);
    expect_oob_message(result.stderrText);

    result = viper::tests::runIsolated(invoke_copy_null_dst);
    expect_oob_message(result.stderrText);

    return 0;
}
