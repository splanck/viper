//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BitwiseXorTests.cpp
// Purpose: Validate VM handler for bitwise XOR opcode.
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace il::core;

namespace
{
void buildXorFunction(Module &module, int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr xorInstr;
    xorInstr.result = builder.reserveTempId();
    xorInstr.op = Opcode::Xor;
    xorInstr.type = Type(Type::Kind::I64);
    xorInstr.operands.push_back(Value::constInt(lhs));
    xorInstr.operands.push_back(Value::constInt(rhs));
    xorInstr.loc = {1, 1, 1};
    bb.instructions.push_back(xorInstr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*xorInstr.result));
    bb.instructions.push_back(ret);
}

int64_t runXor(int64_t lhs, int64_t rhs)
{
    Module module;
    buildXorFunction(module, lhs, rhs);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    // Basic XOR
    assert(runXor(0, 0) == 0);
    assert(runXor(1, 0) == 1);
    assert(runXor(0, 1) == 1);
    assert(runXor(1, 1) == 0);

    // XOR with itself is 0
    assert(runXor(12345, 12345) == 0);
    assert(runXor(-1, -1) == 0);

    // XOR is commutative
    assert(runXor(0xFF, 0x0F) == (0xFF ^ 0x0F));
    assert(runXor(0x0F, 0xFF) == (0x0F ^ 0xFF));

    // XOR with all 1s flips bits
    const int64_t allOnes = -1;
    assert(runXor(0, allOnes) == allOnes);
    assert(runXor(allOnes, 0) == allOnes);
    assert(runXor(0x5555555555555555LL, allOnes) == static_cast<int64_t>(0xAAAAAAAAAAAAAAAAULL));

    // Bit patterns
    assert(runXor(0xF0F0F0F0F0F0F0F0LL, 0x0F0F0F0F0F0F0F0FLL) == -1);

    // Large values
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const int64_t maxVal = std::numeric_limits<int64_t>::max();
    assert(runXor(minVal, maxVal) == -1);
    assert(runXor(minVal, 0) == minVal);
    assert(runXor(maxVal, 0) == maxVal);

    return 0;
}
