//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.Bitwise.cpp
// Purpose: Implement bitwise opcode lowering rules for the provisional IL
//          dialect.  The emitters rely on EmitCommon to manage register
//          materialisation and operand cloning.
// Key invariants: Emitters only trigger when operands are valid and ensure the
//                 resulting machine instructions operate on GPR registers.
// Ownership/Lifetime: Borrowed IL instructions and MIR builders; no ownership
//                     transfer occurs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Bitwise lowering rules for the x86-64 Machine IR pipeline.
/// @details Defines the lowering thunks that convert IL bitwise operators into
///          Machine IR instructions, delegating register management to
///          @ref viper::codegen::x64::EmitCommon while enforcing that only
///          general-purpose registers participate in the resulting instructions.

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

/// @brief Lower a bitwise AND IL instruction.
/// @details Emits an `AND` binary instruction when the IL result type maps to a
///          general-purpose register, folding immediates through
///          @ref EmitCommon::emitBinary.
/// @param instr IL bitwise AND instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitAnd(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::ANDrr, MOpcode::ANDri, cls, true);
    }
}

/// @brief Lower a bitwise OR IL instruction.
/// @details Restricts lowering to general-purpose registers and emits either the
///          register or immediate form of the OR instruction via
///          @ref EmitCommon::emitBinary.
/// @param instr IL bitwise OR instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitOr(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::ORrr, MOpcode::ORri, cls, true);
    }
}

/// @brief Lower a bitwise XOR IL instruction.
/// @details Emits XOR register or immediate forms when the result type maps to
///          a general-purpose register, using @ref EmitCommon::emitBinary to
///          keep operand handling consistent.
/// @param instr IL bitwise XOR instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitXor(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::XORrr, MOpcode::XORri, cls, true);
    }
}

/// @brief Lower a shift-left IL instruction.
/// @details Delegates to @ref EmitCommon::emitShift so the helper can choose
///          between immediate and RCX-based shift encodings.
/// @param instr IL shift-left instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SHLri, MOpcode::SHLrc);
}

/// @brief Lower a logical right-shift IL instruction.
/// @details Uses @ref EmitCommon::emitShift to emit either the immediate or
///          variable shift form corresponding to the SHR opcode family.
/// @param instr IL logical right-shift instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SHRri, MOpcode::SHRrc);
}

/// @brief Lower an arithmetic right-shift IL instruction.
/// @details Invokes @ref EmitCommon::emitShift with SAR opcodes so signed shifts
///          normalise their operand handling across immediate and register
///          counts.
/// @param instr IL arithmetic right-shift instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SARri, MOpcode::SARrc);
}

} // namespace viper::codegen::x64::lowering
