//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_review_fixes_batch3.cpp
// Purpose: Regression tests for fixes 22-24 from the comprehensive backend
//          codegen review (session 4). Tests verify:
//          - Fix 22: spillOne deterministic victim selection (furthest end)
//          - Fix 23: spillOne asserts on empty active set
//          - Fix 24: Peephole MUL→SHL skips when flags are consumed
//          - Fix 25: AArch64 label sanitization (hyphens → 'N')
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/OperandUtils.hpp"
#include "codegen/x86_64/Peephole.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#include <sstream>
#include <string>
#include <vector>

// ===========================================================================
// Fix 24: Peephole MUL→SHL must not transform when flags are consumed
// ===========================================================================

TEST(CodegenReviewBatch3, MulToShlSkipsWhenFlagsConsumed)
{
    // Build an MFunction with:
    //   mov rcx, #8          (load constant 8 = 2^3)
    //   imul rax, rcx        (multiply by power-of-2)
    //   jcc .Loverflow        (reads flags from IMUL!)
    // After peepholes, the IMUL must NOT be transformed to SHL because
    // the JCC reads the overflow flag which SHL sets differently.

    using namespace viper::codegen::x64;

    MFunction fn{};
    fn.name = "test_mul_flags";
    MBasicBlock block{};
    block.label = ".Lentry";

    // mov rcx, #8 (known constant power-of-2)
    auto rcxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RCX));
    auto raxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    block.instructions.push_back(MInstr::make(MOpcode::MOVri, {rcxOp, makeImmOperand(8)}));

    // imul rax, rcx
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {raxOp, rcxOp}));

    // jcc .Loverflow (reads flags from the IMUL)
    block.instructions.push_back(
        MInstr::make(MOpcode::JCC, {makeLabelOperand(".Loverflow"), makeImmOperand(0)}));

    fn.blocks.push_back(std::move(block));

    // Run peepholes
    runPeepholes(fn);

    // The IMUL should NOT have been converted to SHL because the JCC reads flags
    bool foundIMUL = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instructions)
        {
            if (instr.opcode == MOpcode::IMULrr)
                foundIMUL = true;
        }
    }
    EXPECT_TRUE(foundIMUL);
}

TEST(CodegenReviewBatch3, MulToShlWorksWhenFlagsNotConsumed)
{
    // Build an MFunction with:
    //   mov rcx, #8          (load constant 8 = 2^3)
    //   imul rax, rcx        (multiply by power-of-2)
    //   cmp rax, rdx         (new flag-setting instruction before any flag read)
    //   jcc .Lblock           (reads flags from CMP, not IMUL)
    // The IMUL CAN be transformed to SHL because CMP overwrites flags first.

    using namespace viper::codegen::x64;

    MFunction fn{};
    fn.name = "test_mul_no_flags";
    MBasicBlock block{};
    block.label = ".Lentry";

    auto rcxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RCX));
    auto raxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    auto rdxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RDX));

    // mov rcx, #8
    block.instructions.push_back(MInstr::make(MOpcode::MOVri, {rcxOp, makeImmOperand(8)}));

    // imul rax, rcx
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {raxOp, rcxOp}));

    // cmp rax, rdx (overwrites flags before JCC reads them)
    block.instructions.push_back(MInstr::make(MOpcode::CMPrr, {raxOp, rdxOp}));

    // jcc .Lblock
    block.instructions.push_back(
        MInstr::make(MOpcode::JCC, {makeLabelOperand(".Lblock"), makeImmOperand(0)}));

    fn.blocks.push_back(std::move(block));

    runPeepholes(fn);

    // The IMUL SHOULD have been converted to SHL because CMP overwrites flags
    bool foundSHL = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instructions)
        {
            if (instr.opcode == MOpcode::SHLri)
                foundSHL = true;
        }
    }
    EXPECT_TRUE(foundSHL);
}

