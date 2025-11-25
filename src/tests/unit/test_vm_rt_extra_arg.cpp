//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_extra_arg.cpp
// Purpose: Ensure runtime bridge traps when rt_print_str is called with too many arguments.
// Key invariants: Calls with excess args should emit descriptive trap rather than crash.
// Ownership/Lifetime: Test constructs IL module and executes VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern("rt_print_str", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    b.addGlobalStr("g", "hi");
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    Value s = b.emitConstStr("g", {1, 1, 1});
    // Deliberately provide an extra argument beyond the signature.
    b.emitCall("rt_print_str", {s, s}, std::optional<Value>{}, {1, 1, 1});
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
    std::string out(buf);
    bool ok = out.find("Trap @main#1 line 1: DomainError (code=0)") != std::string::npos;
    assert(ok);
    return 0;
}
