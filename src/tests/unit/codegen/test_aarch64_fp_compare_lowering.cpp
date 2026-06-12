//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_fp_compare_lowering.cpp
// Purpose: Unit tests for AArch64 FCMP result materialization. After FCMP,
//          unordered operands set NZCV = 0011; the condition codes used for
//          the ordered predicates (eq, mi, ls, gt, ge) are all FALSE under
//          that state, so each IL FP compare must materialize as exactly ONE
//          CSET — the historical vc-mask (cset+cset+and per compare) encoded
//          the same value with two extra instructions.
//
// Key invariants:
//   - One CSET per FP compare; no AND/ORR masking instructions.
//   - Condition codes are the unordered-false family (mi/ls, not lt/le);
//     FCmpNE stays unordered-true via `ne`.
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/aarch64/FpCompareLowering.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/FpCompareLowering.hpp"
#include "codegen/aarch64/MachineIR.hpp"

#include <cstring>
#include <string>

using namespace viper::codegen::aarch64;

namespace {

/// Lower one FP compare result and return the produced instruction list.
std::vector<MInstr> lowerOne(il::core::Opcode op) {
    MBasicBlock out{};
    out.name = "entry";
    uint16_t nextVReg = 10;
    emitFpCompareResult(out, op, /*dst=*/1, nextVReg);
    return out.instrs;
}

void expectSingleCset(il::core::Opcode op, const char *expectedCond) {
    const auto instrs = lowerOne(op);
    ASSERT_EQ(instrs.size(), 1u);
    EXPECT_TRUE(instrs[0].opc == MOpcode::Cset);
    ASSERT_EQ(instrs[0].ops.size(), 2u);
    ASSERT_TRUE(instrs[0].ops[1].kind == MOperand::Kind::Cond);
    EXPECT_EQ(std::string(instrs[0].ops[1].cond), std::string(expectedCond));
}

} // namespace

TEST(AArch64FpCompare, OrderedPredicatesMaterializeAsSingleUnorderedFalseCset) {
    expectSingleCset(il::core::Opcode::FCmpEQ, "eq");
    expectSingleCset(il::core::Opcode::FCmpLT, "mi");
    expectSingleCset(il::core::Opcode::FCmpLE, "ls");
    expectSingleCset(il::core::Opcode::FCmpGT, "gt");
    expectSingleCset(il::core::Opcode::FCmpGE, "ge");
}

TEST(AArch64FpCompare, UnorderedAwarePredicatesKeepTheirSemantics) {
    expectSingleCset(il::core::Opcode::FCmpNE, "ne");   // unordered-true by IL spec
    expectSingleCset(il::core::Opcode::FCmpOrd, "vc");  // ordered <=> no overflow flag
    expectSingleCset(il::core::Opcode::FCmpUno, "vs");  // unordered <=> overflow flag
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
