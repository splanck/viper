//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Lowering.Arith.cpp
// Purpose: Implement arithmetic opcode lowering rules for the x86-64 backend.
//          Arithmetic emitters delegate common mechanics to EmitCommon, keeping
//          each rule focused on opcode selection.
// Key invariants:
//   - All emitters honour the register classes reported by the MIRBuilder.
//   - No instructions are emitted when operands are malformed.
// Ownership/Lifetime:
//   - Operates on borrowed IL instructions and MIR builders; no persistent state.
// Links: codegen/x86_64/LoweringRules.hpp,
//        codegen/x86_64/Lowering.EmitCommon.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"
#include "Unsupported.hpp"

namespace viper::codegen::x64::lowering {
namespace {

constexpr int64_t kErrInvalidCast = 5;
constexpr int64_t kErrOverflow = 4;

uint64_t signedMinF64Bits(std::uint8_t widthBits) {
    switch (widthBits) {
        case 16:
            return 0xC0E0000000000000ULL; // -2^15
        case 32:
            return 0xC1E0000000000000ULL; // -2^31
        case 64:
            return 0xC3E0000000000000ULL; // -2^63
        default:
            phaseAUnsupported("checked fp-to-si cast: unsupported result width");
    }
}

uint64_t signedUpperExclusiveF64Bits(std::uint8_t widthBits) {
    switch (widthBits) {
        case 16:
            return 0x40E0000000000000ULL; // 2^15
        case 32:
            return 0x41E0000000000000ULL; // 2^31
        case 64:
            return 0x43E0000000000000ULL; // 2^63
        default:
            phaseAUnsupported("checked fp-to-si cast: unsupported result width");
    }
}

uint64_t unsignedUpperExclusiveF64Bits(std::uint8_t widthBits) {
    switch (widthBits) {
        case 16:
            return 0x40F0000000000000ULL; // 2^16
        case 32:
            return 0x41F0000000000000ULL; // 2^32
        case 64:
            return 0x43F0000000000000ULL; // 2^64
        default:
            phaseAUnsupported("checked fp-to-ui cast: unsupported result width");
    }
}

MInstr makePlannedCall(Operand target, uint32_t callPlanId) {
    MInstr call = MInstr::make(MOpcode::CALL, std::vector<Operand>{std::move(target)});
    call.callPlanId = callPlanId;
    return call;
}

void emitTrapRaiseError(MIRBuilder &builder, int64_t errCode) {
    CallLoweringPlan plan{};
    plan.callee = "rt_trap_raise_error";
    plan.numNamedArgs = 1;
    plan.args.push_back(
        CallArg{.cls = CallArgClass::GPR, .vreg = 0, .isImm = true, .imm = errCode});
    const uint32_t callPlanId = builder.recordCallPlan(std::move(plan));
    builder.append(makePlannedCall(makeLabelOperand(std::string{"rt_trap_raise_error"}), callPlanId));
    builder.append(MInstr::make(MOpcode::UD2));
}

Operand emitSignExtendedToWidth(MIRBuilder &builder,
                                EmitCommon &emit,
                                Operand src,
                                std::uint8_t widthBits) {
    Operand value = emit.materialiseGpr(std::move(src));
    if (widthBits >= 64) {
        return value;
    }

    const int shift = 64 - static_cast<int>(widthBits);
    const VReg narrowedReg = builder.makeTempVReg(RegClass::GPR);
    const Operand narrowed = makeVRegOperand(narrowedReg.cls, narrowedReg.id);
    builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(narrowed), emit.clone(value)}));
    builder.append(MInstr::make(MOpcode::SHLri, {emit.clone(narrowed), makeImmOperand(shift)}));
    builder.append(MInstr::make(MOpcode::SARri, {emit.clone(narrowed), makeImmOperand(shift)}));
    return narrowed;
}

Operand emitZeroExtendedToWidth(MIRBuilder &builder,
                                EmitCommon &emit,
                                Operand src,
                                std::uint8_t widthBits) {
    Operand value = emit.materialiseGpr(std::move(src));
    if (widthBits >= 64) {
        return value;
    }

    const int shift = 64 - static_cast<int>(widthBits);
    const VReg narrowedReg = builder.makeTempVReg(RegClass::GPR);
    const Operand narrowed = makeVRegOperand(narrowedReg.cls, narrowedReg.id);
    builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(narrowed), emit.clone(value)}));
    builder.append(MInstr::make(MOpcode::SHLri, {emit.clone(narrowed), makeImmOperand(shift)}));
    builder.append(MInstr::make(MOpcode::SHRri, {emit.clone(narrowed), makeImmOperand(shift)}));
    return narrowed;
}

