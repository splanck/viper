//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/VMContext.hpp
// Purpose: Declares helper utilities for accessing VM execution context shared by
// Key invariants: Maintains a thread-local pointer to the active VM for trap
// Ownership/Lifetime: VMContext references a VM instance owned externally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

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

/**
 * @brief Execution context providing controlled access to VM internals.
 *
 * VMContext encapsulates per-execution state and provides a stable API
 * for dispatch strategies and runtime functions to interact with the VM.
 * Each execution creates its own context that tracks the active VM instance.
 *
 * @invariant Wraps a valid VM reference throughout its lifetime.
 * @ownership Non-owning reference to VM; caller manages VM lifetime.
 */
class VMContext
{
  public:
    /**
     * @brief Construct a context bound to the given VM instance.
     * @param vm VM to provide context for. Must outlive this context.
     */
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
