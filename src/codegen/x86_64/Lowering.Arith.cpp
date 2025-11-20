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
/// @details Translates the opcode suffix into a condition code and emits the
///          appropriate floating-point compare using @ref EmitCommon::emitCmp.
/// @param instr IL floating-point compare instruction.
/// @param builder Machine IR builder receiving the emitted instructions.
void emitFCmp(const ILInstr &instr, MIRBuilder &builder)
{
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

} // namespace viper::codegen::x64::lowering
