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
#include "MachineIR.hpp"

#include <string>

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

/// @brief Lower an idx_chk instruction (bounds check with trap on out-of-bounds).
/// @details Emits inline CMP + JCC + UD2 sequences using in-block LABEL definitions
///          to conditionally trap when the index is outside [lower, upper).
///          The check verifies: lower <= index < upper (unsigned comparison).
///          The result is the index value passed through if the check succeeds.
/// @param instr IL idx_chk instruction: ops[0]=index, ops[1]=lower, ops[2]=upper.
/// @param builder Machine IR builder receiving the emitted code.
void emitIdxChk(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0 || instr.ops.size() < 3)
    {
        return;
    }

    EmitCommon emit(builder);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    // Materialise the index into a GPR
    Operand index = emit.materialiseGpr(builder.makeOperandForValue(instr.ops[0], RegClass::GPR));

    // Copy index to result first (pass-through value)
    builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{dest, index}));

    // Generate unique labels for the skip points
    static uint32_t idxChkCounter = 0;
    const std::string passUpperLabel = ".Lidxchk_u_" + std::to_string(idxChkCounter);
    const std::string passLowerLabel = ".Lidxchk_l_" + std::to_string(idxChkCounter);
    ++idxChkCounter;

    // Check upper bound: if index < upper (unsigned below), skip trap
    Operand upper = builder.makeOperandForValue(instr.ops[2], RegClass::GPR);
    if (std::holds_alternative<OpImm>(upper))
    {
        builder.append(MInstr::make(MOpcode::CMPri, std::vector<Operand>{index, upper}));
    }
    else
    {
        upper = emit.materialiseGpr(std::move(upper));
        builder.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{index, upper}));
    }
    // JCC "b" (below / unsigned less than) = condition code 8
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(8), makeLabelOperand(passUpperLabel)}));
    builder.append(MInstr::make(MOpcode::UD2));
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(passUpperLabel)}));

    // Check lower bound: if index >= lower (unsigned above or equal), skip trap
    Operand lower = builder.makeOperandForValue(instr.ops[1], RegClass::GPR);
    if (std::holds_alternative<OpImm>(lower))
    {
        builder.append(MInstr::make(MOpcode::CMPri, std::vector<Operand>{index, lower}));
    }
    else
    {
        lower = emit.materialiseGpr(std::move(lower));
        builder.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{index, lower}));
    }
    // JCC "ae" (above or equal / unsigned >= ) = condition code 7
    builder.append(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(7), makeLabelOperand(passLowerLabel)}));
    builder.append(MInstr::make(MOpcode::UD2));
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(passLowerLabel)}));
}

/// @brief Lower a switch_i32 instruction (multi-way branch).
/// @details Emits a chain of CMP + JCC pairs, one per case, followed by a JMP to the
///          default label. The operands are: ops[0]=scrutinee, then (value, label) pairs,
///          then an optional default label as the final operand.
/// @param instr IL switch_i32 instruction with variable-length operands.
/// @param builder Machine IR builder receiving the emitted code.
void emitSwitchI32(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }

    EmitCommon emit(builder);
    Operand scrutinee =
        emit.materialiseGpr(builder.makeOperandForValue(instr.ops[0], RegClass::GPR));

    // Process case (value, label) pairs starting at ops[1]
    std::size_t idx = 1;
    while (idx + 1 < instr.ops.size())
    {
        // If this operand is a label, it's the default — stop processing cases
        if (instr.ops[idx].kind == ILValue::Kind::LABEL)
        {
            break;
        }

        // Case value
        Operand caseVal = builder.makeOperandForValue(instr.ops[idx], RegClass::GPR);
        // Case label
        const Operand caseLabel = builder.makeLabelOperand(instr.ops[idx + 1]);

        // CMP scrutinee, case_value
        if (std::holds_alternative<OpImm>(caseVal))
        {
            builder.append(MInstr::make(MOpcode::CMPri, std::vector<Operand>{scrutinee, caseVal}));
        }
        else
        {
            caseVal = emit.materialiseGpr(std::move(caseVal));
            builder.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{scrutinee, caseVal}));
        }

        // JCC "e" (equal) = condition code 0
        builder.append(
            MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), caseLabel}));

        idx += 2;
    }

    // Default label (the remaining operand)
    if (idx < instr.ops.size())
    {
        const Operand defLabel = builder.makeLabelOperand(instr.ops[idx]);
        builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{defLabel}));
    }
}

} // namespace viper::codegen::x64::lowering
