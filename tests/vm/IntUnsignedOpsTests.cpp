// File: tests/vm/IntUnsignedOpsTests.cpp
// Purpose: Validate VM handlers for signed/unsigned div/rem opcodes including traps.
// License: MIT License. See LICENSE in project root for details.

#include "il/build/IRBuilder.hpp"
#include "common/VmFixture.hpp"

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

void buildComparisonFunction(Module &module, Opcode op, int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I1), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(Type::Kind::I1);
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

bool runUnsignedCompare(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildComparisonFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    const int64_t raw = fixture.run(module);
    assert(raw == 0 || raw == 1);
    return raw == 1;
}

void expectDivideByZeroTrap(Opcode op)
{
    Module module;
    buildBinaryFunction(module, op, Type::Kind::I64, 1, 0);
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find("DivideByZero (code=0)") != std::string::npos);
}
} // namespace

int main()
{
    using viper::tests::VmFixture;

    VmFixture fixture;

    {
        Module module;
        buildBinaryFunction(module, Opcode::SDiv, Type::Kind::I64, -9, 4);
        const int64_t value = fixture.run(module);
        assert(value == -2);
    }

    {
        Module module;
        buildBinaryFunction(module, Opcode::SRem, Type::Kind::I64, -9, 4);
        const int64_t value = fixture.run(module);
        assert(value == -1);
    }

    {
        Module module;
        const int64_t lhs = -9;
        const int64_t rhs = 4;
        buildBinaryFunction(module, Opcode::UDiv, Type::Kind::I64, lhs, rhs);
        const int64_t expected =
            static_cast<int64_t>(static_cast<uint64_t>(lhs) / static_cast<uint64_t>(rhs));
        assert(fixture.run(module) == expected);
    }

    {
        Module module;
        const int64_t lhs = -3;
        const int64_t rhs = 5;
        buildBinaryFunction(module, Opcode::URem, Type::Kind::I64, lhs, rhs);
        const int64_t expected =
            static_cast<int64_t>(static_cast<uint64_t>(lhs) % static_cast<uint64_t>(rhs));
        assert(fixture.run(module) == expected);
    }

    {
        Module module;
        const int64_t lhs = -5;
        const int64_t rhs = 12;
        buildBinaryFunction(module, Opcode::And, Type::Kind::I64, lhs, rhs);
        assert(fixture.run(module) == (lhs & rhs));
    }

    {
        Module module;
        const int64_t lhs = -5;
        const int64_t rhs = 12;
        buildBinaryFunction(module, Opcode::Or, Type::Kind::I64, lhs, rhs);
        assert(fixture.run(module) == (lhs | rhs));
    }

    {
        Module module;
        const int64_t lhs = -8;
        const int64_t rhs = 1;
        buildBinaryFunction(module, Opcode::LShr, Type::Kind::I64, lhs, rhs);
        const uint64_t shift = static_cast<uint64_t>(rhs) & 63U;
        const int64_t expected = static_cast<int64_t>(static_cast<uint64_t>(lhs) >> shift);
        assert(fixture.run(module) == expected);
    }

    {
        Module module;
        const int64_t lhs = -8;
        const int64_t rhs = 1;
        buildBinaryFunction(module, Opcode::AShr, Type::Kind::I64, lhs, rhs);
        const int64_t result = fixture.run(module);
        const uint64_t shift = static_cast<uint64_t>(rhs) & 63U;
        uint64_t bits = static_cast<uint64_t>(lhs);
        uint64_t shifted = bits >> shift;
        if (shift != 0 && (bits & (uint64_t{1} << 63U)) != 0)
        {
            shifted |= (~uint64_t{0}) << (64U - shift);
        }
        const int64_t expected = static_cast<int64_t>(shifted);
        assert(result == expected);
        assert(result != static_cast<int64_t>(static_cast<uint64_t>(lhs) >> shift));
    }

    {
        const int64_t highBit = std::numeric_limits<int64_t>::min();
        assert(runUnsignedCompare(Opcode::UCmpLT, 0, highBit));
        assert(!runUnsignedCompare(Opcode::UCmpLT, highBit, 0));
        assert(runUnsignedCompare(Opcode::UCmpLE, 0, 0));
        assert(!runUnsignedCompare(Opcode::UCmpLE, highBit, 0));
        assert(runUnsignedCompare(Opcode::UCmpGT, highBit, 0));
        assert(!runUnsignedCompare(Opcode::UCmpGT, 0, highBit));
        assert(runUnsignedCompare(Opcode::UCmpGE, highBit, highBit));
        assert(!runUnsignedCompare(Opcode::UCmpGE, 0, highBit));
    }

    expectDivideByZeroTrap(Opcode::SDiv);
    expectDivideByZeroTrap(Opcode::UDiv);
    expectDivideByZeroTrap(Opcode::SRem);
    expectDivideByZeroTrap(Opcode::URem);

    return 0;
}
