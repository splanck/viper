// File: src/vm/OpHandlerAccess.hpp
// Purpose: Expose controlled accessors for VM internals to opcode handler code.
// Key invariants: Grants read/write access only to members required for handler semantics.
// Ownership/Lifetime: Accessors operate on VM-owned state without transferring ownership.
// Links: docs/il-guide.md#reference
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

    static inline const std::unordered_map<std::string, const il::core::Function *> &functionMap(
        const VM &vm)
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
};
} // namespace il::vm::detail
