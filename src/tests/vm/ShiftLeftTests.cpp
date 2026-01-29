//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/ShiftLeftTests.cpp
// Purpose: Validate VM handler for shift left opcode (Shl)
//          including edge cases with shift amounts.
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
void buildShlFunction(Module &module, int64_t val, int64_t shift)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = Opcode::Shl;
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

int64_t runShl(int64_t val, int64_t shift)
{
    Module module;
    buildShlFunction(module, val, shift);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // Basic shift left tests
    //=========================================================================

    // Shift by 0 is identity
    assert(runShl(1, 0) == 1);
    assert(runShl(42, 0) == 42);
    assert(runShl(-1, 0) == -1);

    // Basic shifts
    assert(runShl(1, 1) == 2);
    assert(runShl(1, 2) == 4);
    assert(runShl(1, 3) == 8);
    assert(runShl(1, 10) == 1024);

    // Shifting larger values
    assert(runShl(5, 2) == 20);
    assert(runShl(0xFF, 8) == 0xFF00);

    // Shift by 63 (maximum valid shift for 64-bit)
    assert(runShl(1, 63) == static_cast<int64_t>(1ULL << 63));

    //=========================================================================
    // Shift amount masking (shift amount masked to 6 bits for 64-bit)
    //=========================================================================

    // Shift amounts >= 64 are masked to lower 6 bits
    // 64 & 63 = 0, so shift by 64 is effectively shift by 0
    assert(runShl(1, 64) == runShl(1, 0));

    // 65 & 63 = 1, so shift by 65 is effectively shift by 1
    assert(runShl(1, 65) == runShl(1, 1));

    // 128 & 63 = 0
    assert(runShl(1, 128) == runShl(1, 0));

    //=========================================================================
    // Negative values
    //=========================================================================

    // Shifting negative values
    assert(runShl(-1, 1) == -2);
    assert(runShl(-1, 2) == -4);

    // High bits shift out
    assert(runShl(-1, 63) == static_cast<int64_t>(1ULL << 63));

    //=========================================================================
    // Edge cases
    //=========================================================================

    // Zero shifted by anything is zero
    assert(runShl(0, 0) == 0);
    assert(runShl(0, 63) == 0);

    // Negative shift amounts - masked like positive
    // -1 as uint64 is 0xFFFFFFFFFFFFFFFF, masked to 63
    assert(runShl(1, -1) == runShl(1, 63));

    return 0;
}
