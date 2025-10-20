// File: tests/runtime/RTStringRangeTests.cpp
// Purpose: Verify runtime string helpers clamp negative starts but trap on
//          negative lengths.
// Key invariants: LEFT$ retains its trap on negative lengths; MID$ clamps
//                 start positions <= 1 to the full string.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
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

int main()
{
    std::string out = capture(call_left_negative);
    bool ok = out.find("LEFT$: len must be >= 0") != std::string::npos;
    assert(ok);
    rt_string sample = rt_const_cstr("ABCDE");
    rt_string clamped = rt_mid3(sample, -4, 3);
    rt_string abc = rt_const_cstr("ABC");
    assert(rt_str_eq(clamped, abc));
    return 0;
}
