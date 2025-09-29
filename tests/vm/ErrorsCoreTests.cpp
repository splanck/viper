// File: tests/vm/ErrorsCoreTests.cpp
// Purpose: Verify trap.kind emits structured trap diagnostics with kind, IP, and line info.
// Key invariants: Diagnostics must include the requested trap kind, instruction index, and source line.
// Ownership/Lifetime: Spawns child VM processes to capture stderr for each trap sample.
// Links: docs/specs/errors.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace il::core;

namespace
{
std::string captureTrap(il::vm::TrapKind kind, int line)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr trap;
    trap.op = Opcode::TrapKind;
    trap.type = Type(Type::Kind::Void);
    trap.operands.push_back(Value::constInt(static_cast<long long>(kind)));
    trap.loc = {1, static_cast<uint32_t>(line), 1};
    bb.instructions.push_back(trap);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, static_cast<uint32_t>(line), 1};
    bb.instructions.push_back(ret);

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
    struct Sample
    {
        il::vm::TrapKind kind;
        int line;
        const char *token;
    };

    const std::array<Sample, 3> samples = {{{il::vm::TrapKind::DivideByZero, 5, "DivideByZero"},
                                            {il::vm::TrapKind::Bounds, 9, "Bounds"},
                                            {il::vm::TrapKind::RuntimeError, 13, "RuntimeError"}}};

    for (const auto &sample : samples)
    {
        const std::string out = captureTrap(sample.kind, sample.line);
        const bool hasKind = out.find(sample.token) != std::string::npos;
        assert(hasKind && "trap.kind diagnostic must include the trap kind");

        const bool hasIp = out.find("#0") != std::string::npos;
        assert(hasIp && "trap.kind diagnostic must include instruction index");

        const std::string lineToken = "line " + std::to_string(sample.line);
        const bool hasLine = out.find(lineToken) != std::string::npos;
        assert(hasLine && "trap.kind diagnostic must include source line");

        const bool hasCode = out.find("code=0") != std::string::npos;
        assert(hasCode && "trap.kind should default code to zero");
    }

    return 0;
}