void emitSubWidthOverflowCheck(MIRBuilder &builder,
                               EmitCommon &emit,
                               const Operand &value,
                               std::uint8_t widthBits,
                               const std::string &prefix) {
    if (widthBits >= 64) {
        return;
    }

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string trapLabel = prefix + "_trap_" + std::to_string(labelId);
    const std::string doneLabel = prefix + "_done_" + std::to_string(labelId);
    Operand narrowed = emitSignExtendedToWidth(builder, emit, emit.clone(value), widthBits);
    builder.append(MInstr::make(MOpcode::CMPrr, {emit.clone(narrowed), emit.clone(value)}));
    builder.append(
        MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(trapLabel)}));
    builder.append(MInstr::make(MOpcode::JMP, {makeLabelOperand(doneLabel)}));
    builder.append(MInstr::make(MOpcode::LABEL, {makeLabelOperand(trapLabel)}));
    emitTrapRaiseError(builder, kErrOverflow);
    builder.append(MInstr::make(MOpcode::LABEL, {makeLabelOperand(doneLabel)}));
}

Operand emitF64BitsConstant(MIRBuilder &builder, uint64_t bits) {
    const VReg bitsReg = builder.makeTempVReg(RegClass::GPR);
    const Operand bitsOp = makeVRegOperand(bitsReg.cls, bitsReg.id);
    const VReg xmmReg = builder.makeTempVReg(RegClass::XMM);
    const Operand xmmOp = makeVRegOperand(xmmReg.cls, xmmReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVri, {bitsOp, makeImmOperand(static_cast<int64_t>(bits))}));
    builder.append(MInstr::make(MOpcode::MOVQrx, {xmmOp, bitsOp}));
    return xmmOp;
}

Operand emitRoundEvenCall(MIRBuilder &builder, const Operand &src) {
    CallLoweringPlan plan{};
    plan.callee = "rt_round_even";
    plan.numNamedArgs = 2;
    const auto *srcReg = std::get_if<OpReg>(&src);
    if (!srcReg) {
        phaseAUnsupported("rt_round_even source did not materialize to an XMM register");
    }
    plan.args.push_back(
        CallArg{.cls = CallArgClass::FPR, .vreg = srcReg->idOrPhys, .isImm = false, .imm = 0});
    plan.args.push_back(CallArg{.cls = CallArgClass::GPR, .vreg = 0, .isImm = true, .imm = 0});

    const uint32_t callPlanId = builder.recordCallPlan(std::move(plan));
    builder.append(makePlannedCall(makeLabelOperand(std::string{"rt_round_even"}), callPlanId));

    const VReg roundedReg = builder.makeTempVReg(RegClass::XMM);
    const Operand roundedOp = makeVRegOperand(roundedReg.cls, roundedReg.id);
    builder.append(MInstr::make(
        MOpcode::MOVSDrr,
        {roundedOp,
         makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(builder.target().f64ReturnReg))}));
    return roundedOp;
}

} // namespace

/// @brief Lower an integer or floating-point add IL instruction.
/// @details Selects MOV/ADD forms based on the destination register class and
///          delegates operand handling to @ref EmitCommon::emitBinary so immediates
///          can be folded when possible.
/// @param instr IL add instruction to lower.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitAdd(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::ADDrr : MOpcode::FADD;
    const MOpcode opRI = cls == RegClass::GPR ? MOpcode::ADDri : opRR;
    emit.emitBinary(instr, opRR, opRI, cls, cls == RegClass::GPR);
}

/// @brief Lower a subtraction IL instruction.
/// @details Chooses between integer and floating-point subtraction opcodes, then
///          forwards to @ref EmitCommon::emitBinary to handle operand
///          normalisation.
/// @param instr IL subtract instruction to lower.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitSub(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::SUBrr : MOpcode::FSUB;
    emit.emitBinary(instr, opRR, opRR, cls, false);
}

/// @brief Lower a multiply IL instruction.
/// @details Selects integer or floating-point multiply opcodes and leverages
///          @ref EmitCommon::emitBinary to move operands into their canonical
///          locations.
/// @param instr IL multiply instruction to lower.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitMul(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::IMULrr : MOpcode::FMUL;
    emit.emitBinary(instr, opRR, opRR, cls, false);
}

