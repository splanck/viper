//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/BytecodeVM.hpp
// Purpose: Stack-based bytecode interpreter for compiled Viper programs.
// Key invariants: Module must outlive the VM. Call depth never exceeds kMaxCallDepth.
//                 Operand stack per frame never exceeds kMaxStackSize.
//                 Thread-local active VM pointer is managed by ActiveBytecodeVMGuard.
// Ownership: VM borrows the BytecodeModule (non-owning pointer). Owns all internal
//            state (value stack, call stack, alloca buffer, string cache, globals).
// Lifetime: Constructed once, reusable across multiple exec() calls on the same module.
// Links: Bytecode.hpp, BytecodeModule.hpp, BytecodeCompiler.hpp
//
//===----------------------------------------------------------------------===//
//
// This file defines the BytecodeVM which executes compiled bytecode.
// The VM uses a stack-based evaluation model with local variable slots
// and supports basic control flow, arithmetic, and function calls.

#pragma once

#include "bytecode/Bytecode.hpp"
#include "bytecode/BytecodeModule.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare runtime string type
struct rt_string_impl;
using rt_string = rt_string_impl *;

// Forward declarations for runtime integration
namespace il::vm
{
union Slot;
struct RuntimeCallContext;
} // namespace il::vm

namespace viper
{
namespace bytecode
{

/// @brief Type alias for native function handlers invokable directly from bytecode.
/// @details A NativeHandler receives a pointer to the argument slots, the argument
///          count, and a pointer to a result slot. It reads arguments from the
///          args array and writes the return value (if any) to the result slot.
/// @param args   Pointer to the first argument BCSlot.
/// @param argc   Number of arguments provided.
/// @param result Pointer to the result BCSlot (written only if the function returns a value).
using NativeHandler = std::function<void(BCSlot *, uint32_t, BCSlot *)>;

/// @brief Trap kinds for runtime error classification.
/// @details When the VM encounters an exceptional condition, it raises a trap
///          with one of these kinds. Exception handlers can inspect the kind
///          to determine the appropriate recovery strategy.
enum class TrapKind : uint8_t
{
    None = 0,         ///< No trap (normal execution).
    Overflow,         ///< Integer arithmetic overflow.
    InvalidCast,      ///< Invalid type conversion (e.g., out-of-range float-to-int).
    DivisionByZero,   ///< Division or remainder by zero.
    IndexOutOfBounds, ///< Array or bounds-check index violation.
    NullPointer,      ///< Null pointer dereference.
    StackOverflow,    ///< Call stack depth exceeded kMaxCallDepth.
    InvalidOpcode,    ///< Unrecognized or unsupported opcode.
    RuntimeError      ///< Generic runtime error (e.g., from native code).
};

/// @brief VM execution state.
/// @details Tracks the current phase of the virtual machine lifecycle.
enum class VMState
{
    Ready,   ///< Module loaded, ready to execute.
    Running, ///< Currently executing bytecode.
    Halted,  ///< Execution completed normally.
    Trapped  ///< Execution halted due to an unhandled trap.
};

/// @brief Call frame for a single function invocation on the call stack.
/// @details Each call creates a new BCFrame that tracks the function being
///          executed, the program counter, pointers into the value stack for
///          locals and operand stack, and exception handler state.
struct BCFrame
{
    const BytecodeFunction *func; ///< Function being executed in this frame.
    uint32_t pc;                  ///< Program counter (index into func->code).
    BCSlot *locals;               ///< Pointer to the first local variable slot.
    BCSlot *stackBase;            ///< Operand stack base for this frame.
    uint32_t ehStackDepth;        ///< Exception handler stack depth at frame entry.
    uint32_t callSitePc;          ///< PC at the call site (for debugging/stack traces).
    size_t allocaBase;            ///< Alloca stack position at frame entry (for cleanup).
};

/// @brief Exception handler entry on the handler stack.
/// @details Pushed by EH_PUSH and popped by EH_POP. When a trap occurs, the
///          VM walks the handler stack to find a matching handler for the
///          current frame.
struct BCExceptionHandler
{
    uint32_t handlerPc;   ///< PC of the handler entry point.
    uint32_t frameIndex;  ///< Call stack frame index when this handler was registered.
    BCSlot *stackPointer; ///< Operand stack pointer when this handler was registered.
};

/// @brief Debug callback type for breakpoints and single-stepping.
/// @details Called by the VM when a breakpoint is hit or during single-step
///          execution. The callback can inspect VM state and decide whether
///          to continue execution.
/// @param vm            Reference to the executing BytecodeVM.
/// @param func          The function currently being executed.
/// @param pc            The current program counter.
/// @param is_breakpoint True if this is a breakpoint hit; false for single-step.
/// @return True to continue execution; false to pause.
using DebugCallback =
    std::function<bool(class BytecodeVM &, const BytecodeFunction *, uint32_t, bool)>;

/// @brief Bytecode virtual machine for executing compiled Viper programs.
/// @details The BytecodeVM loads a BytecodeModule and executes its functions
///          using a stack-based evaluation model. Features include:
///          - Operand stack and local variable slots per frame
///          - Nested function calls with configurable max depth
///          - Exception handling with try/catch-style handler registration
///          - Native function integration (via RuntimeBridge or registered handlers)
///          - Debug support (breakpoints, single-stepping, variable inspection)
///          - Optional threaded dispatch for higher throughput on GCC/Clang
class BytecodeVM
{
  public:
    /// @brief Construct a new BytecodeVM in the Ready state.
    BytecodeVM();

