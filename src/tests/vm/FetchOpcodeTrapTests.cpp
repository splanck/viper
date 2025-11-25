//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/FetchOpcodeTrapTests.cpp
// Purpose: Verify fetchOpcode reports the trap opcode after exhausting a block.
// Key invariants: Once a block has no remaining instructions the VM must clear the
// Ownership/Lifetime: Constructs a temporary VM and execution state using
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/TestIRBuilder.hpp"
#include "unit/VMTestHook.hpp"
#include "vm/VMContext.hpp"

#include <cassert>

using namespace il::core;

TEST_WITH_IL(il, {
    il.retVoid(il.loc());

    il::vm::VM vm(il.module());
    il::vm::ActiveVMGuard guard(&vm);
    auto state = il::vm::VMTestHook::prepare(vm, il.function());

    assert(state.bb != nullptr && "execution state must reference the entry block");
    state.ip = state.bb->instructions.size();

    il::vm::VMContext context(vm);
    const Opcode opcode = context.fetchOpcode(state);
    assert(opcode == Opcode::Trap && "fetchOpcode should report trap after block exhaustion");
    assert(state.currentInstr == nullptr &&
           "selectInstruction must clear currentInstr when halting before dispatch");
});
