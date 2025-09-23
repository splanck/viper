// File: tests/unit/test_vm_unknown_global.cpp
// Purpose: Ensure VM traps when referencing undefined globals.
// Key invariants: Missing global names must emit "unknown global" trap.
// Ownership: Test constructs IL module and executes VM.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    b.emitConstStr("missing", {1, 1, 1});
    b.emitRet(std::optional<il::core::Value>{}, {1, 1, 1});

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
    bool ok = out.find("unknown global") != std::string::npos;
    assert(ok);
    return 0;
}
