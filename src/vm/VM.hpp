//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file
 * @brief Stack-based virtual machine that executes Viper IL.
 *
 * Declares the interpreter core, associated execution context, and dispatch
 * strategies. The VM caches runtime data (e.g., string literals) and exposes
 * a small public API for running modules and integrating debug/trace hooks.
 *
 * @section invariants Key invariants
 * - Inline string-literal cache owns one handle per literal per VM.
 * - The VM does not own the Module or RuntimeBridge it references.
 *
 * @section concurrency Concurrency model
 * Each VM instance is single-threaded: only one thread may execute within a
 * given VM instance at a time. This avoids synchronization on the hot path and
 * simplifies resource management. To parallelize, create multiple VMs (one per
 * thread). Each VM maintains its own:
 * - Runtime context (RNG state, module variables, file channels, type registry)
 * - Register file and operand stack
 * - String literal cache
 * - Trap/error state
 *
 * @par Thread-local state
 * The active VM for a thread is tracked via a thread-local guard
 * (`ActiveVMGuard`, see `VMContext.hpp`).
 * - `ActiveVMGuard guard(&vm)` installs `vm` as the active VM for the scope
 * - The guard binds the VM's runtime context via `rt_set_current_context`
 * - `VM::activeInstance()` returns the active VM for the calling thread
 * - Guard destruction restores the previous VM (supports nesting)
 *
 * @par Usage patterns
 * - Single-threaded (most common): `VM vm(module); vm.run();`
 * - Multi-threaded (one VM per thread): spawn threads, construct per-thread
 *   VMs, and call `run()` independently.
 *
 * @par Debug assertions
 * In debug builds, the VM asserts if:
 * - `ActiveVMGuard` re-enters an already active VM on the same thread
 * - `activeInstance()` is called when no VM is active (nullptr in release)
 */
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"
#include "rt.hpp"
#include "support/source_location.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/VMConfig.hpp"
#include "vm/ViperStringHandle.hpp"
#include "vm/control_flow.hpp"

