//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.Mem.cpp
// Purpose: Implement memory-oriented opcode lowering rules for the provisional
//          IL dialect, including loads, stores, and call sequencing.
// Key invariants: Emitters rely on EmitCommon for operand preparation and never
//                 emit instructions when operand requirements are unmet.
// Ownership/Lifetime: Operates purely on borrowed MIRBuilder state.
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "CallLowering.hpp"
#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"

#include <utility>
#include <vector>

namespace viper::codegen::x64::lowering
{

void emitCall(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }

    CallLoweringPlan plan{};
    plan.calleeLabel = instr.ops.front().label;

    for (std::size_t idx = 1; idx < instr.ops.size(); ++idx)
    {
        const auto &argVal = instr.ops[idx];
        CallArg arg{};
        arg.kind = builder.regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

        if (builder.isImmediate(argVal))
        {
            arg.isImm = true;
            arg.imm = argVal.i64;
        }
        else
        {
            const Operand operand = builder.makeOperandForValue(argVal, builder.regClassFor(argVal.kind));
            if (const auto *reg = std::get_if<OpReg>(&operand))
            {
                arg.vreg = reg->idOrPhys;
            }
            else if (const auto *imm = std::get_if<OpImm>(&operand))
            {
                arg.isImm = true;
                arg.imm = imm->val;
            }
        }

        plan.args.push_back(arg);
    }

    if (instr.resultId >= 0)
    {
        (void)builder.ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    builder.recordCallPlan(std::move(plan));
    builder.append(MInstr::make(MOpcode::CALL,
                                std::vector<Operand>{builder.makeLabelOperand(instr.ops[0])}));
}

void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitLoad(instr, builder.regClassFor(instr.resultKind));
}

void emitStore(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitStore(instr);
}

} // namespace viper::codegen::x64::lowering

