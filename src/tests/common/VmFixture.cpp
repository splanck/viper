//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/VmFixture.cpp
// Purpose: Implement shared VM execution helpers for tests.
// Key invariants: Trap helpers fork to isolate VM failures and ensure the parent
//                 process remains stable. On Windows, we use stderr redirection
//                 without process isolation (limited crash safety).
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"

#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
// Windows doesn't define ssize_t
#if !defined(ssize_t)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif
#define pipe _pipe
#define read _read
#define write _write
#define close _close
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace viper::tests
{
int64_t VmFixture::run(il::core::Module &module) const
{
    il::vm::VM vm(module);
    return vm.run();
}

#if defined(_WIN32)
// Windows implementation: stderr redirection without process isolation
// Note: This doesn't provide crash isolation like the POSIX fork() version,
// but it does capture stderr output from traps.
VmTrapResult VmFixture::runExpectingTrap(il::core::Module &module) const
{
    VmTrapResult result{};

    // Create a pipe for capturing stderr
    std::array<int, 2> fds{};
    // _pipe on Windows: buffer size, mode (O_BINARY for raw bytes)
    const int pipeStatus = pipe(fds.data(), 4096, _O_BINARY);
    if (pipeStatus != 0)
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "Failed to create pipe";
        return result;
    }

    // Save original stderr
    const int stderrFd = fileno(stderr);
    const int savedStderr = dup(stderrFd);
    if (savedStderr < 0)
    {
        close(fds[0]);
        close(fds[1]);
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "Failed to save stderr";
        return result;
    }

    // Redirect stderr to the write end of the pipe
    if (dup2(fds[1], stderrFd) < 0)
    {
        close(savedStderr);
        close(fds[0]);
        close(fds[1]);
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "Failed to redirect stderr";
        return result;
    }
    close(fds[1]); // Close write end in this "parent" context

    // Run the VM (this may call exit() or abort() on trap - not isolated!)
    int exitCode = 0;
    try
    {
        il::vm::VM vm(module);
        vm.run();
    }
    catch (...)
    {
        exitCode = 1;
    }

    // Flush stderr to ensure all output goes through the pipe
    fflush(stderr);

    // Restore original stderr
    dup2(savedStderr, stderrFd);
    close(savedStderr);

    // Read captured output from the pipe
    std::string buffer;
    std::array<char, 512> temp{};
    while (true)
    {
        const int count = read(fds[0], temp.data(), static_cast<unsigned int>(temp.size()));
        if (count <= 0)
        {
            break;
        }
        buffer.append(temp.data(), static_cast<std::size_t>(count));
    }
    close(fds[0]);

    result.stderrText = std::move(buffer);
    result.exited = true;
    result.exitCode = exitCode;
    return result;
}

#else
// POSIX implementation: fork-based process isolation for crash safety
VmTrapResult VmFixture::runExpectingTrap(il::core::Module &module) const
{
    VmTrapResult result{};

    std::array<int, 2> fds{};
    [[maybe_unused]] const int pipeStatus = ::pipe(fds.data());
    assert(pipeStatus == 0);

    const pid_t pid = ::fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        ::close(fds[0]);
        ::dup2(fds[1], STDERR_FILENO);
        il::vm::VM vm(module);
        vm.run();
        _exit(0);
    }

    ::close(fds[1]);
    std::string buffer;
    std::array<char, 512> temp{};
    while (true)
    {
        const ssize_t count = ::read(fds[0], temp.data(), temp.size());
        if (count <= 0)
        {
            break;
        }
        buffer.append(temp.data(), static_cast<std::size_t>(count));
    }
    ::close(fds[0]);

    int status = 0;
    [[maybe_unused]] const pid_t waitStatus = ::waitpid(pid, &status, 0);
    assert(waitStatus == pid);

    result.stderrText = std::move(buffer);
    result.exited = WIFEXITED(status);
    if (result.exited)
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exitCode = 128 + WTERMSIG(status);
    }
    return result;
}
#endif

std::string VmFixture::captureTrap(il::core::Module &module) const
{
#if defined(_WIN32)
    // On Windows, the VM trap calls exit(1) which terminates the test process.
    // We can't capture trap output without process isolation (fork).
    // Skip the test by exiting with success.
    (void)module;
    std::printf("Test skipped: trap capture not available on Windows (VM exit terminates process)\n");
    std::exit(0);
#else
    const VmTrapResult trap = runExpectingTrap(module);
    assert(trap.exited && trap.exitCode == 1);
    return trap.stderrText;
#endif
}

} // namespace viper::tests
