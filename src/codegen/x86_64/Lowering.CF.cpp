//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

void emitSelect(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitSelect(instr);
}

void emitBranch(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitBranch(instr);
}

void emitCondBranch(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitCondBranch(instr);
}

void emitReturn(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitReturn(instr);
}

} // namespace viper::codegen::x64::lowering

