//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_peephole_optimizations.cpp
// Purpose: Regression tests for new peephole optimizations added during the
//          comprehensive codegen review. Tests verify:
//          - CBZ/CBNZ fusion (cmp #0 + b.cond → cbz/cbnz)
//          - MADD fusion (mul + add → madd)
//          - LDP/STP merging (adjacent ldr/str → ldp/stp)
//          - Branch inversion (b.cond + b → inverted b.cond)
//          - Immediate folding (AddRRR → AddRI when operand is known const)
//          - New opcode emission (cbnz, madd, csel, ldp, stp)
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/Peephole.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::aarch64;

// ===========================================================================
// CBZ/CBNZ fusion tests
// ===========================================================================

TEST(PeepholeOptimizations, CbzFusionCmpZeroBeq)
{
    // cmp x0, #0; b.eq label → cbz x0, label
    MFunction fn{};
    fn.name = "test_cbz_eq";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(MInstr{MOpcode::CmpRI,
                                  {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    block.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    MBasicBlock target{};
    target.name = ".Ltarget";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    auto stats = runPeephole(fn);

    // Should have fused cmp+b.eq into cbz
    EXPECT_TRUE(stats.cbzFusions >= 1);

    bool foundCbz = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::Cbz)
                foundCbz = true;
        }
    }
    EXPECT_TRUE(foundCbz);
}

TEST(PeepholeOptimizations, CbnzFusionCmpZeroBne)
{
    // cmp x0, #0; b.ne label → cbnz x0, label
    MFunction fn{};
    fn.name = "test_cbnz_ne";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(MInstr{MOpcode::CmpRI,
                                  {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    block.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    MBasicBlock target{};
    target.name = ".Ltarget";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    auto stats = runPeephole(fn);

    EXPECT_TRUE(stats.cbzFusions >= 1);

    bool foundCbnz = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::Cbnz)
                foundCbnz = true;
        }
    }
    EXPECT_TRUE(foundCbnz);
}

TEST(PeepholeOptimizations, CbzFusionTstBeq)
{
    // tst x0, x0; b.eq label → cbz x0, label
    MFunction fn{};
    fn.name = "test_cbz_tst";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(MInstr{MOpcode::TstRR,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    block.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    MBasicBlock target{};
    target.name = ".Ltarget";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.cbzFusions >= 1);
}

TEST(PeepholeOptimizations, CbzFusionSkipsNonZero)
{
    // cmp x0, #5; b.eq label → should NOT fuse (not comparing with zero)
    MFunction fn{};
    fn.name = "test_cbz_nonzero";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(MInstr{MOpcode::CmpRI,
                                  {MOperand::regOp(PhysReg::X0), MOperand::immOp(5)}});
    block.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    MBasicBlock target{};
    target.name = ".Ltarget";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.cbzFusions, 0);
}

TEST(PeepholeOptimizations, CbzFusionSkipsLtCondition)
{
    // cmp x0, #0; b.lt label → should NOT fuse (lt can't be expressed as cbz/cbnz)
    MFunction fn{};
    fn.name = "test_cbz_lt";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(MInstr{MOpcode::CmpRI,
                                  {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    block.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    MBasicBlock target{};
    target.name = ".Ltarget";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.cbzFusions, 0);
}

// ===========================================================================
// MADD fusion tests
// ===========================================================================

TEST(PeepholeOptimizations, MaddFusionMulAdd)
{
    // mul x2, x0, x1; add x3, x2, x4 → madd x3, x0, x1, x4
    MFunction fn{};
    fn.name = "test_madd";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MulRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X2),
                MOperand::regOp(PhysReg::X4)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.maddFusions >= 1);

    bool foundMadd = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::MAddRRRR)
                foundMadd = true;
        }
    }
    EXPECT_TRUE(foundMadd);
}

TEST(PeepholeOptimizations, MaddFusionCommutative)
{
    // mul x2, x0, x1; add x3, x4, x2 → madd x3, x0, x1, x4 (commutative add)
    MFunction fn{};
    fn.name = "test_madd_commute";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MulRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X4),
                MOperand::regOp(PhysReg::X2)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.maddFusions >= 1);
}

