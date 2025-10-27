//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the opcode-driven lowering rules that translate provisional IL into
// Machine IR.  Each rule consists of a light-weight matcher and an emitter that
// relies on MIRBuilder to perform the heavy lifting.  Grouping the behaviour in a
// registry keeps LowerILToMIR focused on orchestration while enabling new
// opcodes to be added without touching a central switch.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief x86-64 lowering rules for the provisional IL dialect.
/// @details Encapsulates small helper functions for arithmetic, control-flow,
///          and call-related opcodes.  Rules are registered in a static vector
///          that is consulted by LowerILToMIR during lowering.

#include "LoweringRules.hpp"

#include "LoweringRuleTable.hpp"

#include "CallLowering.hpp"
#include "LowerILToMIR.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace lowering
{
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

[[nodiscard]] std::optional<int> icmpConditionCode(std::string_view opcode) noexcept
{
    if (!opcode.starts_with("icmp_"))
    {
        return std::nullopt;
    }

    const std::string_view suffix = opcode.substr(5);
    if (suffix == "eq")
    {
        return 0;
    }
    if (suffix == "ne")
    {
        return 1;
    }
    if (suffix == "slt")
    {
        return 2;
    }
    if (suffix == "sle")
    {
        return 3;
    }
    if (suffix == "sgt")
    {
        return 4;
    }
    if (suffix == "sge")
    {
        return 5;
    }
    if (suffix == "ugt")
    {
        return 6;
    }
    if (suffix == "uge")
    {
        return 7;
    }
    if (suffix == "ult")
    {
        return 8;
    }
    if (suffix == "ule")
    {
        return 9;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int> fcmpConditionCode(std::string_view opcode) noexcept
{
    if (!opcode.starts_with("fcmp_"))
    {
        return std::nullopt;
    }

    const std::string_view suffix = opcode.substr(5);
    if (suffix == "eq")
    {
        return 0;
    }
    if (suffix == "ne")
    {
        return 1;
    }
    if (suffix == "lt")
    {
        return 2;
    }
    if (suffix == "le")
    {
        return 3;
    }
    if (suffix == "gt")
    {
        return 4;
    }
    if (suffix == "ge")
    {
        return 5;
    }
    return std::nullopt;
}

void emitBinary(const ILInstr &instr,
                MIRBuilder &builder,
                MOpcode opcRR,
                MOpcode opcRI,
                RegClass cls,
                bool requireImm32)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = builder.makeOperandForValue(instr.ops[0], cls);
    Operand rhs = builder.makeOperandForValue(instr.ops[1], cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        builder.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
    }
    else
    {
        builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
    }

    const auto canUseImm = [&]() {
        if (opcRI == opcRR)
        {
            return false;
        }
        const auto *imm = std::get_if<OpImm>(&rhs);
        if (!imm)
        {
            return false;
        }
        if (!requireImm32)
        {
            return true;
        }
        return imm->val >= static_cast<int64_t>(std::numeric_limits<int32_t>::min()) &&
               imm->val <= static_cast<int64_t>(std::numeric_limits<int32_t>::max());
    }();

    if (canUseImm)
    {
        builder.append(MInstr::make(opcRI, std::vector<Operand>{cloneOperand(dest), rhs}));
        return;
    }

    const auto materialiseToReg = [&builder, cls](Operand operand) {
        if (std::holds_alternative<OpReg>(operand))
        {
            return operand;
        }

        const VReg tmp = builder.makeTempVReg(cls);
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            builder.append(MInstr::make(
                MOpcode::MOVri, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpLabel>(operand) || std::holds_alternative<OpRipLabel>(operand))
        {
            builder.append(MInstr::make(
                MOpcode::LEA, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else
        {
            builder.append(MInstr::make(
                MOpcode::MOVrr, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }

        return tmpOp;
    };

    const Operand rhsReg = materialiseToReg(rhs);
    builder.append(MInstr::make(opcRR, std::vector<Operand>{cloneOperand(dest), cloneOperand(rhsReg)}));
}

void emitShift(const ILInstr &instr, MIRBuilder &builder, MOpcode opcImm, MOpcode opcReg)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = builder.makeOperandForValue(instr.ops[0], destReg.cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        builder.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
    }
    else
    {
        builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
    }

    Operand rhs = builder.makeOperandForValue(instr.ops[1], destReg.cls);
    if (auto *imm = std::get_if<OpImm>(&rhs))
    {
        const auto masked = static_cast<int64_t>(static_cast<std::uint8_t>(imm->val));
        builder.append(
            MInstr::make(opcImm, std::vector<Operand>{cloneOperand(dest), makeImmOperand(masked)}));
        return;
    }

    const Operand clOperand =
        makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RCX));

    bool alreadyCl = false;
    if (const auto *reg = std::get_if<OpReg>(&rhs); reg)
    {
        alreadyCl = reg->isPhys && reg->cls == RegClass::GPR &&
                    reg->idOrPhys == static_cast<uint16_t>(PhysReg::RCX);
    }

    if (!alreadyCl)
    {
        builder.append(MInstr::make(
            MOpcode::MOVrr, std::vector<Operand>{cloneOperand(clOperand), cloneOperand(rhs)}));
    }

    builder.append(
        MInstr::make(opcReg, std::vector<Operand>{cloneOperand(dest), cloneOperand(clOperand)}));
}

void emitCmp(const ILInstr &instr, MIRBuilder &builder, RegClass cls, int defaultCond)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    int condCode = defaultCond;
    Operand condOperand{};
    if (instr.ops.size() > 2)
    {
        condOperand = builder.makeOperandForValue(instr.ops[2], RegClass::GPR);
        if (const auto *imm = std::get_if<OpImm>(&condOperand))
        {
            condCode = static_cast<int>(imm->val);
        }
    }

    const Operand lhs = builder.makeOperandForValue(instr.ops[0], cls);
    const Operand rhs = builder.makeOperandForValue(instr.ops[1], cls);

    const MOpcode cmpOpc = cls == RegClass::XMM ? MOpcode::UCOMIS : MOpcode::CMPrr;
    builder.append(MInstr::make(cmpOpc, std::vector<Operand>{cloneOperand(lhs), rhs}));

    if (instr.resultId < 0)
    {
        return;
    }

    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    builder.append(MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(condCode), dest}));
}

void emitSelect(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0 || instr.ops.size() < 3)
    {
        return;
    }

    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand cond = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    const Operand trueVal = builder.makeOperandForValue(instr.ops[1], destReg.cls);
    const Operand falseVal = builder.makeOperandForValue(instr.ops[2], destReg.cls);

    if (destReg.cls == RegClass::GPR)
    {
        Operand cmovSource = trueVal;
        if (std::holds_alternative<OpImm>(cmovSource))
        {
            const VReg tmpVReg = builder.makeTempVReg(destReg.cls);
            cmovSource = makeVRegOperand(tmpVReg.cls, tmpVReg.id);
            builder.append(MInstr::make(MOpcode::MOVri,
                                        std::vector<Operand>{cloneOperand(cmovSource), trueVal}));
        }

        const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
        std::vector<Operand> movOperands{};
        movOperands.push_back(cloneOperand(dest));
        movOperands.push_back(cloneOperand(falseVal));
        movOperands.push_back(cloneOperand(cmovSource));
        builder.append(MInstr::make(falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr, std::move(movOperands)));

        builder.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
        builder.append(
            MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
        return;
    }

    std::vector<Operand> movOperands{};
    movOperands.push_back(cloneOperand(dest));
    movOperands.push_back(cloneOperand(falseVal));
    movOperands.push_back(cloneOperand(trueVal));
    builder.append(MInstr::make(MOpcode::MOVSDrr, std::move(movOperands)));

    builder.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    builder.append(MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
}

void emitBranch(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{builder.makeLabelOperand(instr.ops[0])}));
}

void emitCondBranch(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.size() < 3)
    {
        return;
    }

    const Operand cond = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    const Operand trueLabel = builder.makeLabelOperand(instr.ops[1]);
    const Operand falseLabel = builder.makeLabelOperand(instr.ops[2]);

    builder.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    builder.append(MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(1), trueLabel}));
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{falseLabel}));
}