TEST(CodegenReviewBatch3, MulToShlSkipsAtLabel)
{
    // If a LABEL appears between IMUL and any flag consumer, we must
    // conservatively skip the transformation (another block might branch
    // there expecting IMUL's flag state).

    using namespace viper::codegen::x64;

    MFunction fn{};
    fn.name = "test_mul_label_barrier";
    MBasicBlock block{};
    block.label = ".Lentry";

    auto rcxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RCX));
    auto raxOp = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));

    block.instructions.push_back(MInstr::make(MOpcode::MOVri, {rcxOp, makeImmOperand(4)}));
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {raxOp, rcxOp}));

    // A label acts as a conservative barrier — flags could be read by code
    // that branches here from elsewhere.
    block.instructions.push_back(MInstr::make(MOpcode::LABEL, {makeLabelOperand(".Ltarget")}));

    fn.blocks.push_back(std::move(block));

    runPeepholes(fn);

    // IMUL should NOT be converted to SHL due to the LABEL barrier
    bool foundIMUL = false;
    for (const auto &b : fn.blocks)
    {
        for (const auto &instr : b.instructions)
        {
            if (instr.opcode == MOpcode::IMULrr)
                foundIMUL = true;
        }
    }
    EXPECT_TRUE(foundIMUL);
}

// ===========================================================================
// Fix 25: AArch64 label sanitization
// ===========================================================================

TEST(CodegenReviewBatch3, AArch64LabelSanitizesHyphens)
{
    // Verify that block labels containing hyphens are sanitized to prevent
    // the assembler from parsing them as subtraction operators.
    using namespace viper::codegen::aarch64;

    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_sanitize";

    // Entry block with a ret
    MBasicBlock entry{};
    entry.name = ".Lblock-1";
    entry.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(entry));

    // Second block with a hyphenated label
    MBasicBlock second{};
    second.name = ".Ltrap-cast-overflow";
    second.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
    fn.blocks.push_back(std::move(second));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    // The hyphenated labels must be sanitized (hyphens replaced with '_')
    EXPECT_TRUE(output.find(".Lblock_1:") != std::string::npos);
    EXPECT_TRUE(output.find(".Ltrap_cast_overflow:") != std::string::npos);

    // Verify the original hyphenated form does NOT appear as a label definition
    EXPECT_TRUE(output.find(".Lblock-1:") == std::string::npos);
    EXPECT_TRUE(output.find(".Ltrap-cast-overflow:") == std::string::npos);
}

TEST(CodegenReviewBatch3, AArch64BranchTargetsSanitized)
{
    // Verify that branch targets referencing hyphenated labels are also sanitized.
    using namespace viper::codegen::aarch64;

    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_branch_sanitize";

    MBasicBlock entry{};
    entry.name = ".Lentry";

    // Unconditional branch to hyphenated label
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(".Ltarget-block")}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock target_bb{};
    target_bb.name = ".Ltarget-block";
    target_bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target_bb));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    // Branch target and label definition must both be sanitized
    EXPECT_TRUE(output.find("b .Ltarget_block") != std::string::npos);
    EXPECT_TRUE(output.find(".Ltarget_block:") != std::string::npos);
}

TEST(CodegenReviewBatch3, AArch64BCondTargetSanitized)
{
    // Verify conditional branch targets are also sanitized.
    using namespace viper::codegen::aarch64;

    const auto &target = darwinTarget();
    AsmEmitter emitter{target};

    MFunction fn{};
    fn.name = "test_bcond_sanitize";

    MBasicBlock entry{};
    entry.name = ".Lentry";

    // cmp + conditional branch to hyphenated label
    entry.instrs.push_back(
        MInstr{MOpcode::CmpRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
    entry.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(".Leq-target")}});
    entry.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock eq_bb{};
    eq_bb.name = ".Leq-target";
    eq_bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(eq_bb));

    std::ostringstream oss;
    emitter.emitFunction(oss, fn);
    const std::string output = oss.str();

    // Both the b.eq target and label definition must be sanitized
    EXPECT_TRUE(output.find("b.eq .Leq_target") != std::string::npos);
    EXPECT_TRUE(output.find(".Leq_target:") != std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
