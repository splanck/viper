//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/codegen/test_codegen_preregalloc_opt.cpp
// Purpose: Regression tests for x86-64 and AArch64 pre-RA MIR cleanup.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/PreRegAllocOpt.hpp"
#include "codegen/x86_64/PreRegAllocOpt.hpp"

#include <utility>
#include <variant>

namespace {

const viper::codegen::x64::OpReg *x64Reg(const viper::codegen::x64::Operand &operand) {
    return std::get_if<viper::codegen::x64::OpReg>(&operand);
}

} // namespace

TEST(CodegenPreRegAllocOpt, X86RemovesIdentityAndForwardsSingleUseCopy) {
    namespace x64 = viper::codegen::x64;

    x64::MFunction fn;
    fn.name = "x86_pre_ra";
    x64::MBasicBlock block;
    block.label = ".Lentry";

    auto v1 = x64::makeVRegOperand(x64::RegClass::GPR, 1);
    auto v2 = x64::makeVRegOperand(x64::RegClass::GPR, 2);
    auto v3 = x64::makeVRegOperand(x64::RegClass::GPR, 3);

    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrr, {v1, v1}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrr, {v2, v1}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::ADDrr, {v3, v2}));
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(x64::runPreRegAllocOpt(fn), 2u);
    ASSERT_EQ(fn.blocks.front().instructions.size(), 1u);
    const auto &add = fn.blocks.front().instructions.front();
    ASSERT_EQ(add.opcode, x64::MOpcode::ADDrr);
    ASSERT_NE(x64Reg(add.operands[1]), nullptr);
    EXPECT_EQ(x64Reg(add.operands[1])->idOrPhys, 1u);
}

TEST(CodegenPreRegAllocOpt, X86DoesNotForwardAcrossSourceClobber) {
    namespace x64 = viper::codegen::x64;

    x64::MFunction fn;
    fn.name = "x86_pre_ra_clobber";
    x64::MBasicBlock block;
    block.label = ".Lentry";

    auto v1 = x64::makeVRegOperand(x64::RegClass::GPR, 1);
    auto v2 = x64::makeVRegOperand(x64::RegClass::GPR, 2);
    auto v3 = x64::makeVRegOperand(x64::RegClass::GPR, 3);

    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrr, {v2, v1}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVri, {v1, x64::makeImmOperand(9)}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::ADDrr, {v3, v2}));
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(x64::runPreRegAllocOpt(fn), 0u);
    ASSERT_EQ(fn.blocks.front().instructions.size(), 3u);
}

TEST(CodegenPreRegAllocOpt, X86DoesNotEraseCopyNeededAfterCall) {
    namespace x64 = viper::codegen::x64;

    x64::MFunction fn;
    fn.name = "x86_pre_ra_call_live";
    x64::MBasicBlock block;
    block.label = ".Lentry";

    auto physRax = x64::makePhysRegOperand(x64::RegClass::GPR, static_cast<uint16_t>(x64::PhysReg::RAX));
    auto physRbp = x64::makePhysReg(x64::RegClass::GPR, static_cast<uint16_t>(x64::PhysReg::RBP));
    auto v2 = x64::makeVRegOperand(x64::RegClass::GPR, 2);
    auto v3 = x64::makeVRegOperand(x64::RegClass::GPR, 3);
    auto stackSlot = x64::makeMemOperand(physRbp, -8);

    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrr, {v2, physRax}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrm, {stackSlot, v2}));
    block.instructions.push_back(
        x64::MInstr::make(x64::MOpcode::CALL, {x64::makeLabelOperand("may_clobber")}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::ADDrr, {v3, v2}));
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(x64::runPreRegAllocOpt(fn), 0u);
    ASSERT_EQ(fn.blocks.front().instructions.size(), 4u);
    EXPECT_EQ(fn.blocks.front().instructions.front().opcode, x64::MOpcode::MOVrr);
}

TEST(CodegenPreRegAllocOpt, X86DoesNotForwardPhysicalSourceBeforeRegAlloc) {
    namespace x64 = viper::codegen::x64;

    x64::MFunction fn;
    fn.name = "x86_pre_ra_phys_src";
    x64::MBasicBlock block;
    block.label = ".Lentry";

    auto physRax = x64::makePhysRegOperand(x64::RegClass::GPR, static_cast<uint16_t>(x64::PhysReg::RAX));
    auto physRbp = x64::makePhysReg(x64::RegClass::GPR, static_cast<uint16_t>(x64::PhysReg::RBP));
    auto v2 = x64::makeVRegOperand(x64::RegClass::GPR, 2);
    auto v3 = x64::makeVRegOperand(x64::RegClass::GPR, 3);
    auto stackSlot = x64::makeMemOperand(physRbp, -8);

    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrr, {v2, physRax}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVri, {v3, x64::makeImmOperand(9)}));
    block.instructions.push_back(x64::MInstr::make(x64::MOpcode::MOVrm, {stackSlot, v2}));
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(x64::runPreRegAllocOpt(fn), 0u);
    ASSERT_EQ(fn.blocks.front().instructions.size(), 3u);
    EXPECT_EQ(fn.blocks.front().instructions.front().opcode, x64::MOpcode::MOVrr);
}

