// File: tests/vm/IntUnsignedOpsTests.cpp
// Purpose: Validate VM handlers for signed/unsigned div/rem opcodes including traps.
// License: MIT License. See LICENSE in project root for details.

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>
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

void buildBinaryFunction(Module &module,
                         Opcode op,
                         Type::Kind type,
                         int64_t lhs,
                         int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(type);
    instr.operands.push_back(Value::constInt(lhs));
    instr.operands.push_back(Value::constInt(rhs));
    instr.loc = {1, 1, 1};
    bb.instructions.push_back(instr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*instr.result));
    bb.instructions.push_back(ret);
}

void expectDivideByZeroTrap(Opcode op)
{
    Module module;
    buildBinaryFunction(module, op, Type::Kind::I64, 1, 0);
    const std::string out = captureTrap(module);
    assert(out.find("DivideByZero (code=0)") != std::string::npos);
}
} // namespace

int main()
{
    {
        Module module;
        buildBinaryFunction(module, Opcode::SDiv, Type::Kind::I64, -9, 4);
        il::vm::VM vm(module);
        assert(vm.run() == -2);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::SRem, Type::Kind::I64, -9, 4);
        il::vm::VM vm(module);
        assert(vm.run() == -1);
    }

    {
        Module module;
        const int64_t lhs = -9;
        const int64_t rhs = 4;
        buildBinaryFunction(module, Opcode::UDiv, Type::Kind::I64, lhs, rhs);
        il::vm::VM vm(module);
        const int64_t expected = static_cast<int64_t>(static_cast<uint64_t>(lhs) /
                                                       static_cast<uint64_t>(rhs));
        assert(vm.run() == expected);
    }

    {
        Module module;
        const int64_t lhs = -3;
        const int64_t rhs = 5;
        buildBinaryFunction(module, Opcode::URem, Type::Kind::I64, lhs, rhs);
        il::vm::VM vm(module);
        const int64_t expected = static_cast<int64_t>(static_cast<uint64_t>(lhs) %
                                                       static_cast<uint64_t>(rhs));
        assert(vm.run() == expected);
    }

    {
        Module module;
        const int64_t lhs = -5;
        const int64_t rhs = 12;
        buildBinaryFunction(module, Opcode::And, Type::Kind::I64, lhs, rhs);
        il::vm::VM vm(module);
        assert(vm.run() == (lhs & rhs));
    }

    {
        Module module;
        const int64_t lhs = -5;
        const int64_t rhs = 12;
        buildBinaryFunction(module, Opcode::Or, Type::Kind::I64, lhs, rhs);
        il::vm::VM vm(module);
        assert(vm.run() == (lhs | rhs));
    }

    expectDivideByZeroTrap(Opcode::SDiv);
    expectDivideByZeroTrap(Opcode::UDiv);
    expectDivideByZeroTrap(Opcode::SRem);
    expectDivideByZeroTrap(Opcode::URem);

    return 0;
}
