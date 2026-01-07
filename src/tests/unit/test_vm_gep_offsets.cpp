//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_gep_offsets.cpp
// Purpose: Validate VM GEP correctly applies positive and negative byte offsets.
// Key invariants: Pointer arithmetic advances forward for positive offsets and
// Ownership/Lifetime: Standalone unit test executable exercising VM memory ops.
// Links: docs/il-guide.md#reference
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
#include <cstddef>
#include <cstdint>
#include <optional>

namespace
{

il::core::Module makeModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(64));
    alloca.loc = {1, 1, 1};
    entry.instructions.push_back(alloca);

    Instr forward;
    forward.result = 1U;
    forward.op = Opcode::GEP;
    forward.type = Type(Type::Kind::Ptr);
    forward.operands.push_back(Value::temp(0));
    forward.operands.push_back(Value::constInt(24));
    forward.loc = {1, 2, 1};
    entry.instructions.push_back(forward);

    Instr backward;
    backward.result = 2U;
    backward.op = Opcode::GEP;
    backward.type = Type(Type::Kind::Ptr);
    backward.operands.push_back(Value::temp(1));
    backward.operands.push_back(Value::constInt(-16));
    backward.loc = {1, 3, 1};
    entry.instructions.push_back(backward);

    Instr rewind;
    rewind.result = 3U;
    rewind.op = Opcode::GEP;
    rewind.type = Type(Type::Kind::Ptr);
    rewind.operands.push_back(Value::temp(2));
    rewind.operands.push_back(Value::constInt(-8));
    rewind.loc = {1, 4, 1};
    entry.instructions.push_back(rewind);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::I64);
    ret.operands.push_back(Value::constInt(0));
    ret.loc = {1, 5, 1};
    entry.instructions.push_back(ret);

    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(4);
    m.functions.push_back(std::move(fn));
    return m;
}

} // namespace

int main()
{
    il::core::Module module = makeModule();
    il::vm::VM vm(module);

    const auto &fn = module.functions.front();
    il::vm::VMTestHook::State state = il::vm::VMTestHook::prepare(vm, fn);

    auto stepExpectRunning = [&](il::vm::VM &machine, il::vm::VMTestHook::State &exec)
    {
        std::optional<il::vm::Slot> slot = il::vm::VMTestHook::step(machine, exec);
        assert(!slot.has_value());
    };

    stepExpectRunning(vm, state);
    auto *basePtr = static_cast<std::uint8_t *>(state.fr.regs[0].ptr);

    stepExpectRunning(vm, state);
    auto *forwardPtr = static_cast<std::uint8_t *>(state.fr.regs[1].ptr);
    std::ptrdiff_t forwardDelta = forwardPtr - basePtr;
    assert(forwardDelta == 24);

    stepExpectRunning(vm, state);
    auto *backPtr = static_cast<std::uint8_t *>(state.fr.regs[2].ptr);
    std::ptrdiff_t backwardDelta = backPtr - forwardPtr;
    assert(backwardDelta == -16);
    std::ptrdiff_t midDelta = backPtr - basePtr;
    assert(midDelta == 8);

    stepExpectRunning(vm, state);
    auto *rewindPtr = static_cast<std::uint8_t *>(state.fr.regs[3].ptr);
    assert(rewindPtr == basePtr);

    std::optional<il::vm::Slot> exit = il::vm::VMTestHook::step(vm, state);
    assert(exit.has_value());

    return 0;
}
