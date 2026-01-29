//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/IntEqualityTests.cpp
// Purpose: Validate VM handlers for integer equality comparison opcodes
//          (ICmpEq, ICmpNe).
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
void buildIntCompareFunction(Module &module, Opcode op, int64_t lhs, int64_t rhs)
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

bool runIntCompare(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildIntCompareFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    const int64_t result = fixture.run(module);
    assert(result == 0 || result == 1);
    return result == 1;
}

} // namespace

int main()
{
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const int64_t maxVal = std::numeric_limits<int64_t>::max();

    //=========================================================================
    // ICmpEq tests (integer equality)
    //=========================================================================

    // Basic equality
    assert(runIntCompare(Opcode::ICmpEq, 0, 0) == true);
    assert(runIntCompare(Opcode::ICmpEq, 1, 1) == true);
    assert(runIntCompare(Opcode::ICmpEq, -1, -1) == true);
    assert(runIntCompare(Opcode::ICmpEq, 42, 42) == true);

    // Inequality
    assert(runIntCompare(Opcode::ICmpEq, 0, 1) == false);
    assert(runIntCompare(Opcode::ICmpEq, 1, 0) == false);
    assert(runIntCompare(Opcode::ICmpEq, -1, 1) == false);
    assert(runIntCompare(Opcode::ICmpEq, 1, -1) == false);

    // Edge cases
    assert(runIntCompare(Opcode::ICmpEq, minVal, minVal) == true);
    assert(runIntCompare(Opcode::ICmpEq, maxVal, maxVal) == true);
    assert(runIntCompare(Opcode::ICmpEq, minVal, maxVal) == false);
    assert(runIntCompare(Opcode::ICmpEq, maxVal, minVal) == false);

    // Adjacent values
    assert(runIntCompare(Opcode::ICmpEq, 0, -1) == false);
    assert(runIntCompare(Opcode::ICmpEq, maxVal, maxVal - 1) == false);
    assert(runIntCompare(Opcode::ICmpEq, minVal, minVal + 1) == false);

    //=========================================================================
    // ICmpNe tests (integer inequality)
    //=========================================================================

    // Basic inequality
    assert(runIntCompare(Opcode::ICmpNe, 0, 1) == true);
    assert(runIntCompare(Opcode::ICmpNe, 1, 0) == true);
    assert(runIntCompare(Opcode::ICmpNe, -1, 1) == true);
    assert(runIntCompare(Opcode::ICmpNe, 42, 43) == true);

    // Equality
    assert(runIntCompare(Opcode::ICmpNe, 0, 0) == false);
    assert(runIntCompare(Opcode::ICmpNe, 1, 1) == false);
    assert(runIntCompare(Opcode::ICmpNe, -1, -1) == false);
    assert(runIntCompare(Opcode::ICmpNe, 42, 42) == false);

    // Edge cases
    assert(runIntCompare(Opcode::ICmpNe, minVal, maxVal) == true);
    assert(runIntCompare(Opcode::ICmpNe, maxVal, minVal) == true);
    assert(runIntCompare(Opcode::ICmpNe, minVal, minVal) == false);
    assert(runIntCompare(Opcode::ICmpNe, maxVal, maxVal) == false);

    return 0;
}
