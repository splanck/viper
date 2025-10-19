// File: tests/runtime/RTArrayI32Tests.cpp
// Purpose: Verify basic behavior of the int32 runtime array helpers.
// Key invariants: Resizing zero-initializes new slots and preserves prior values.
// Ownership: Tests own allocated arrays and release them via rt_arr_i32_release().
// Links: docs/runtime-vm.md#runtime-abi

#include "rt_array.h"

#include <cassert>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#if !defined(_WIN32)
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static void expect_zero_range(int32_t *arr, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
    {
        int32_t value = rt_arr_i32_get(arr, i);
        if (value != 0)
        {
            std::fprintf(stderr, "expected zero at %zu, got %d\n", i, value);
            std::abort();
        }
        assert(value == 0);
    }
}

int main()
{
    int32_t *arr = rt_arr_i32_new(0);
    if (!arr)
        std::abort();
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 0);

    int rc = rt_arr_i32_resize(&arr, 3);
    if (rc != 0)
        std::abort();
    assert(rc == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 3);
    expect_zero_range(arr, 0, 3);

    rt_arr_i32_set(arr, 0, 7);
    rt_arr_i32_set(arr, 1, -2);
    rt_arr_i32_set(arr, 2, 99);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);

    rc = rt_arr_i32_resize(&arr, 6);
    if (rc != 0)
        std::abort();
    assert(rc == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 6);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    assert(rt_arr_i32_get(arr, 2) == 99);
    expect_zero_range(arr, 3, 6);

    rc = rt_arr_i32_resize(&arr, 2);
    if (rc != 0)
        std::abort();
    assert(rc == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 2);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);

    rc = rt_arr_i32_resize(&arr, 5);
    if (rc != 0)
        std::abort();
    assert(rc == 0);
    assert(arr != nullptr);
    assert(rt_arr_i32_len(arr) == 5);
    assert(rt_arr_i32_get(arr, 0) == 7);
    assert(rt_arr_i32_get(arr, 1) == -2);
    expect_zero_range(arr, 2, 5);

    int32_t *fresh = nullptr;
    rc = rt_arr_i32_resize(&fresh, 4);
    if (rc != 0)
        std::abort();
    assert(rc == 0);
    if (!fresh)
        std::abort();
    assert(fresh != nullptr);
    assert(rt_arr_i32_len(fresh) == 4);
    expect_zero_range(fresh, 0, 4);

    rt_arr_i32_release(arr);
    rt_arr_i32_release(fresh);

#if !defined(_WIN32)
    auto capture_stderr = [](void (*fn)()) {
        int fds[2];
        int pipe_rc = pipe(fds);
        if (pipe_rc != 0)
        {
            std::perror("pipe");
            std::abort();
        }
        assert(pipe_rc == 0);
        pid_t pid = fork();
        if (pid < 0)
        {
            std::perror("fork");
            std::abort();
        }
        assert(pid >= 0);
        if (pid == 0)
        {
            close(fds[0]);
            dup2(fds[1], 2);
            fn();
            _exit(0);
        }
        close(fds[1]);
        char buf[256];
        ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
        if (n < 0)
            n = 0;
        buf[n] = '\0';
        close(fds[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        return std::string(buf);
    };

    auto expect_oob_message = [](const std::string &stderr_output) {
        bool saw_panic = stderr_output.find("rt_arr_i32: index 1 out of bounds") != std::string::npos;
        if (!saw_panic)
        {
            std::fprintf(stderr, "expected panic message, got: %s\n", stderr_output.c_str());
            std::abort();
        }
    };

    auto invoke_oob_get = []() {
        int32_t *panic_arr = rt_arr_i32_new(1);
        if (!panic_arr)
            std::abort();
        assert(panic_arr != nullptr);
        rt_arr_i32_get(panic_arr, 1);
    };
    expect_oob_message(capture_stderr(invoke_oob_get));

    auto invoke_oob_set = []() {
        int32_t *panic_arr = rt_arr_i32_new(1);
        if (!panic_arr)
            std::abort();
        assert(panic_arr != nullptr);
        rt_arr_i32_set(panic_arr, 1, 42);
    };
    expect_oob_message(capture_stderr(invoke_oob_set));
#endif

    return 0;
}

