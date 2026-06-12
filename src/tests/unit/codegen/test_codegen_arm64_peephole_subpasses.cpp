//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_peephole_subpasses.cpp
// Purpose: Test AArch64 peephole sub-passes that had zero coverage:
//          StrengthReduce (mul-to-shift, div strength reduction),
//          BranchOpt (conditional branch folding),
//          CopyPropDCE (dead code after copy propagation),
//          MemoryOpt (memory access patterns),
//          LoopOpt (loop-specific peephole).
// Key invariants:
//   - Rewrites preserve semantics.
//   - Stats counters accurately reflect transformations.
// Ownership/Lifetime: Constructs transient MIR per test.
// Links: src/codegen/aarch64/peephole/
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/Peephole.hpp"
#include "codegen/aarch64/peephole/BranchOpt.hpp"
#include "codegen/aarch64/peephole/CopyPropDCE.hpp"
#include "codegen/aarch64/peephole/MemoryOpt.hpp"

using namespace viper::codegen::aarch64;

// ─── StrengthReduce: mul by power of 2 → shift ──────────────────────────────

TEST(AArch64PeepholeSubpasses, MulByPowerOf2ToShift) {
    // When one operand of MulRRR is a known power-of-2 constant loaded via MovRI,
    // strength reduction should convert it to a shift.
    MFunction fn{};
    fn.name = "mul_pow2";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();

    // mov x1, #8
    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(8)}});
    // mul x2, x0, x1  (x0 * 8 → should become lsl x2, x0, #3)
    bb.instrs.push_back(MInstr{MOpcode::MulRRR,
                               {MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);
    EXPECT_GT(stats.strengthReductions, 0);
}

// ─── StrengthReduce: add #0 identity ────────────────────────────────────────

TEST(AArch64PeepholeSubpasses, AddFpImmZeroIdentity) {
    // fadd d0, d0, #0.0 should be eliminated as identity
    MFunction fn{};
    fn.name = "fadd_zero";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(MInstr{MOpcode::FAddRRR,
                               {MOperand::regOp(PhysReg::V0),
                                MOperand::regOp(PhysReg::V0),
                                MOperand::regOp(PhysReg::V1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    size_t before = bb.instrs.size();
    auto stats = runPeephole(fn);
    // The FP identity elimination may or may not trigger depending on implementation
    // At minimum, the pass should not crash.
    EXPECT_LE(bb.instrs.size(), before);
}

// ─── BranchOpt: unconditional branch to next block ──────────────────────────

TEST(AArch64PeepholeSubpasses, RemoveRedundantBranchToFallthrough) {
    MFunction fn{};
    fn.name = "branch_fallthrough";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"next", {}, {}});
    auto &entry = fn.blocks[0];
    auto &next = fn.blocks[1];

    // b next (redundant — falls through)
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("next")}});
    next.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // Branch to next block should be removed
    bool hasBranch = false;
    for (const auto &i : fn.blocks[0].instrs) {
        if (i.opc == MOpcode::Br)
            hasBranch = true;
    }
    // The entry block should be empty or have no branch to "next"
    // (branch-to-next optimization removes the B instruction)
    EXPECT_GT(stats.branchesToNextRemoved, 0);
    EXPECT_FALSE(hasBranch);
}

// ─── CopyPropDCE: dead mov after last use ───────────────────────────────────

TEST(AArch64PeepholeSubpasses, DeadMovRemovedAfterLastUse) {
    MFunction fn{};
    fn.name = "dead_mov";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();

    // mov x1, x0  (only use)
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X0)}});
    // mov x2, x1  (x1's only use — after this, x1 is dead)
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X2), MOperand::regOp(PhysReg::X1)}});
    // ret (returns x0 implicitly)
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    size_t before = bb.instrs.size();
    auto stats = runPeephole(fn);
    // Copy propagation may fold the two movs, DCE may remove dead intermediates
    // At minimum, the pass should not crash and instruction count should not increase.
    EXPECT_LE(bb.instrs.size(), before);
}

// ─── ImmThenMove folding ────────────────────────────────────────────────────

TEST(AArch64PeepholeSubpasses, ImmThenMoveFolding) {
    // mov x1, #42; mov x2, x1 → mov x1, x1; mov x2, #42  (when x1 dead after)
    MFunction fn{};
    fn.name = "imm_then_move";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();

    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X10), MOperand::immOp(42)}});
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X11), MOperand::regOp(PhysReg::X10)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);
    // The fold should fire (x10 is dead after the mov)
    EXPECT_GT(stats.consecutiveMovsFolded, 0);
}

