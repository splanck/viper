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
// Key invariants: Emitters rely on EmitCommon for operand preparation, preserve
//                 ABI-mandated register classes, and never emit instructions
//                 when operand requirements are unmet.
// Ownership/Lifetime: Operates purely on borrowed MIRBuilder state and records
//                     call metadata for later passes without taking ownership of
//                     IR nodes.
// Links: docs/codemap.md, docs/architecture.md#codegen
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

/// @brief Lower an IL call instruction into the backend call plan.
/// @details Builds a @ref CallLoweringPlan by classifying the callee operand,
///          materialising argument descriptors, and reserving result vregs when
///          present.  The completed plan is recorded on the @p builder so that
///          later lowering phases can emit ABI-conforming prologues and
///          epilogues.  Finally, a placeholder CALL is appended to the Machine
///          IR so scheduling and register allocation see the pending call.
/// @param instr High-level IL instruction describing the call.
/// @param builder MIR construction context that owns register state.
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

/// @brief Lower an automatic storage load instruction.
/// @details Delegates to @ref EmitCommon::emitLoad so that addressing modes and
///          register class selection stay consistent with the rest of the
///          backend.  The helper ensures the result vreg is allocated in the
///          correct class for the instruction's result kind.
/// @param instr IL instruction representing the load.
/// @param builder MIR construction context.
void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitLoad(instr, builder.regClassFor(instr.resultKind));
}

/// @brief Lower a store instruction targeting automatic storage.
/// @details Invokes @ref EmitCommon::emitStore to synthesise the necessary
///          Machine IR operations.  Using the shared helper keeps store
///          semantics aligned with other lowering paths and guarantees
///          consistent operand validation.
/// @param instr IL store instruction.
/// @param builder MIR construction context.
void emitStore(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitStore(instr);
}

} // namespace viper::codegen::x64::lowering

