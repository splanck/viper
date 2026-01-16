// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// BytecodeVM.hpp - Bytecode interpreter
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
using rt_string = rt_string_impl*;

// Forward declarations for runtime integration
namespace il::vm {
union Slot;
struct RuntimeCallContext;
}

namespace viper {
namespace bytecode {

/// Native function handler type for direct bytecode calls
/// Takes: (args pointer, arg count, result pointer)
using NativeHandler = std::function<void(BCSlot*, uint32_t, BCSlot*)>;

/// Trap kinds for error handling
enum class TrapKind : uint8_t {
    None = 0,
    Overflow,
    InvalidCast,
    DivisionByZero,
    IndexOutOfBounds,
    NullPointer,
    StackOverflow,
    InvalidOpcode,
    RuntimeError
};

/// VM execution state
enum class VMState {
    Ready,
    Running,
    Halted,
    Trapped
};

/// Call frame for a function invocation
struct BCFrame {
    const BytecodeFunction* func;  // Function being executed
    uint32_t pc;                   // Program counter (index into code)
    BCSlot* locals;                // Pointer to first local
    BCSlot* stackBase;             // Stack base for this frame
    uint32_t ehStackDepth;         // Exception handler stack depth at entry
    uint32_t callSitePc;           // PC at call site (for debugging)
    size_t allocaBase;             // Alloca stack position at frame entry
};

/// Exception handler entry on the handler stack
struct BCExceptionHandler {
    uint32_t handlerPc;            // PC of handler entry point
    uint32_t frameIndex;           // Call stack frame index when registered
    BCSlot* stackPointer;          // Stack pointer when registered
};

/// Debug callback type for breakpoints and single-stepping
/// Args: (vm reference, function, pc, is_breakpoint)
/// Returns: true to continue, false to pause
using DebugCallback = std::function<bool(class BytecodeVM&, const BytecodeFunction*, uint32_t, bool)>;

/// Bytecode virtual machine
class BytecodeVM {
public:
    BytecodeVM();
    ~BytecodeVM();

    /// Load a bytecode module for execution
    void load(const BytecodeModule* module);

    /// Execute the specified function and return result
    BCSlot exec(const std::string& funcName,
                const std::vector<BCSlot>& args = {});

    /// Execute a function by pointer
    BCSlot exec(const BytecodeFunction* func,
                const std::vector<BCSlot>& args = {});

    /// Get current VM state
    VMState state() const { return state_; }

    /// Get last trap kind
    TrapKind trapKind() const { return trapKind_; }

    /// Get last trap message
    const std::string& trapMessage() const { return trapMessage_; }

    /// Get instruction count (for profiling)
    uint64_t instrCount() const { return instrCount_; }

    /// Reset instruction count
    void resetInstrCount() { instrCount_ = 0; }

    /// Enable runtime bridge integration for native function calls
    /// When enabled, CALL_NATIVE uses the Viper RuntimeBridge
    void setRuntimeBridgeEnabled(bool enabled) { runtimeBridgeEnabled_ = enabled; }

    /// Check if runtime bridge is enabled
    bool runtimeBridgeEnabled() const { return runtimeBridgeEnabled_; }

    /// Register a native handler for direct invocation (bypasses RuntimeBridge)
    void registerNativeHandler(const std::string& name, NativeHandler handler);

    /// Set whether to use threaded dispatch (computed goto) when available
    /// Threaded dispatch is faster but only available on GCC/Clang
    void setThreadedDispatch(bool enabled) { useThreadedDispatch_ = enabled; }

    /// Check if threaded dispatch is enabled
    bool useThreadedDispatch() const { return useThreadedDispatch_; }

    //==========================================================================
    // Debug Support
    //==========================================================================

    /// Set debug callback for breakpoints and single-stepping
    void setDebugCallback(DebugCallback callback) { debugCallback_ = std::move(callback); }

    /// Enable/disable single-step mode
    void setSingleStep(bool enabled) { singleStep_ = enabled; }

    /// Check if single-stepping is enabled
    bool singleStep() const { return singleStep_; }

    /// Set a breakpoint at a specific PC in a function
    void setBreakpoint(const std::string& funcName, uint32_t pc);

    /// Clear a breakpoint
    void clearBreakpoint(const std::string& funcName, uint32_t pc);

    /// Clear all breakpoints
    void clearAllBreakpoints();

    /// Get current PC (for debugging)
    uint32_t currentPc() const { return fp_ ? fp_->pc : 0; }

    /// Get current function (for debugging)
    const BytecodeFunction* currentFunction() const { return fp_ ? fp_->func : nullptr; }

    /// Get exception handler stack depth
    size_t exceptionHandlerDepth() const { return ehStack_.size(); }

    /// Get source line for current PC (0 if not available)
    uint32_t currentSourceLine() const;

    /// Get source line for a specific PC in a function (0 if not available)
    static uint32_t getSourceLine(const BytecodeFunction* func, uint32_t pc);

private:
    // Module being executed
    const BytecodeModule* module_;

    // Execution state
    VMState state_;
    TrapKind trapKind_;
    std::string trapMessage_;

    // Value stack (holds locals and operand stack for all frames)
    std::vector<BCSlot> valueStack_;

    // Call stack
    std::vector<BCFrame> callStack_;

    // Current stack pointer (points to next free slot)
    BCSlot* sp_;

    // Current frame pointer
    BCFrame* fp_;

    // Profiling
    uint64_t instrCount_;

    // Runtime integration
    bool runtimeBridgeEnabled_;
    bool useThreadedDispatch_;
    std::unordered_map<std::string, NativeHandler> nativeHandlers_;

    // Exception handler stack
    std::vector<BCExceptionHandler> ehStack_;

    // Alloca buffer for stack allocations (separate from operand stack)
    std::vector<uint8_t> allocaBuffer_;
    size_t allocaTop_;  // Current allocation position

    // Debug support
    bool singleStep_;
    DebugCallback debugCallback_;
    std::unordered_map<std::string, std::set<uint32_t>> breakpoints_;  // funcName -> set of PCs

    // String literal cache - stores proper rt_string objects for string constants
    // The cache is indexed by string pool index and stores rt_string pointers
    // that the runtime expects (not raw C strings)
    std::vector<rt_string> stringCache_;

    // Initialize string cache with rt_string objects for all strings in pool
    void initStringCache();

    // Main interpreter loop (switch-based dispatch)
    void run();

    // Threaded interpreter loop (computed goto dispatch - faster)
    // Only available when VIPER_BC_THREADED is defined
#if defined(__GNUC__) || defined(__clang__)
    void runThreaded();
#endif

    // Call a function
    void call(const BytecodeFunction* func);

    // Pop current frame, returning true if there are more frames
    bool popFrame();

    // Raise a trap
    void trap(TrapKind kind, const char* message);

    // Helper to check for overflow
    bool addOverflow(int64_t a, int64_t b, int64_t& result);
    bool subOverflow(int64_t a, int64_t b, int64_t& result);
    bool mulOverflow(int64_t a, int64_t b, int64_t& result);

    // Exception handling helpers
    void pushExceptionHandler(uint32_t handlerPc);
    void popExceptionHandler();
    bool dispatchTrap(TrapKind kind);  // Returns true if handler found

    // Debug helpers
    bool checkBreakpoint();  // Returns true if should pause
};

} // namespace bytecode
} // namespace viper