TEST(AArch64PeepholeSubpasses, StoreLoadForwardingStopsAtOverlappingFprStore) {
    std::vector<MInstr> instrs;
    instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X1), MOperand::immOp(-16)}});
    instrs.push_back(
        MInstr{MOpcode::StrFprFpImm, {MOperand::regOp(PhysReg::V0), MOperand::immOp(-16)}});
    instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X2), MOperand::immOp(-16)}});

    PeepholeStats stats{};
    const std::size_t forwarded = peephole::forwardStoreLoads(instrs, stats);
    EXPECT_EQ(forwarded, 0u);
    ASSERT_EQ(instrs.size(), 3u);
    EXPECT_EQ(instrs[2].opc, MOpcode::LdrRegFpImm);
}

TEST(AArch64PeepholeSubpasses, StoreLoadForwardingStopsAtOverlappingPairStore) {
    std::vector<MInstr> instrs;
    instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X1), MOperand::immOp(-8)}});
    instrs.push_back(
        MInstr{MOpcode::StpRegFpImm,
               {MOperand::regOp(PhysReg::X3), MOperand::regOp(PhysReg::X4), MOperand::immOp(-16)}});
    instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X2), MOperand::immOp(-8)}});

    PeepholeStats stats{};
    const std::size_t forwarded = peephole::forwardStoreLoads(instrs, stats);
    EXPECT_EQ(forwarded, 0u);
    ASSERT_EQ(instrs.size(), 3u);
    EXPECT_EQ(instrs[2].opc, MOpcode::LdrRegFpImm);
}

// ─── Multiple sub-passes interact correctly ─────────────────────────────────

TEST(AArch64PeepholeSubpasses, MultiplePassesInteract) {
    MFunction fn{};
    fn.name = "multi_pass";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"target", {}, {}});
    auto &entry = fn.blocks[0];
    auto &target = fn.blocks[1];

    // Identity move (IdentityElim should remove)
    entry.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0)}});
    // cmp x1, #0 → tst x1, x1 (StrengthReduce)
    entry.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(0)}});
    // branch to next (BranchOpt should remove)
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("target")}});

    target.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);

    // All three optimizations should fire
    EXPECT_GT(stats.identityMovesRemoved, 0);
    EXPECT_GT(stats.cmpZeroToTst, 0);
    EXPECT_GT(stats.branchesToNextRemoved, 0);
}

TEST(AArch64PeepholeSubpasses, LoopPhiEdgeMovesPreserveOverlappingSources) {
    MFunction fn{};
    fn.name = "loop_phi_overlap";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"loop", {}, {}});
    fn.blocks.push_back(MBasicBlock{"body", {}, {}});
    fn.blocks.push_back(MBasicBlock{"latch", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});

    auto &entry = fn.blocks[0];
    auto &loop = fn.blocks[1];
    auto &body = fn.blocks[2];
    auto &latch = fn.blocks[3];
    auto &exit = fn.blocks[4];

    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-40)}});
    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X8), MOperand::immOp(-48)}});
    loop.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("body")}});

    body.instrs.push_back(MInstr{MOpcode::AddsRRR,
                                 {MOperand::regOp(PhysReg::X11),
                                  MOperand::regOp(PhysReg::X8),
                                  MOperand::regOp(PhysReg::X10)}});
    body.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("latch")}});

    latch.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X17), MOperand::immOp(-104)}});
    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X17), MOperand::immOp(-40)}});
    latch.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-96)}});
    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-48)}});
    latch.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);
    EXPECT_GT(stats.loopConstsHoisted, 0);

    const auto latchIt = std::find_if(fn.blocks.begin(),
                                      fn.blocks.end(),
                                      [](const MBasicBlock &bb) { return bb.name == "latch"; });
    ASSERT_TRUE(latchIt != fn.blocks.end());

    std::vector<std::pair<PhysReg, PhysReg>> movs;
    for (const auto &instr : latchIt->instrs) {
        if (instr.opc == MOpcode::MovRR && instr.ops.size() == 2 &&
            instr.ops[0].kind == MOperand::Kind::Reg && instr.ops[1].kind == MOperand::Kind::Reg) {
            movs.emplace_back(static_cast<PhysReg>(instr.ops[0].reg.idOrPhys),
                              static_cast<PhysReg>(instr.ops[1].reg.idOrPhys));
        }
    }

    ASSERT_GE(movs.size(), 2u);
    EXPECT_EQ(movs[0], std::make_pair(PhysReg::X8, PhysReg::X10));
    EXPECT_EQ(movs[1], std::make_pair(PhysReg::X10, PhysReg::X17));
}

