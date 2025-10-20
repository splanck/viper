// File: tests/runtime/RTObjectNewTests.cpp
// Purpose: Validate rt_obj_new_i64 traps on invalid sizes and succeeds for zero-length allocations.
// Key invariants: Negative and oversized requests report descriptive traps; zero-byte allocation yields a usable payload.
// Ownership: Exercises runtime allocation helpers and releases created objects.
// Links: docs/runtime-vm.md#runtime-abi

#include "rt.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

std::string capture(void (*fn)())
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

void call_new_negative()
{
    (void)rt_obj_new_i64(0, -1);
}

#if SIZE_MAX < INT64_MAX
void call_new_oversize()
{
    (void)rt_obj_new_i64(0, static_cast<int64_t>(SIZE_MAX) + 1);
}
#endif

} // namespace

int main()
{
    {
        std::string out = capture(call_new_negative);
        bool trapped = out.find("rt_obj_new_i64: negative size") != std::string::npos;
        assert(trapped);
    }

    {
        void *payload = rt_obj_new_i64(42, 0);
        assert(payload != nullptr);
        int32_t freed = rt_obj_release_check0(payload);
        assert(freed == 1);
    }

#if SIZE_MAX < INT64_MAX
    {
        std::string out = capture(call_new_oversize);
        bool trapped = out.find("rt_obj_new_i64: size too large") != std::string::npos;
        assert(trapped);
    }
#endif

    return 0;
}

