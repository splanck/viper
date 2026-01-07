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

/**
 * @brief RAII guard that installs a VM as the thread-local active instance.
 *
 * ActiveVMGuard manages the thread-local `tlsActiveVM` pointer that enables
 * `VM::activeInstance()` to retrieve the currently executing VM. It also
 * binds the VM's runtime context via `rt_set_current_context()` so that
 * C runtime functions access the correct per-VM state.
 *
 * ## Thread-Local Semantics
 *
 * - On construction: saves the current `tlsActiveVM` and sets it to the new VM
 * - On destruction: restores the saved value (supports nesting)
 * - Each thread has its own `tlsActiveVM`, so concurrent threads can each
 *   have their own active VM without interference
 *
 * ## Debug Assertions
 *
 * In debug builds (`NDEBUG` not defined), the guard asserts that:
 * - **No re-entry**: If `tlsActiveVM` is already non-null and points to a
 *   *different* VM, an assertion fires. This catches accidental concurrent
 *   use of a VM from the same thread.
 * - **Consistent restore**: The destructor asserts that `tlsActiveVM` still
 *   points to the expected VM before restoring.
 *
 * ## Usage
 *
 * @code
 *   {
 *       ActiveVMGuard guard(&myVM);
 *       // VM::activeInstance() now returns &myVM
 *       // rt_get_current_context() returns myVM.rtContext
 *       myVM.run();
 *   }  // guard destructor restores previous active VM
 * @endcode
 *
 * @invariant Restores the previous active VM value on destruction.
 * @invariant In debug builds, asserts on invalid re-entry patterns.
 * @ownership Does not own the VM pointer; lifetime is managed by the caller.
 */
struct ActiveVMGuard
{
    /**
     * @brief Install @p vm as the active VM for this thread.
     * @param vm VM to activate; may be nullptr to clear active state.
     * @pre In debug builds, asserts if a different VM is already active.
     */
    explicit ActiveVMGuard(VM *vm);

    /**
     * @brief Restore the previously active VM.
     * @pre In debug builds, asserts if tlsActiveVM was unexpectedly modified.
     */
    ~ActiveVMGuard();

    ActiveVMGuard(const ActiveVMGuard &) = delete;
    ActiveVMGuard &operator=(const ActiveVMGuard &) = delete;

  private:
    VM *previous = nullptr;                 ///< Previously active VM instance.
    VM *current = nullptr;                  ///< VM installed by this guard (for debug checks).
    RtContext *previousRtContext = nullptr; ///< Previously bound runtime context.
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
