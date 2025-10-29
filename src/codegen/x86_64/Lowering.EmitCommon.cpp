//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
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

/// @file
/// @brief Shared lowering helpers used across x86-64 opcode emitters.
/// @details Provides the @ref viper::codegen::x64::EmitCommon façade used by
///          individual lowering translation units to materialise operands,
///          append Machine IR instructions, and synthesise helper operations
///          such as comparisons, shifts, and select emission. Centralising the
///          utilities keeps opcode-specific files focused on control flow while
///          guaranteeing consistent register-class handling.

#include "Lowering.EmitCommon.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

/// @brief Check whether a 64-bit integer fits in the 32-bit immediate domain.
/// @details The check is used before selecting immediate-based instruction
///          forms.  Guarding the conversion prevents accidental truncation when
///          lowering wide IL constants into Machine IR encodings that only
///          accept 32-bit operands.
/// @param value Signed integer to test.
/// @return True when @p value lies within the signed 32-bit range.
[[nodiscard]] bool fitsImm32(int64_t value) noexcept
{
    return value >= static_cast<int64_t>(std::numeric_limits<int32_t>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<int32_t>::max());
}

} // namespace

/// @brief Construct an @ref EmitCommon helper that appends to @p builder.
/// @details Stores the builder pointer for later reuse so subsequent helper
///          calls share a common emission context without repeatedly passing the
///          builder reference.
/// @param builder Active MIR builder receiving emitted instructions.
EmitCommon::EmitCommon(MIRBuilder &builder) noexcept : builder_(&builder) {}

/// @brief Access the underlying MIR builder.
/// @return Reference to the builder supplied at construction.
MIRBuilder &EmitCommon::builder() const noexcept
{
    return *builder_;
}

/// @brief Create a shallow copy of a Machine IR operand.
/// @details Operands are lightweight value types, so copying by value is
///          sufficient when forwarding operands into new instructions.  The
///          helper centralises the intent and improves readability at call
///          sites.
/// @param operand Operand to duplicate.
/// @return Copy of @p operand.
Operand EmitCommon::clone(const Operand &operand) const
{
    return operand;
}

/// @brief Materialise an operand into a register of the requested class.
/// @details Values already represented as registers are passed through.
///          Immediates become MOV instructions, labels trigger LEA, and other
///          operands are copied via MOVrr.  A fresh temporary register is
///          created when materialisation is required so callers can subsequently
///          reference the operand in register-only instruction forms.
/// @param operand Operand that may require materialisation.
/// @param cls Target register class for the temporary.
/// @return Operand referencing the original value or the created temporary.
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
    else if (std::holds_alternative<OpLabel>(operand) ||
             std::holds_alternative<OpRipLabel>(operand))
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

/// @brief Ensure an operand is materialised in a general-purpose register.
/// @details Convenience wrapper around @ref materialise that hard codes the GPR
///          register class, used when instructions require integer register
///          operands.
/// @param operand Operand to materialise if necessary.
/// @return Operand referencing the resulting GPR.
Operand EmitCommon::materialiseGpr(Operand operand)
{
    return materialise(std::move(operand), RegClass::GPR);
}

/// @brief Emit a binary arithmetic instruction with immediate folding support.
/// @details Copies the left-hand operand into the destination register, then
///          attempts to use the immediate form when the right-hand operand is a
///          suitable immediate value.  Otherwise the helper materialises the
///          right-hand operand into a register and emits the register-register
///          encoding.  The routine is shared by many arithmetic lowerings to
///          keep operand-handling logic consistent.
/// @param instr IL instruction currently being lowered.
/// @param opcRR Opcode used when both operands reside in registers.
/// @param opcRI Opcode used when the right operand fits the immediate form.
/// @param cls Register class expected by the instruction.
/// @param requireImm32 When true, immediates must fit in 32 bits to use @p opcRI.
void EmitCommon::emitBinary(
    const ILInstr &instr, MOpcode opcRR, MOpcode opcRI, RegClass cls, bool requireImm32)
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

    const bool canUseImm = [&]()
    {
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

/// @brief Emit a shift instruction choosing between immediate and register forms.
/// @details Moves the left operand into the destination register, then inspects
///          the shift amount.  Literal counts are masked to the architectural
///          width and use the immediate opcode, while variable counts are
///          materialised into RCX (the x86 shift-count register) before emitting
///          the register form.  The helper therefore encapsulates the subtle
///          register conventions of x86 shifts.
/// @param instr IL instruction being lowered.
/// @param opcImm Opcode for the immediate shift form.
/// @param opcReg Opcode for the register-controlled shift form.
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
        builder().append(
            MInstr::make(MOpcode::MOVrr, std::vector<Operand>{clone(clOperand), clone(rhs)}));
    }

    builder().append(MInstr::make(opcReg, std::vector<Operand>{clone(dest), clone(clOperand)}));
}

/// @brief Emit a compare instruction and optional SETcc materialisation.
/// @details Evaluates optional condition-code operands, performs the register or
///          floating-point comparison, and when the IL instruction produces a
///          result emits a SETcc pseudo with the resolved condition code.  The
///          helper shields callers from the boilerplate associated with IL
///          comparisons that may or may not capture their results.
/// @param instr IL compare instruction.
/// @param cls Register class required for the compare operands.
/// @param defaultCond Condition code used when the IL instruction omits an override.
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
    builder().append(
        MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(condCode), dest}));
}

