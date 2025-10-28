//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EmitCommon.cpp
// Purpose: Implement the shared lowering helpers declared in
//          Lowering.EmitCommon.hpp.  Consolidating the logic keeps the opcode
//          specific translation units focused on control flow while reusing the
//          register materialisation and instruction assembly machinery.
// Key invariants: Helper routines respect the register class requested by the
//                 caller and only create temporaries when strictly necessary.
// Ownership/Lifetime: Operates on a borrowed MIRBuilder reference; no IL or MIR
//                     objects are owned by this file.
//
//===----------------------------------------------------------------------===//

#include "Lowering.EmitCommon.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

[[nodiscard]] bool fitsImm32(int64_t value) noexcept
{
    return value >= static_cast<int64_t>(std::numeric_limits<int32_t>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<int32_t>::max());
}

} // namespace

EmitCommon::EmitCommon(MIRBuilder &builder) noexcept : builder_(&builder) {}

MIRBuilder &EmitCommon::builder() const noexcept
{
    return *builder_;
}

Operand EmitCommon::clone(const Operand &operand) const
{
    return operand;
}

Operand EmitCommon::materialise(Operand operand, RegClass cls)
{
    if (std::holds_alternative<OpReg>(operand))
    {
        return operand;
    }

    const VReg tmp = builder().makeTempVReg(cls);
    const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

    if (std::holds_alternative<OpImm>(operand))
    {
        builder().append(
            MInstr::make(MOpcode::MOVri, std::vector<Operand>{clone(tmpOp), clone(operand)}));
    }
    else if (std::holds_alternative<OpLabel>(operand) || std::holds_alternative<OpRipLabel>(operand))
    {
        builder().append(
            MInstr::make(MOpcode::LEA, std::vector<Operand>{clone(tmpOp), clone(operand)}));
    }
    else
    {
        builder().append(
            MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(tmpOp), clone(operand)}));
    }

    return tmpOp;
}

Operand EmitCommon::materialiseGpr(Operand operand)
{
    return materialise(std::move(operand), RegClass::GPR);
}

void EmitCommon::emitBinary(const ILInstr &instr,
                            MOpcode opcRR,
                            MOpcode opcRI,
                            RegClass cls,
                            bool requireImm32)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = builder().makeOperandForValue(instr.ops[0], cls);
    Operand rhs = builder().makeOperandForValue(instr.ops[1], cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        builder().append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{clone(dest), lhs}));
    }
    else
    {
        builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(dest), lhs}));
    }

    const bool canUseImm = [&]() {
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
        return fitsImm32(imm->val);
    }();

    if (canUseImm)
    {
        builder().append(MInstr::make(opcRI, std::vector<Operand>{clone(dest), rhs}));
        return;
    }

    const Operand rhsReg = materialise(std::move(rhs), cls);
    builder().append(MInstr::make(opcRR, std::vector<Operand>{clone(dest), clone(rhsReg)}));
}

void EmitCommon::emitShift(const ILInstr &instr, MOpcode opcImm, MOpcode opcReg)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = builder().makeOperandForValue(instr.ops[0], destReg.cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        builder().append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{clone(dest), lhs}));
    }
    else
    {
        builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(dest), lhs}));
    }

    Operand rhs = builder().makeOperandForValue(instr.ops[1], destReg.cls);
    if (auto *imm = std::get_if<OpImm>(&rhs))
    {
        const auto masked = static_cast<int64_t>(static_cast<std::uint8_t>(imm->val));
        builder().append(
            MInstr::make(opcImm, std::vector<Operand>{clone(dest), makeImmOperand(masked)}));
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
        builder().append(MInstr::make(MOpcode::MOVrr,
                                      std::vector<Operand>{clone(clOperand), clone(rhs)}));
    }

    builder().append(MInstr::make(opcReg, std::vector<Operand>{clone(dest), clone(clOperand)}));
}

