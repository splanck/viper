//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_runtime_trap_metadata.cpp
// Purpose: Ensure runtime trap metadata clears stale function/block identifiers when omitted.
// Key invariants: Subsequent traps without metadata must not reuse prior function/block names.
// Ownership/Lifetime: Spawns child processes to capture diagnostics and runtime context output.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"
#include "support/source_location.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include "VMTestHook.hpp"

#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kFirstFunction = "first_fn";
constexpr const char *kFirstBlock = "first_block";

il::vm::VM *gExitVm = nullptr;
bool gReportContext = false;

void reportRuntimeContext()
{
    if (!gReportContext || !gExitVm)
        return;
    const auto &ctx = il::vm::VMTestHook::runtimeContext(*gExitVm);
    fprintf(
        stderr, "runtime-context: fn='%s' block='%s'\n", ctx.function.c_str(), ctx.block.c_str());
}

std::string captureTrap(bool includeMetadata, bool primeContext)
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        close(fds[1]);

        il::core::Module module;
        il::vm::VM vm(module);
        il::vm::ActiveVMGuard guard(&vm);

        if (primeContext)
        {
            auto &ctx = il::vm::VMTestHook::runtimeContext(vm);
            ctx.function = kFirstFunction;
            ctx.block = kFirstBlock;
            gExitVm = &vm;
            gReportContext = true;
            std::atexit(reportRuntimeContext);
        }
        else
        {
            gExitVm = nullptr;
            gReportContext = false;
        }

        const il::support::SourceLoc loc =
            includeMetadata ? il::support::SourceLoc{1, 1, 1} : il::support::SourceLoc{};
        const std::string message =
            includeMetadata ? std::string("first trap") : std::string("second trap");
        const std::string fn = includeMetadata ? std::string(kFirstFunction) : std::string();
        const std::string block = includeMetadata ? std::string(kFirstBlock) : std::string();

        il::vm::RuntimeBridge::trap(il::vm::TrapKind::DomainError, message, loc, fn, block);
    }

    close(fds[1]);
    char buffer[1024];
    ssize_t n = read(fds[0], buffer, sizeof(buffer) - 1);
    if (n < 0)
        n = 0;
    buffer[n] = '\0';
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status));
    return std::string(buffer);
}
} // namespace

int main()
{
    SKIP_TEST_NO_FORK();
    const std::string firstDiag = captureTrap(true, false);
    assert(firstDiag.find("Trap @first_fn") != std::string::npos);
    assert(firstDiag.find("first trap") != std::string::npos);

    const std::string secondDiag = captureTrap(false, true);
    assert(secondDiag.find("Trap @first_fn") == std::string::npos);
    assert(secondDiag.find("<unknown>") != std::string::npos);
    assert(secondDiag.find("second trap") != std::string::npos);
    assert(secondDiag.find("runtime-context: fn='' block=''\n") != std::string::npos);

    return 0;
}
