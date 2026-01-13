//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlerAccess.hpp
// Purpose: Expose controlled accessors for VM internals to opcode handler code.
// Key invariants: Grants read/write access only to members required for handler semantics.
// Ownership/Lifetime: Accessors operate on VM-owned state without transferring ownership.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Function.hpp"
#include "vm/VM.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace il::vm::detail
{
struct VMAccess
{
    using ExecState = VM::ExecState;

    static inline ExecState *currentExecState(VM &vm)
    {
        return vm.execStack.empty() ? nullptr : vm.execStack.back();
    }

    static inline Slot eval(VM &vm, Frame &fr, const il::core::Value &value)
    {
        return vm.eval(fr, value);
    }

    static inline DebugCtrl &debug(VM &vm)
    {
        return vm.debug;
    }

    /// @brief Fast-path check for active memory watches.
    /// @return True when memory watches are installed.
    static inline bool hasMemWatchesActive(const VM &vm) noexcept
    {
        return vm.memWatchActive_;
    }

    /// @brief Fast-path check for active variable watches.
    /// @return True when variable watches are installed.
    static inline bool hasVarWatchesActive(const VM &vm) noexcept
    {
        return vm.varWatchActive_;
    }

    static inline const VM::FnMap &functionMap(const VM &vm)
    {
        return vm.fnMap;
    }

    static inline RuntimeCallContext &runtimeContext(VM &vm)
    {
        return vm.runtimeContext;
    }

    static inline Slot callFunction(VM &vm,
                                    const il::core::Function &fn,
                                    std::span<const Slot> args)
    {
        return vm.execFunction(fn, args);
    }

    // Stepping helpers for components that need controlled access -------------
    static inline ExecState prepare(VM &vm,
                                    const il::core::Function &fn,
                                    std::span<const Slot> args)
    {
        return vm.prepareExecution(fn, args);
    }

    static inline std::optional<Slot> stepOnce(VM &vm, ExecState &st)
    {
        return vm.stepOnce(st);
    }

    static inline void setMaxSteps(VM &vm, uint64_t max)
    {
        vm.maxSteps = max;
    }

    static inline void setPollConfig(VM &vm, uint32_t everyN, std::function<bool(VM &)> cb)
    {
        vm.pollEveryN_ = everyN;
        vm.pollCallback_ = std::move(cb);
    }

    /// @brief Access the last trap state for diagnostic reporting.
    static inline const VM::TrapState &lastTrapState(const VM &vm)
    {
        return vm.lastTrap;
    }

    /// @brief Refresh debug fast-path flags after configuration changes.
    static inline void refreshDebugFlags(VM &vm)
    {
        vm.refreshDebugFlags();
    }

    /// @brief Access the precomputed register count cache.
    /// @details Used by TCO to reuse cached maxSsaId values instead of rescanning.
    static inline std::unordered_map<const il::core::Function *, size_t> &regCountCache(VM &vm)
    {
        return vm.regCountCache_;
    }

    /// @brief Compute or retrieve the cached maximum SSA ID for a function.
    ///
    /// @details The maximum SSA ID determines the required register file size.
    ///          This helper checks the VM's cache first, and if not found,
    ///          scans the function's parameters and instruction results to
    ///          find the highest SSA value ID used, then caches the result.
    ///
    /// @invariant The returned value is the largest SSA value ID used by the
    ///            function. Register files sized to (maxSsaId + 1) will
    ///            accommodate all values without resizing.
    ///
    /// @param vm VM owning the register count cache.
    /// @param fn Function to compute the maximum SSA ID for.
    /// @return The maximum SSA value ID used by the function.
    static inline size_t computeMaxSsaId(VM &vm, const il::core::Function &fn)
    {
        auto &cache = vm.regCountCache_;
        if (auto it = cache.find(&fn); it != cache.end())
            return it->second;

        // Use valueNames size as initial estimate if available (common case)
        size_t maxSsaId = fn.valueNames.empty() ? 0 : fn.valueNames.size() - 1;

        // Scan function parameters
        for (const auto &p : fn.params)
            maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));

        // Scan block parameters and instruction results
        for (const auto &block : fn.blocks)
        {
            for (const auto &p : block.params)
                maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));
            for (const auto &instr : block.instructions)
                if (instr.result)
                    maxSsaId = std::max(maxSsaId, static_cast<size_t>(*instr.result));
        }

        cache.emplace(&fn, maxSsaId);
        return maxSsaId;
    }
};
} // namespace il::vm::detail
