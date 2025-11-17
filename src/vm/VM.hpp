// File: src/vm/VM.hpp
// Purpose: Declares stack-based virtual machine executing IL and caching runtime
//          resources.
// Key invariants: Inline string literal cache owns one handle per literal.
// Ownership/Lifetime: VM does not own module or runtime bridge.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"
#include "rt.hpp"
#include "support/source_location.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/VMConfig.hpp"
#include "vm/control_flow.hpp"
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::vm
{

class Runner; // fwd for friend declaration of Runner::Impl

class VMContext;

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
} // namespace detail

/// @brief Scripted debug actions.
class DebugScript;

/// @brief Runtime slot capable of holding IL values.
/// @invariant Only one member is valid based on value type.
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
};

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
    ///          1KB is sufficient for most BASIC programs which use stack
    ///          for temporary string operations and small local arrays.
    static constexpr size_t kDefaultStackSize = 1024;

    /// @brief Operand stack storage.
    /// @ownership Owned by the frame; fixed capacity.
    std::array<uint8_t, kDefaultStackSize> stack;

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

/// @brief Simple interpreter for the IL.
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
    friend struct detail::VMAccess; ///< Allow opcode handlers to access internals via helper
    friend class detail::FnTableDispatchDriver;
    friend class detail::SwitchDispatchDriver;
#if VIPER_THREADING_SUPPORTED
    friend class detail::ThreadedDispatchDriver;
#endif
    friend struct detail::ops::OperandDispatcher; ///< Allow shared helpers to evaluate operands
    friend class RuntimeBridge; ///< Runtime bridge accesses trap formatting helpers
    friend void vm_raise(TrapKind kind, int32_t code);
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
    using BlockMap = std::unordered_map<std::string, const il::core::BasicBlock *>;

    /// @brief Create VM for module @p m.
    /// @param m IL module to execute.
    /// @param tc Trace configuration.
    /// @param maxSteps Abort after executing @p maxSteps instructions (0 = unlimited).
    VM(const il::core::Module &m,
       TraceConfig tc = {},
       uint64_t maxSteps = 0,
       DebugCtrl dbg = {},
       DebugScript *script = nullptr);

    /// @brief Release runtime string handles retained by the VM.
    ~VM();

    VM(const VM &) = delete;
    VM &operator=(const VM &) = delete;
    VM(VM &&) noexcept = default;
    VM &operator=(VM &&) = delete;

    /// @brief Execute the module's entry function.
    /// @return Exit code from @c main or `1` when the entry point is missing.
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

    /// @brief Trace output sink.
    /// @ownership Owned by the VM.
    TraceSink tracer;

    /// @brief Breakpoint controller.
    /// @ownership Owned by the VM.
    DebugCtrl debug;

    /// @brief Optional debug command script.
    /// @ownership Non-owning pointer; may be @c nullptr.
    DebugScript *script;

    /// @brief Step limit; @c 0 means unlimited.
    uint64_t maxSteps;

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
    std::unordered_map<std::string, const il::core::Function *> fnMap;

    /// @brief Interned runtime strings.
    /// @ownership Owned by the VM; manages @c rt_string handles for globals.
    std::unordered_map<std::string, rt_string> strMap;

    /// @brief Cached runtime handles for inline string literals containing embedded NULs.
    /// @ownership Owned by the VM; stores @c rt_string handles created via @c rt_string_from_bytes.
    /// @invariant Each literal string maps to at most one active handle, released exactly once
    ///            when the cache is cleared.
    std::unordered_map<std::string, rt_string> inlineLiteralCache;

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

#if VIPER_VM_OPCOUNTS
    /// @brief Per-opcode execution counters (enabled via VIPER_VM_OPCOUNTS).
    std::array<uint64_t, il::core::kNumOpcodes> opCounts_{};
    /// @brief Runtime toggle for counting.
    bool enableOpcodeCounts = true;
#endif

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
                     std::unordered_map<std::string, const il::core::BasicBlock *> &blocks,
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
    /// @param fr Current frame.
    /// @param in Instruction to execute.
    /// @param blocks Block lookup table.
    /// @param bb [in,out] Current basic block.
    /// @param ip [in,out] Instruction pointer within @p bb.
    /// @return Result describing jump or return.
    ExecResult executeOpcode(
        Frame &fr,
        const il::core::Instr &in,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blocks,
        const il::core::BasicBlock *&bb,
        size_t &ip);

    /// @brief Update current trap context for instruction @p in.
    void setCurrentContext(Frame &fr,
                           const il::core::BasicBlock *bb,
                           size_t ip,
                           const il::core::Instr &in);

    /// @brief Clear the active trap context.
    void clearCurrentContext();

    /// @brief Format and record trap diagnostics.
    FrameInfo buildFrameInfo(const VmError &error) const;
    std::string recordTrap(const VmError &error, const FrameInfo &frame);

    /// @brief Access active VM instance for thread-local trap reporting.
    static VM *activeInstance();

    /// @brief Aggregate execution state for a running function.
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
        std::optional<Slot> pendingResult{};  ///< Result staged by threaded interpreter
        const il::core::Instr *currentInstr =
            nullptr;                ///< Instruction under execution for inline dispatch
        bool exitRequested = false; ///< Whether the active loop should exit

        // Host polling configuration/state ----------------------------------
        struct PollConfig
        {
            uint32_t interruptEveryN = 0;
            std::function<bool(VM &)> pollCallback;
        } config; ///< Per-run polling configuration

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
            exitRequested = true;
        }

        /// @brief Cache for resolved branch targets per instruction.
        /// @details Maps an instruction pointer to a vector of resolved
        ///          BasicBlock* targets in the same order as @c in.labels.
        std::unordered_map<const il::core::Instr *, std::vector<const il::core::BasicBlock *>>
            branchTargetCache;
    };

    bool prepareTrap(VmError &error);
    [[noreturn]] void throwForTrap(ExecState *target);

    struct TrapDispatchSignal : std::exception
    {
        explicit TrapDispatchSignal(ExecState *targetState);

        ExecState *target;

        const char *what() const noexcept override;
    };

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

#include "vm/ops/generated/InlineHandlersDecl.inc"
#include "vm/ops/generated/SwitchDispatchDecl.inc"

  public:
    /// @brief Return executed instruction count.
    uint64_t getInstrCount() const;

    /// @brief Retrieve the formatted diagnostic for the most recent unhandled trap.
    /// @return Trap message when a trap terminated execution, or `std::nullopt` otherwise.
    std::optional<std::string> lastTrapMessage() const;

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
};

} // namespace il::vm