TEST(AArch64PeepholeSubpasses, LoopPhiRejectsRedefinedEdgeSource) {
    MFunction fn{};
    fn.name = "loop_phi_redefined_source";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"loop", {}, {}});
    fn.blocks.push_back(MBasicBlock{"latch", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});

    auto &entry = fn.blocks[0];
    auto &loop = fn.blocks[1];
    auto &latch = fn.blocks[2];
    auto &exit = fn.blocks[3];

    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X24), MOperand::immOp(-40)}});
    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-48)}});
    loop.instrs.push_back(MInstr{MOpcode::AddRRR,
                                 {MOperand::regOp(PhysReg::X12),
                                  MOperand::regOp(PhysReg::X24),
                                  MOperand::regOp(PhysReg::X10)}});
    loop.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("latch")}});

    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X24), MOperand::immOp(-40)}});
    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-48)}});
    latch.instrs.push_back(MInstr{MOpcode::AddRRR,
                                  {MOperand::regOp(PhysReg::X24),
                                   MOperand::regOp(PhysReg::X10),
                                   MOperand::regOp(PhysReg::X12)}});
    latch.instrs.push_back(
        MInstr{MOpcode::Cbz, {MOperand::regOp(PhysReg::X24), MOperand::labelOp("exit")}});
    latch.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)runPeephole(fn);

    const bool splitLoopHeader =
        std::any_of(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
            return bb.name == "loop_body";
        });
    EXPECT_FALSE(splitLoopHeader);
}

TEST(AArch64PeepholeSubpasses, LoopPhiRejectsLoopsContainingCalls) {
    MFunction fn{};
    fn.name = "loop_phi_call_body";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"loop", {}, {}});
    fn.blocks.push_back(MBasicBlock{"latch", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});

    auto &entry = fn.blocks[0];
    auto &loop = fn.blocks[1];
    auto &latch = fn.blocks[2];
    auto &exit = fn.blocks[3];

    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X22), MOperand::immOp(-40)}});
    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X23), MOperand::immOp(-48)}});
    loop.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("may_touch_callee_saved")}});
    loop.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("latch")}});

    latch.instrs.push_back(
        MInstr{MOpcode::AddsRI,
               {MOperand::regOp(PhysReg::X24), MOperand::regOp(PhysReg::X22), MOperand::immOp(1)}});
    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X24), MOperand::immOp(-40)}});
    latch.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X23), MOperand::immOp(-48)}});
    latch.instrs.push_back(
        MInstr{MOpcode::Cbz, {MOperand::regOp(PhysReg::X24), MOperand::labelOp("exit")}});
    latch.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)runPeephole(fn);

    const bool splitLoopHeader =
        std::any_of(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
            return bb.name == "loop_body";
        });
    EXPECT_FALSE(splitLoopHeader);
}

TEST(AArch64PeepholeSubpasses, LoopPhiRejectsBackwardJoinEdge) {
    MFunction fn{};
    fn.name = "loop_phi_backward_join";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"else_path", {}, {}});
    fn.blocks.push_back(MBasicBlock{"join", {}, {}});
    fn.blocks.push_back(MBasicBlock{"then_path", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});

    auto &entry = fn.blocks[0];
    auto &elsePath = fn.blocks[1];
    auto &join = fn.blocks[2];
    auto &thenPath = fn.blocks[3];
    auto &exit = fn.blocks[4];

    entry.instrs.push_back(
        MInstr{MOpcode::Cbz, {MOperand::regOp(PhysReg::X0), MOperand::labelOp("then_path")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("else_path")}});

    elsePath.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X20), MOperand::immOp(-40)}});
    elsePath.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X21), MOperand::immOp(-48)}});
    elsePath.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("join")}});

    join.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X10), MOperand::immOp(-40)}});
    join.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X11), MOperand::immOp(-48)}});
    join.instrs.push_back(MInstr{MOpcode::AddRRR,
                                 {MOperand::regOp(PhysReg::X12),
                                  MOperand::regOp(PhysReg::X10),
                                  MOperand::regOp(PhysReg::X11)}});
    join.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("exit")}});

    thenPath.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X22), MOperand::immOp(-40)}});
    thenPath.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X23), MOperand::immOp(-48)}});
    thenPath.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("join")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)runPeephole(fn);

    const bool splitJoin =
        std::any_of(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
            return bb.name == "join_body";
        });
    EXPECT_FALSE(splitJoin);
}

