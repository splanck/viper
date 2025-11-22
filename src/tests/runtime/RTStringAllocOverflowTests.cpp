//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringAllocOverflowTests.cpp
// Purpose: Ensure rt_string_alloc traps when length+1 would overflow. 
// Key invariants: Runtime string allocation guards against size_t overflow.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include <cassert>
#include <stdint.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

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

static void call_string_len_overflow()
{
    rt_string_from_bytes(NULL, SIZE_MAX);
}

int main()
{
    std::string out = capture(call_string_len_overflow);
    bool ok = out.find("rt_string_alloc: length overflow") != std::string::npos;
    assert(ok);
    return 0;
}
