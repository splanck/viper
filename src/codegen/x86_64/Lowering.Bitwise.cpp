//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

void emitAnd(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::ANDrr, MOpcode::ANDri, cls, true);
    }
}

void emitOr(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::ORrr, MOpcode::ORri, cls, true);
    }
}

void emitXor(const ILInstr &instr, MIRBuilder &builder)
{
    const RegClass cls = builder.regClassFor(instr.resultKind);
    if (cls == RegClass::GPR)
    {
        EmitCommon(builder).emitBinary(instr, MOpcode::XORrr, MOpcode::XORri, cls, true);
    }
}

void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SHLri, MOpcode::SHLrc);
}

void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SHRri, MOpcode::SHRrc);
}

void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitShift(instr, MOpcode::SARri, MOpcode::SARrc);
}

} // namespace viper::codegen::x64::lowering

