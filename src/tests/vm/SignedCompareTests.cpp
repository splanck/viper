//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/SignedCompareTests.cpp
// Purpose: Validate VM handlers for signed integer comparison opcodes
//          including edge cases with MIN/MAX values.
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
void buildSignedCompareFunction(Module &module, Opcode op, int64_t lhs, int64_t rhs)
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

bool runSignedCompare(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildSignedCompareFunction(module, op, lhs, rhs);
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
    // SCmpLT tests (signed less than)
    //=========================================================================

    // Basic comparisons
    assert(runSignedCompare(Opcode::SCmpLT, 1, 2) == true);
    assert(runSignedCompare(Opcode::SCmpLT, 2, 1) == false);
    assert(runSignedCompare(Opcode::SCmpLT, 1, 1) == false);

    // Negative numbers
    assert(runSignedCompare(Opcode::SCmpLT, -1, 0) == true);
    assert(runSignedCompare(Opcode::SCmpLT, -2, -1) == true);
    assert(runSignedCompare(Opcode::SCmpLT, 0, -1) == false);

    // Edge cases with MIN/MAX
    assert(runSignedCompare(Opcode::SCmpLT, minVal, 0) == true);
    assert(runSignedCompare(Opcode::SCmpLT, minVal, maxVal) == true);
    assert(runSignedCompare(Opcode::SCmpLT, maxVal, minVal) == false);
    assert(runSignedCompare(Opcode::SCmpLT, minVal, minVal) == false);
    assert(runSignedCompare(Opcode::SCmpLT, minVal, minVal + 1) == true);

    // Key difference from unsigned: high bit set means negative
    // In signed: -1 (0xFFFFFFFFFFFFFFFF) < 0
    // In unsigned: 0xFFFFFFFFFFFFFFFF > 0
    assert(runSignedCompare(Opcode::SCmpLT, -1, 0) == true);

    //=========================================================================
    // SCmpLE tests (signed less than or equal)
    //=========================================================================

    assert(runSignedCompare(Opcode::SCmpLE, 1, 2) == true);
    assert(runSignedCompare(Opcode::SCmpLE, 1, 1) == true);
    assert(runSignedCompare(Opcode::SCmpLE, 2, 1) == false);

    assert(runSignedCompare(Opcode::SCmpLE, -1, -1) == true);
    assert(runSignedCompare(Opcode::SCmpLE, -1, 0) == true);
    assert(runSignedCompare(Opcode::SCmpLE, 0, -1) == false);

    assert(runSignedCompare(Opcode::SCmpLE, minVal, minVal) == true);
    assert(runSignedCompare(Opcode::SCmpLE, maxVal, maxVal) == true);
    assert(runSignedCompare(Opcode::SCmpLE, minVal, maxVal) == true);

    //=========================================================================
    // SCmpGT tests (signed greater than)
    //=========================================================================

    assert(runSignedCompare(Opcode::SCmpGT, 2, 1) == true);
    assert(runSignedCompare(Opcode::SCmpGT, 1, 2) == false);
    assert(runSignedCompare(Opcode::SCmpGT, 1, 1) == false);

    assert(runSignedCompare(Opcode::SCmpGT, 0, -1) == true);
    assert(runSignedCompare(Opcode::SCmpGT, -1, -2) == true);
    assert(runSignedCompare(Opcode::SCmpGT, -1, 0) == false);

    assert(runSignedCompare(Opcode::SCmpGT, maxVal, minVal) == true);
    assert(runSignedCompare(Opcode::SCmpGT, maxVal, 0) == true);
    assert(runSignedCompare(Opcode::SCmpGT, 0, maxVal) == false);

    //=========================================================================
    // SCmpGE tests (signed greater than or equal)
    //=========================================================================

    assert(runSignedCompare(Opcode::SCmpGE, 2, 1) == true);
    assert(runSignedCompare(Opcode::SCmpGE, 1, 1) == true);
    assert(runSignedCompare(Opcode::SCmpGE, 1, 2) == false);

    assert(runSignedCompare(Opcode::SCmpGE, 0, 0) == true);
    assert(runSignedCompare(Opcode::SCmpGE, 0, -1) == true);
    assert(runSignedCompare(Opcode::SCmpGE, -1, 0) == false);

    assert(runSignedCompare(Opcode::SCmpGE, maxVal, maxVal) == true);
    assert(runSignedCompare(Opcode::SCmpGE, minVal, minVal) == true);
    assert(runSignedCompare(Opcode::SCmpGE, maxVal, minVal) == true);
    assert(runSignedCompare(Opcode::SCmpGE, minVal, maxVal) == false);

    //=========================================================================
    // Verify signed vs unsigned semantics
    //=========================================================================

    // The value -1 when interpreted as signed is less than 0
    // But when interpreted as unsigned (0xFFFFFFFFFFFFFFFF) is greater than 0
    // This test verifies we're using signed comparisons
    const int64_t negOne = -1;
    assert(runSignedCompare(Opcode::SCmpLT, negOne, 0) == true);
    assert(runSignedCompare(Opcode::SCmpGT, negOne, 0) == false);

    return 0;
}