void emitReturn(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        builder.append(MInstr::make(MOpcode::RET, {}));
        return;
    }

    const ILValue &retVal = instr.ops.front();
    const RegClass cls = builder.regClassFor(retVal.kind);

    Operand src = builder.makeOperandForValue(retVal, cls);

    if (retVal.kind == ILValue::Kind::I1)
    {
        if (const auto *imm = std::get_if<OpImm>(&src))
        {
            src = makeImmOperand(imm->val != 0 ? 1 : 0);
        }
    }

    const auto materialiseToReg = [&builder](Operand operand, RegClass expectedCls) -> Operand {
        if (std::holds_alternative<OpReg>(operand))
        {
            return operand;
        }

        const VReg tmp = builder.makeTempVReg(expectedCls);
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            builder.append(
                MInstr::make(MOpcode::MOVri, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpMem>(operand))
        {
            const MOpcode loadOpc = expectedCls == RegClass::XMM ? MOpcode::MOVSDmr : MOpcode::MOVrr;
            builder.append(MInstr::make(loadOpc, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpLabel>(operand))
        {
            builder.append(MInstr::make(MOpcode::LEA, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }

        return tmpOp;
    };

    Operand srcReg = materialiseToReg(std::move(src), cls);

    if (retVal.kind == ILValue::Kind::I1 && std::holds_alternative<OpReg>(srcReg))
    {
        const auto &reg = std::get<OpReg>(srcReg);
        if (!reg.isPhys)
        {
            const VReg zx = builder.makeTempVReg(RegClass::GPR);
            const Operand zxOp = makeVRegOperand(zx.cls, zx.id);
            builder.append(MInstr::make(MOpcode::MOVZXrr32, {cloneOperand(zxOp), cloneOperand(srcReg)}));
            srcReg = zxOp;
        }
    }

    if (cls == RegClass::XMM)
    {
        const Operand retReg = makePhysRegOperand(RegClass::XMM,
                                                  static_cast<uint16_t>(builder.target().f64ReturnReg));
        builder.append(MInstr::make(MOpcode::MOVSDrr, {retReg, cloneOperand(srcReg)}));
    }
    else
    {
        const Operand retReg = makePhysRegOperand(RegClass::GPR,
                                                  static_cast<uint16_t>(builder.target().intReturnReg));
        builder.append(MInstr::make(MOpcode::MOVrr, {retReg, cloneOperand(srcReg)}));
    }

    builder.append(MInstr::make(MOpcode::RET, {}));
}

void emitCall(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }

    CallLoweringPlan plan{};
    plan.calleeLabel = instr.ops.front().label;

    for (std::size_t idx = 1; idx < instr.ops.size(); ++idx)
    {
        const auto &argVal = instr.ops[idx];
        CallArg arg{};
        arg.kind = builder.regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

        if (builder.isImmediate(argVal))
        {
            arg.isImm = true;
            arg.imm = argVal.i64;
        }
        else
        {
            const Operand operand = builder.makeOperandForValue(argVal, builder.regClassFor(argVal.kind));
            if (const auto *reg = std::get_if<OpReg>(&operand))
            {
                arg.vreg = reg->idOrPhys;
            }
            else if (const auto *imm = std::get_if<OpImm>(&operand))
            {
                arg.isImm = true;
                arg.imm = imm->val;
            }
        }

        plan.args.push_back(arg);
    }

    if (instr.resultId >= 0)
    {
        (void)builder.ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    builder.recordCallPlan(std::move(plan));
    builder.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{builder.makeLabelOperand(instr.ops[0])}));
}

void emitLoad(const ILInstr &instr, MIRBuilder &builder, RegClass cls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    Operand baseOp = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);
    if (!baseReg)
    {
        return;
    }

    const int32_t disp = instr.ops.size() > 1 ? static_cast<int32_t>(instr.ops[1].i64) : 0;
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand mem = makeMemOperand(*baseReg, disp);

    if (cls == RegClass::GPR)
    {
        builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), mem}));
    }
    else
    {
        builder.append(MInstr::make(MOpcode::MOVSDmr, std::vector<Operand>{cloneOperand(dest), mem}));
    }
}

