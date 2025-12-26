//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAllocTests.cpp
// Purpose: Verify rt_alloc traps on negative allocation sizes.
// Key invariants: rt_alloc reports "negative allocation" when bytes < 0.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include "tests/common/WaitCompat.hpp"
#include "tests/common/PosixCompat.h"

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

static void call_alloc_negative()
{
    rt_alloc(-1);
}

int main()
{
    std::string out = capture(call_alloc_negative);
    bool ok = out.find("negative allocation") != std::string::npos;
    assert(ok);
    return 0;
}
