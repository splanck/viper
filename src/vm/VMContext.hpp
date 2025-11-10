// File: src/vm/VMContext.hpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Declares helper utilities for accessing VM execution context shared by
//          dispatch strategies and opcode handlers.
// Key invariants: Maintains a thread-local pointer to the active VM for trap
//                 reporting and centralises evaluation helpers used during
//                 interpretation.
// Ownership/Lifetime: VMContext references a VM instance owned externally.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/VM.hpp"

namespace il::vm
{

/// @brief RAII helper installing the active VM for thread-local trap reporting.
/// @invariant Restores the previous active VM value on destruction.
/// @ownership Does not own the VM pointer; lifetime is managed by the caller.
struct ActiveVMGuard
{
    explicit ActiveVMGuard(VM *vm);
    ~ActiveVMGuard();

  private:
    VM *previous = nullptr; ///< Previously active VM instance.
};

/// @brief Provides access to interpreter helpers required by dispatch strategies.
/// @invariant Wraps a valid VM reference throughout its lifetime.
/// @ownership Does not own the VM; callers manage VM lifetime.
class VMContext
{
  public:
    explicit VMContext(VM &vm) noexcept;

    Slot eval(Frame &fr, const il::core::Value &value) const;
    std::optional<Slot> stepOnce(VM::ExecState &state) const;
    bool handleTrapDispatch(const VM::TrapDispatchSignal &signal, VM::ExecState &state) const;
    il::core::Opcode fetchOpcode(VM::ExecState &state) const;
    void handleInlineResult(VM::ExecState &state, const VM::ExecResult &exec) const;
    [[noreturn]] void trapUnimplemented(il::core::Opcode opcode) const;
    void traceStep(const il::core::Instr &instr, Frame &frame) const;
    VM::ExecResult executeOpcode(Frame &frame,
                                 const il::core::Instr &instr,
                                 const VM::BlockMap &blocks,
                                 const il::core::BasicBlock *&bb,
                                 size_t &ip) const;
    void clearCurrentContext() const;

    TraceSink &traceSink() const noexcept;
    DebugCtrl &debugController() const noexcept;
    VM *vm() const noexcept;

  private:
    VM *vmInstance = nullptr; ///< Bound VM instance.
  public:
    struct Config
    {
        bool enableOpcodeCounts = true;
    } config; ///< Lightweight runtime config snapshot used by macros.
};

/// @brief Return the active VM associated with the current thread.
VM *activeVMInstance();

} // namespace il::vm