/// @brief Lower a floating-point division IL instruction.
/// @details Division always occurs in XMM registers, so the helper directly
///          invokes @ref EmitCommon::emitBinary with FDIV opcodes and floating
///          register classes.
/// @param instr IL floating-point divide instruction.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitFDiv(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitBinary(instr, MOpcode::FDIV, MOpcode::FDIV, RegClass::XMM, false);
}

/// @brief Lower an integer compare IL instruction.
/// @details Uses @ref EmitCommon::icmpConditionCode to resolve the condition
///          code and @ref EmitCommon::emitCmp to produce the Machine IR compare
///          sequence.
/// @param instr IL integer compare instruction.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitICmp(const ILInstr &instr, MIRBuilder &builder) {
    if (const auto cond = EmitCommon::icmpConditionCode(instr.opcode)) {
        EmitCommon(builder).emitCmp(instr, RegClass::GPR, *cond);
    }
}

/// @brief Lower a floating-point compare IL instruction.
/// @details Routes NaN-sensitive comparisons (eq, ne, lt, le) through the
///          multi-instruction @ref EmitCommon::emitFCmpNanSafe helper that
///          correctly handles the UCOMISD flag state when either operand is NaN.
///          The remaining comparisons (gt, ge, ord, uno) use simple SETcc via
///          the generic @ref EmitCommon::emitCmp path.
/// @param instr IL floating-point compare instruction.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitFCmp(const ILInstr &instr, MIRBuilder &builder) {
    if (!instr.opcode.starts_with("fcmp_")) {
        return;
    }

    const std::string_view suffix(instr.opcode.data() + 5, instr.opcode.size() - 5);

    // eq/ne/lt/le require NaN-safe multi-instruction sequences.
    if (suffix == "eq" || suffix == "ne" || suffix == "lt" || suffix == "le") {
        EmitCommon(builder).emitFCmpNanSafe(instr, suffix);
        return;
    }

    // gt/ge/ord/uno are already NaN-correct with a single SETcc.
    if (const auto cond = EmitCommon::fcmpConditionCode(instr.opcode)) {
        EmitCommon(builder).emitCmp(instr, RegClass::XMM, *cond);
    }
}

/// @brief Lower an explicit compare IL instruction that encodes the result type.
/// @details Determines the register class using either the result or first
///          operand kind, then emits a compare that materialises the condition
///          into the destination virtual register.
/// @param instr IL compare instruction with explicit operands.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitCmpExplicit(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    const RegClass cls =
        builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind);
    emit.emitCmp(instr, cls, 1);
}

/// @brief Lower an overflow-checked integer add.
/// @details Emits the ADDOvfrr pseudo-op which the post-lowering pass will
///          expand into ADD + JO (trap on overflow).
void emitAddOvf(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    if (instr.resultBits < 64) {
        if (instr.resultId < 0 || instr.ops.size() < 2) {
            phaseAUnsupported("iadd.ovf: missing operands");
        }
        const Operand lhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[0], RegClass::GPR), instr.resultBits);
        const Operand rhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[1], RegClass::GPR), instr.resultBits);
        const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
        const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
        builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(dest), emit.clone(lhs)}));
        builder.append(MInstr::make(MOpcode::ADDrr, {emit.clone(dest), emit.clone(rhs)}));
        emitSubWidthOverflowCheck(builder, emit, dest, instr.resultBits, ".Liadd_ovf");
        return;
    }
    emit.emitBinary(instr, MOpcode::ADDOvfrr, MOpcode::ADDOvfrr, RegClass::GPR, false);
}

/// @brief Lower an overflow-checked integer subtract.
/// @details Emits the SUBOvfrr pseudo-op.
void emitSubOvf(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    if (instr.resultBits < 64) {
        if (instr.resultId < 0 || instr.ops.size() < 2) {
            phaseAUnsupported("isub.ovf: missing operands");
        }
        const Operand lhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[0], RegClass::GPR), instr.resultBits);
        const Operand rhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[1], RegClass::GPR), instr.resultBits);
        const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
        const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
        builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(dest), emit.clone(lhs)}));
        builder.append(MInstr::make(MOpcode::SUBrr, {emit.clone(dest), emit.clone(rhs)}));
        emitSubWidthOverflowCheck(builder, emit, dest, instr.resultBits, ".Lisub_ovf");
        return;
    }
    emit.emitBinary(instr, MOpcode::SUBOvfrr, MOpcode::SUBOvfrr, RegClass::GPR, false);
}

