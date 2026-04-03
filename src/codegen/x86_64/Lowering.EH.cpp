//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EH.cpp
// Purpose: x86-64 MIR lowering for residual EH markers and trap instructions.
//          Structured native EH is rewritten earlier by NativeEHLowering; the
//          marker emitters here exist only as inert fallbacks if raw EH slips
//          past the shared rewrite. trap emits a call to rt_trap which performs
//          longjmp if a thread-local recovery point is set, or aborts.
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "MachineIR.hpp"

namespace viper::codegen::x64::lowering {

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

void emitTrap(const ILInstr &, MIRBuilder &builder) {
    // Emit a call to rt_trap(NULL) which will longjmp to the thread-local
    // recovery point if one is set, or abort the process otherwise.
    // The trap message (if any) is not forwarded in the current implementation;
    // rt_trap(NULL) emits the generic "Trap" diagnostic.

    // Record a call plan so lowerPendingCalls matches this CALL correctly.
    // Without a plan, the sequential plan-to-CALL matching drifts and
    // subsequent calls receive the wrong argument setup.
    CallLoweringPlan plan{};
    plan.calleeLabel = "rt_trap";
    // Pass NULL as the message argument.
    CallArg nullArg{};
    nullArg.kind = CallArg::GPR;
    nullArg.isImm = true;
    nullArg.imm = 0;
    plan.args.push_back(nullArg);
    builder.recordCallPlan(std::move(plan));

    builder.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap")}));
    // rt_trap does not return (either longjmp or _Exit), but emit UD2 as a
    // safety net so the CPU faults if we somehow fall through.
    builder.append(MInstr::make(MOpcode::UD2));
}

} // namespace viper::codegen::x64::lowering