// Forward declare C runtime context struct
struct RtContext;

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace il::vm
{

class Runner; // fwd for friend declaration of Runner::Impl

class VMContext;
class DispatchStrategy; // fwd for friend declaration

namespace detail
{
struct VMAccess; ///< Forward declaration of helper granting opcode handlers internal access

namespace ops
{
struct OperandDispatcher; ///< Forward declaration of operand evaluation helper
} // namespace ops
class FnTableDispatchDriver;
class SwitchDispatchDriver;
class ThreadedDispatchDriver;
class FnTableStrategy;
class SwitchStrategy;
class ThreadedStrategy;
} // namespace detail

/// @brief Scripted debug actions.
class DebugScript;

/// @brief Runtime slot capable of holding IL values.
/// @invariant Only one member is valid based on value type.
/// @note Slot is designed to be trivially copyable (8 bytes) for efficient
///       value semantics in the interpreter dispatch loop.
union Slot
{
    /// @brief Signed integer value.
    int64_t i64;

    /// @brief Floating-point value.
    double f64;

    /// @brief Generic pointer.
    void *ptr;

    /// @brief Runtime string handle.
    rt_string str;

    /// @brief Compare two slots for bitwise equality (type-agnostic).
    /// @note This compares the raw i64 representation. Use only when type is unknown
    ///       or when comparing integral/pointer types.
    [[nodiscard]] inline bool bitwiseEquals(const Slot &other) const noexcept
    {
        return i64 == other.i64;
    }
};

// Ensure Slot remains efficient for interpreter dispatch
static_assert(sizeof(Slot) == 8, "Slot must be 8 bytes for optimal copy performance");
static_assert(std::is_trivially_copyable_v<Slot>, "Slot must be trivially copyable");

/// @brief Call frame storing registers and operand stack.
/// @invariant Stack pointer @c sp never exceeds @c stack size.
struct Frame
{
    struct HandlerRecord
    {
        const il::core::BasicBlock *handler = nullptr;
        size_t ipSnapshot = 0;
    };

    /// @brief Deferred resumption metadata used by trap handlers.
    /// @invariant When @c valid is true, @c block references the faulting block
    ///            and @c nextIp is either @c faultIp + 1 or @c block->instructions.size().
    struct ResumeState
    {
        const il::core::BasicBlock *block = nullptr;
        size_t faultIp = 0;
        size_t nextIp = 0;
        bool valid = false;
    };

    /// @brief Executing function.
    /// @ownership Non-owning; function resides in the module.
    /// @invariant Never null for a valid frame.
    const il::core::Function *func;

    /// @brief Register file for SSA values.
    /// @ownership Owned by the frame; sized to the function's register count.
    std::vector<Slot> regs;

    /// @brief Default operand stack size in bytes.
    /// @details Sized to accommodate typical alloca usage patterns.
    ///          64KB is sufficient for most BASIC programs including games
    ///          with moderate-sized screen buffers and local arrays.
    ///          BUG-OOP-033: Increased from 1KB to 64KB to support reasonable
    ///          array sizes (e.g., 80x25 screen buffer = 16KB as i64).
    static constexpr size_t kDefaultStackSize = 65536;

    /// @brief Operand stack storage.
    /// @ownership Owned by the frame; dynamically sized based on configuration.
    /// @invariant Once initialized, the vector capacity should not change during
    ///            execution to avoid pointer invalidation from prior @c alloca calls.
    std::vector<uint8_t> stack;

    /// @brief Stack pointer in bytes.
    /// @invariant Never exceeds @c stack.size().
    size_t sp = 0;

    /// @brief Pending block parameter values indexed by SSA id.
    /// @ownership Owned by the frame; sized to the register file and reset on use.
    std::vector<std::optional<Slot>> params;

    /// @brief Active exception handlers in this frame.
    std::vector<HandlerRecord> ehStack;

    /// @brief Error payload staged for the current handler.
    VmError activeError{};

    /// @brief Resume token payload for handler resumption.
    ResumeState resumeState{};
};

/**
 * @brief Virtual machine for executing Viper IL modules.
 *
 * The VM interprets IL bytecode using one of three dispatch strategies:
 * function table, switch statement, or threaded code (when supported).
 *
 * ## Concurrency Model
 *
 * Each VM instance is **single-threaded**: it may only be executed by one
 * thread at a time. The VM does not use internal locks or atomic operations
 * in its hot paths to maximize interpreter performance.
 *
 * **Parallelism** is achieved by creating multiple VM instances, one per
 * thread. Each VM instance owns its own runtime context (`RtContext`),
 * providing isolated state for:
 * - Random number generator (RNG seed and state)
 * - Module-level variables (modvar storage)
 * - File I/O channels
 * - Type registry (class/interface registrations)
 *
 * **Thread-local active VM**: The static `VM::activeInstance()` method
 * returns the VM currently executing on the calling thread. This relies
 * on `thread_local` state managed by `ActiveVMGuard`:
 *
 * @code
 *   // Internal usage pattern (ActiveVMGuard is RAII):
 *   {
 *       ActiveVMGuard guard(&vm);  // Sets tlsActiveVM and binds rtContext
 *       // ... VM execution ...
 *   }  // Guard destructor restores previous state
 * @endcode
 *
 * @invariant The module passed to the constructor must outlive the VM instance.
 * @invariant Only one thread may execute within a VM instance at a time.
 * @invariant A thread may have at most one VM actively executing at any time
 *            (debug builds assert on re-entry attempts).
 *
 * @see ActiveVMGuard for the RAII helper managing thread-local state.
 * @see VMContext for the execution context API used by dispatch strategies.
 */
class VM
{
  public:
    /// @brief Dispatch strategy for executing the interpreter loop.
    enum DispatchKind
    {
        FnTable,  ///< Use function-table based dispatch through executeOpcode.
        Switch,   ///< Use switch-based inline dispatch.
        Threaded, ///< Use computed goto threaded dispatch when supported.
    };

    friend class VMContext;         ///< Allow context helpers access to internals
    friend struct ActiveVMGuard;    ///< Allow ActiveVMGuard to access rtContext
    friend struct detail::VMAccess; ///< Allow opcode handlers to access internals via helper
    friend class detail::FnTableDispatchDriver;
    friend class detail::SwitchDispatchDriver;
#if VIPER_THREADING_SUPPORTED
    friend class detail::ThreadedDispatchDriver;
#endif
    friend struct detail::ops::OperandDispatcher; ///< Allow shared helpers to evaluate operands
    friend class DispatchStrategy; ///< Allow dispatch strategies to access execution state
    friend class detail::FnTableStrategy;
    friend class detail::SwitchStrategy;
#if VIPER_THREADING_SUPPORTED
    friend class detail::ThreadedStrategy;
#endif
    friend class RuntimeBridge; ///< Runtime bridge accesses trap formatting helpers
    friend void vm_raise(TrapKind kind, int32_t code);
    /// @brief Handles error condition.
    friend void vm_raise_from_error(const VmError &error);
    friend struct VMTestHook; ///< Unit tests access interpreter internals
    friend VmError *vm_acquire_trap_token();
    friend const VmError *vm_current_trap_token();
    friend void vm_clear_trap_token();
    friend void vm_store_trap_token_message(std::string_view text);
    friend std::string vm_current_trap_message();

    /// @brief Result of executing one opcode.
    struct ExecResult
    {
        /// @brief Whether control transferred to another block.
        bool jumped = false;

        /// @brief Whether execution returned from the function.
        bool returned = false;

        /// @brief Return value when @c returned is true.
        Slot value{};
    };

    /// @brief Block lookup table keyed by label.
    // Use string_view keys to avoid key string copies while relying on the
    // module-owned lifetime of names/labels.
    struct TransparentHashSV
    {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }

        size_t operator()(const std::string &s) const noexcept
        {
            return (*this)(std::string_view{s});
        }

        size_t operator()(const char *s) const noexcept
        {
            return (*this)(std::string_view{s});
        }
    };

    struct TransparentEqualSV
    {
        using is_transparent = void;

        bool operator()(std::string_view a, std::string_view b) const noexcept
        {
            return a == b;
        }

        bool operator()(const std::string &a, const std::string &b) const noexcept
        {
            return a == b;
        }

        bool operator()(const std::string &a, std::string_view b) const noexcept
        {
            return a == b;
        }

        bool operator()(std::string_view a, const std::string &b) const noexcept
        {
            return a == b;
        }

        bool operator()(const char *a, std::string_view b) const noexcept
        {
            return std::string_view{a} == b;
        }

        bool operator()(std::string_view a, const char *b) const noexcept
        {
            return a == std::string_view{b};
        }
    };

    using BlockMap = std::unordered_map<std::string_view,
                                        const il::core::BasicBlock *,
                                        TransparentHashSV,
                                        TransparentEqualSV>;

    /// @brief Aggregate execution state for a running function.
    /// @details Moved to public section for dispatch strategy access.
    struct ExecState
    {
        Frame fr;                                 ///< Current frame
        BlockMap blocks;                          ///< Basic block lookup
        const il::core::BasicBlock *bb = nullptr; ///< Active basic block
        size_t ip = 0;                            ///< Instruction pointer within @p bb
        bool skipBreakOnce = false;               ///< Whether to skip next breakpoint
        const il::core::BasicBlock *callSiteBlock =
            nullptr;                          ///< Block of the call that entered this frame
        size_t callSiteIp = 0;                ///< Instruction index of the call in the caller
        il::support::SourceLoc callSiteLoc{}; ///< Source location of the call site
        viper::vm::SwitchCache switchCache{}; ///< Memoized switch dispatch data for this frame
        Slot pendingResult{};                 ///< Result staged by interpreter
        bool hasPendingResult = false;        ///< Whether pendingResult is valid
        const il::core::Instr *currentInstr =
            nullptr;                ///< Instruction under execution for inline dispatch
        bool exitRequested = false; ///< Whether the active loop should exit

        // Host polling configuration/state ----------------------------------
        struct PollConfig
        {
            uint32_t interruptEveryN = 0;
            std::function<bool(VM &)> pollCallback;
            bool enableOpcodeCounts = true; ///< Runtime toggle for opcode counting
        } config;                           ///< Per-run polling configuration

        uint64_t pollTick = 0; ///< Instruction counter for polling cadence

        VM *owner = nullptr; ///< Owning VM used by callbacks

        /// @brief Access the owning VM for this execution state.
        VM *vm()
        {
            return owner;
        }

        /// @brief Request the interpreter loop to pause at the next boundary.
        void requestPause()
        {
            Slot s{};
            s.i64 = 1; // generic pause sentinel
            pendingResult = s;
            hasPendingResult = true;
            exitRequested = true;
        }

        /// @brief Cache for resolved branch targets per instruction.
        std::unordered_map<size_t, const il::core::BasicBlock *> branchCache;

        /// @brief Cache for resolved branch targets per instruction.
        /// @details Maps an instruction pointer to a vector of resolved
        ///          BasicBlock* targets in the same order as @c in.labels.
        std::unordered_map<const il::core::Instr *, std::vector<const il::core::BasicBlock *>>
            branchTargetCache;
    };

    /// @brief RAII helper that pushes/pops the execution stack around function calls.
    /// @details Manages the execStack to track active execution states for trap
    ///          unwinding, debugging, and re-entrancy detection. The stack is
    ///          pre-allocated in the VM constructor to avoid heap allocation in
    ///          common cases (HIGH-5 optimization).
    /// @invariant Constructor pushes state; destructor pops if state is still top.
    struct ExecStackGuard
    {
        VM &vm;
        ExecState *state;

        /// @brief Push the execution state onto the VM stack.
        ExecStackGuard(VM &vmRef, ExecState &stRef) noexcept : vm(vmRef), state(&stRef)
        {
            vm.execStack.push_back(state);
        }

        /// @brief Pop the execution state if it is still the active frame.
        /// @note Uses conditional pop to handle cases where the stack may have
        ///       been modified by exception handling during unwinding.
        ~ExecStackGuard() noexcept
        {
            if (!vm.execStack.empty() && vm.execStack.back() == state)
                vm.execStack.pop_back();
        }

        // Non-copyable, non-movable
        ExecStackGuard(const ExecStackGuard &) = delete;
        ExecStackGuard &operator=(const ExecStackGuard &) = delete;
        ExecStackGuard(ExecStackGuard &&) = delete;
        ExecStackGuard &operator=(ExecStackGuard &&) = delete;
    };

    // Public map aliases for handler-facing utilities
    using FnMap = std::unordered_map<std::string_view,
                                     const il::core::Function *,
                                     TransparentHashSV,
                                     TransparentEqualSV>;
    using StrMap = std::
        unordered_map<std::string_view, ViperStringHandle, TransparentHashSV, TransparentEqualSV>;

    /**
     * @brief Construct a VM for executing an IL module.
     *
     * @param m IL module to execute. Must remain valid for VM lifetime.
     * @param tc Trace configuration for debugging and profiling output.
     * @param maxSteps Maximum instruction count before forced termination (0 = unlimited).
     * @param dbg Debug control for breakpoints and single-stepping.
     * @param script Optional debug script for automated debugging sessions.
     * @param stackBytes Per-frame operand stack size in bytes (default 64KB).
     *
     * @invariant The module reference must remain valid throughout VM lifetime.
     *
     * @note This constructor performs memory allocation for module globals and
     *       runtime state. On allocation failure, the program terminates via
     *       `std::abort()` after printing a diagnostic. This is consistent with
     *       the project's non-throwing convention for library entry points.
     */
    VM(const il::core::Module &m,
       TraceConfig tc = {},
       uint64_t maxSteps = 0,
       DebugCtrl dbg = {},
       DebugScript *script = nullptr,
       std::size_t stackBytes = Frame::kDefaultStackSize);

    /// @brief Release runtime string handles retained by the VM.
    ~VM();

    VM(const VM &) = delete;
    VM &operator=(const VM &) = delete;
    VM(VM &&) noexcept = default;
    VM &operator=(VM &&) = delete;

    /**
     * @brief Execute the module's main function.
     *
     * Locates and executes the module's "main" function, handling any
     * runtime errors that occur during execution.
     *
     * @return Exit code from main function, or 1 if main is missing or execution fails.
     * @throws May propagate exceptions from runtime functions if configured.
     */
    int64_t run();

    /// @brief Function signature for opcode handlers.
    using OpcodeHandler = ExecResult (*)(VM &,
                                         Frame &,
                                         const il::core::Instr &,
                                         const BlockMap &,
                                         const il::core::BasicBlock *&,
                                         size_t &);

    /// @brief Compile-time table type storing handlers per opcode.
    using OpcodeHandlerTable = std::array<OpcodeHandler, il::core::kNumOpcodes>;

    /// @brief Obtain immutable table mapping opcodes to handlers.
    static const OpcodeHandlerTable &getOpcodeHandlers();

    // Dispatch interface methods - made public for dispatch strategy access
  public:
    /// @brief Reset interpreter bookkeeping for a new dispatch cycle.
    void beginDispatch(ExecState &state);

    /// @brief Select the next instruction to execute.
    /// @param state Current execution state containing control-flow metadata.
    /// @param instr Set to point at the instruction when available.
    /// @return False when execution should halt, leaving @p state updated.
    bool selectInstruction(ExecState &state, const il::core::Instr *&instr);

    /// @brief Trace an instruction and account for instruction counts.
    void traceInstruction(const il::core::Instr &instr, Frame &frame);

    /// @brief Apply standard bookkeeping after executing an opcode.
    /// @return True when the caller should stop dispatching.
    bool finalizeDispatch(ExecState &state, const ExecResult &exec);

    // =========================================================================
    // Fast-path accessors for debug hooks (inlined for hot paths)
    // =========================================================================

    /// @brief Check if tracing is active (cheap boolean test).
    /// @return True when tracer.onStep() should be called.
    [[nodiscard]] bool isTracingActive() const noexcept
    {
        return tracingActive_;
    }

    /// @brief Check if memory watches are active (cheap boolean test).
    /// @return True when memory write callbacks should be invoked.
    [[nodiscard]] bool hasMemWatchesActive() const noexcept
    {
        return memWatchActive_;
    }

    /// @brief Check if variable watches are active (cheap boolean test).
    /// @return True when variable store callbacks should be invoked.
    [[nodiscard]] bool hasVarWatchesActive() const noexcept
    {
        return varWatchActive_;
    }

    /// @brief Refresh all debug fast-path flags from current state.
    /// @details Call this after changing trace config, adding/removing watches.
    void refreshDebugFlags();

    /// @brief Execute an opcode via function table dispatch.
    ExecResult executeOpcode(Frame &fr,
                             const il::core::Instr &in,
                             const BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip);

    /// @brief Clear current execution context for trap handling.
    void clearCurrentContext();

    /// @brief Update current trap context for instruction @p in.
    void setCurrentContext(Frame &fr,
                           const il::core::BasicBlock *bb,
                           size_t ip,
                           const il::core::Instr &in);

    // dispatchOpcodeSwitch is declared via generated include file

    /// @brief Exception type for trap dispatch control flow.
    struct TrapDispatchSignal : std::exception
    {
        explicit TrapDispatchSignal(ExecState *targetState);

        ExecState *target;

        const char *what() const noexcept override;
    };

  private:
    /// @brief Captures instruction context for trap diagnostics.
    struct TrapContext
    {
        const il::core::Function *function = nullptr; ///< Active function
        const il::core::BasicBlock *block = nullptr;  ///< Active basic block
        size_t instructionIndex = 0;                  ///< Instruction index within block
        bool hasInstruction = false;                  ///< Whether instruction metadata is valid
        il::support::SourceLoc loc{};                 ///< Source location of the instruction
    };

    /// @brief Last trap recorded by the VM for diagnostic reporting.
    struct TrapState
    {
        VmError error{};     ///< Structured error payload
        FrameInfo frame{};   ///< Captured frame metadata
        std::string message; ///< Formatted diagnostic message
    };

    struct TrapToken
    {
        VmError error{};     ///< Stored error payload for trap.err construction
        std::string message; ///< Message associated with the token
        bool valid = false;  ///< Whether the token contains a constructed error
    };

    /// @brief Module to execute.
    /// @ownership Non-owning reference; module must outlive the VM.
    const il::core::Module &mod;

    /// @brief Custom deleter for RtContext that calls rt_context_cleanup before deletion.
    struct RtContextDeleter
    {
        void operator()(RtContext *ctx) const noexcept;
    };

    /// @brief Per-VM runtime context for isolated state.
    /// @ownership Owned by the VM via unique_ptr; initialized on construction, cleaned up on
    /// destruction.
    /// @details Provides isolated RNG, modvar, and other runtime state per VM instance.
    std::unique_ptr<RtContext, RtContextDeleter> rtContext;

    /// @brief Trace output sink.
    /// @ownership Owned by the VM.
    TraceSink tracer;

    /// @brief Breakpoint controller.
    /// @ownership Owned by the VM.
    DebugCtrl debug;

    /// @brief Optional debug command script.
    /// @ownership Non-owning pointer; may be @c nullptr.
    DebugScript *script;

    // =========================================================================
    // Fast-path flags for debug hooks in hot paths
    // =========================================================================
    // These flags enable cheap checks in frequently executed code paths.
    // When false, the corresponding debug functionality is skipped entirely
    // without any function calls or complex checks.

    /// @brief True when tracing is enabled and onStep should be called.
    /// @details Cached from tracer.cfg.enabled() to avoid repeated virtual calls.
    ///          Updated when trace configuration changes.
    bool tracingActive_ = false;

    /// @brief True when memory watches are installed.
    /// @details Cached from debug.hasMemWatches() to enable fast-path bypass
    ///          in store handlers. Updated when watches are added/removed.
    bool memWatchActive_ = false;

    /// @brief True when any variable watches are installed.
    /// @details Cached to enable fast-path bypass for onStore callbacks.
    bool varWatchActive_ = false;

    /// @brief Step limit; @c 0 means unlimited.
    uint64_t maxSteps;

    /// @brief Configured stack size in bytes for each Frame.
    std::size_t stackBytes_;

    /// @brief Interpreter dispatch strategy selected at construction.
    DispatchKind dispatchKind = FnTable;

    /// @brief Internal driver implementing the selected dispatch mechanism.
    struct DispatchDriver;

    struct DispatchDriverDeleter
    {
        void operator()(DispatchDriver *driver) const;
    };

    std::unique_ptr<DispatchDriver, DispatchDriverDeleter> dispatchDriver;

    static std::unique_ptr<DispatchDriver, DispatchDriverDeleter> makeDispatchDriver(
        DispatchKind kind);

    /// @brief Executed instruction count.
    /// @invariant Monotonically increases during execution.
    uint64_t instrCount = 0;

    /// @brief Remaining instructions to step before pausing.
    uint64_t stepBudget = 0;

    /// @brief Function name lookup table.
    /// @ownership Owned by the VM; values reference functions in @c mod.
    std::unordered_map<std::string_view,
                       const il::core::Function *,
                       TransparentHashSV,
                       TransparentEqualSV>
        fnMap;

    /// @brief Interned runtime strings.
    /// @ownership Owned by the VM via ViperStringHandle RAII; manages string handles for globals.
    std::unordered_map<std::string_view, ViperStringHandle, TransparentHashSV, TransparentEqualSV>
        strMap;

    /// @brief Storage for mutable module-level globals.
    /// @ownership Owned by the VM; each global maps to allocated storage.
    /// @invariant Storage lifetime matches VM lifetime; freed in destructor.
    std::unordered_map<std::string_view, void *, TransparentHashSV, TransparentEqualSV>
        mutableGlobalMap;

    /// @brief Cached runtime handles for inline string literals containing embedded NULs.
    /// @ownership Owned by the VM via ViperStringHandle RAII; handles created via @c
    /// rt_string_from_bytes.
    /// @invariant Each literal string maps to at most one active handle, released automatically
    ///            when the cache is cleared or the VM is destroyed.
    std::unordered_map<std::string_view, ViperStringHandle, TransparentHashSV, TransparentEqualSV>
        inlineLiteralCache;

    /// @brief Reverse map from basic block pointer to owning function.
    /// @ownership Owned by the VM; populated during initialization.
    /// @invariant Each BasicBlock* in the module maps to exactly one Function*.
    /// @details Eliminates O(N*M) linear scan in exception handler lookup,
    ///          providing 50-90% improvement in error path performance.
    std::unordered_map<const il::core::BasicBlock *, const il::core::Function *> blockToFunction;

    /// @brief Cache of precomputed register file sizes per function.
    /// @details Avoids rescanning instructions to determine the maximum SSA id
    ///          every time a frame is prepared for the same function.
    std::unordered_map<const il::core::Function *, size_t> regCountCache_;

    /// @brief Cached pointer to the opcode handler table for quick access.
    const OpcodeHandlerTable *handlerTable_ = nullptr;

    /// @brief Trap metadata for the currently executing runtime call.
    RuntimeCallContext runtimeContext;

    /// @brief Currently executing instruction context for trap reporting.
    TrapContext currentContext{};

    /// @brief Most recent trap emitted by the VM.
    TrapState lastTrap{};
    TrapToken trapToken{};

  public:
#if VIPER_VM_OPCOUNTS
    /// @brief Per-opcode execution counters (enabled via VIPER_VM_OPCOUNTS).
    /// @note Made public for dispatch strategy access.
    std::array<uint64_t, il::core::kNumOpcodes> opCounts_{};
    /// @brief Runtime toggle for counting.
    bool enableOpcodeCounts = true;
#endif
  private:
    /// @brief Execute function @p fn with optional arguments.
    /// @param fn Function to execute.
    /// @param args Argument slots for the callee's entry block.
    /// @return Return value slot.
    Slot execFunction(const il::core::Function &fn, const std::vector<Slot> &args = {});

    /// @brief Evaluate IL value @p v within frame @p fr.
    /// @param fr Current frame.
    /// @param v Value to evaluate.
    /// @return Result stored in a Slot.
    Slot eval(Frame &fr, const il::core::Value &v);

    /// @brief Initialize a frame for @p fn with arguments.
    /// @param fn Function to execute.
    /// @param args Argument slots for entry block parameters.
    /// @param blocks Map populated with basic block labels.
    /// @param bb Set to point at the entry block.
    Frame setupFrame(const il::core::Function &fn,
                     const std::vector<Slot> &args,
                     BlockMap &blocks,
                     const il::core::BasicBlock *&bb);

    /// @brief Handle pending debug breaks at block entry or instruction boundaries.
    /// @param fr Current frame.
    /// @param bb Current basic block.
    /// @param ip Instruction index within @p bb.
    /// @param skipBreakOnce Internal flag to skip a single break.
    /// @param in Optional instruction for source-line breaks.
    /// @return Slot signalling pause if engaged.
    std::optional<Slot> handleDebugBreak(Frame &fr,
                                         const il::core::BasicBlock &bb,
                                         size_t ip,
                                         bool &skipBreakOnce,
                                         const il::core::Instr *in);

    /// @brief Transfer pending parameters for @p bb into @p fr.
    /// @param fr Frame whose registers receive parameter values.
    /// @param bb Basic block whose parameters are being populated.
    void transferBlockParams(Frame &fr, const il::core::BasicBlock &bb);

    /// @brief Execute instruction @p in updating control flow state.
    // executeOpcode, setCurrentContext, and clearCurrentContext moved to public section

    /// @brief Format and record trap diagnostics.
    FrameInfo buildFrameInfo(const VmError &error) const;
    std::string recordTrap(const VmError &error, const FrameInfo &frame);

    /// @brief Access active VM instance for thread-local trap reporting.
    static VM *activeInstance();

    bool prepareTrap(VmError &error);
    [[noreturn]] void throwForTrap(ExecState *target);

    // TrapDispatchSignal moved to public section

    /// @brief Active execution stack for trap unwinding.
    std::vector<ExecState *> execStack;

    /// @brief Prepare initial execution state for @p fn.
    /// @param fn Function to execute.
    /// @param args Argument slots for the callee's entry block.
    /// @return Fully initialized execution state.
    ExecState prepareExecution(const il::core::Function &fn, const std::vector<Slot> &args);

    /// @brief Process step limits and debug breakpoints.
    /// @param st Current execution state.
    /// @param in Optional instruction for source breakpoints.
    /// @param postExec True if called after executing an opcode.
    /// @return Slot signalling pause/stop if engaged.
    std::optional<Slot> processDebugControl(ExecState &st,
                                            const il::core::Instr *in,
                                            bool postExec);

    /// @brief Forward to debug control logic for pause decisions.
    std::optional<Slot> shouldPause(ExecState &st, const il::core::Instr *in, bool postExec);

    /// @brief Execute a single interpreter step.
    std::optional<Slot> stepOnce(ExecState &st);

    /// @brief Handle a trap dispatch signal raised during interpretation.
    bool handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &st);

    // Dispatch methods moved to public section

    /// @brief Fetch the next opcode for switch-based dispatch.
    il::core::Opcode fetchOpcode(ExecState &st);

    /// @brief Process the result of an inline opcode handler.
    void handleInlineResult(ExecState &st, const ExecResult &exec);

    /// @brief Trap when no handler implementation exists for @p op.
    [[noreturn]] void trapUnimplemented(il::core::Opcode op);

    /// @brief Run the main interpreter loop.
    /// @param st Prepared execution state.
    /// @return Return value slot.
    Slot runFunctionLoop(ExecState &st);

    // =========================================================================
    // Dispatch Handler Declarations (Generated)
    // =========================================================================
    // These includes expand to one handler declaration per opcode defined in
    // il/core/Opcode.def. The inline handlers (inline_handle_*) and switch
    // dispatch (dispatchOpcodeSwitch) are the core dispatch entry points.
    //
    // Generated files - do not edit. See docs/generated-files.md.
    // =========================================================================
