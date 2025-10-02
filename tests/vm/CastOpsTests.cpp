// File: tests/vm/CastOpsTests.cpp
// Purpose: Verify VM cast handlers for 1-bit truncation and zero-extension.
// License: MIT License. See LICENSE in the project root for details.

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

using namespace il::core;

namespace
{

int64_t runTrunc1(int64_t input)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I1), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr trunc;
    trunc.result = builder.reserveTempId();
    trunc.op = Opcode::Trunc1;
    trunc.type = Type(Type::Kind::I1);
    trunc.operands.push_back(Value::constInt(input));
    trunc.loc = {1, 1, 1};
    bb.instructions.push_back(trunc);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*trunc.result));
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    return vm.run();
}

int64_t runZext1(int64_t input)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr zext;
    zext.result = builder.reserveTempId();
    zext.op = Opcode::Zext1;
    zext.type = Type(Type::Kind::I64);
    zext.operands.push_back(Value::constInt(input));
    zext.loc = {1, 1, 1};
    bb.instructions.push_back(zext);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*zext.result));
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    return vm.run();
}

uint64_t runCastFpToUiRteChk(double input)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr cast;
    cast.result = builder.reserveTempId();
    cast.op = Opcode::CastFpToUiRteChk;
    cast.type = Type(Type::Kind::I64);
    cast.operands.push_back(Value::constFloat(input));
    cast.loc = {1, 1, 1};
    bb.instructions.push_back(cast);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*cast.result));
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    return static_cast<uint64_t>(vm.run());
}

std::string captureCastFpToUiTrap(double input)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr cast;
    cast.result = builder.reserveTempId();
    cast.op = Opcode::CastFpToUiRteChk;
    cast.type = Type(Type::Kind::I64);
    cast.operands.push_back(Value::constFloat(input));
    cast.loc = {1, 1, 1};
    bb.instructions.push_back(cast);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*cast.result));
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
    const std::array<std::pair<int64_t, int64_t>, 7> truncCases = {
        {{0, 0},
         {1, 1},
         {-1, 1},
         {2, 1},
         {-2, 1},
         {std::numeric_limits<int64_t>::min(), 1},
         {std::numeric_limits<int64_t>::max(), 1}}};

    for (const auto &[input, expected] : truncCases)
    {
        assert(runTrunc1(input) == expected);
    }

    const std::array<std::pair<int64_t, int64_t>, 2> zextCases = {{{0, 0}, {1, 1}}};

    for (const auto &[input, expected] : zextCases)
    {
        assert(runZext1(input) == expected);
    }

    const std::array<std::pair<double, uint64_t>, 6> fpToUiCases = {{{0.0, 0},
                                                                     {0.5, 0},
                                                                     {1.5, 2},
                                                                     {2.5, 2},
                                                                     {3.5, 4},
                                                                     {4294967296.5, 4294967296ULL}}};

    for (const auto &[input, expected] : fpToUiCases)
    {
        const uint64_t result = runCastFpToUiRteChk(input);
        assert(result == expected);
    }

    const std::string nanTrap = captureCastFpToUiTrap(std::numeric_limits<double>::quiet_NaN());
    assert(nanTrap.find("Overflow") != std::string::npos);

    const std::string negativeTrap = captureCastFpToUiTrap(-1.0);
    assert(negativeTrap.find("Overflow") != std::string::npos);

    const std::string maxTrap = captureCastFpToUiTrap(std::ldexp(1.0, 64));
    assert(maxTrap.find("Overflow") != std::string::npos);

    return 0;
}

