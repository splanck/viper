//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_review_fixes.cpp
// Purpose: Regression tests for bugs found during the comprehensive backend
//          codegen review. Each test covers a specific fix to prevent regression.
//
//===----------------------------------------------------------------------===//
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/ra/Liveness.hpp"
#include "codegen/aarch64/ra/OpcodeClassify.hpp"
#include "codegen/aarch64/ra/RegPools.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace viper::codegen::aarch64;

// ---------------------------------------------------------------------------
// Fix 1: V8-V15 must NOT appear in callerSavedFPR (AAPCS64 compliance)
// ---------------------------------------------------------------------------

TEST(CodegenReviewFix, CalleeSavedFPRNotInCallerSaved) {
    const auto &ti = darwinTarget();

    // V8-V15 are callee-saved per AAPCS64 — they must NOT be in callerSavedFPR
    for (auto csReg : ti.calleeSavedFPR) {
        bool foundInCallerSaved =
            std::find(ti.callerSavedFPR.begin(), ti.callerSavedFPR.end(), csReg) !=
            ti.callerSavedFPR.end();
        EXPECT_FALSE(foundInCallerSaved);
    }
}

TEST(CodegenReviewFix, CallerSavedFPRExcludesV8toV15) {
    const auto &ti = darwinTarget();

    // Specifically verify V8-V15 are absent from callerSavedFPR
    const PhysReg calleeSaved[] = {PhysReg::V8,
                                   PhysReg::V9,
                                   PhysReg::V10,
                                   PhysReg::V11,
                                   PhysReg::V12,
                                   PhysReg::V13,
                                   PhysReg::V14,
                                   PhysReg::V15};
    for (auto reg : calleeSaved) {
        bool inCallerSaved = std::find(ti.callerSavedFPR.begin(), ti.callerSavedFPR.end(), reg) !=
                             ti.callerSavedFPR.end();
        EXPECT_FALSE(inCallerSaved);
    }
}

TEST(CodegenReviewFix, CallerSavedFPRIncludesV0toV7) {
    const auto &ti = darwinTarget();

    // V0-V7 are caller-saved (argument/return registers)
    const PhysReg argRegs[] = {PhysReg::V0,
                               PhysReg::V1,
                               PhysReg::V2,
                               PhysReg::V3,
                               PhysReg::V4,
                               PhysReg::V5,
                               PhysReg::V6,
                               PhysReg::V7};
    for (auto reg : argRegs) {
        bool inCallerSaved = std::find(ti.callerSavedFPR.begin(), ti.callerSavedFPR.end(), reg) !=
                             ti.callerSavedFPR.end();
        EXPECT_TRUE(inCallerSaved);
    }
}

TEST(CodegenReviewFix, CallerSavedFPRIncludesV16toV31) {
    const auto &ti = darwinTarget();

    // V16-V31 are caller-saved
    const PhysReg highRegs[] = {PhysReg::V16,
                                PhysReg::V17,
                                PhysReg::V18,
                                PhysReg::V19,
                                PhysReg::V20,
                                PhysReg::V21,
                                PhysReg::V22,
                                PhysReg::V23,
                                PhysReg::V24,
                                PhysReg::V25,
                                PhysReg::V26,
                                PhysReg::V27,
                                PhysReg::V28,
                                PhysReg::V29,
                                PhysReg::V30,
                                PhysReg::V31};
    for (auto reg : highRegs) {
        bool inCallerSaved = std::find(ti.callerSavedFPR.begin(), ti.callerSavedFPR.end(), reg) !=
                             ti.callerSavedFPR.end();
        EXPECT_TRUE(inCallerSaved);
    }
}

TEST(CodegenReviewFix, CalleeSavedFPRIsV8toV15) {
    const auto &ti = darwinTarget();

    // Exactly V8-V15 should be callee-saved
    EXPECT_EQ(ti.calleeSavedFPR.size(), 8U);
    EXPECT_EQ(ti.calleeSavedFPR[0], PhysReg::V8);
    EXPECT_EQ(ti.calleeSavedFPR[7], PhysReg::V15);
}

TEST(CodegenReviewFix, CallerSavedFPRCount) {
    const auto &ti = darwinTarget();

    // Should be V0-V7 (8) + V16-V31 (16) = 24 caller-saved FPRs
    EXPECT_EQ(ti.callerSavedFPR.size(), 24U);
}

// ---------------------------------------------------------------------------
// Fix: GPR sets are disjoint (no register in both caller and callee saved)
// ---------------------------------------------------------------------------

TEST(CodegenReviewFix, GPRSetsAreDisjoint) {
    const auto &ti = darwinTarget();

    for (auto csReg : ti.calleeSavedGPR) {
        bool foundInCallerSaved =
            std::find(ti.callerSavedGPR.begin(), ti.callerSavedGPR.end(), csReg) !=
            ti.callerSavedGPR.end();
        EXPECT_FALSE(foundInCallerSaved);
    }
}

TEST(CodegenReviewFix, FramePointerNotAllocatorCalleeSaved) {
    const auto &ti = darwinTarget();
    EXPECT_TRUE(std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), PhysReg::X29) ==
                ti.calleeSavedGPR.end());
}

TEST(CodegenReviewFix, CbzCbnzAreTerminatorsButStillAllocateOperands) {
    EXPECT_TRUE(ra::isTerminator(MOpcode::Cbz));
    EXPECT_TRUE(ra::isTerminator(MOpcode::Cbnz));
    EXPECT_FALSE(ra::isBranch(MOpcode::Cbz));
    EXPECT_FALSE(ra::isBranch(MOpcode::Cbnz));
}

TEST(CodegenReviewFix, NoreturnTrapCallDoesNotCreateFallthroughLiveness) {
    MFunction fn;
    fn.name = "noreturn_liveness";

    MBasicBlock entry;
    entry.name = "entry";
    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(42)}});
    entry.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock next;
    next.name = "next";
    next.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(0)}});
    next.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(next));

    ra::LivenessAnalysis liveness;
    liveness.run(fn);
    EXPECT_FALSE(liveness.liveOutGPR(0).contains(1));
}

// ---------------------------------------------------------------------------
// Regression: Stack alignment is 16 bytes per AAPCS64
// ---------------------------------------------------------------------------

TEST(CodegenReviewFix, StackAlignment16) {
    const auto &ti = darwinTarget();
    EXPECT_EQ(ti.stackAlignment, 16U);
}

TEST(CodegenReviewFix, FPRPoolCanPreferCalleeSavedAndRejectDuplicateRelease) {
    const auto &ti = darwinTarget();
    ra::RegPools pools;
    pools.build(ti);

    const PhysReg first = pools.takeFPRPreferCalleeSaved(ti);
    EXPECT_EQ(first, PhysReg::V8);

    pools.releaseFPR(first, ti);
    EXPECT_THROWS(pools.releaseFPR(first, ti), std::runtime_error);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