void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder)
{
    emitLoad(instr, builder, builder.regClassFor(instr.resultKind));
}

void emitStore(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    const Operand value = builder.makeOperandForValue(instr.ops[0],
                                                      builder.regClassFor(instr.ops[0].kind));
    Operand baseOp = builder.makeOperandForValue(instr.ops[1], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);
    if (!baseReg)
    {
        return;
    }
    const int32_t disp = instr.ops.size() > 2 ? static_cast<int32_t>(instr.ops[2].i64) : 0;
    const Operand mem = makeMemOperand(*baseReg, disp);

    if (std::holds_alternative<OpReg>(value))
    {
        const auto cls = std::get<OpReg>(value).cls;
        if (cls == RegClass::XMM)
        {
            builder.append(MInstr::make(MOpcode::MOVSDrm, std::vector<Operand>{mem, value}));
        }
        else
        {
            builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{mem, value}));
        }
    }
    else
    {
        builder.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{mem, value}));
    }
}

void emitCast(const ILInstr &instr,
              MIRBuilder &builder,
              MOpcode opc,
              RegClass dstCls,
              RegClass srcCls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    const Operand src = builder.makeOperandForValue(instr.ops[0], srcCls);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    if (opc == MOpcode::MOVrr || std::holds_alternative<OpImm>(src))
    {
        builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), src}));
    }
    else
    {
        builder.append(MInstr::make(opc, std::vector<Operand>{cloneOperand(dest), src}));
    }

    (void)dstCls;
}

