//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/ShiftConformanceTests.cpp
// Purpose: Conformance tests for arithmetic and logical shift right (AShr,
//          LShr) including sign extension, zero extension, and shift amount
//          masking. Complements ShiftLeftTests.cpp which covers Shl.
//
// Semantics (see docs/arithmetic-semantics.md):
//   - Shift amounts masked to [0, 63] via `shift & 63`.
//   - AShr: arithmetic (sign-extending) right shift.
//   - LShr: logical (zero-extending) right shift.
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

void buildShiftFunction(Module &module, Opcode op, int64_t val, int64_t shift)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(Value::constInt(val));
    instr.operands.push_back(Value::constInt(shift));
    instr.loc = {1, 1, 1};
    bb.instructions.push_back(instr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*instr.result));
    bb.instructions.push_back(ret);
}

int64_t runShift(Opcode op, int64_t val, int64_t shift)
{
    Module module;
    buildShiftFunction(module, op, val, shift);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runAShr(int64_t val, int64_t shift)
{
    return runShift(Opcode::AShr, val, shift);
}

int64_t runLShr(int64_t val, int64_t shift)
{
    return runShift(Opcode::LShr, val, shift);
}

} // namespace

int main()
{
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const int64_t maxVal = std::numeric_limits<int64_t>::max();

    //=========================================================================
    // AShr (Arithmetic Shift Right) — sign-extending
    //=========================================================================

    // Shift by 0 is identity
    assert(runAShr(1, 0) == 1);
    assert(runAShr(-1, 0) == -1);
    assert(runAShr(42, 0) == 42);

    // Basic positive shifts
    assert(runAShr(8, 1) == 4);
    assert(runAShr(8, 2) == 2);
    assert(runAShr(8, 3) == 1);
    assert(runAShr(1024, 10) == 1);

    // Negative values — sign bit extends
    assert(runAShr(-8, 1) == -4);
    assert(runAShr(-8, 2) == -2);
    assert(runAShr(-8, 3) == -1);
    assert(runAShr(-1, 1) == -1);      // All ones stays all ones
    assert(runAShr(-1, 63) == -1);     // Still all ones at max shift

    // MAX shifted right
    assert(runAShr(maxVal, 1) == maxVal / 2);
    assert(runAShr(maxVal, 62) == 1);
    assert(runAShr(maxVal, 63) == 0);  // Positive, sign bit 0 → 0

    // MIN shifted right
    assert(runAShr(minVal, 1) == minVal / 2);     // -4611686018427387904
    assert(runAShr(minVal, 62) == -2);
    assert(runAShr(minVal, 63) == -1);            // Sign fills

    // Odd values — truncation toward negative infinity
    assert(runAShr(-7, 1) == -4);   // -7 >> 1 = -4 (floor division)
    assert(runAShr(7, 1) == 3);     // 7 >> 1 = 3

    //=========================================================================
    // AShr shift amount masking
    //=========================================================================

    // Shift by 64 masked to 0 → identity
    assert(runAShr(1, 64) == runAShr(1, 0));
    assert(runAShr(-1, 64) == runAShr(-1, 0));

    // Shift by 65 masked to 1
    assert(runAShr(8, 65) == runAShr(8, 1));

    // Shift by 128 masked to 0
    assert(runAShr(42, 128) == runAShr(42, 0));

    // Negative shift amount: -1 as uint64 → masked to 63
    assert(runAShr(1, -1) == runAShr(1, 63));
    assert(runAShr(-1, -1) == runAShr(-1, 63));

    //=========================================================================
    // LShr (Logical Shift Right) — zero-extending
    //=========================================================================

    // Shift by 0 is identity
    assert(runLShr(1, 0) == 1);
    assert(runLShr(-1, 0) == -1);
    assert(runLShr(42, 0) == 42);

    // Basic positive shifts (same as AShr for positive values)
    assert(runLShr(8, 1) == 4);
    assert(runLShr(8, 2) == 2);
    assert(runLShr(8, 3) == 1);
    assert(runLShr(1024, 10) == 1);

    // Negative values — zero extends instead of sign extends
    // -1 = 0xFFFFFFFFFFFFFFFF
    assert(runLShr(-1, 1) == maxVal);    // 0x7FFFFFFFFFFFFFFF
    assert(runLShr(-1, 63) == 1);        // Only the high bit remains

    // -8 = 0xFFFFFFFFFFFFFFF8
    // LShr by 1: 0x7FFFFFFFFFFFFFFC = 9223372036854775804
    assert(runLShr(-8, 1) == static_cast<int64_t>(static_cast<uint64_t>(-8) >> 1));
    assert(runLShr(-8, 3) == static_cast<int64_t>(static_cast<uint64_t>(-8) >> 3));

    // MAX shifted right (same as AShr for positive values)
    assert(runLShr(maxVal, 1) == maxVal / 2);
    assert(runLShr(maxVal, 63) == 0);

    // MIN shifted right — LShr produces positive result
    // MIN = 0x8000000000000000
    assert(runLShr(minVal, 1) == static_cast<int64_t>(1ULL << 62));
    assert(runLShr(minVal, 63) == 1);

    //=========================================================================
    // LShr shift amount masking
    //=========================================================================

    // Shift by 64 masked to 0 → identity
    assert(runLShr(1, 64) == runLShr(1, 0));
    assert(runLShr(-1, 64) == runLShr(-1, 0));

    // Shift by 65 masked to 1
    assert(runLShr(8, 65) == runLShr(8, 1));

    // Shift by 128 masked to 0
    assert(runLShr(42, 128) == runLShr(42, 0));

    // Negative shift amount: masked to 63
    assert(runLShr(1, -1) == runLShr(1, 63));

    //=========================================================================
    // Zero edge cases
    //=========================================================================

    assert(runAShr(0, 0) == 0);
    assert(runAShr(0, 63) == 0);
    assert(runLShr(0, 0) == 0);
    assert(runLShr(0, 63) == 0);

    //=========================================================================
    // AShr vs LShr contrast — key difference
    //=========================================================================

    // For positive values, AShr and LShr agree
    assert(runAShr(maxVal, 1) == runLShr(maxVal, 1));
    assert(runAShr(100, 3) == runLShr(100, 3));

    // For negative values, they diverge
    assert(runAShr(-1, 1) == -1);          // Sign extends
    assert(runLShr(-1, 1) == maxVal);      // Zero extends
    assert(runAShr(minVal, 63) == -1);     // All sign bits
    assert(runLShr(minVal, 63) == 1);      // Just the former sign bit

    return 0;
}