/// @brief Lower an overflow-checked integer multiply.
/// @details Emits the IMULOvfrr pseudo-op.
void emitMulOvf(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    if (instr.resultBits < 64) {
        if (instr.resultId < 0 || instr.ops.size() < 2) {
            phaseAUnsupported("imul.ovf: missing operands");
        }
        const Operand lhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[0], RegClass::GPR), instr.resultBits);
        const Operand rhs = emitSignExtendedToWidth(
            builder, emit, builder.makeOperandForValue(instr.ops[1], RegClass::GPR), instr.resultBits);
        const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
        const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
        builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(dest), emit.clone(lhs)}));
        builder.append(MInstr::make(MOpcode::IMULrr, {emit.clone(dest), emit.clone(rhs)}));
        emitSubWidthOverflowCheck(builder, emit, dest, instr.resultBits, ".Limul_ovf");
        return;
    }
    emit.emitBinary(instr, MOpcode::IMULOvfrr, MOpcode::IMULOvfrr, RegClass::GPR, false);
}

void emitDivFamily(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitDivRem(instr, instr.opcode);
}

void emitZSTrunc(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon emit(builder);
    emit.emitCast(
        instr,
        MOpcode::MOVrr,
        builder.regClassFor(instr.resultKind),
        builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
}

void emitSIToFP(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitCast(instr, MOpcode::CVTSI2SD, RegClass::XMM, RegClass::GPR);
}

void emitFPToSI(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.resultId < 0 || instr.ops.empty()) {
        return;
    }

    EmitCommon emit(builder);
    const Operand src =
        emit.materialise(builder.makeOperandForValue(instr.ops[0], RegClass::XMM), RegClass::XMM);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string invalidLabel = ".Lfptosi_invalid_" + std::to_string(labelId);
    const std::string overflowLabel = ".Lfptosi_ovf_" + std::to_string(labelId);
    const std::string doneLabel = ".Lfptosi_done_" + std::to_string(labelId);

    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(src), emit.clone(src)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(10), makeLabelOperand(invalidLabel)}));

    const Operand minOp = emitF64BitsConstant(builder, signedMinF64Bits(instr.resultBits));
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(src), emit.clone(minOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(8), makeLabelOperand(overflowLabel)}));

    const Operand maxExclusiveOp =
        emitF64BitsConstant(builder, signedUpperExclusiveF64Bits(instr.resultBits));
    builder.append(MInstr::make(MOpcode::UCOMIS,
                                std::vector<Operand>{emit.clone(src), emit.clone(maxExclusiveOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(7), makeLabelOperand(overflowLabel)}));

    builder.append(
        MInstr::make(MOpcode::CVTTSD2SI, std::vector<Operand>{emit.clone(dest), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(invalidLabel)}));
    emitTrapRaiseError(builder, kErrInvalidCast);
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(overflowLabel)}));
    emitTrapRaiseError(builder, kErrOverflow);
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
}

void emitFPToSIChecked(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.resultId < 0 || instr.ops.empty())
        return;

    EmitCommon emit(builder);
    const Operand src =
        emit.materialise(builder.makeOperandForValue(instr.ops[0], RegClass::XMM), RegClass::XMM);
    const Operand rounded = emitRoundEvenCall(builder, src);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string invalidLabel = ".Lfptosi_chk_invalid_" + std::to_string(labelId);
    const std::string overflowLabel = ".Lfptosi_chk_ovf_" + std::to_string(labelId);
    const std::string doneLabel = ".Lfptosi_chk_done_" + std::to_string(labelId);

    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(rounded), emit.clone(rounded)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(10), makeLabelOperand(invalidLabel)}));

    const Operand minOp = emitF64BitsConstant(builder, signedMinF64Bits(instr.resultBits));
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(rounded), emit.clone(minOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(8), makeLabelOperand(overflowLabel)}));

    const Operand maxExclusiveOp =
        emitF64BitsConstant(builder, signedUpperExclusiveF64Bits(instr.resultBits));
    builder.append(MInstr::make(MOpcode::UCOMIS,
                                std::vector<Operand>{emit.clone(rounded), emit.clone(maxExclusiveOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(7), makeLabelOperand(overflowLabel)}));

    builder.append(
        MInstr::make(MOpcode::CVTTSD2SI, std::vector<Operand>{emit.clone(dest), emit.clone(rounded)}));
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(invalidLabel)}));
    emitTrapRaiseError(builder, kErrInvalidCast);
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(overflowLabel)}));
    emitTrapRaiseError(builder, kErrOverflow);
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
}

