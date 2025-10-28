//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

namespace viper::codegen::x64::lowering
{

void emitAdd(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::ADDrr : MOpcode::FADD;
    const MOpcode opRI = cls == RegClass::GPR ? MOpcode::ADDri : opRR;
    emit.emitBinary(instr, opRR, opRI, cls, cls == RegClass::GPR);
}

void emitSub(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::SUBrr : MOpcode::FSUB;
    emit.emitBinary(instr, opRR, opRR, cls, false);
}

void emitMul(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon emit(builder);
    const RegClass cls = builder.regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::IMULrr : MOpcode::FMUL;
    emit.emitBinary(instr, opRR, opRR, cls, false);
}

void emitFDiv(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitBinary(instr, MOpcode::FDIV, MOpcode::FDIV, RegClass::XMM, false);
}

void emitICmp(const ILInstr &instr, MIRBuilder &builder)
{
    if (const auto cond = EmitCommon::icmpConditionCode(instr.opcode))
    {
        EmitCommon(builder).emitCmp(instr, RegClass::GPR, *cond);
    }
}

void emitFCmp(const ILInstr &instr, MIRBuilder &builder)
{
    if (const auto cond = EmitCommon::fcmpConditionCode(instr.opcode))
    {
        EmitCommon(builder).emitCmp(instr, RegClass::XMM, *cond);
    }
}

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
    emit.emitCast(instr,
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

