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

#include "vm/VM.hpp"

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
                                    const std::vector<Slot> &args)
    {
        return vm.execFunction(fn, args);
    }

    // Stepping helpers for components that need controlled access -------------
    static inline ExecState prepare(VM &vm,
                                    const il::core::Function &fn,
                                    const std::vector<Slot> &args)
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
};
} // namespace il::vm::detail