void emitFpToUi(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.resultId < 0 || instr.ops.empty()) {
        return;
    }

    EmitCommon emit(builder);
    const Operand src =
        emit.materialise(builder.makeOperandForValue(instr.ops[0], RegClass::XMM), RegClass::XMM);
    const Operand rounded = emitRoundEvenCall(builder, src);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string invalidLabel = ".Lfptoui_invalid_" + std::to_string(labelId);
    const std::string overflowLabel = ".Lfptoui_ovf_" + std::to_string(labelId);
    const std::string smallLabel = ".Lfptoui_sm_" + std::to_string(labelId);
    const std::string doneLabel = ".Lfptoui_done_" + std::to_string(labelId);

    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(rounded), emit.clone(rounded)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(10), makeLabelOperand(invalidLabel)}));

    const Operand zeroOp = emitF64BitsConstant(builder, 0x0000000000000000ULL);
    builder.append(MInstr::make(MOpcode::UCOMIS,
                                std::vector<Operand>{emit.clone(rounded), emit.clone(zeroOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(8), makeLabelOperand(invalidLabel)}));

    const Operand upperExclusiveOp =
        emitF64BitsConstant(builder, unsignedUpperExclusiveF64Bits(instr.resultBits));
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(rounded), emit.clone(upperExclusiveOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(7), makeLabelOperand(overflowLabel)}));

    const Operand limitOp = emitF64BitsConstant(builder, 0x43E0000000000000ULL);
    builder.append(MInstr::make(
        MOpcode::UCOMIS, std::vector<Operand>{emit.clone(rounded), emit.clone(limitOp)}));
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(8), makeLabelOperand(smallLabel)}));

    const VReg adjReg = builder.makeTempVReg(RegClass::XMM);
    const Operand adjOp = makeVRegOperand(adjReg.cls, adjReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVSDrr, std::vector<Operand>{emit.clone(adjOp), emit.clone(rounded)}));
    builder.append(MInstr::make(MOpcode::FSUB, std::vector<Operand>{adjOp, emit.clone(limitOp)}));
    builder.append(MInstr::make(MOpcode::CVTTSD2SI,
                                std::vector<Operand>{emit.clone(dest), emit.clone(adjOp)}));
    const VReg highBitReg = builder.makeTempVReg(RegClass::GPR);
    const Operand highBitOp = makeVRegOperand(highBitReg.cls, highBitReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVri,
                     std::vector<Operand>{
                         highBitOp, makeImmOperand(static_cast<int64_t>(0x8000000000000000ULL))}));
    builder.append(MInstr::make(MOpcode::ORrr, std::vector<Operand>{dest, highBitOp}));
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));

    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(invalidLabel)}));
    emitTrapRaiseError(builder, kErrInvalidCast);
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(overflowLabel)}));
    emitTrapRaiseError(builder, kErrOverflow);

    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(smallLabel)}));
    builder.append(
        MInstr::make(MOpcode::CVTTSD2SI, std::vector<Operand>{dest, emit.clone(rounded)}));

    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
}

void emitNarrowCastChecked(const ILInstr &instr, MIRBuilder &builder, bool isSigned) {
    if (instr.resultId < 0 || instr.ops.empty()) {
        return;
    }

    EmitCommon emit(builder);
    const Operand src =
        emit.materialiseGpr(builder.makeOperandForValue(instr.ops[0], RegClass::GPR));
    const Operand narrowed = isSigned
                                 ? emitSignExtendedToWidth(builder, emit, emit.clone(src), instr.resultBits)
                                 : emitZeroExtendedToWidth(builder, emit, emit.clone(src), instr.resultBits);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string trapLabel = ".Lnarrow_chk_trap_" + std::to_string(labelId);
    const std::string doneLabel = ".Lnarrow_chk_done_" + std::to_string(labelId);

    builder.append(MInstr::make(MOpcode::CMPrr, {emit.clone(narrowed), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(trapLabel)}));
    builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(dest), emit.clone(narrowed)}));
    builder.append(MInstr::make(MOpcode::JMP, {makeLabelOperand(doneLabel)}));
    builder.append(MInstr::make(MOpcode::LABEL, {makeLabelOperand(trapLabel)}));
    emitTrapRaiseError(builder, kErrOverflow);
    builder.append(MInstr::make(MOpcode::LABEL, {makeLabelOperand(doneLabel)}));
}

