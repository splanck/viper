//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_trap_loc.cpp
// Purpose: Verify runtime-originated traps report instruction source locations.
// Key invariants: Trap output includes function, block, and location.
// Ownership/Lifetime: Test builds IL calling runtime and runs VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

std::string captureRuntimeTrap(bool attachLoc)
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern("rt_to_int", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    b.addGlobalStr("g", "12x");
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    Value s = b.emitConstStr("g", {1, 1, 1});
    const il::support::SourceLoc callLoc =
        attachLoc ? il::support::SourceLoc{1, 1, 1} : il::support::SourceLoc{};
    b.emitCall("rt_to_int", {s}, std::optional<Value>{}, callLoc);
    b.emitRet(std::optional<Value>{}, {1, 1, 1});

    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        il::vm::VM vm(m);
        vm.run();
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

} // namespace

int main()
{
    const std::string withLoc = captureRuntimeTrap(true);
    const bool precise =
        withLoc.find("Trap @main#1 line 1: DomainError (code=0)") != std::string::npos;
    assert(precise);

    const std::string withoutLoc = captureRuntimeTrap(false);
    const bool cleared = withoutLoc.find("line -1") != std::string::npos;
    const bool reusedOldLine = withoutLoc.find("line 1") != std::string::npos;
    assert(cleared && !reusedOldLine);
    return 0;
}
