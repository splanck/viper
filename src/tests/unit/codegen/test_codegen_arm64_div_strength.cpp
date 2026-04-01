//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_div_strength.cpp
// Purpose: Verify division strength reduction in ARM64 peephole:
//          unsigned div by power-of-2 -> logical shift right.
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/Peephole.hpp"

using namespace viper::codegen::aarch64;

/// Unsigned division by 8 (power of 2) should become lsr by 3.
TEST(AArch64DivStrength, UDivByPowerOf2BecomesLsr) {
    MFunction fn{};
    fn.name = "test_udiv_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x1, #8 (load constant divisor)
    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(8)}});
    // udiv x0, x2, x1 (unsigned divide by 8)
    bb.instrs.push_back(MInstr{MOpcode::UDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // UDivRRR should have been rewritten to LsrRI
    EXPECT_TRUE(stats.strengthReductions >= 1);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::LsrRI);
    EXPECT_EQ(bb.instrs[1].ops[2].imm, 3); // log2(8) = 3
}

/// Unsigned division by 1 (2^0) should become lsr by 0 (identity).
TEST(AArch64DivStrength, UDivBy1BecomesLsr0) {
    MFunction fn{};
    fn.name = "test_udiv_by_1";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(1)}});
    bb.instrs.push_back(MInstr{MOpcode::UDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    EXPECT_TRUE(stats.strengthReductions >= 1);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::LsrRI);
    EXPECT_EQ(bb.instrs[1].ops[2].imm, 0); // log2(1) = 0
}

/// Signed division by power-of-2 should be reduced to sign-corrected shift.
/// For x / 4 (k=2): asr tmp, x, #63; lsr tmp, tmp, #62; add tmp, x, tmp; asr dst, tmp, #2
TEST(AArch64DivStrength, SDivByPowerOf2BecomesShift) {
    MFunction fn{};
    fn.name = "test_sdiv_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(4)}});
    bb.instrs.push_back(MInstr{MOpcode::SDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // SDivRRR should be expanded to 4 instructions:
    // mov x1, #4 (original, may be optimized away)
    // asr x1, x2, #63
    // lsr x1, x1, #62
    // add x1, x2, x1
    // asr x0, x1, #2
    EXPECT_TRUE(stats.strengthReductions >= 1);
    // The SDivRRR at original index 1 should now be an AsrRI (first of expansion)
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::AsrRI);
}

/// Signed division by arbitrary non-power-of-2 constant currently remains as SDIV.
/// The previous magic-number lowering was disabled after it regressed truncation-
/// toward-zero semantics in native O1 codegen.
TEST(AArch64DivStrength, SDivByConstantNotReducedYet) {
    MFunction fn{};
    fn.name = "test_sdiv_const";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(7)}});
    bb.instrs.push_back(MInstr{MOpcode::SDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    EXPECT_EQ(stats.strengthReductions, 0);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::SDivRRR);
}

/// Non-power-of-2 divisor for UDIV should not be reduced (unsigned magic not implemented).
TEST(AArch64DivStrength, UDivByNonPowerOf2NotReduced) {
    MFunction fn{};
    fn.name = "test_udiv_non_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(7)}});
    bb.instrs.push_back(MInstr{MOpcode::UDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // 7 is not power of 2, should remain as UDivRRR
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::UDivRRR);
}

/// UREM by power-of-2: udiv+msub -> and mask
TEST(AArch64DivStrength, URemByPowerOf2BecomesAnd) {
    MFunction fn{};
    fn.name = "test_urem_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x1, #8 (divisor)
    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(8)}});
    // udiv x3, x2, x1
    bb.instrs.push_back(MInstr{MOpcode::UDivRRR,
                               {MOperand::regOp(PhysReg::X3),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    // msub x0, x3, x1, x2  (x0 = x2 - x3*x1 = remainder)
    bb.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X3),
                                MOperand::regOp(PhysReg::X1),
                                MOperand::regOp(PhysReg::X2)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // UDIV+MSUB should be fused into AND dst, lhs, #7
    EXPECT_TRUE(stats.strengthReductions >= 1);
    // Find the AndRI instruction
    bool foundAnd = false;
    for (const auto &instr : bb.instrs) {
        if (instr.opc == MOpcode::AndRI) {
            foundAnd = true;
            EXPECT_EQ(instr.ops[2].imm, 7); // 8-1 = 7
            break;
        }
    }
    EXPECT_TRUE(foundAnd);
}

/// SREM by power-of-2: sdiv+msub -> sign-corrected and+sub
TEST(AArch64DivStrength, SRemByPowerOf2BecomesSignCorrectedAnd) {
    MFunction fn{};
    fn.name = "test_srem_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x1, #4 (divisor)
    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(4)}});
    // sdiv x3, x2, x1
    bb.instrs.push_back(MInstr{MOpcode::SDivRRR,
                               {MOperand::regOp(PhysReg::X3),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    // msub x0, x3, x1, x2  (x0 = x2 - x3*x1 = remainder)
    bb.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X3),
                                MOperand::regOp(PhysReg::X1),
                                MOperand::regOp(PhysReg::X2)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // SDIV+MSUB should be fused into sign-corrected sequence
    EXPECT_TRUE(stats.strengthReductions >= 1);
    // The SDivRRR and MSubRRRR should both be gone
    bool foundSDiv = false;
    bool foundMSub = false;
    for (const auto &instr : bb.instrs) {
        if (instr.opc == MOpcode::SDivRRR)
            foundSDiv = true;
        if (instr.opc == MOpcode::MSubRRRR)
            foundMSub = true;
    }
    EXPECT_FALSE(foundSDiv);
    EXPECT_FALSE(foundMSub);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