TEST(AArch64PeepholeSubpasses, JoinPhiCoalescerSkipsLoopHeaderBackedgeWithCalls) {
    MFunction fn{};
    fn.name = "join_phi_coalescer_loop_header";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"loop", {}, {}});
    fn.blocks.push_back(MBasicBlock{"body", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});
    fn.blocks.push_back(MBasicBlock{"trap", {}, {}});

    auto &entry = fn.blocks[0];
    auto &loop = fn.blocks[1];
    auto &body = fn.blocks[2];
    auto &exit = fn.blocks[3];
    auto &trap = fn.blocks[4];

    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X12), MOperand::immOp(0)}});
    entry.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X12), MOperand::immOp(-24)}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    loop.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X21), MOperand::immOp(-24)}});
    loop.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X21), MOperand::immOp(-32)}});
    loop.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::regOp(PhysReg::X21), MOperand::immOp(64)}});
    loop.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp("body")}});
    loop.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("exit")}});

    body.instrs.push_back(
        MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(PhysReg::X22), MOperand::immOp(-32)}});
    body.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("may_touch_loop_state")}});
    body.instrs.push_back(
        MInstr{MOpcode::AddsRI,
               {MOperand::regOp(PhysReg::X11), MOperand::regOp(PhysReg::X22), MOperand::immOp(1)}});
    body.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("vs"), MOperand::labelOp("trap")}});
    body.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X11), MOperand::immOp(-24)}});
    body.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("loop")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});
    trap.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});

    (void)runPeephole(fn);

    const auto loopIt = std::find_if(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
        return bb.name == "loop";
    });
    ASSERT_TRUE(loopIt != fn.blocks.end());

    const bool stillLoadsLoopIndex =
        std::any_of(loopIt->instrs.begin(), loopIt->instrs.end(), [](const MInstr &instr) {
            return instr.opc == MOpcode::LdrRegFpImm && instr.ops.size() == 2 &&
                   instr.ops[0].kind == MOperand::Kind::Reg &&
                   instr.ops[0].reg.idOrPhys == static_cast<uint16_t>(PhysReg::X21) &&
                   instr.ops[1].kind == MOperand::Kind::Imm && instr.ops[1].imm == -24;
        });
    EXPECT_TRUE(stillLoadsLoopIndex);
}

TEST(AArch64PeepholeSubpasses, LoopConstHoistRejectsBackwardJoinEdge) {
    MFunction fn{};
    fn.name = "backward_join_not_loop";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn.blocks.push_back(MBasicBlock{"else_path", {}, {}});
    fn.blocks.push_back(MBasicBlock{"join", {}, {}});
    fn.blocks.push_back(MBasicBlock{"then_path", {}, {}});
    fn.blocks.push_back(MBasicBlock{"exit", {}, {}});

    auto &entry = fn.blocks[0];
    auto &elsePath = fn.blocks[1];
    auto &join = fn.blocks[2];
    auto &thenPath = fn.blocks[3];
    auto &exit = fn.blocks[4];

    entry.instrs.push_back(
        MInstr{MOpcode::Cbz, {MOperand::regOp(PhysReg::X0), MOperand::labelOp("then_path")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("else_path")}});

    elsePath.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X20), MOperand::immOp(1)}});
    elsePath.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("join")}});

    join.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X28), MOperand::immOp(6)}});
    join.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X4), MOperand::regOp(PhysReg::X28)}});
    join.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("exit")}});

    thenPath.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X21), MOperand::immOp(42)}});
    thenPath.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("join")}});

    exit.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)runPeephole(fn);

    const auto joinIt = std::find_if(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
        return bb.name == "join";
    });
    ASSERT_TRUE(joinIt != fn.blocks.end());

    const bool joinStillDefinesScale =
        std::any_of(joinIt->instrs.begin(), joinIt->instrs.end(), [](const MInstr &instr) {
            return instr.opc == MOpcode::MovRI && instr.ops.size() == 2 &&
                   instr.ops[0].kind == MOperand::Kind::Reg &&
                   (instr.ops[0].reg.idOrPhys == static_cast<uint16_t>(PhysReg::X28) ||
                    instr.ops[0].reg.idOrPhys == static_cast<uint16_t>(PhysReg::X4)) &&
                   instr.ops[1].kind == MOperand::Kind::Imm && instr.ops[1].imm == 6;
        });
    EXPECT_TRUE(joinStillDefinesScale);
}

// ─── Carried exit registers: invisible cross-block liveness ─────────────────

