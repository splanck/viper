//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/IntOpsTests.cpp
// Purpose: Validate integer VM op semantics for mixed signed cases and traps.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

using namespace il::core;

namespace
{
void buildBinaryFunction(Module &module, Opcode op, Type::Kind type, int64_t lhs, int64_t rhs)
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

void buildUnaryFunction(Module &module, Opcode op, Type::Kind type, int64_t operand)
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
    using viper::tests::VmFixture;

    VmFixture fixture;

    const struct
    {
        int64_t lhs;
        int64_t rhs;
        int64_t expected;
    } sremCases[] = {
        {-3, 2, -1},
        {3, -2, 1},
        {-3, -2, -1},
    };

    for (const auto &sample : sremCases)
    {
        Module module;
        buildBinaryFunction(module, Opcode::SRemChk0, Type::Kind::I32, sample.lhs, sample.rhs);
        const int64_t value = fixture.run(module);
        assert(value == sample.expected);
    }

    {
        Module module;
        buildBinaryFunction(
            module, Opcode::IAddOvf, Type::Kind::I16, std::numeric_limits<int16_t>::max(), 1);
        const std::string out = fixture.captureTrap(module);
        assert(out.find("Overflow (code=0)") != std::string::npos);
    }

    {
        Module module;
        buildBinaryFunction(
            module, Opcode::SDivChk0, Type::Kind::I16, std::numeric_limits<int16_t>::min(), -1);
        const std::string out = fixture.captureTrap(module);
        assert(out.find("Overflow (code=0)") != std::string::npos);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::UDivChk0, Type::Kind::I64, -1, 2);
        const int64_t value = fixture.run(module);
        assert(value == std::numeric_limits<int64_t>::max());
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::URemChk0, Type::Kind::I64, -1, 2);
        const int64_t value = fixture.run(module);
        assert(value == 1);
    }

    {
        Module module;
        buildUnaryFunction(module, Opcode::CastSiNarrowChk, Type::Kind::I16, 12345);
        const int64_t value = fixture.run(module);
        assert(value == 12345);
    }

    {
        Module module;
        buildUnaryFunction(
            module, Opcode::CastSiNarrowChk, Type::Kind::I16, std::numeric_limits<int32_t>::max());
        const std::string out = fixture.captureTrap(module);
        assert(out.find("Trap @main:entry#0 line 1: InvalidCast (code=0)") != std::string::npos);
    }

    return 0;
}
