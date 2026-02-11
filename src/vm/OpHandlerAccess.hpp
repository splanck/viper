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

    /// @brief Retrieve the currently active execution state from the VM stack.
    /// @param vm Virtual machine instance.
    /// @return Pointer to the top execution state, or nullptr if the stack is empty.
    static inline ExecState *currentExecState(VM &vm)
    {
        return vm.execStack.empty() ? nullptr : vm.execStack.back();
    }

    /// @brief Evaluate an IL value within a frame using the VM's evaluation logic.
    /// @param vm Virtual machine instance.
    /// @param fr Active frame containing register state.
    /// @param value IL value to evaluate.
    /// @return Slot containing the evaluated result.
    static inline Slot eval(VM &vm, Frame &fr, const il::core::Value &value)
    {
        return vm.eval(fr, value);
    }

    /// @brief Access the VM's debug controller for breakpoint and watch management.
    /// @param vm Virtual machine instance.
    /// @return Reference to the debug controller owned by the VM.
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

    /// @brief Access the VM's function name lookup table.
    /// @param vm Virtual machine instance.
    /// @return Const reference to the map from function names to Function pointers.
    static inline const VM::FnMap &functionMap(const VM &vm)
    {
        return vm.fnMap;
    }

    /// @brief Access the VM's runtime call context used for trap metadata.
    /// @param vm Virtual machine instance.
    /// @return Reference to the runtime call context owned by the VM.
    static inline RuntimeCallContext &runtimeContext(VM &vm)
    {
        return vm.runtimeContext;
    }

    /// @brief Execute a function within the VM and return its result.
    /// @param vm Virtual machine instance.
    /// @param fn Function to call.
    /// @param args Argument slots passed to the function's entry block.
    /// @return Slot containing the function's return value.
    static inline Slot callFunction(VM &vm,
                                    const il::core::Function &fn,
                                    std::span<const Slot> args)
    {
        return vm.execFunction(fn, args);
    }

    // Stepping helpers for components that need controlled access -------------

    /// @brief Prepare an execution state for stepping through a function.
    /// @param vm Virtual machine instance.
    /// @param fn Function to prepare for execution.
    /// @param args Argument slots for the function's entry block.
    /// @return Fully initialized execution state ready for stepOnce().
    static inline ExecState prepare(VM &vm,
                                    const il::core::Function &fn,
                                    std::span<const Slot> args)
    {
        return vm.prepareExecution(fn, args);
    }

    /// @brief Execute a single interpreter step within the given execution state.
    /// @param vm Virtual machine instance.
    /// @param st Execution state to advance by one instruction.
    /// @return The function's return value when execution completes, or nullopt to continue.
    static inline std::optional<Slot> stepOnce(VM &vm, ExecState &st)
    {
        return vm.stepOnce(st);
    }

    /// @brief Set the maximum instruction count before forced termination.
    /// @param vm Virtual machine instance.
    /// @param max Step limit; 0 disables the limit.
    static inline void setMaxSteps(VM &vm, uint64_t max)
    {
        vm.maxSteps = max;
    }

    /// @brief Configure periodic host polling for cooperative multitasking.
    /// @param vm Virtual machine instance.
    /// @param everyN Invoke the callback every N instructions; 0 disables polling.
    /// @param cb Callback returning false to request a VM pause.
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

    /// @brief Transfer block parameters from pending slots to registers.
    /// @details Used by TCO to ensure parameters are copied to registers after
    ///          setting up the tail-call frame.
    /// @param vm VM owning the frame.
    /// @param fr Frame whose parameters should be transferred.
    /// @param bb Basic block whose parameter definitions guide the transfer.
    static inline void transferBlockParams(VM &vm, Frame &fr, const il::core::BasicBlock &bb)
    {
        vm.transferBlockParams(fr, bb);
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