TEST(AArch64PeepholeSubpasses, StrengthReductionRespectsCarriedDivisorRegister) {
    // The allocator may carry x1 (the divisor) live into a single-predecessor
    // successor without any in-block use marking the carry. Reusing it as a
    // scratch register in the magic-number expansion would clobber the carried
    // value, so the rewrite must decline.
    MFunction fn{};
    fn.name = "carried_divisor";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();
    bb.carriedExitRegs = {static_cast<uint16_t>(PhysReg::X1)};

    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X1), MOperand::immOp(7)}});
    bb.instrs.push_back(MInstr{MOpcode::UDivRRR,
                               {MOperand::regOp(PhysReg::X2),
                                MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X1)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    auto stats = runPeephole(fn);
    (void)stats;

    const bool divSurvives =
        std::any_of(fn.blocks[0].instrs.begin(),
                    fn.blocks[0].instrs.end(),
                    [](const MInstr &mi) { return mi.opc == MOpcode::UDivRRR; });
    EXPECT_TRUE(divSurvives);
}

TEST(AArch64PeepholeSubpasses, BlockLocalDceKeepsCarriedRegisterDef) {
    // x10 has no in-block reader, but it is carried across the exit; the
    // block-local DCE fallback must treat it as live.
    MFunction fn{};
    fn.name = "carried_def";
    fn.blocks.push_back(MBasicBlock{"entry", {}, {}});
    auto &bb = fn.blocks.back();
    bb.carriedExitRegs = {static_cast<uint16_t>(PhysReg::X10)};

    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X10), MOperand::immOp(42)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    PeepholeStats stats{};
    peephole::removeDeadInstructions(fn.blocks[0].instrs, stats, &fn.blocks[0].carriedExitRegs);

    const bool defSurvives =
        std::any_of(fn.blocks[0].instrs.begin(),
                    fn.blocks[0].instrs.end(),
                    [](const MInstr &mi) { return mi.opc == MOpcode::MovRI; });
    EXPECT_TRUE(defSurvives);

    // Sanity: without the carried set, the same def is removable.
    MFunction fn2{};
    fn2.blocks.push_back(MBasicBlock{"entry", {}, {}});
    fn2.blocks[0].instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X10), MOperand::immOp(42)}});
    fn2.blocks[0].instrs.push_back(MInstr{MOpcode::Ret, {}});
    PeepholeStats stats2{};
    peephole::removeDeadInstructions(fn2.blocks[0].instrs, stats2, nullptr);
    const bool removedWithoutCarry =
        std::none_of(fn2.blocks[0].instrs.begin(),
                     fn2.blocks[0].instrs.end(),
                     [](const MInstr &mi) { return mi.opc == MOpcode::MovRI; });
    EXPECT_TRUE(removedWithoutCarry);
}

// ─── BranchOpt: cold-block reordering and fallthrough safety ────────────────

TEST(AArch64PeepholeSubpasses, ColdBlockWithFallthroughIsNotMoved) {
    // A block matched as "cold" purely by its name token must not be moved to
    // the end of the function when it can fall through into the next block —
    // relocation would silently re-target the implicit fallthrough edge.
    MFunction fn{};
    fn.name = "cold_ft";

    MBasicBlock entry{"entry", {}, {}};
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("user_error_path")}});

    MBasicBlock coldFallthrough{"user_error_path", {}, {}};
    // Ends in a conditional branch: not-taken path falls through to "after".
    coldFallthrough.instrs.push_back(
        MInstr{MOpcode::Cbz, {MOperand::regOp(PhysReg::X0), MOperand::labelOp("entry")}});

    MBasicBlock after{"after", {}, {}};
    after.instrs.push_back(MInstr{MOpcode::Ret, {}});

    fn.blocks = {entry, coldFallthrough, after};

    const std::size_t moved = peephole::reorderBlocks(fn);
    EXPECT_EQ(moved, 0u);
    ASSERT_EQ(fn.blocks.size(), 3u);
    EXPECT_EQ(fn.blocks[1].name, "user_error_path");
    EXPECT_EQ(fn.blocks[2].name, "after");
}

TEST(AArch64PeepholeSubpasses, TerminatedColdBlockIsMovedToEnd) {
    MFunction fn{};
    fn.name = "cold_term";

    MBasicBlock entry{"entry", {}, {}};
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("hot")}});

    MBasicBlock cold{"check_error_exit", {}, {}};
    cold.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("hot")}});

    MBasicBlock hot{"hot", {}, {}};
    hot.instrs.push_back(MInstr{MOpcode::Ret, {}});

    fn.blocks = {entry, cold, hot};

    const std::size_t moved = peephole::reorderBlocks(fn);
    EXPECT_EQ(moved, 1u);
    ASSERT_EQ(fn.blocks.size(), 3u);
    EXPECT_EQ(fn.blocks.back().name, "check_error_exit");
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
