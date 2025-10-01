// File: tests/vm/UnknownOpcodeTests.cpp
// Purpose: Ensure the VM traps gracefully when executing an opcode lacking a handler.
// Key invariants: Diagnostics must report InvalidOperation and include opcode mnemonic details.
// Ownership/Lifetime: Forks a child VM process to capture trap diagnostics.
// Links: docs/il-guide.md#reference

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

    Instr bad;
    bad.result = builder.reserveTempId();
    bad.op = Opcode::CastFpToUiRteChk;
    bad.type = Type(Type::Kind::I64);
    bad.operands.push_back(Value::constFloat(1.0));
    bad.loc = {1, 1, 1};
    bb.instructions.push_back(bad);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    const std::string out = captureTrap(module);
    const bool hasInvalidOperation = out.find("InvalidOperation") != std::string::npos;
    assert(hasInvalidOperation && "expected InvalidOperation trap for unhandled opcode");

    const bool hasDiagnostic = out.find("unimplemented opcode") != std::string::npos;
    assert(hasDiagnostic && "expected diagnostic to mention unimplemented opcode");

    const bool hasMnemonic = out.find("cast.fp_to_ui.rte.chk") != std::string::npos;
    assert(hasMnemonic && "expected diagnostic to include opcode mnemonic");

    return 0;
}
