//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_run_loop_helpers.cpp
// Purpose: Validate VM run loop helper behaviour for debug pauses and trap dispatch. 
// Key invariants: stepOnce honours breakpoints and trap dispatch clears context.
// Ownership/Lifetime: Test constructs IL module and runs helper wrappers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include "VMTestHook.hpp"

#include <cassert>
#include <optional>

int main()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(7));
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(bb);
    m.functions.push_back(fn);

    auto &mainFn = m.functions.front();

    il::vm::DebugCtrl dbg;
    auto sym = dbg.internLabel("entry");
    dbg.addBreak(sym);

    il::vm::VM vm(m, {}, 0, dbg);

    il::vm::VMTestHook::State state = il::vm::VMTestHook::prepare(vm, mainFn);

    auto pause = il::vm::VMTestHook::step(vm, state);
    assert(pause.has_value());
    assert(pause->i64 == 10);

    state.skipBreakOnce = true;
    auto result = il::vm::VMTestHook::step(vm, state);
    assert(result.has_value());
    assert(result->i64 == 7);

    const auto &instr = mainFn.blocks.front().instructions.front();
    il::vm::VMTestHook::setContext(vm, state.fr, state.bb, state.ip, instr);
    auto targeted = il::vm::VMTestHook::makeTrap(state);
    bool handled = il::vm::VMTestHook::handleTrap(vm, targeted, state);
    assert(handled);
    assert(!il::vm::VMTestHook::hasInstruction(vm));

    il::vm::VMTestHook::setContext(vm, state.fr, state.bb, state.ip, instr);
    auto other = il::vm::VMTestHook::clone(state);
    auto otherSignal = il::vm::VMTestHook::makeTrap(other);
    handled = il::vm::VMTestHook::handleTrap(vm, otherSignal, state);
    assert(!handled);
    assert(il::vm::VMTestHook::hasInstruction(vm));

    return 0;
}