    /// @brief Destroy the VM and release all internal resources.
    ~BytecodeVM();

    /// @brief Load a bytecode module for execution.
    /// @details Initializes the VM's global variable storage, string literal
    ///          cache, and internal state from the module. The module pointer
    ///          must remain valid for the lifetime of the VM.
    /// @param module Pointer to the BytecodeModule to execute (must not be null).
    void load(const BytecodeModule *module);

    /// @brief Execute a function by name and return its result.
    /// @details Looks up the function in the loaded module, pushes a new frame,
    ///          executes until the function returns or a trap occurs, and returns
    ///          the result slot. For void functions, the returned BCSlot is zero.
    /// @param funcName The fully qualified name of the function to execute.
    /// @param args     Arguments to pass to the function (default: empty).
    /// @return The function's return value as a BCSlot.
    BCSlot exec(const std::string &funcName, const std::vector<BCSlot> &args = {});

    /// @brief Execute a function by pointer and return its result.
    /// @details Directly executes the given function without a name lookup.
    /// @param func Pointer to the BytecodeFunction to execute (must not be null).
    /// @param args Arguments to pass to the function (default: empty).
    /// @return The function's return value as a BCSlot.
    BCSlot exec(const BytecodeFunction *func, const std::vector<BCSlot> &args = {});

    /// @brief Get the current VM execution state.
    /// @return The current VMState (Ready, Running, Halted, or Trapped).
    VMState state() const
    {
        return state_;
    }

    /// @brief Get the kind of the last trap that occurred.
    /// @return The TrapKind of the most recent trap, or TrapKind::None.
    TrapKind trapKind() const
    {
        return trapKind_;
    }

    /// @brief Get the human-readable message of the last trap.
    /// @return A reference to the trap message string (empty if no trap).
    const std::string &trapMessage() const
    {
        return trapMessage_;
    }

    /// @brief Get the total number of instructions executed (for profiling).
    /// @return Cumulative instruction count since the last reset.
    uint64_t instrCount() const
    {
        return instrCount_;
    }

    /// @brief Reset the instruction counter to zero.
    void resetInstrCount()
    {
        instrCount_ = 0;
    }

    /// @brief Enable or disable the RuntimeBridge for native function calls.
    /// @details When enabled, CALL_NATIVE instructions route through the Viper
    ///          RuntimeBridge. When disabled, only directly registered handlers
    ///          are used.
    /// @param enabled True to enable the RuntimeBridge; false to disable.
    void setRuntimeBridgeEnabled(bool enabled)
    {
        runtimeBridgeEnabled_ = enabled;
    }

    /// @brief Check whether the RuntimeBridge is enabled.
    /// @return True if native calls use the RuntimeBridge; false otherwise.
    bool runtimeBridgeEnabled() const
    {
        return runtimeBridgeEnabled_;
    }

    /// @brief Register a native handler for direct invocation by name.
    /// @details Handlers registered here bypass the RuntimeBridge and are
    ///          called directly by the VM when a matching CALL_NATIVE is executed.
    /// @param name    The fully qualified native function name to register.
    /// @param handler The callback to invoke when this function is called.
    void registerNativeHandler(const std::string &name, NativeHandler handler);

    /// @brief Enable or disable threaded dispatch (computed goto).
    /// @details Threaded dispatch uses compiler-specific computed goto for
    ///          faster opcode dispatch. Only available on GCC and Clang.
    ///          Falls back to switch-based dispatch when not available.
    /// @param enabled True to enable threaded dispatch; false for switch-based.
    void setThreadedDispatch(bool enabled)
    {
        useThreadedDispatch_ = enabled;
    }

