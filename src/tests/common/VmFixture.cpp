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
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"

#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <sys/wait.h>
#include <unistd.h>

namespace viper::tests
{
int64_t VmFixture::run(il::core::Module &module) const
{
    il::vm::VM vm(module);
    return vm.run();
}

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

std::string VmFixture::captureTrap(il::core::Module &module) const
{
    const VmTrapResult trap = runExpectingTrap(module);
    assert(trap.exited && trap.exitCode == 1);
    return trap.stderrText;
}

} // namespace viper::tests
