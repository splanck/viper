//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Lowering.EH.cpp
// Purpose: x86-64 MIR lowering for residual EH markers and trap instructions.
//          Structured native EH is rewritten earlier by NativeEHLowering; marker
//          emitters here are inert fallbacks for any EH that slips through.
// Key invariants:
//   - Trap emitters call rt_trap which performs longjmp or aborts.
//   - EH marker emitters are no-ops when structured EH has already been lowered.
// Ownership/Lifetime:
//   - Operates on borrowed MIR builders; no persistent state.
// Links: codegen/x86_64/LoweringRules.hpp,
//        codegen/x86_64/Lowering.EmitCommon.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"
#include "MachineIR.hpp"

namespace viper::codegen::x64::lowering {

namespace {

MInstr makePlannedCall(Operand target, uint32_t callPlanId) {
    MInstr call = MInstr::make(MOpcode::CALL, std::vector<Operand>{std::move(target)});
    call.callPlanId = callPlanId;
    return call;
}

} // namespace

void emitEhPush(const ILInstr &, MIRBuilder &) {
    // NativeEHLowering should have rewritten eh.push before MIR lowering.
    // Keep the fallback emitter inert so stale marker instructions do not
    // invent duplicate machine-level EH state.
}

void emitEhPop(const ILInstr &, MIRBuilder &) {
    // NativeEHLowering should have rewritten eh.pop before MIR lowering.
}

void emitEhEntry(const ILInstr &, MIRBuilder &) {
    // Handler entry markers are erased by NativeEHLowering. Leave a no-op
    // fallback so residual markers remain harmless.
}

void emitTrap(const ILInstr &instr, MIRBuilder &builder) {
    // Emit a call to rt_trap(payload) which will longjmp to the thread-local
    // recovery point if one is set, or abort the process otherwise.

    CallLoweringPlan plan{};
    plan.callee = "rt_trap";
    plan.numNamedArgs = 1;

    CallArg trapArg{};
    trapArg.cls = CallArgClass::GPR;
    if (instr.ops.empty()) {
        trapArg.isImm = true;
        trapArg.imm = 0;
    } else {
        EmitCommon emit(builder);
        Operand payload = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
        if (!std::holds_alternative<OpReg>(payload) && !std::holds_alternative<OpImm>(payload)) {
            payload = emit.materialise(std::move(payload), RegClass::GPR);
        }
        if (const auto *reg = std::get_if<OpReg>(&payload)) {
            trapArg.vreg = reg->idOrPhys;
        } else if (const auto *imm = std::get_if<OpImm>(&payload)) {
            trapArg.isImm = true;
            trapArg.imm = imm->val;
        } else {
            trapArg.isImm = true;
            trapArg.imm = 0;
        }
    }
    plan.args.push_back(trapArg);

    const uint32_t callPlanId = builder.recordCallPlan(std::move(plan));
    builder.append(makePlannedCall(makeLabelOperand(std::string{"rt_trap"}), callPlanId));
    // rt_trap does not return (either longjmp or _Exit), but emit UD2 as a
    // safety net so the CPU faults if we somehow fall through.
    builder.append(MInstr::make(MOpcode::UD2));
}

} // namespace viper::codegen::x64::lowering