    /// @brief Check whether threaded dispatch is enabled.
    /// @return True if threaded dispatch is active; false otherwise.
    bool useThreadedDispatch() const
    {
        return useThreadedDispatch_;
    }

    //==========================================================================
    // Debug Support
    //==========================================================================

    /// @brief Set the debug callback for breakpoints and single-stepping.
    /// @param callback The callback function to invoke on debug events.
    void setDebugCallback(DebugCallback callback)
    {
        debugCallback_ = std::move(callback);
    }

    /// @brief Enable or disable single-step execution mode.
    /// @details When enabled, the debug callback is invoked before each
    ///          instruction is executed.
    /// @param enabled True to enable single-stepping; false to disable.
    void setSingleStep(bool enabled)
    {
        singleStep_ = enabled;
    }

    /// @brief Check whether single-step mode is enabled.
    /// @return True if single-stepping is active; false otherwise.
    bool singleStep() const
    {
        return singleStep_;
    }

    /// @brief Set a breakpoint at a specific program counter in a function.
    /// @param funcName The fully qualified name of the function.
    /// @param pc       The program counter (instruction index) to break at.
    void setBreakpoint(const std::string &funcName, uint32_t pc);

    /// @brief Clear a previously set breakpoint.
    /// @param funcName The fully qualified name of the function.
    /// @param pc       The program counter of the breakpoint to clear.
    void clearBreakpoint(const std::string &funcName, uint32_t pc);

    /// @brief Clear all breakpoints in all functions.
    void clearAllBreakpoints();

    /// @brief Get the current program counter.
    /// @details Returns the PC of the current frame, or 0 if no frame is active.
    /// @return The current instruction index.
    uint32_t currentPc() const
    {
        return fp_ ? fp_->pc : 0;
    }

    /// @brief Get the function currently being executed.
    /// @return Pointer to the current BytecodeFunction, or nullptr if idle.
    const BytecodeFunction *currentFunction() const
    {
        return fp_ ? fp_->func : nullptr;
    }

    /// @brief Get the current exception handler stack depth.
    /// @return Number of exception handlers currently registered.
    size_t exceptionHandlerDepth() const
    {
        return ehStack_.size();
    }

    /// @brief Get the source line number corresponding to the current PC.
    /// @return The source line number, or 0 if debug info is not available.
    uint32_t currentSourceLine() const;

    /// @brief Get the source line number for a specific PC in a function.
    /// @param func The function containing the PC.
    /// @param pc   The program counter to look up.
    /// @return The source line number, or 0 if debug info is not available.
    static uint32_t getSourceLine(const BytecodeFunction *func, uint32_t pc);

  private:
    /// @brief The module being executed (borrowed, non-owning pointer).
    const BytecodeModule *module_;

    // Execution state
    VMState state_;            ///< Current VM execution state.
    TrapKind trapKind_;        ///< Kind of the most recent trap.
    int32_t currentErrorCode_; ///< Error code for the current exception handler.
    std::string trapMessage_;  ///< Human-readable message for the most recent trap.

    /// @brief Value stack holding locals and operand stack entries for all frames.
    std::vector<BCSlot> valueStack_;

    /// @brief Call stack of active function frames.
    std::vector<BCFrame> callStack_;

    /// @brief Current stack pointer (points to the next free operand slot).
    BCSlot *sp_;

    /// @brief Current frame pointer (top of the call stack).
    BCFrame *fp_;

    // Profiling
    uint64_t instrCount_; ///< Cumulative instruction count for profiling.

    // Runtime integration
    bool runtimeBridgeEnabled_; ///< Whether CALL_NATIVE routes through RuntimeBridge.
    bool useThreadedDispatch_;  ///< Whether to use computed-goto dispatch.
    std::unordered_map<std::string, NativeHandler> nativeHandlers_; ///< Registered native handlers.

    /// @brief Exception handler stack (pushed by EH_PUSH, popped by EH_POP).
    std::vector<BCExceptionHandler> ehStack_;

    /// @brief Alloca buffer for stack allocations (separate from the operand stack).
    std::vector<uint8_t> allocaBuffer_;

    /// @brief Current allocation position in the alloca buffer.
    size_t allocaTop_;

    // Debug support
    bool singleStep_;             ///< Whether single-step mode is active.
    DebugCallback debugCallback_; ///< Callback for debug events.
    std::unordered_map<std::string, std::set<uint32_t>>
        breakpoints_; ///< Per-function breakpoint PCs.

    /// @brief Global variable storage (one BCSlot per global, indexed by global index).
    std::vector<BCSlot> globals_;

