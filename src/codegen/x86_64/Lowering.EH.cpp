//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EH.cpp
// Purpose: x86-64 MIR lowering for IL exception handling markers and trap
//          instructions.  eh.push/eh.pop are stubs (setjmp-based recovery is
//          handled at the runtime level); trap emits a call to rt_trap which
//          performs longjmp if a thread-local recovery point is set, or aborts.
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "MachineIR.hpp"

namespace viper::codegen::x64::lowering {

void emitEhPush(const ILInstr &, MIRBuilder &) {
    // eh.push registers a handler label for trap recovery.  In native codegen
    // the runtime's thread-local jmp_buf-based recovery (rt_trap_set_recovery /
    // rt_trap / longjmp) handles trap recovery at the C level.  Full setjmp-
    // based handler dispatch in generated code is not yet implemented.
}

void emitEhPop(const ILInstr &, MIRBuilder &) {
    // eh.pop clears the current handler.  The runtime clears recovery via
    // rt_trap_clear_recovery when appropriate.
}

void emitEhEntry(const ILInstr &, MIRBuilder &) {
    // Handler block entry — no-op.  The handler block is already materialised
    // as a MIR block with a label.
}

void emitTrap(const ILInstr &, MIRBuilder &builder) {
    // Emit a call to rt_trap(NULL) which will longjmp to the thread-local
    // recovery point if one is set, or abort the process otherwise.
    // The trap message (if any) is not forwarded in the current implementation;
    // rt_trap(NULL) emits the generic "Trap" diagnostic.
    builder.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap")}));
    // rt_trap does not return (either longjmp or _Exit), but emit UD2 as a
    // safety net so the CPU faults if we somehow fall through.
    builder.append(MInstr::make(MOpcode::UD2));
}

} // namespace viper::codegen::x64::lowering