#include "vm/ops/generated/InlineHandlersDecl.inc"
#include "vm/ops/generated/SwitchDispatchDecl.inc"

  public:
    /// @brief Return executed instruction count.
    uint64_t getInstrCount() const;

    /// @brief Retrieve the formatted diagnostic for the most recent unhandled trap.
    /// @return Trap message when a trap terminated execution, or `std::nullopt` otherwise.
    std::optional<std::string> lastTrapMessage() const;

    /// @brief Clear stale trap state before a new execution.
    /// @details Resets lastTrap and trapToken so subsequent executions start
    ///          with a clean slate. Call this before run() if you need to
    ///          distinguish between "no trap occurred" and "previous trap exists."
    void clearTrapState();

    /// @brief Emit a tail-call debug/trace event.
    void onTailCall(const il::core::Function *from, const il::core::Function *to);

#if VIPER_VM_OPCOUNTS
    /// @brief Access per-opcode execution counters.
    const std::array<uint64_t, il::core::kNumOpcodes> &opcodeCounts() const;
    /// @brief Reset all opcode execution counters to zero.
    void resetOpcodeCounts();
    /// @brief Return top-N opcodes by execution count as (opcode index, count).
    std::vector<std::pair<int, uint64_t>> topOpcodes(std::size_t n) const;
