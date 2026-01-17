//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_unknown_helper.cpp
// Purpose: Ensure runtime bridge traps when unknown runtime helpers are invoked.
// Key invariants: Calls to helpers absent from the runtime registry must produce traps in all build
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <optional>
#include <string>

int main()
{
    SKIP_TEST_NO_FORK();
    using namespace il::core;
    Module module;
    il::build::IRBuilder builder(module);
    builder.addExtern("rt_missing", Type(Type::Kind::Void), {});

    auto &fn = builder.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);
    builder.emitCall("rt_missing", {}, std::optional<Value>{}, {1, 1, 1});
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        il::vm::VM vm(module);
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
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    std::string out(buf);
    bool messageOk =
        out.find("Trap @main:entry#0 line 1: DomainError (code=0)") != std::string::npos;
    assert(messageOk && "expected runtime trap diagnostic for unknown runtime helper");
    return 0;
}
