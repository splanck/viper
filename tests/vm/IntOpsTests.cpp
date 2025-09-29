// File: tests/vm/IntOpsTests.cpp
// Purpose: Validate integer VM op semantics for mixed signed cases and traps.
// License: MIT License. See LICENSE in project root for details.

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>
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

void buildUnaryFunction(Module &module,
                        Opcode op,
                        Type::Kind type,
                        int64_t operand)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(type);
    instr.operands.push_back(Value::constInt(operand));
    instr.loc = {1, 1, 1};
    bb.instructions.push_back(instr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*instr.result));
    bb.instructions.push_back(ret);
}
} // namespace

int main()
{
    {
        Module module;
        buildBinaryFunction(module, Opcode::SRemChk0, Type::Kind::I32, -3, 2);
        il::vm::VM vm(module);
        assert(vm.run() == -1);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::SRemChk0, Type::Kind::I32, 3, -2);
        il::vm::VM vm(module);
        assert(vm.run() == 1);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::SRemChk0, Type::Kind::I32, -3, -2);
        il::vm::VM vm(module);
        assert(vm.run() == -1);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::IAddOvf, Type::Kind::I16, std::numeric_limits<int16_t>::max(), 1);
        const std::string out = captureTrap(module);
        assert(out.find("Overflow (code=0)") != std::string::npos);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::SDivChk0, Type::Kind::I16, std::numeric_limits<int16_t>::min(), -1);
        const std::string out = captureTrap(module);
        assert(out.find("Overflow (code=0)") != std::string::npos);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::UDivChk0, Type::Kind::I64, -1, 2);
        il::vm::VM vm(module);
        assert(vm.run() == std::numeric_limits<int64_t>::max());
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::URemChk0, Type::Kind::I64, -1, 2);
        il::vm::VM vm(module);
        assert(vm.run() == 1);
    }

    {
        Module module;
        buildUnaryFunction(module, Opcode::CastSiNarrowChk, Type::Kind::I16, 12345);
        il::vm::VM vm(module);
        assert(vm.run() == 12345);
    }

    {
        Module module;
        buildUnaryFunction(module, Opcode::CastSiNarrowChk, Type::Kind::I16, std::numeric_limits<int32_t>::max());
        const std::string out = captureTrap(module);
        assert(out.find("Trap @main#0 line 1: InvalidCast (code=0)") != std::string::npos);
    }

    return 0;
}
