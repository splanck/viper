//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.Arith.cpp
// Purpose: Implement arithmetic opcode lowering rules for the provisional IL
//          dialect.  Arithmetic emitters delegate common mechanics to
//          EmitCommon, keeping each rule focused on opcode selection.
// Key invariants: All emitters honour the register classes reported by the
//                 MIRBuilder and never emit instructions when operands are
//                 malformed.
// Ownership/Lifetime: Operates on borrowed IL instructions and MIR builders.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Arithmetic lowering rules for the x86-64 backend.
/// @details Translates IL arithmetic instructions into Machine IR by
///          orchestrating @ref viper::codegen::x64::EmitCommon helpers.  Each
///          rule chooses opcode variants that match the operand register class
///          and ensures integer immediates and floating-point operations are
///          handled consistently.

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

/// @brief Lower an integer or floating-point add IL instruction.
/// @details Selects MOV/ADD forms based on the destination register class and
///          delegates operand handling to @ref EmitCommon::emitBinary so immediates
///          can be folded when possible.
/// @param instr IL add instruction to lower.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitAdd(const ILInstr &instr, MIRBuilder &builder)
{
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
void emitSub(const ILInstr &instr, MIRBuilder &builder)
{
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
void emitMul(const ILInstr &instr, MIRBuilder &builder)
{
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
void emitFDiv(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitBinary(instr, MOpcode::FDIV, MOpcode::FDIV, RegClass::XMM, false);
}

/// @brief Lower an integer compare IL instruction.
/// @details Uses @ref EmitCommon::icmpConditionCode to resolve the condition
///          code and @ref EmitCommon::emitCmp to produce the Machine IR compare
///          sequence.
/// @param instr IL integer compare instruction.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitICmp(const ILInstr &instr, MIRBuilder &builder)
{
    if (const auto cond = EmitCommon::icmpConditionCode(instr.opcode))
    {
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
void emitFCmp(const ILInstr &instr, MIRBuilder &builder)
{
    if (!instr.opcode.starts_with("fcmp_"))
    {
        return;
    }

    const std::string_view suffix(instr.opcode.data() + 5, instr.opcode.size() - 5);

    // eq/ne/lt/le require NaN-safe multi-instruction sequences.
    if (suffix == "eq" || suffix == "ne" || suffix == "lt" || suffix == "le")
    {
        EmitCommon(builder).emitFCmpNanSafe(instr, suffix);
        return;
    }

    // gt/ge/ord/uno are already NaN-correct with a single SETcc.
    if (const auto cond = EmitCommon::fcmpConditionCode(instr.opcode))
    {
        EmitCommon(builder).emitCmp(instr, RegClass::XMM, *cond);
    }
}

/// @brief Lower an explicit compare IL instruction that encodes the result type.
/// @details Determines the register class using either the result or first
///          operand kind, then emits a compare that materialises the condition
///          into the destination virtual register.
/// @param instr IL compare instruction with explicit operands.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitCmpExplicit(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    const RegClass cls =
        builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind);
    emit.emitCmp(instr, cls, 1);
}

/// @brief Lower an overflow-checked integer add.
/// @details Emits the ADDOvfrr pseudo-op which the post-lowering pass will
///          expand into ADD + JO (trap on overflow).
void emitAddOvf(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    emit.emitBinary(instr, MOpcode::ADDOvfrr, MOpcode::ADDOvfrr, RegClass::GPR, false);
}

/// @brief Lower an overflow-checked integer subtract.
/// @details Emits the SUBOvfrr pseudo-op.
void emitSubOvf(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    emit.emitBinary(instr, MOpcode::SUBOvfrr, MOpcode::SUBOvfrr, RegClass::GPR, false);
}

/// @brief Lower an overflow-checked integer multiply.
/// @details Emits the IMULOvfrr pseudo-op.
void emitMulOvf(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    emit.emitBinary(instr, MOpcode::IMULOvfrr, MOpcode::IMULOvfrr, RegClass::GPR, false);
}

void emitDivFamily(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitDivRem(instr, instr.opcode);
}

void emitZSTrunc(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    emit.emitCast(
        instr,
        MOpcode::MOVrr,
        builder.regClassFor(instr.resultKind),
        builder.regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
}

void emitSIToFP(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitCast(instr, MOpcode::CVTSI2SD, RegClass::XMM, RegClass::GPR);
}

void emitFPToSI(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitCast(instr, MOpcode::CVTTSD2SI, RegClass::GPR, RegClass::XMM);
}

/// @brief Lower an IL `fptoui` instruction (floating-point to unsigned int, checked).
/// @details Handles the full unsigned 64-bit output range [0, 2^64).
///
///          x86-64 has no native float-to-unsigned-int64 instruction, so we use a
///          two-path approach:
///          1. If the input is < 2^63 as a float, CVTTSD2SI produces the correct
///             result directly (the signed and unsigned representations coincide).
///          2. If the input is >= 2^63, subtract 2^63.0, convert the remainder with
///             CVTTSD2SI, then set bit 63 in the integer result.
///          A NaN or negative input traps via UD2 (checked variant).
///
///          Generated sequence:
///            ucomisd %src, %src            ; NaN check (PF=1 if NaN)
///            jp      .Ltrap               ; trap on NaN
///            xorpd   %zero, %zero         ; materialise 0.0
///            ucomisd %src, %zero
///            jb      .Ltrap               ; trap on negative
///            movsd   .LC_2pow63, %limit   ; load 2^63 as double
///            ucomisd %src, %limit
///            jb      .Lsmall              ; < 2^63 → direct path
///            subsd   %limit, %src2        ; src - 2^63
///            cvttsd2si %src2, %dst
///            btcq    $63, %dst            ; set bit 63
///            jmp     .Ldone
///          .Lsmall:
///            cvttsd2si %src, %dst
///          .Ldone:
void emitFpToUi(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    EmitCommon emit(builder);
    const Operand src = emit.materialise(
        builder.makeOperandForValue(instr.ops[0], RegClass::XMM), RegClass::XMM);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string trapLabel = ".Lfptoui_trap_" + std::to_string(labelId);
    const std::string smallLabel = ".Lfptoui_sm_" + std::to_string(labelId);
    const std::string doneLabel = ".Lfptoui_done_" + std::to_string(labelId);

    // NaN check: ucomisd %src, %src — PF=1 if NaN.
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(src), emit.clone(src)}));
    // JP → trap (condition code 10 = "p" / parity)
    builder.append(
        MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(10),
                                                         makeLabelOperand(trapLabel)}));

    // Negative check: compare with 0.0.
    // Materialise 0.0 by zeroing a GPR and transferring to XMM.
    const VReg zeroGpr = builder.makeTempVReg(RegClass::GPR);
    const Operand zeroGprOp = makeVRegOperand(zeroGpr.cls, zeroGpr.id);
    builder.append(MInstr::make(MOpcode::MOVri,
                                std::vector<Operand>{emit.clone(zeroGprOp), makeImmOperand(0)}));
    const VReg zeroReg = builder.makeTempVReg(RegClass::XMM);
    const Operand zeroOp = makeVRegOperand(zeroReg.cls, zeroReg.id);
    builder.append(MInstr::make(MOpcode::MOVQrx,
                                std::vector<Operand>{emit.clone(zeroOp), emit.clone(zeroGprOp)}));
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(src), emit.clone(zeroOp)}));
    // JB → trap (condition code 8 = "b" / below: src < 0.0)
    builder.append(
        MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(8),
                                                         makeLabelOperand(trapLabel)}));

    // Load 2^63 as double constant (9223372036854775808.0 = 0x43E0000000000000).
    const VReg limitReg = builder.makeTempVReg(RegClass::XMM);
    const Operand limitOp = makeVRegOperand(limitReg.cls, limitReg.id);
    const VReg gprTmp = builder.makeTempVReg(RegClass::GPR);
    const Operand gprOp = makeVRegOperand(gprTmp.cls, gprTmp.id);
    // Move 0x43E0000000000000 to GPR then to XMM via bit transfer.
    builder.append(MInstr::make(MOpcode::MOVri,
                                std::vector<Operand>{emit.clone(gprOp),
                                                     makeImmOperand(0x43E0000000000000LL)}));
    builder.append(MInstr::make(MOpcode::MOVQrx,
                                std::vector<Operand>{emit.clone(limitOp), emit.clone(gprOp)}));

    // Compare src with 2^63.
    builder.append(
        MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{emit.clone(src), emit.clone(limitOp)}));
    // JB → small path (src < 2^63, direct conversion is safe)
    builder.append(
        MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(8),
                                                         makeLabelOperand(smallLabel)}));

    // Large path: src >= 2^63.  Subtract 2^63, convert, then set bit 63.
    // adj = src; adj -= limit  (2-operand FSUB: dest -= src)
    const VReg adjReg = builder.makeTempVReg(RegClass::XMM);
    const Operand adjOp = makeVRegOperand(adjReg.cls, adjReg.id);
    builder.append(MInstr::make(MOpcode::MOVSDrr,
                                std::vector<Operand>{emit.clone(adjOp), emit.clone(src)}));
    builder.append(MInstr::make(MOpcode::FSUB,
                                std::vector<Operand>{adjOp, emit.clone(limitOp)}));
    builder.append(
        MInstr::make(MOpcode::CVTTSD2SI, std::vector<Operand>{emit.clone(dest), emit.clone(adjOp)}));
    // Set bit 63: OR with 0x8000000000000000.
    const VReg highBitReg = builder.makeTempVReg(RegClass::GPR);
    const Operand highBitOp = makeVRegOperand(highBitReg.cls, highBitReg.id);
    builder.append(MInstr::make(MOpcode::MOVri,
                                std::vector<Operand>{highBitOp,
                                                     makeImmOperand(
                                                         static_cast<int64_t>(0x8000000000000000ULL))}));
    builder.append(MInstr::make(MOpcode::ORrr, std::vector<Operand>{dest, highBitOp}));
    builder.append(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));

    // Trap block: UD2.
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(trapLabel)}));
    builder.append(MInstr::make(MOpcode::UD2));

    // Small path: src in [0, 2^63), direct CVTTSD2SI.
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(smallLabel)}));
    builder.append(
        MInstr::make(MOpcode::CVTTSD2SI, std::vector<Operand>{dest, emit.clone(src)}));

    // Done.
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
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
void emitUiToFp(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    EmitCommon emit(builder);
    const Operand src = emit.materialiseGpr(
        builder.makeOperandForValue(instr.ops[0], RegClass::GPR));
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
    builder.append(
        MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(2),
                                                         makeLabelOperand(highLabel)}));

    // Small path: src in [0, 2^63), direct CVTSI2SD.
    builder.append(
        MInstr::make(MOpcode::CVTSI2SD, std::vector<Operand>{emit.clone(dest), emit.clone(src)}));
    builder.append(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(doneLabel)}));

    // High path: src >= 2^63 (bit 63 set).
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(highLabel)}));

    // tmp = src >> 1 (logical shift right, halves the value)
    const VReg tmpReg = builder.makeTempVReg(RegClass::GPR);
    const Operand tmpOp = makeVRegOperand(tmpReg.cls, tmpReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVrr, std::vector<Operand>{emit.clone(tmpOp), emit.clone(src)}));
    builder.append(
        MInstr::make(MOpcode::SHRri, std::vector<Operand>{tmpOp, makeImmOperand(1)}));

    // lsb = src & 1 (preserve LSB for correct rounding)
    const VReg lsbReg = builder.makeTempVReg(RegClass::GPR);
    const Operand lsbOp = makeVRegOperand(lsbReg.cls, lsbReg.id);
    builder.append(
        MInstr::make(MOpcode::MOVrr, std::vector<Operand>{emit.clone(lsbOp), emit.clone(src)}));
    builder.append(
        MInstr::make(MOpcode::ANDri, std::vector<Operand>{lsbOp, makeImmOperand(1)}));

    // tmp |= lsb (OR back the rounding bit)
    builder.append(
        MInstr::make(MOpcode::ORrr, std::vector<Operand>{tmpOp, emit.clone(lsbOp)}));

    // Convert halved value to double.
    builder.append(
        MInstr::make(MOpcode::CVTSI2SD, std::vector<Operand>{emit.clone(dest), emit.clone(tmpOp)}));

    // Double the result: dst += dst  (2-operand FADD: dest += src).
    builder.append(
        MInstr::make(MOpcode::FADD, std::vector<Operand>{dest, emit.clone(dest)}));

    // Done.
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
}

} // namespace viper::codegen::x64::lowering
