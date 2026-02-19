//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/VMTestHook.hpp
// Purpose: Provide privileged access to VM internals for unit tests.
// Key invariants: Mirror VM friend expectations; must stay in sync across tests.
// Ownership/Lifetime: Header-only utilities for unit test translation units.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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

    static State prepare(VM &vm, const il::core::Function &fn, const std::vector<Slot> &args)
    {
        return vm.prepareExecution(fn, args);
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

    static void setContext(
        VM &vm, Frame &fr, const il::core::BasicBlock *bb, size_t ip, const il::core::Instr &in)
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

    static std::size_t execDepth(const VM &vm)
    {
        return vm.execStack.size();
    }

#if VIPER_VM_OPCOUNTS
    static void setOpcodeCountsEnabled(VM &vm, bool enabled)
    {
        vm.enableOpcodeCounts = enabled;
    }
#endif

    /// @brief Access the pre-resolved operand cache for the current block.
    static const BlockExecCache *blockCache(const VM::ExecState &st)
    {
        return st.blockCache;
    }

    /// @brief Number of switch cache entries accumulated by the VM.
    /// @details A non-zero count after executing a switch confirms that the
    ///          VM-level cache was populated.  Equal counts across multiple
    ///          calls confirm the cache was reused rather than rebuilt.
    static size_t switchCacheSize(const VM &vm)
    {
        return vm.switchCache_.entries.size();
    }

    /// @brief True when the ExecState poll callback slot holds a non-null fn ptr.
    static bool hasPollFnPtr(const VM::ExecState &st)
    {
        return st.config.pollCallback != nullptr;
    }

    /// @brief Set the poll config on @p vm directly (bypasses VMAccess for test isolation).
    static void setPoll(VM &vm, uint32_t everyN, std::function<bool(VM &)> cb)
    {
        vm.pollEveryN_ = everyN;
        vm.pollCallback_ = std::move(cb);
    }
};
} // namespace il::vm
