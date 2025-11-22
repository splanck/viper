//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTChrInvalidTests.cpp
// Purpose: Ensure rt_chr traps on out-of-range input. 
// Key invariants: Codes outside 0-255 trigger runtime trap.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        rt_chr(-1);
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
    std::string out(buf);
    bool ok = out.find("CHR$: code must be 0-255") != std::string::npos;
    assert(ok);
    return 0;
}