void EmitCommon::emitCmp(const ILInstr &instr, RegClass cls, int defaultCond)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    int condCode = defaultCond;
    Operand condOperand{};
    if (instr.ops.size() > 2)
    {
        condOperand = builder().makeOperandForValue(instr.ops[2], RegClass::GPR);
        if (const auto *imm = std::get_if<OpImm>(&condOperand))
        {
            condCode = static_cast<int>(imm->val);
        }
    }

    const Operand lhs = builder().makeOperandForValue(instr.ops[0], cls);
    const Operand rhs = builder().makeOperandForValue(instr.ops[1], cls);

    const MOpcode cmpOpc = cls == RegClass::XMM ? MOpcode::UCOMIS : MOpcode::CMPrr;
    builder().append(MInstr::make(cmpOpc, std::vector<Operand>{clone(lhs), rhs}));

    if (instr.resultId < 0)
    {
        return;
    }

    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    builder().append(MInstr::make(MOpcode::SETcc,
                                  std::vector<Operand>{makeImmOperand(condCode), dest}));
}

void EmitCommon::emitSelect(const ILInstr &instr)
{
    if (instr.resultId < 0 || instr.ops.size() < 3)
    {
        return;
    }

    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand cond = builder().makeOperandForValue(instr.ops[0], RegClass::GPR);
    const Operand trueVal = builder().makeOperandForValue(instr.ops[1], destReg.cls);
    const Operand falseVal = builder().makeOperandForValue(instr.ops[2], destReg.cls);

    if (destReg.cls == RegClass::GPR)
    {
        Operand cmovSource = trueVal;
        if (std::holds_alternative<OpImm>(cmovSource))
        {
            const VReg tmpVReg = builder().makeTempVReg(destReg.cls);
            cmovSource = makeVRegOperand(tmpVReg.cls, tmpVReg.id);
            builder().append(MInstr::make(MOpcode::MOVri,
                                          std::vector<Operand>{clone(cmovSource), trueVal}));
        }

        const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
        std::vector<Operand> movOperands{};
        movOperands.push_back(clone(dest));
        movOperands.push_back(clone(falseVal));
        movOperands.push_back(clone(cmovSource));
        builder().append(MInstr::make(falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr,
                                      std::move(movOperands)));

        builder().append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{clone(cond), cond}));
        builder().append(
            MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), clone(dest)}));
        return;
    }

    std::vector<Operand> movOperands{};
    movOperands.push_back(clone(dest));
    movOperands.push_back(clone(falseVal));
    movOperands.push_back(clone(trueVal));
    builder().append(MInstr::make(MOpcode::MOVSDrr, std::move(movOperands)));

    builder().append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{clone(cond), cond}));
    builder().append(
        MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), clone(dest)}));
}

void EmitCommon::emitBranch(const ILInstr &instr)
{
    if (instr.ops.empty())
    {
        return;
    }
    builder().append(MInstr::make(MOpcode::JMP,
                                  std::vector<Operand>{builder().makeLabelOperand(instr.ops[0])}));
}

void EmitCommon::emitCondBranch(const ILInstr &instr)
{
    if (instr.ops.size() < 3)
    {
        return;
    }

    const Operand cond = builder().makeOperandForValue(instr.ops[0], RegClass::GPR);
    const Operand trueLabel = builder().makeLabelOperand(instr.ops[1]);
    const Operand falseLabel = builder().makeLabelOperand(instr.ops[2]);

    builder().append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{clone(cond), cond}));
    builder().append(MInstr::make(MOpcode::JCC,
                                  std::vector<Operand>{makeImmOperand(1), trueLabel}));
    builder().append(MInstr::make(MOpcode::JMP, std::vector<Operand>{falseLabel}));
}

