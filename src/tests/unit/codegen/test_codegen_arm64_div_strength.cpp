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
TEST(AArch64DivStrength, UDivByPowerOf2BecomesLsr)
{
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
TEST(AArch64DivStrength, UDivBy1BecomesLsr0)
{
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

/// Signed division should NOT be strength-reduced (rounding differs).
TEST(AArch64DivStrength, SDivByPowerOf2NotReduced)
{
    MFunction fn{};
    fn.name = "test_sdiv_no_reduce";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(4)}});
    bb.instrs.push_back(MInstr{MOpcode::SDivRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // SDivRRR should NOT be converted (signed division rounds differently)
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::SDivRRR);
}

/// Non-power-of-2 divisor should not be reduced.
TEST(AArch64DivStrength, UDivByNonPowerOf2NotReduced)
{
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

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
