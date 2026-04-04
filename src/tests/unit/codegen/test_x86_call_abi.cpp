//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_x86_call_abi.cpp
// Purpose: Verify x86-64 call-lowering ABI details that are easy to regress:
//          Win64 mixed entry parameters, indirect-call bool returns, and
//          indirect-call vararg metadata.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/x86_64/LowerILToMIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#include <optional>
#include <vector>

using namespace viper::codegen::x64;

namespace {

ILValue makeValue(ILValue::Kind kind, int id) {
    ILValue value{};
    value.kind = kind;
    value.id = id;
    return value;
}

ILValue makeConstI64(int64_t imm) {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = -1;
    value.i64 = imm;
    return value;
}

ILValue makeLabel(const char *name) {
    ILValue value{};
    value.kind = ILValue::Kind::LABEL;
    value.id = -1;
    value.label = name;
    return value;
}

ILInstr makeRet(ILValue value) {
    ILInstr instr{};
    instr.opcode = "ret";
    instr.ops = {std::move(value)};
    return instr;
}

const OpReg *asReg(const Operand &operand) {
    return std::get_if<OpReg>(&operand);
}

const OpMem *asMem(const Operand &operand) {
    return std::get_if<OpMem>(&operand);
}

std::optional<std::size_t> findInstruction(const MBasicBlock &block, MOpcode opcode) {
    for (std::size_t i = 0; i < block.instructions.size(); ++i) {
        if (block.instructions[i].opcode == opcode)
            return i;
    }
    return std::nullopt;
}

} // namespace

TEST(X64CallABI, Win64EntryParamsUseUnifiedRegisterPositions) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(win64Target(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2, 3, 4};
    entry.paramKinds = {ILValue::Kind::I64,
                        ILValue::Kind::F64,
                        ILValue::Kind::I64,
                        ILValue::Kind::F64,
                        ILValue::Kind::I64};
    entry.instrs = {makeRet(makeConstI64(0))};

    ILFunction fn{};
    fn.name = "mixed_entry";
    fn.blocks = {entry};

    const MFunction mir = lowering.lower(fn);
    ASSERT_FALSE(mir.blocks.empty());
    const auto &instrs = mir.blocks.front().instructions;
    ASSERT_FALSE(instrs.empty());

    const MInstr &px = instrs.front();
    ASSERT_EQ(px.opcode, MOpcode::PX_COPY);
    ASSERT_EQ(px.operands.size(), 8u);

    const OpReg *src0 = asReg(px.operands[1]);
    const OpReg *src1 = asReg(px.operands[3]);
    const OpReg *src2 = asReg(px.operands[5]);
    const OpReg *src3 = asReg(px.operands[7]);
    ASSERT_NE(src0, nullptr);
    ASSERT_NE(src1, nullptr);
    ASSERT_NE(src2, nullptr);
    ASSERT_NE(src3, nullptr);
    EXPECT_TRUE(src0->isPhys);
    EXPECT_TRUE(src1->isPhys);
    EXPECT_TRUE(src2->isPhys);
    EXPECT_TRUE(src3->isPhys);
    EXPECT_EQ(static_cast<PhysReg>(src0->idOrPhys), PhysReg::RCX);
    EXPECT_EQ(static_cast<PhysReg>(src1->idOrPhys), PhysReg::XMM1);
    EXPECT_EQ(static_cast<PhysReg>(src2->idOrPhys), PhysReg::R8);
    EXPECT_EQ(static_cast<PhysReg>(src3->idOrPhys), PhysReg::XMM3);

    bool foundStackLoad = false;
    for (const auto &instr : instrs) {
        if (instr.opcode != MOpcode::MOVmr)
            continue;
        if (instr.operands.size() < 2)
            continue;
        const OpMem *mem = asMem(instr.operands[1]);
        if (!mem || !mem->base.isPhys)
            continue;
        if (static_cast<PhysReg>(mem->base.idOrPhys) == PhysReg::RBP && mem->disp == 48) {
            foundStackLoad = true;
            break;
        }
    }
    EXPECT_TRUE(foundStackLoad);
}

TEST(X64CallABI, IndirectBoolCallUsesMovzxAndCarriesPlanId) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILInstr gaddr{};
    gaddr.opcode = "gaddr";
    gaddr.resultId = 0;
    gaddr.resultKind = ILValue::Kind::PTR;
    gaddr.ops = {makeLabel("flag_source")};

    ILInstr call{};
    call.opcode = "call.indirect";
    call.resultId = 1;
    call.resultKind = ILValue::Kind::I1;
    call.ops = {makeValue(ILValue::Kind::PTR, 0)};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {gaddr, call, makeRet(makeValue(ILValue::Kind::I1, 1))};

    ILFunction fn{};
    fn.name = "call_bool_indirect";
    fn.blocks = {entry};

    const MFunction mir = lowering.lower(fn);
    ASSERT_EQ(lowering.callPlans().size(), 1u);
    ASSERT_FALSE(mir.blocks.empty());

    const auto &block = mir.blocks.front();
    const auto callIndex = findInstruction(block, MOpcode::CALL);
    ASSERT_TRUE(callIndex.has_value());
    ASSERT_LT(*callIndex + 1, block.instructions.size());

    const MInstr &callInstr = block.instructions[*callIndex];
    EXPECT_NE(callInstr.callPlanId, MInstr::kNoCallPlanId);
    ASSERT_LT(callInstr.callPlanId, lowering.callPlans().size());
    EXPECT_EQ(lowering.callPlans()[callInstr.callPlanId].args.size(), 0u);
    EXPECT_EQ(block.instructions[*callIndex + 1].opcode, MOpcode::MOVZXrr32);
}

TEST(X64CallABI, IndirectLabelCallsPreserveKnownVarArgMetadata) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILInstr call{};
    call.opcode = "call.indirect";
    call.resultId = 4;
    call.resultKind = ILValue::Kind::I64;
    call.ops = {makeLabel("rt_snprintf"),
                makeConstI64(1),
                makeConstI64(2),
                makeConstI64(3),
                makeConstI64(4)};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {call, makeRet(makeValue(ILValue::Kind::I64, 4))};

    ILFunction fn{};
    fn.name = "call_vararg_indirect";
    fn.blocks = {entry};

    (void)lowering.lower(fn);
    ASSERT_EQ(lowering.callPlans().size(), 1u);
    const CallLoweringPlan &plan = lowering.callPlans().front();
    EXPECT_EQ(plan.callee, "rt_snprintf");
    EXPECT_TRUE(plan.isVarArg);
    EXPECT_EQ(plan.numNamedArgs, 0u);
    EXPECT_EQ(plan.args.size(), 4u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