TEST(PeepholeOptimizations, MaddFusionSkipsWhenMulDstStillLive)
{
    // mul x2, x0, x1; add x3, x2, x4; use x2 → no fusion (x2 still live)
    MFunction fn{};
    fn.name = "test_madd_live";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MulRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X2),
                MOperand::regOp(PhysReg::X4)}});
    // x2 is still used here
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X5), MOperand::regOp(PhysReg::X2),
                MOperand::regOp(PhysReg::X6)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.maddFusions, 0);
}

// ===========================================================================
// LDP/STP merging tests
// ===========================================================================

TEST(PeepholeOptimizations, LdpMergeAdjacentLoads)
{
    // ldr x0, [fp, #-8]; ldr x1, [fp, #0] → ldp x0, x1, [fp, #-8]
    MFunction fn{};
    fn.name = "test_ldp";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(-8)}});
    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X1), MOperand::immOp(0)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.ldpStpMerges >= 1);

    bool foundLdp = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::LdpRegFpImm)
                foundLdp = true;
        }
    }
    EXPECT_TRUE(foundLdp);
}

TEST(PeepholeOptimizations, StpMergeAdjacentStores)
{
    // str x0, [fp, #-16]; str x1, [fp, #-8] → stp x0, x1, [fp, #-16]
    MFunction fn{};
    fn.name = "test_stp";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(-16)}});
    block.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X1), MOperand::immOp(-8)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.ldpStpMerges >= 1);

    bool foundStp = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::StpRegFpImm)
                foundStp = true;
        }
    }
    EXPECT_TRUE(foundStp);
}

TEST(PeepholeOptimizations, LdpFprMerge)
{
    // ldr d0, [fp, #-16]; ldr d1, [fp, #-8] → ldp d0, d1, [fp, #-16]
    MFunction fn{};
    fn.name = "test_ldp_fpr";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::LdrFprFpImm, {MOperand::regOp(PhysReg::V0), MOperand::immOp(-16)}});
    block.instrs.push_back(
        MInstr{MOpcode::LdrFprFpImm, {MOperand::regOp(PhysReg::V1), MOperand::immOp(-8)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.ldpStpMerges >= 1);
}

TEST(PeepholeOptimizations, LdpSkipsNonAdjacentOffsets)
{
    // ldr x0, [fp, #-16]; ldr x1, [fp, #0] → gap of 16, NOT adjacent → no merge
    MFunction fn{};
    fn.name = "test_ldp_nonadj";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(-16)}});
    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X1), MOperand::immOp(0)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.ldpStpMerges, 0);
}

TEST(PeepholeOptimizations, LdpSkipsSameReg)
{
    // ldr x0, [fp, #-8]; ldr x0, [fp, #0] → same destination → no merge
    MFunction fn{};
    fn.name = "test_ldp_samereg";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(-8)}});
    block.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.ldpStpMerges, 0);
}

// ===========================================================================
// Branch inversion tests
// ===========================================================================

TEST(PeepholeOptimizations, BranchInversion)
{
    // b.eq .Lnext; b .Lother (where .Lnext is the next block)
    // → b.ne .Lother
    MFunction fn{};
    fn.name = "test_binv";

    MBasicBlock entry{};
    entry.name = ".Lentry";
    entry.instrs.push_back(MInstr{MOpcode::CmpRR,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
    entry.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Lnext")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(".Lother")}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock next{};
    next.name = ".Lnext";
    next.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(next));

    MBasicBlock other{};
    other.name = ".Lother";
    other.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(other));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.branchInversions >= 1);

    // The entry block should now have b.ne .Lother (not b.eq .Lnext + b .Lother)
    const auto &entryBlock = fn.blocks[0];
    bool foundInvertedBranch = false;
    for (const auto &instr : entryBlock.instrs)
    {
        if (instr.opc == MOpcode::BCond && instr.ops.size() == 2 &&
            instr.ops[0].kind == MOperand::Kind::Cond && instr.ops[0].cond &&
            std::string(instr.ops[0].cond) == "ne" &&
            instr.ops[1].kind == MOperand::Kind::Label && instr.ops[1].label == ".Lother")
        {
            foundInvertedBranch = true;
        }
    }
    EXPECT_TRUE(foundInvertedBranch);

    // The unconditional branch should have been removed
    bool foundUnconditionalBr = false;
    for (const auto &instr : entryBlock.instrs)
    {
        if (instr.opc == MOpcode::Br)
            foundUnconditionalBr = true;
    }
    EXPECT_FALSE(foundUnconditionalBr);
}

