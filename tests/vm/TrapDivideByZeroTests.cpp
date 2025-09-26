// File: tests/vm/TrapDivideByZeroTests.cpp
// Purpose: Ensure DivideByZero traps report kind and instruction index.
// Key invariants: Diagnostic mentions DivideByZero and instruction #0 for the failing op.
// Ownership/Lifetime: Forks child VM process to capture trap output.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace il::core;

namespace
{
std::string captureTrap(Module &module)
{
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
    char buffer[512];
    ssize_t n = read(fds[0], buffer, sizeof(buffer) - 1);
    if (n < 0)
        n = 0;
    buffer[n] = '\0';
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    return std::string(buffer);
}
} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr div;
    div.result = builder.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(1));
    div.operands.push_back(Value::constInt(0));
    div.loc = {1, 1, 1};
    bb.instructions.push_back(div);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    const std::string out = captureTrap(module);
    const bool ok =
        out.find("runtime trap: DivideByZero @ main: entry[#0] (1:1:1): divide by zero in sdiv.chk0") != std::string::npos;
    assert(ok && "expected DivideByZero trap diagnostic with instruction index");
    return 0;
}