void EmitCommon::emitReturn(const ILInstr &instr)
{
    if (instr.ops.empty())
    {
        builder().append(MInstr::make(MOpcode::RET, {}));
        return;
    }

    const ILValue &retVal = instr.ops.front();
    const RegClass cls = builder().regClassFor(retVal.kind);

    Operand src = builder().makeOperandForValue(retVal, cls);

    if (retVal.kind == ILValue::Kind::I1)
    {
        if (const auto *imm = std::get_if<OpImm>(&src))
        {
            src = makeImmOperand(imm->val != 0 ? 1 : 0);
        }
    }

    Operand srcReg = materialise(std::move(src), cls);

    if (retVal.kind == ILValue::Kind::I1 && std::holds_alternative<OpReg>(srcReg))
    {
        const auto &reg = std::get<OpReg>(srcReg);
        if (!reg.isPhys)
        {
            const VReg zx = builder().makeTempVReg(RegClass::GPR);
            const Operand zxOp = makeVRegOperand(zx.cls, zx.id);
            builder().append(
                MInstr::make(MOpcode::MOVZXrr32, std::vector<Operand>{clone(zxOp), clone(srcReg)}));
            srcReg = zxOp;
        }
    }

    if (cls == RegClass::XMM)
    {
        const Operand retReg = makePhysRegOperand(
            RegClass::XMM, static_cast<uint16_t>(builder().target().f64ReturnReg));
        builder().append(MInstr::make(MOpcode::MOVSDrr,
                                      std::vector<Operand>{retReg, clone(srcReg)}));
    }
    else
    {
        const Operand retReg = makePhysRegOperand(
            RegClass::GPR, static_cast<uint16_t>(builder().target().intReturnReg));
        builder().append(MInstr::make(MOpcode::MOVrr,
                                      std::vector<Operand>{retReg, clone(srcReg)}));
    }

    builder().append(MInstr::make(MOpcode::RET, {}));
}

void EmitCommon::emitLoad(const ILInstr &instr, RegClass cls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    Operand baseOp = builder().makeOperandForValue(instr.ops[0], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);
    if (!baseReg)
    {
        return;
    }

    const int32_t disp = instr.ops.size() > 1 ? static_cast<int32_t>(instr.ops[1].i64) : 0;
    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand mem = makeMemOperand(*baseReg, disp);

    if (cls == RegClass::GPR)
    {
        builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(dest), mem}));
    }
    else
    {
        builder().append(MInstr::make(MOpcode::MOVSDmr, std::vector<Operand>{clone(dest), mem}));
    }
}

void EmitCommon::emitStore(const ILInstr &instr)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    const Operand value = builder().makeOperandForValue(
        instr.ops[0], builder().regClassFor(instr.ops[0].kind));
    Operand baseOp = builder().makeOperandForValue(instr.ops[1], RegClass::GPR);
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
            builder().append(MInstr::make(MOpcode::MOVSDrm, std::vector<Operand>{mem, value}));
        }
        else
        {
            builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{mem, value}));
        }
    }
    else
    {
        builder().append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{mem, value}));
    }
}

void EmitCommon::emitCast(const ILInstr &instr, MOpcode opc, RegClass dstCls, RegClass srcCls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    const Operand src = builder().makeOperandForValue(instr.ops[0], srcCls);
    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    if (opc == MOpcode::MOVrr || std::holds_alternative<OpImm>(src))
    {
        builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(dest), src}));
    }
    else
    {
        builder().append(MInstr::make(opc, std::vector<Operand>{clone(dest), src}));
    }

    (void)dstCls;
}

void EmitCommon::emitDivRem(const ILInstr &instr, std::string_view opcode)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = builder().ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    Operand dividend = builder().makeOperandForValue(instr.ops[0], RegClass::GPR);
    Operand divisor = builder().makeOperandForValue(instr.ops[1], RegClass::GPR);

    if (!std::holds_alternative<OpReg>(dividend) && !std::holds_alternative<OpImm>(dividend))
    {
        dividend = materialiseGpr(dividend);
    }

    divisor = materialiseGpr(divisor);

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

    builder().append(MInstr::make(pseudo,
                                  std::vector<Operand>{clone(dest),
                                                       clone(dividend),
                                                       clone(divisor)}));
}

std::optional<int> EmitCommon::icmpConditionCode(std::string_view opcode) noexcept
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

std::optional<int> EmitCommon::fcmpConditionCode(std::string_view opcode) noexcept
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

} // namespace viper::codegen::x64