    /// @brief String literal cache storing proper rt_string objects for string constants.
    /// @details Indexed by string pool index. Ensures the runtime receives rt_string
    ///          pointers rather than raw C strings.
    std::vector<rt_string> stringCache_;

    /// @brief Initialize the string cache with rt_string objects for all strings in the pool.
    void initStringCache();

    /// @brief Main interpreter loop using switch-based dispatch.
    void run();

    /// @brief Threaded interpreter loop using computed-goto dispatch (faster).
    /// @details Only available when compiled with GCC or Clang (VIPER_BC_THREADED).
#if defined(__GNUC__) || defined(__clang__)
    void runThreaded();
#endif

    /// @brief Push a new call frame for the given function.
    /// @details Allocates locals and operand stack space on the value stack,
    ///          copies arguments into parameter slots, and begins execution.
    /// @param func The function to call.
    void call(const BytecodeFunction *func);

    /// @brief Pop the current call frame and restore the caller's state.
    /// @return True if there are more frames on the call stack; false if the
    ///         top-level function has returned.
    bool popFrame();

    /// @brief Raise a trap with the specified kind and message.
    /// @details Sets the VM state to Trapped and attempts to dispatch to an
    ///          exception handler. If no handler is found, execution halts.
    /// @param kind    The trap classification.
    /// @param message A human-readable description of the error.
    void trap(TrapKind kind, const char *message);

    /// @brief Check for signed addition overflow.
    /// @param a      Left operand.
    /// @param b      Right operand.
    /// @param result [out] The result of a + b (valid only when no overflow).
    /// @return True if the addition overflows; false otherwise.
    bool addOverflow(int64_t a, int64_t b, int64_t &result);

    /// @brief Check for signed subtraction overflow.
    /// @param a      Left operand.
    /// @param b      Right operand.
    /// @param result [out] The result of a - b (valid only when no overflow).
    /// @return True if the subtraction overflows; false otherwise.
    bool subOverflow(int64_t a, int64_t b, int64_t &result);

    /// @brief Check for signed multiplication overflow.
    /// @param a      Left operand.
    /// @param b      Right operand.
    /// @param result [out] The result of a * b (valid only when no overflow).
    /// @return True if the multiplication overflows; false otherwise.
    bool mulOverflow(int64_t a, int64_t b, int64_t &result);

    /// @brief Push an exception handler onto the handler stack.
    /// @param handlerPc The PC of the handler entry point.
    void pushExceptionHandler(uint32_t handlerPc);

    /// @brief Pop the most recently pushed exception handler.
    void popExceptionHandler();

    /// @brief Attempt to dispatch a trap to a registered exception handler.
    /// @details Walks the handler stack looking for a handler in the current
    ///          or an enclosing frame. If found, unwinds the call and operand
    ///          stacks and transfers control to the handler.
    /// @param kind The trap kind to dispatch.
    /// @return True if a handler was found and control was transferred; false
    ///         if the trap is unhandled.
    bool dispatchTrap(TrapKind kind);

    /// @brief Check whether the current PC matches a breakpoint.
    /// @return True if the debugger should pause; false otherwise.
    bool checkBreakpoint();
};

/// @brief Get the currently active BytecodeVM on this thread.
/// @details The active VM is set by ActiveBytecodeVMGuard and is used by
///          native functions that need to access VM state.
/// @return Pointer to the active BytecodeVM, or nullptr if none.
BytecodeVM *activeBytecodeVMInstance();

/// @brief Get the BytecodeModule of the active BytecodeVM on this thread.
/// @return Pointer to the module, or nullptr if no VM is active.
const BytecodeModule *activeBytecodeModule();

/// @brief RAII guard that sets the active BytecodeVM for the current thread.
/// @details On construction, saves the previous active VM and sets a new one.
///          On destruction, restores the previous active VM. This ensures
///          that re-entrant native calls see the correct VM context.
struct ActiveBytecodeVMGuard
{
    /// @brief Set @p vm as the active VM for this thread.
    /// @param vm The BytecodeVM to make active (must not be null).
    explicit ActiveBytecodeVMGuard(BytecodeVM *vm);

    /// @brief Restore the previous active VM.
    ~ActiveBytecodeVMGuard();

    ActiveBytecodeVMGuard(const ActiveBytecodeVMGuard &) = delete;
    ActiveBytecodeVMGuard &operator=(const ActiveBytecodeVMGuard &) = delete;

  private:
    BytecodeVM *previous_; ///< The VM that was active before this guard.
    BytecodeVM *current_;  ///< The VM made active by this guard.
};

} // namespace bytecode
} // namespace viper