void emitDivRem(const ILInstr &instr, MIRBuilder &builder, std::string_view opcode)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    Operand dividend = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    Operand divisor = builder.makeOperandForValue(instr.ops[1], RegClass::GPR);

    const auto materialiseGprReg = [&builder](const Operand &operand) -> Operand {
        if (std::holds_alternative<OpReg>(operand))
        {
            return cloneOperand(operand);
        }

        const VReg tmp = builder.makeTempVReg(RegClass::GPR);
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            builder.append(MInstr::make(
                MOpcode::MOVri, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpLabel>(operand))
        {
            builder.append(MInstr::make(
                MOpcode::LEA, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else
        {
            builder.append(MInstr::make(
                MOpcode::MOVrr, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }

        return tmpOp;
    };

    if (!std::holds_alternative<OpReg>(dividend) && !std::holds_alternative<OpImm>(dividend))
    {
        dividend = materialiseGprReg(dividend);
    }

    divisor = materialiseGprReg(divisor);

    const MOpcode pseudo = [&]() {
        if (opcode == "div" || opcode == "sdiv")
        {
            return MOpcode::DIVS64rr;
        }
        if (opcode == "rem" || opcode == "srem")
        {
            return MOpcode::REMS64rr;
        }
        if (opcode == "udiv")
        {
            return MOpcode::DIVU64rr;
        }
        return MOpcode::REMU64rr;
    }();

    builder.append(MInstr::make(pseudo,
                                std::vector<Operand>{cloneOperand(dest),
                                                     cloneOperand(dividend),
                                                     cloneOperand(divisor)}));
}

// Rule emitters --------------------------------------------------------------

void emitAdd(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::ADDrr : MOpcode::FADD;
    const MOpcode opRI = cls == RegClass::GPR ? MOpcode::ADDri : opRR;
    emitBinary(instr, builder, opRR, opRI, cls, cls == RegClass::GPR);
}

void emitSub(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::SUBrr : MOpcode::FSUB;
    emitBinary(instr, builder, opRR, opRR, cls, false);
}

void emitMul(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::IMULrr : MOpcode::FMUL;
    emitBinary(instr, builder, opRR, opRR, cls, false);
}

void emitFDiv(const ILInstr &instr, MIRBuilder &builder)
{
    emitBinary(instr, builder, MOpcode::FDIV, MOpcode::FDIV, RegClass::XMM, false);
}

void emitAnd(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        emitBinary(instr, builder, MOpcode::ANDrr, MOpcode::ANDri, cls, true);
    }
}

void emitOr(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        emitBinary(instr, builder, MOpcode::ORrr, MOpcode::ORri, cls, true);
    }
}

void emitXor(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        emitBinary(instr, builder, MOpcode::XORrr, MOpcode::XORri, cls, true);
    }
}

void emitICmp(const ILInstr &instr, MIRBuilder &builder)
{
    const auto cond = icmpConditionCode(instr.opcode);
    if (cond)
    {
        emitCmp(instr, builder, RegClass::GPR, *cond);
    }
}

void emitFCmp(const ILInstr &instr, MIRBuilder &builder)
{
    const auto cond = fcmpConditionCode(instr.opcode);
    if (cond)
    {
        emitCmp(instr, builder, RegClass::XMM, *cond);
    }
}

void emitCmpExplicit(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls =
        builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind);
    emitCmp(instr, builder, cls, 1);
}

void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder)
{
    emitShift(instr, builder, MOpcode::SHLri, MOpcode::SHLrc);
}

