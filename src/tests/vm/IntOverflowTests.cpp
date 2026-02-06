//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/IntOverflowTests.cpp
// Purpose: Validate VM handlers for overflow-checking arithmetic opcodes
//          (IAddOvf, ISubOvf, IMulOvf) including trap behavior.
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
void buildOverflowFunction(Module &module, Opcode op, int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
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

int64_t runOverflow(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildOverflowFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

void expectOverflowTrap(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildOverflowFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find("Overflow") != std::string::npos);
}

} // namespace

int main()
{
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const int64_t maxVal = std::numeric_limits<int64_t>::max();

    //=========================================================================
    // IAddOvf tests (checked addition)
    //=========================================================================

    // Non-overflowing cases
    assert(runOverflow(Opcode::IAddOvf, 1, 2) == 3);
    assert(runOverflow(Opcode::IAddOvf, -1, 1) == 0);
    assert(runOverflow(Opcode::IAddOvf, 0, 0) == 0);
    assert(runOverflow(Opcode::IAddOvf, maxVal - 1, 1) == maxVal);
    assert(runOverflow(Opcode::IAddOvf, minVal + 1, -1) == minVal);

    // Overflowing cases - should trap
    expectOverflowTrap(Opcode::IAddOvf, maxVal, 1);      // positive overflow
    expectOverflowTrap(Opcode::IAddOvf, maxVal, maxVal); // large positive overflow
    expectOverflowTrap(Opcode::IAddOvf, minVal, -1);     // negative overflow
    expectOverflowTrap(Opcode::IAddOvf, minVal, minVal); // large negative overflow

    //=========================================================================
    // ISubOvf tests (checked subtraction)
    //=========================================================================

    // Non-overflowing cases
    assert(runOverflow(Opcode::ISubOvf, 5, 3) == 2);
    assert(runOverflow(Opcode::ISubOvf, 1, 1) == 0);
    assert(runOverflow(Opcode::ISubOvf, -1, -1) == 0);
    assert(runOverflow(Opcode::ISubOvf, minVal + 1, 1) == minVal);
    assert(runOverflow(Opcode::ISubOvf, maxVal - 1, -1) == maxVal);

    // Overflowing cases - should trap
    expectOverflowTrap(Opcode::ISubOvf, minVal, 1);      // negative overflow (MIN - 1)
    expectOverflowTrap(Opcode::ISubOvf, maxVal, -1);     // positive overflow (MAX - (-1))
    expectOverflowTrap(Opcode::ISubOvf, minVal, maxVal); // large overflow

    //=========================================================================
    // IMulOvf tests (checked multiplication)
    //=========================================================================

    // Non-overflowing cases
    assert(runOverflow(Opcode::IMulOvf, 2, 3) == 6);
    assert(runOverflow(Opcode::IMulOvf, -2, 3) == -6);
    assert(runOverflow(Opcode::IMulOvf, -2, -3) == 6);
    assert(runOverflow(Opcode::IMulOvf, 0, maxVal) == 0);
    assert(runOverflow(Opcode::IMulOvf, 1, minVal) == minVal);
    assert(runOverflow(Opcode::IMulOvf, -1, maxVal) == -maxVal);

    // Edge case: -1 * MIN overflows because -MIN > MAX
    expectOverflowTrap(Opcode::IMulOvf, -1, minVal);

    // Large multiplications that overflow
    expectOverflowTrap(Opcode::IMulOvf, maxVal, 2);
    expectOverflowTrap(Opcode::IMulOvf, minVal, 2);
    expectOverflowTrap(Opcode::IMulOvf, maxVal, maxVal);

    // Powers of 2 that overflow
    const int64_t largePos = int64_t{1} << 32;
    expectOverflowTrap(Opcode::IMulOvf, largePos, largePos);

    return 0;
}