/// @brief Emit the Machine IR sequence that implements an IL select.
/// @details Handles both integer and floating-point selects by materialising the
///          false branch, conditionally overwriting it with the true branch, and
///          generating the required TEST/SETcc pair to drive control flow.  For
///          integer selects immediates are first moved into temporaries so that
///          conditional moves have register operands.
/// @param instr IL select instruction containing condition, true, and false operands.
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
            builder().append(
                MInstr::make(MOpcode::MOVri, std::vector<Operand>{clone(cmovSource), trueVal}));
        }

        const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
        std::vector<Operand> movOperands{};
        movOperands.push_back(clone(dest));
        movOperands.push_back(clone(falseVal));
        movOperands.push_back(clone(cmovSource));
        builder().append(
            MInstr::make(falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr, std::move(movOperands)));

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

/// @brief Emit an unconditional branch to the target label.
/// @details Extracts the label operand from the IL instruction and appends a
///          Machine IR JMP instruction pointing to the resolved block label.
/// @param instr IL branch instruction providing the label operand.
void EmitCommon::emitBranch(const ILInstr &instr)
{
    if (instr.ops.empty())
    {
        return;
    }
    builder().append(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{builder().makeLabelOperand(instr.ops[0])}));
}

/// @brief Emit a conditional branch that tests the provided operand.
/// @details Lowers to a TEST/JCC/JMP sequence that mirrors the IL conditional
///          branch semantics.  The helper prepares both the taken and fallthrough
///          labels so control flow mirrors the IL structure.
/// @param instr IL conditional branch instruction (cond, true, false operands).
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
    builder().append(
        MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(1), trueLabel}));
    builder().append(MInstr::make(MOpcode::JMP, std::vector<Operand>{falseLabel}));
}

/// @brief Emit the return sequence for an IL return instruction.
/// @details Handles empty returns by emitting a bare RET.  When a value is
///          returned, the helper materialises the operand, performs sign or zero
///          extension for boolean returns, moves the value into the appropriate
///          ABI register, and finally emits RET.  This centralises ABI-specific
///          logic so callers remain simple.
/// @param instr IL return instruction.
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
        builder().append(
            MInstr::make(MOpcode::MOVSDrr, std::vector<Operand>{retReg, clone(srcReg)}));
    }
    else
    {
        const Operand retReg = makePhysRegOperand(
            RegClass::GPR, static_cast<uint16_t>(builder().target().intReturnReg));
        builder().append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{retReg, clone(srcReg)}));
    }

    builder().append(MInstr::make(MOpcode::RET, {}));
}

/// @brief Emit a load from memory into a virtual register.
/// @details Accepts a base register and optional displacement, verifies that the
///          base is addressable, and then emits either a MOV or MOVSD load based
///          on the requested register class.  The helper ensures the destination
///          virtual register exists before appending the instruction.
/// @param instr IL load instruction describing the address and displacement.
/// @param cls Register class expected for the loaded value.
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

/// @brief Emit a store from a value operand into memory.
/// @details Resolves the address operands, ensures the base is a register, and
///          emits the appropriate MOV variant depending on whether the value is
///          an integer, floating-point, register, or immediate.  The helper
///          therefore abstracts the heterogeneity of IL store operands.
/// @param instr IL store instruction.
void EmitCommon::emitStore(const ILInstr &instr)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    const Operand value =
        builder().makeOperandForValue(instr.ops[0], builder().regClassFor(instr.ops[0].kind));
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

/// @brief Emit a cast or move between register classes.
/// @details Creates the destination virtual register and either forwards MOV
///          when the operation is a pure copy, or appends the supplied opcode to
///          perform the conversion.  Immediates are copied using MOV to ensure a
///          register destination is produced.
/// @param instr IL cast instruction.
/// @param opc Machine opcode implementing the conversion.
/// @param dstCls Destination register class (unused but recorded for clarity).
/// @param srcCls Source register class for operand materialisation.
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

/// @brief Emit a division or remainder pseudo instruction.
/// @details Materialises both dividend and divisor into GPRs, selecting the
///          appropriate pseudo opcode based on the mnemonic string.  The helper
///          emits a three-operand instruction capturing destination, dividend,
///          and divisor, leaving it to later passes to expand into concrete
///          machine instructions.
/// @param instr IL div/rem instruction with dividend and divisor operands.
/// @param opcode Textual opcode (e.g. "div", "srem") used to select the pseudo.
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

    const MOpcode pseudo = [&]()
    {
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

    builder().append(
        MInstr::make(pseudo, std::vector<Operand>{clone(dest), clone(dividend), clone(divisor)}));
}

/// @brief Map an integer compare opcode string to a condition code.
/// @details Recognises the canonical `icmp_*` prefixes and converts the suffix
///          into the SETcc encoding expected by Machine IR.  Unrecognised
///          strings yield @c std::nullopt so callers can detect missing support.
/// @param opcode IL opcode string to translate.
/// @return Condition code index or empty optional when unsupported.
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

/// @brief Map a floating-point compare opcode string to a condition code.
/// @details Similar to @ref icmpConditionCode but handles the `fcmp_*` family of
///          opcodes, returning the encoding index consumed by SETcc expansion.
/// @param opcode IL opcode string to translate.
/// @return Condition code index or empty optional when unsupported.
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
