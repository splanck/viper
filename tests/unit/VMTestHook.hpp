// File: tests/unit/VMTestHook.hpp
// Purpose: Provide privileged access to VM internals for unit tests.
// Key invariants: Mirror VM friend expectations; must stay in sync across tests.
// Ownership: Header-only utilities for unit test translation units.
// Links: docs/codemap.md

#pragma once

#include "il/core/Function.hpp"
#include "vm/VM.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::vm
{
/// @brief Grant unit tests controlled access to VM private helpers.
struct VMTestHook
{
    using State = VM::ExecState;
    using TrapSignal = VM::TrapDispatchSignal;

    static State prepare(VM &vm, const il::core::Function &fn)
    {
        return vm.prepareExecution(fn, {});
    }

    static State clone(const State &st)
    {
        return st;
    }

    static std::optional<Slot> step(VM &vm, State &st)
    {
        return vm.stepOnce(st);
    }

    static TrapSignal makeTrap(State &st)
    {
        return TrapSignal(&st);
    }

    static bool handleTrap(VM &vm, const TrapSignal &signal, State &st)
    {
        return vm.handleTrapDispatch(signal, st);
    }

    static void setContext(VM &vm,
                           Frame &fr,
                           const il::core::BasicBlock *bb,
                           size_t ip,
                           const il::core::Instr &in)
    {
        vm.setCurrentContext(fr, bb, ip, in);
    }

    static bool hasInstruction(const VM &vm)
    {
        return vm.currentContext.hasInstruction;
    }

    static Slot run(VM &vm, const il::core::Function &fn, const std::vector<Slot> &args)
    {
        return vm.execFunction(fn, args);
    }

    static size_t literalCacheSize(const VM &vm)
    {
        return vm.inlineLiteralCache.size();
    }

    static rt_string literalCacheLookup(const VM &vm, const std::string &literal)
    {
        auto it = vm.inlineLiteralCache.find(literal);
        if (it == vm.inlineLiteralCache.end())
            return nullptr;
        return it->second;
    }

    static RuntimeCallContext &runtimeContext(VM &vm)
    {
        return vm.runtimeContext;
    }

    static const RuntimeCallContext &runtimeContext(const VM &vm)
    {
        return vm.runtimeContext;
    }
};
} // namespace il::vm

