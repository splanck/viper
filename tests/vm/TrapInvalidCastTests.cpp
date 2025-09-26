// File: tests/vm/TrapInvalidCastTests.cpp
// Purpose: Ensure InvalidCast traps report kind and instruction index.
// Key invariants: Diagnostic mentions InvalidCast and instruction #0 for cast op.
// Ownership/Lifetime: Uses forked VM process to capture stderr.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cmath>
#include <limits>
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

    Instr cast;
    cast.result = builder.reserveTempId();
    cast.op = Opcode::CastFpToSiRteChk;
    cast.type = Type(Type::Kind::I64);
    cast.operands.push_back(Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
    cast.loc = {1, 1, 1};
    bb.instructions.push_back(cast);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    const std::string out = captureTrap(module);
    const bool ok = out.find(
                       "runtime trap: InvalidCast @ main: entry[#0] (1:1:1): invalid fp operand in cast.fp_to_si.rte.chk") !=
                   std::string::npos;
    assert(ok && "expected InvalidCast trap diagnostic with instruction index");
    return 0;
}
