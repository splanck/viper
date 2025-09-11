// File: tests/runtime/RTStringRangeTests.cpp
// Purpose: Verify runtime string helpers report negative start/length diagnostics.
// Key invariants: LEFT$ and MID$ trap with specific messages on invalid ranges.
// Ownership: Uses runtime library.
// Links: docs/runtime-abi.md
#include "rt.hpp"
#include <cassert>
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

static void call_left_negative()
{
    rt_left(rt_const_cstr("A"), -1);
}

static void call_mid_negative()
{
    rt_mid3(rt_const_cstr("A"), -1, 1);
}

int main()
{
    std::string out = capture(call_left_negative);
    bool ok = out.find("LEFT$: len must be >= 0") != std::string::npos;
    assert(ok);
    out = capture(call_mid_negative);
    ok = out.find("MID$: start must be >= 0") != std::string::npos;
    assert(ok);
    return 0;
}
