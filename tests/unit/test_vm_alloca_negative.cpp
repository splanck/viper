// File: tests/unit/test_vm_alloca_negative.cpp
// Purpose: Ensure VM traps on negative allocation sizes.
// Key invariants: Alloca with negative bytes must emit "negative allocation" trap.
// Ownership: Test constructs IL module and executes VM.
// Links: docs/class-catalog.md

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
    il::core::Instr in;
    in.op = il::core::Opcode::Alloca;
    in.type = il::core::Type(il::core::Type::Kind::Ptr);
    in.operands.push_back(il::core::Value::constInt(-8));
    in.loc = {1, 1, 1};
    bb.instructions.push_back(in);

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
    bool ok = out.find("negative allocation") != std::string::npos;
    assert(ok);
    return 0;
}