#endif

    // Polling configuration stored on VM and propagated to new ExecStates
    uint32_t pollEveryN_ = 0;
    std::function<bool(VM &)> pollCallback_{};

    //===------------------------------------------------------------------===//
    // Per-VM Extern Registry
    //===------------------------------------------------------------------===//

    /// @brief Optional per-VM extern registry for isolated function resolution.
    /// @details When non-null, extern calls are first resolved against this
    ///          registry before falling back to the process-global registry.
    ///          This enables multi-tenant scenarios where different VMs can
    ///          have different sets of host-provided functions.
    /// @ownership Non-owning pointer; the registry must outlive the VM.
    ///            Typically the embedder owns the registry via ExternRegistryPtr.
    /// @invariant May only be modified when the VM is not executing.
    ExternRegistry *externRegistry_ = nullptr;

  public:
    /// @brief Assign a per-VM extern registry for isolated function resolution.
    /// @param registry Pointer to the registry, or nullptr to use only global.
    /// @details The registry must outlive the VM. Typically created via
    ///          createExternRegistry() and owned by the embedder.
    /// @note Thread safety: Call only when the VM is not executing.
    void setExternRegistry(ExternRegistry *registry) noexcept
    {
        externRegistry_ = registry;
    }

    /// @brief Retrieve the per-VM extern registry, if any.
    /// @return Pointer to the assigned registry, or nullptr if using global only.
    [[nodiscard]] ExternRegistry *externRegistry() const noexcept
    {
        return externRegistry_;
    }
};

} // namespace il::vm