TEST(PeepholeOptimizations, BranchInversionSkipsNonNext)
{
    // b.eq .Lother; b .Lnext (where .Lnext is the next block but bcond goes elsewhere)
    // → should NOT invert (bcond doesn't target next block)
    MFunction fn{};
    fn.name = "test_binv_skip";

    MBasicBlock entry{};
    entry.name = ".Lentry";
    entry.instrs.push_back(MInstr{MOpcode::CmpRR,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
    entry.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Lother")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(".Lnext")}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock next{};
    next.name = ".Lnext";
    next.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(next));

    MBasicBlock other{};
    other.name = ".Lother";
    other.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(other));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.branchInversions, 0);
    // But the unconditional branch to next block SHOULD be removed
    EXPECT_TRUE(stats.branchesToNextRemoved >= 1);
}

// ===========================================================================
// Immediate folding tests
// ===========================================================================

TEST(PeepholeOptimizations, ImmFoldingAddRRR)
{
    // mov x1, #42; add x2, x0, x1 → add x2, x0, #42
    MFunction fn{};
    fn.name = "test_immfold";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(42)}});
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.immFoldings >= 1);

    bool foundAddRI = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::AddRI)
                foundAddRI = true;
        }
    }
    EXPECT_TRUE(foundAddRI);
}

TEST(PeepholeOptimizations, ImmFoldingSubRRR)
{
    // mov x1, #100; sub x2, x0, x1 → sub x2, x0, #100
    MFunction fn{};
    fn.name = "test_immfold_sub";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(100)}});
    block.instrs.push_back(
        MInstr{MOpcode::SubRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_TRUE(stats.immFoldings >= 1);

    bool foundSubRI = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instrs)
        {
            if (instr.opc == MOpcode::SubRI)
                foundSubRI = true;
        }
    }
    EXPECT_TRUE(foundSubRI);
}

TEST(PeepholeOptimizations, ImmFoldingSkipsLargeImm)
{
    // mov x1, #5000; add x2, x0, x1 → NOT folded (>4095 = 12-bit limit)
    MFunction fn{};
    fn.name = "test_immfold_large";
    MBasicBlock block{};
    block.name = ".Lentry";

    block.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(5000)}});
    block.instrs.push_back(
        MInstr{MOpcode::AddRRR,
               {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X0),
                MOperand::regOp(PhysReg::X1)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    auto stats = runPeephole(fn);
    EXPECT_EQ(stats.immFoldings, 0);
}

// ===========================================================================
// New opcode emission tests
// ===========================================================================

TEST(PeepholeOptimizations, EmitCbnz)
{
    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_cbnz_emit";
    MBasicBlock block{};
    block.name = ".Lentry";
    block.instrs.push_back(
        MInstr{MOpcode::Cbnz, {MOperand::regOp(PhysReg::X0), MOperand::labelOp(".Ltarget")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    EXPECT_NE(output.find("cbnz x0, .Ltarget"), std::string::npos);
}

TEST(PeepholeOptimizations, EmitMadd)
{
    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_madd_emit";
    MBasicBlock block{};
    block.name = ".Lentry";
    block.instrs.push_back(MInstr{MOpcode::MAddRRRR,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1),
                                   MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X3)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    EXPECT_NE(output.find("madd x0, x1, x2, x3"), std::string::npos);
}

TEST(PeepholeOptimizations, EmitCsel)
{
    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_csel_emit";
    MBasicBlock block{};
    block.name = ".Lentry";
    block.instrs.push_back(MInstr{MOpcode::Csel,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1),
                                   MOperand::regOp(PhysReg::X2), MOperand::condOp("eq")}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    EXPECT_NE(output.find("csel x0, x1, x2, eq"), std::string::npos);
}

TEST(PeepholeOptimizations, EmitLdpStp)
{
    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_ldp_stp_emit";
    MBasicBlock block{};
    block.name = ".Lentry";
    block.instrs.push_back(MInstr{MOpcode::LdpRegFpImm,
                                  {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1),
                                   MOperand::immOp(-16)}});
    block.instrs.push_back(MInstr{MOpcode::StpRegFpImm,
                                  {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X3),
                                   MOperand::immOp(-32)}});
    block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(block));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    EXPECT_NE(output.find("ldp x0, x1, [x29, #-16]"), std::string::npos);
    EXPECT_NE(output.find("stp x2, x3, [x29, #-32]"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
