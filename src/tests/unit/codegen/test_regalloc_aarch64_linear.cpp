//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_regalloc_aarch64_linear.cpp
// Purpose: Validate AArch64 linear-scan allocator assigns phys regs, spills,
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <algorithm>
#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/LowerOvf.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

using namespace viper::codegen::aarch64;

TEST(Arm64RegAlloc, SpillsAndCalleeSaved) {
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_test";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    // Reserve a local alloca (one i64)
    FrameBuilder fb{fn};
    fb.addLocal(/*tempId*/ 1, /*size*/ 8, /*align*/ 8);

    // Create many virtual temporaries to exceed the available caller-saved pool
    // and force usage of callee-saved + spills. We define 40 vregs sequentially.
    const int N = 40;
    for (int i = 0; i < N; ++i) {
        MOperand dst = MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i));
        bb.instrs.push_back(MInstr{MOpcode::MovRI, {dst, MOperand::immOp(i)}});
    }
    // Use every temporary after all definitions so the live range overlap
    // exceeds the available register pool and forces actual spill/reload code.
    uint16_t acc = 0;
    uint16_t next = static_cast<uint16_t>(N);
    for (int i = 1; i < N; ++i) {
        const uint16_t dstId = next++;
        bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                                   {MOperand::vregOp(RegClass::GPR, dstId),
                                    MOperand::vregOp(RegClass::GPR, acc),
                                    MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i))}});
        acc = dstId;
    }
    // Move result to x0 to make output deterministic
    bb.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, acc)}});

    // Run allocator
    auto result = allocate(fn, ti);
    // Expect that we used at least one callee-saved register
    bool usedCallee = false;
    for (auto r : fn.savedGPRs) {
        if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), r) !=
            ti.calleeSavedGPR.end()) {
            usedCallee = true;
        }
    }
    EXPECT_TRUE(usedCallee);

    // Emit to text and look for spills/loads and prologue shape
    AsmEmitter emit{ti};
    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string asmText = os.str();
    // Spill stores and reload loads should appear
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);

    // Prologue must adjust sp by total frame size
    const int frameSize = fn.frame.totalBytes;
    if (frameSize > 0) {
        const std::string sub = "sub sp, sp, #" + std::to_string(frameSize);
        EXPECT_NE(asmText.find(sub), std::string::npos);
    }
    // Callee-saved used in RA must be saved in prologue
    for (auto r : fn.savedGPRs) {
        const char *name = regName(r);
        const std::string needle = std::string(name) + ",";
        EXPECT_NE(asmText.find(needle), std::string::npos);
    }
}

TEST(Arm64RegAlloc, LiveOutSpillsStayAfterInternalOverflowBranch) {
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_ovf_liveout";
    fn.blocks.resize(4);
    fn.blocks[0].name = "entry";
    fn.blocks[1].name = "then";
    fn.blocks[2].name = "exit";
    fn.blocks[3].name = "trap";

    auto &entry = fn.blocks[0];
    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(40)}});
    entry.instrs.push_back(MInstr{MOpcode::AddOvfRI,
                                  {MOperand::vregOp(RegClass::GPR, 2),
                                   MOperand::vregOp(RegClass::GPR, 1),
                                   MOperand::immOp(2)}});
    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 3), MOperand::immOp(7)}});
    entry.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(0)}});
    entry.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp("then")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("exit")}});

    fn.blocks[1].instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 2)}});
    fn.blocks[1].instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks[2].instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 3)}});
    fn.blocks[2].instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks[3].instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});

    lowerOverflowOps(fn);
    auto result = allocate(fn, ti);
    (void)result;

    const auto &rewritten = fn.blocks[0].instrs;
    auto addIt = std::find_if(rewritten.begin(), rewritten.end(), [](const MInstr &instr) {
        return instr.opc == MOpcode::AddsRI;
    });
    ASSERT_NE(addIt, rewritten.end());
    auto afterAdd = std::next(addIt);
    ASSERT_NE(afterAdd, rewritten.end());
    EXPECT_EQ(afterAdd->opc, MOpcode::BCond);

    const auto trailingBr =
        std::find_if(rewritten.begin(), rewritten.end(), [](const MInstr &instr) {
            return instr.opc == MOpcode::Br;
        });
    ASSERT_NE(trailingBr, rewritten.end());
    const auto firstTrailingTerm = std::prev(trailingBr);
    EXPECT_EQ(firstTrailingTerm->opc, MOpcode::BCond);
    const bool hasSpillBeforeTrailingTerm =
        std::any_of(std::next(addIt), firstTrailingTerm, [](const MInstr &instr) {
            return instr.opc == MOpcode::StrRegFpImm || instr.opc == MOpcode::StrFprFpImm;
        });
    EXPECT_TRUE(hasSpillBeforeTrailingTerm);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
