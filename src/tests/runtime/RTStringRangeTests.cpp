//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringRangeTests.cpp
// Purpose: Verify runtime string helpers report negative start/length diagnostics.
// Key invariants: LEFT$ and MID$ trap with specific messages on invalid ranges.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include <cassert>
#include <cstdio>
#include <string>

static std::string capture(void (*fn)())
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
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
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    return std::string(buf);
}

static void call_left_negative()
{
    rt_str_left(rt_const_cstr("A"), -1);
}

static void call_mid_negative()
{
    rt_str_mid_len(rt_const_cstr("A"), -1, 1);
}

int main()
{
    SKIP_TEST_NO_FORK();
    std::string out = capture(call_left_negative);
    bool ok = out.find("LEFT$: len must be >= 0") != std::string::npos;
    assert(ok);
    out = capture(call_mid_negative);
    ok = out.find("MID$: start must be >= 1") != std::string::npos;
    assert(ok);

    rt_string sample = rt_const_cstr("ABCDEF");
    rt_string start_one = rt_str_mid(sample, 1);
    assert(rt_str_eq(start_one, sample));

    rt_string start_len = rt_str_mid(sample, 6);
    assert(rt_str_eq(start_len, rt_const_cstr("F")));

    rt_string start_len_with_count = rt_str_mid_len(sample, 6, 5);
    assert(rt_str_eq(start_len_with_count, rt_const_cstr("F")));

    rt_string start_beyond = rt_str_mid_len(sample, 7, 3);
    assert(rt_str_eq(start_beyond, rt_str_empty()));

    return 0;
}
