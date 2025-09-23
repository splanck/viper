// File: tests/unit/test_vm_rt_concat_missing_args.cpp
// Purpose: Ensure runtime bridge traps when rt_concat is called with too few arguments.
// Key invariants: Calls with insufficient args should emit descriptive trap rather than crash.
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
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern("rt_concat", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::Str)});
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    // Deliberately omit both required arguments.
    b.emitCall("rt_concat", {}, std::optional<Value>{}, {1, 1, 1});
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
    bool ok = out.find("rt_concat: expected 2 argument") != std::string::npos;
    assert(ok);
    return 0;
}