void emitSiNarrowChecked(const ILInstr &instr, MIRBuilder &builder) {
    emitNarrowCastChecked(instr, builder, true);
}

void emitUiNarrowChecked(const ILInstr &instr, MIRBuilder &builder) {
    emitNarrowCastChecked(instr, builder, false);
}

/// @brief Lower an IL `uitofp` instruction (unsigned int to floating-point).
/// @details Handles the full unsigned 64-bit input range [0, 2^64).
///
///          x86-64 CVTSI2SD interprets its source as signed.  Values in [0, 2^63)
///          convert correctly.  For values with bit 63 set (>= 2^63), we shift
///          right by 1, preserving the LSB for rounding, convert the halved value,
///          then double the result with ADDSD.
///
///          Generated sequence:
///            testq  %src, %src
///            js     .Lhigh           ; sign bit set → large path
///            cvtsi2sd %src, %dst     ; [0, 2^63) direct
///            jmp    .Ldone
///          .Lhigh:
///            movq   %src, %tmp
///            andq   $1, %lsb        ; save LSB
///            shrq   $1, %tmp        ; halve (now positive)
///            orq    %lsb, %tmp      ; restore rounding bit
///            cvtsi2sd %tmp, %dst
///            addsd  %dst, %dst      ; double back
///          .Ldone:
void emitUiToFp(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.resultId < 0 || instr.ops.empty()) {
        return;
    }

    EmitCommon emit(builder);
    const Operand src =
        emit.materialiseGpr(builder.makeOperandForValue(instr.ops[0], RegClass::GPR));
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string highLabel = ".Luitofp_hi_" + std::to_string(labelId);
    const std::string doneLabel = ".Luitofp_done_" + std::to_string(labelId);

    // TEST src, src — sets SF if bit 63 is set.
    builder.append(
        MInstr::make(MOpcode::TESTrr, std::vector<Operand>{emit.clone(src), emit.clone(src)}));
    // JS → high path (condition code: "s" is not in our table, but we can use
    // the sign flag via JCC with condition code for signed-less-than after TEST).
    // After TESTrr: SF = bit 63 of src.  "JS" = jump if SF=1.
    // Looking at conditionSuffix: code 2 = "l" (signed less), code 8 = "b" (unsigned below).
    // After TEST %r,%r: ZF and SF are set, CF=OF=0.
    // "l" (SF!=OF) → SF=1,OF=0 → true when bit 63 set. This is equivalent to JS.
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(2), makeLabelOperand(highLabel)}));

    // Small path: src in [0, 2^63), direct CVTSI2SD.
    builder.append(
        MInstr::make(MOpcode::CVTSI2SD, std::vector<Operand>{emit.clone(dest), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));

    // High path: src >= 2^63 (bit 63 set).
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(highLabel)}));

    // tmp = src >> 1 (logical shift right, halves the value)
    const VReg tmpReg = builder.makeTempVReg(RegClass::GPR);
    const Operand tmpOp = makeVRegOperand(tmpReg.cls, tmpReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVrr, std::vector<Operand>{emit.clone(tmpOp), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::SHRri, std::vector<Operand>{tmpOp, makeImmOperand(1)}));

    // lsb = src & 1 (preserve LSB for correct rounding)
    const VReg lsbReg = builder.makeTempVReg(RegClass::GPR);
    const Operand lsbOp = makeVRegOperand(lsbReg.cls, lsbReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVrr, std::vector<Operand>{emit.clone(lsbOp), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::ANDri, std::vector<Operand>{lsbOp, makeImmOperand(1)}));

    // tmp |= lsb (OR back the rounding bit)
    builder.append(MInstr::make(MOpcode::ORrr, std::vector<Operand>{tmpOp, emit.clone(lsbOp)}));

    // Convert halved value to double.
    builder.append(
        MInstr::make(MOpcode::CVTSI2SD, std::vector<Operand>{emit.clone(dest), emit.clone(tmpOp)}));

    // Double the result: dst += dst  (2-operand FADD: dest += src).
    builder.append(MInstr::make(MOpcode::FADD, std::vector<Operand>{dest, emit.clone(dest)}));

    // Done.
    builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
}

} // namespace viper::codegen::x64::lowering
