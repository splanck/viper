//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_peephole.cpp
// Purpose: Verify AArch64 peephole optimizations work correctly.
//
// Tests:
// - Identity move removal (mov r, r)
// - Identity FPR move removal (fmov d, d)
// - Consecutive move folding
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/Peephole.hpp"

using namespace viper::codegen::aarch64;

/// @brief Test that identity GPR moves (mov r, r) are removed.
TEST(AArch64Peephole, RemoveIdentityMovRR)
{
    MFunction fn{};
    fn.name = "test_identity_mov";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x0, x1 (not identity)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
    // mov x0, x0 (identity - should be removed)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    // mov x2, x2 (identity - should be removed)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X2)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    EXPECT_EQ(bb.instrs.size(), 4U);

    auto stats = runPeephole(fn);

    // Should have removed 2 identity moves
    EXPECT_EQ(stats.identityMovesRemoved, 2);
    // Remaining: mov x0, x1 and ret
    EXPECT_EQ(bb.instrs.size(), 2U);
    EXPECT_EQ(bb.instrs[0].opc, MOpcode::MovRR);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::Ret);
}

/// @brief Test that identity FPR moves (fmov d, d) are removed.
TEST(AArch64Peephole, RemoveIdentityFMovRR)
{
    MFunction fn{};
    fn.name = "test_identity_fmov";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // fmov d0, d1 (not identity)
    bb.instrs.push_back(MInstr{MOpcode::FMovRR,
                               {MOperand::regOp(PhysReg::V0), MOperand::regOp(PhysReg::V1)}});
    // fmov d0, d0 (identity - should be removed)
    bb.instrs.push_back(MInstr{MOpcode::FMovRR,
                               {MOperand::regOp(PhysReg::V0), MOperand::regOp(PhysReg::V0)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    EXPECT_EQ(bb.instrs.size(), 3U);

    auto stats = runPeephole(fn);

    // Should have removed 1 identity FPR move
    EXPECT_EQ(stats.identityFMovesRemoved, 1);
    // Remaining: fmov d0, d1 and ret
    EXPECT_EQ(bb.instrs.size(), 2U);
    EXPECT_EQ(bb.instrs[0].opc, MOpcode::FMovRR);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::Ret);
}

/// @brief Test that consecutive moves are folded.
/// mov x1, x2; mov x3, x1 -> mov x3, x2 (if x1 is dead)
TEST(AArch64Peephole, FoldConsecutiveMoves)
{
    MFunction fn{};
    fn.name = "test_fold_moves";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x1, x2
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X2)}});
    // mov x3, x1 (can be folded to mov x3, x2 since x1 is not used after)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X1)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    EXPECT_EQ(bb.instrs.size(), 3U);

    auto stats = runPeephole(fn);

    // Should have folded 1 consecutive move pair
    EXPECT_EQ(stats.consecutiveMovsFolded, 1);
    // First move becomes identity (x2, x2) and gets removed
    // Second move becomes mov x3, x2
    // Final: mov x3, x2 and ret
    EXPECT_EQ(bb.instrs.size(), 2U);
    EXPECT_EQ(bb.instrs[0].opc, MOpcode::MovRR);
    // Verify the folded move has x3 as dst and x2 as src
    const auto &foldedMov = bb.instrs[0];
    EXPECT_EQ(static_cast<PhysReg>(foldedMov.ops[0].reg.idOrPhys), PhysReg::X3);
    EXPECT_EQ(static_cast<PhysReg>(foldedMov.ops[1].reg.idOrPhys), PhysReg::X2);
}

/// @brief Test that consecutive moves are NOT folded when the intermediate
/// register is used later.
TEST(AArch64Peephole, NoFoldWhenIntermediateLive)
{
    MFunction fn{};
    fn.name = "test_no_fold";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x1, x2
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X2)}});
    // mov x3, x1
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X1)}});
    // add x4, x1, x5 (x1 is still used, so we can't fold the moves above)
    bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                               {MOperand::regOp(PhysReg::X4),
                                MOperand::regOp(PhysReg::X1),
                                MOperand::regOp(PhysReg::X5)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    EXPECT_EQ(bb.instrs.size(), 4U);

    auto stats = runPeephole(fn);

    // Should NOT have folded because x1 is used later
    EXPECT_EQ(stats.consecutiveMovsFolded, 0);
    EXPECT_EQ(bb.instrs.size(), 4U);
}

/// @brief Test mixed identity moves and real operations.
TEST(AArch64Peephole, MixedOperations)
{
    MFunction fn{};
    fn.name = "test_mixed";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x0, x0 (identity)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    // add x1, x2, x3
    bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                               {MOperand::regOp(PhysReg::X1),
                                MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X3)}});
    // mov x4, x4 (identity)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X4), MOperand::regOp(PhysReg::X4)}});
    // sub x5, x6, x7
    bb.instrs.push_back(MInstr{MOpcode::SubRRR,
                               {MOperand::regOp(PhysReg::X5),
                                MOperand::regOp(PhysReg::X6),
                                MOperand::regOp(PhysReg::X7)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    EXPECT_EQ(bb.instrs.size(), 5U);

    auto stats = runPeephole(fn);

    // Should have removed 2 identity moves
    EXPECT_EQ(stats.identityMovesRemoved, 2);
    // Remaining: add, sub, ret
    EXPECT_EQ(bb.instrs.size(), 3U);
    EXPECT_EQ(bb.instrs[0].opc, MOpcode::AddRRR);
    EXPECT_EQ(bb.instrs[1].opc, MOpcode::SubRRR);
    EXPECT_EQ(bb.instrs[2].opc, MOpcode::Ret);
}

/// @brief Test that peephole produces correct assembly output.
TEST(AArch64Peephole, EmittedAssemblyNoIdentityMoves)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};

    MFunction fn{};
    fn.name = "test_emit";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // mov x0, x1 (real move)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
    // mov x0, x0 (identity - should be removed)
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    // add x2, x0, x3
    bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                               {MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X3)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    // Run peephole
    [[maybe_unused]] auto stats = runPeephole(fn);

    // Emit assembly
    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string asmText = os.str();

    // Count occurrences of "mov x0"
    std::size_t movCount = 0;
    std::size_t pos = 0;
    while ((pos = asmText.find("mov x0", pos)) != std::string::npos)
    {
        ++movCount;
        pos += 6;
    }

    // Should only have one "mov x0" (the mov x0, x1), not two
    EXPECT_EQ(movCount, 1U);
    // Should have the add instruction
    EXPECT_NE(asmText.find("add x2, x0, x3"), std::string::npos);
}

/// @brief Test statistics are accurate.
TEST(AArch64Peephole, StatsAccuracy)
{
    MFunction fn{};
    fn.name = "test_stats";
    fn.blocks.push_back(MBasicBlock{"entry", {}});
    auto &bb = fn.blocks.back();

    // 3 identity GPR moves
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::MovRR,
                               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X2)}});
    // 2 identity FPR moves
    bb.instrs.push_back(MInstr{MOpcode::FMovRR,
                               {MOperand::regOp(PhysReg::V0), MOperand::regOp(PhysReg::V0)}});
    bb.instrs.push_back(MInstr{MOpcode::FMovRR,
                               {MOperand::regOp(PhysReg::V1), MOperand::regOp(PhysReg::V1)}});
    // ret
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    EXPECT_EQ(stats.identityMovesRemoved, 3);
    EXPECT_EQ(stats.identityFMovesRemoved, 2);
    EXPECT_EQ(stats.total(), 5);
    // Only ret should remain
    EXPECT_EQ(bb.instrs.size(), 1U);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