void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder)
{
    emitShift(instr, builder, MOpcode::SHRri, MOpcode::SHRrc);
}

void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder)
{
    emitShift(instr, builder, MOpcode::SARri, MOpcode::SARrc);
}

void emitZSTrunc(const ILInstr &instr, MIRBuilder &builder)
{
    emitCast(instr,
             builder,
             MOpcode::MOVrr,
             builder.regClassFor(instr.resultKind),
             builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
}

void emitSIToFP(const ILInstr &instr, MIRBuilder &builder)
{
    emitCast(instr, builder, MOpcode::CVTSI2SD, RegClass::XMM, RegClass::GPR);
}

void emitFPToSI(const ILInstr &instr, MIRBuilder &builder)
{
    emitCast(instr, builder, MOpcode::CVTTSD2SI, RegClass::GPR, RegClass::XMM);
}

void emitDivFamily(const ILInstr &instr, MIRBuilder &builder)
{
    emitDivRem(instr, builder, instr.opcode);
}

} // namespace lowering

namespace
{

template <std::size_t Index>
bool matchRuleThunk(const IL::Instr &instr)
{
    return matchesRuleSpec(lowering::kLoweringRuleTable[Index], instr);
}

template <std::size_t Index>
void emitRuleThunk(const IL::Instr &instr, MIRBuilder &builder)
{
    lowering::kLoweringRuleTable[Index].emit(instr, builder);
}

template <std::size_t... Indices>
const std::vector<LoweringRule> &makeRules(std::index_sequence<Indices...>)
{
    static const std::vector<LoweringRule> rules{
        LoweringRule{&matchRuleThunk<Indices>, &emitRuleThunk<Indices>,
                     lowering::kLoweringRuleTable[Indices].name}...};
    return rules;
}

const std::vector<LoweringRule> &buildRules()
{
    static const auto &rules =
        makeRules(std::make_index_sequence<lowering::kLoweringRuleTable.size()>{});
    return rules;
}

} // namespace

const std::vector<LoweringRule> &viper_get_lowering_rules()
{
    return buildRules();
}

const LoweringRule *viper_select_rule(const IL::Instr &instr)
{
    const auto *spec = lookupRuleSpec(instr);
    if (!spec)
    {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(spec - lowering::kLoweringRuleTable.data());
    const auto &rules = buildRules();
    return index < rules.size() ? &rules[index] : nullptr;
}

} // namespace viper::codegen::x64
