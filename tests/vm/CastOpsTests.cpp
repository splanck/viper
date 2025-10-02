// File: tests/vm/CastOpsTests.cpp
// Purpose: Verify VM cast handlers for 1-bit truncation and zero-extension.
// License: MIT License. See LICENSE in the project root for details.

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
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

    return 0;
}