TEST(CodegenPreRegAllocOpt, AArch64RemovesIdentityAndForwardsSingleUseCopy) {
    namespace a64 = viper::codegen::aarch64;

    a64::MFunction fn;
    fn.name = "a64_pre_ra";
    a64::MBasicBlock block;
    block.name = ".Lentry";

    auto v1 = a64::MOperand::vregOp(a64::RegClass::GPR, 1);
    auto v2 = a64::MOperand::vregOp(a64::RegClass::GPR, 2);
    auto v3 = a64::MOperand::vregOp(a64::RegClass::GPR, 3);
    auto v4 = a64::MOperand::vregOp(a64::RegClass::GPR, 4);

    block.instrs.push_back(a64::MInstr{a64::MOpcode::MovRR, {v1, v1}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::MovRR, {v2, v1}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::AddRRR, {v3, v2, v4}});
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(a64::runPreRegAllocOpt(fn), 2u);
    ASSERT_EQ(fn.blocks.front().instrs.size(), 1u);
    const auto &add = fn.blocks.front().instrs.front();
    ASSERT_EQ(add.opc, a64::MOpcode::AddRRR);
    ASSERT_EQ(add.ops[1].kind, a64::MOperand::Kind::Reg);
    EXPECT_EQ(add.ops[1].reg.idOrPhys, 1u);
}

TEST(CodegenPreRegAllocOpt, AArch64DoesNotForwardPhysicalSourceBeforeRegAlloc) {
    namespace a64 = viper::codegen::aarch64;

    a64::MFunction fn;
    fn.name = "a64_pre_ra_phys_src";
    a64::MBasicBlock block;
    block.name = ".Lentry";

    auto x0 = a64::MOperand::regOp(a64::PhysReg::X0);
    auto v2 = a64::MOperand::vregOp(a64::RegClass::GPR, 2);
    auto v3 = a64::MOperand::vregOp(a64::RegClass::GPR, 3);

    block.instrs.push_back(a64::MInstr{a64::MOpcode::MovRR, {v2, x0}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::LdrRegFpImm, {v3, a64::MOperand::immOp(-16)}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::StrRegFpImm, {v2, a64::MOperand::immOp(-8)}});
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(a64::runPreRegAllocOpt(fn), 0u);
    ASSERT_EQ(fn.blocks.front().instrs.size(), 3u);
    EXPECT_EQ(fn.blocks.front().instrs.front().opc, a64::MOpcode::MovRR);
}

TEST(CodegenPreRegAllocOpt, AArch64DoesNotEraseCopyNeededAfterCall) {
    namespace a64 = viper::codegen::aarch64;

    a64::MFunction fn;
    fn.name = "a64_pre_ra_call_live";
    a64::MBasicBlock block;
    block.name = ".Lentry";

    auto x0 = a64::MOperand::regOp(a64::PhysReg::X0);
    auto x1 = a64::MOperand::regOp(a64::PhysReg::X1);
    auto v2 = a64::MOperand::vregOp(a64::RegClass::GPR, 2);
    auto v3 = a64::MOperand::vregOp(a64::RegClass::GPR, 3);

    block.instrs.push_back(a64::MInstr{a64::MOpcode::MovRR, {v2, x0}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::StrRegFpImm, {v2, a64::MOperand::immOp(-8)}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::Bl, {a64::MOperand::labelOp("may_clobber")}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::StrRegBaseImm, {x1, v2, a64::MOperand::immOp(0)}});
    block.instrs.push_back(a64::MInstr{a64::MOpcode::AddRRR, {v3, v2, x1}});
    fn.blocks.push_back(std::move(block));

    EXPECT_EQ(a64::runPreRegAllocOpt(fn), 0u);
    ASSERT_EQ(fn.blocks.front().instrs.size(), 5u);
    EXPECT_EQ(fn.blocks.front().instrs.front().opc, a64::MOpcode::MovRR);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
