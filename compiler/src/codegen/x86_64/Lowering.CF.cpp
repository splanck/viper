//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.CF.cpp
// Purpose: Implement control-flow lowering rules for the provisional IL
//          dialect, covering branches, selects, and returns.
// Key invariants: Emitters rely on EmitCommon for operand preparation and obey
//                 the register classes dictated by MIRBuilder.
// Ownership/Lifetime: Works with borrowed MIRBuilder and IL instruction data.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Control-flow lowering hooks for the x86-64 backend.
/// @details Provides thin wrappers that forward branch, select, and return IL
///          instructions to @ref viper::codegen::x64::EmitCommon, ensuring a
///          consistent emission strategy regardless of the caller.

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

/// @brief Lower a SELECT IL instruction into Machine IR.
/// @details Delegates to @ref EmitCommon::emitSelect so the helper can implement
///          conditional move sequencing for both integer and floating-point
///          values.
/// @param instr IL select instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitSelect(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitSelect(instr);
}

/// @brief Lower an unconditional branch IL instruction.
/// @details Calls @ref EmitCommon::emitBranch to append a JMP to the target
///          label extracted from the IL operand list.
/// @param instr IL branch instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitBranch(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitBranch(instr);
}

/// @brief Lower a conditional branch IL instruction.
/// @details Uses @ref EmitCommon::emitCondBranch to build the TEST/JCC/JMP
///          sequence that mirrors IL conditional control flow.
/// @param instr IL conditional branch instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitCondBranch(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitCondBranch(instr);
}

/// @brief Lower a RETURN IL instruction.
/// @details Forwards to @ref EmitCommon::emitReturn so ABI-specific register
///          conventions and optional return values are handled uniformly.
/// @param instr IL return instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitReturn(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitReturn(instr);
}

} // namespace viper::codegen::x64::lowering
