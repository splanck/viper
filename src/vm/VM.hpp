// File: src/vm/VM.hpp
// Purpose: Declares stack-based virtual machine executing IL.
// Key invariants: None.
// Ownership/Lifetime: VM does not own module or runtime bridge.
// Links: docs/il-spec.md
#pragma once

#include "VM/Debug.h"
#include "VM/Trace.h"
#include "il/core/Module.hpp"
#include "rt.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::vm
{

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
    /// @brief Executing function.
    /// @ownership Non-owning; function resides in the module.
    /// @invariant Never null for a valid frame.
    const il::core::Function *func;

    /// @brief Register file for SSA values.
    /// @ownership Owned by the frame; sized to the function's register count.
    std::vector<Slot> regs;

    /// @brief Operand stack storage.
    /// @ownership Owned by the frame; fixed capacity of 1024 bytes.
    std::array<uint8_t, 1024> stack;

    /// @brief Stack pointer in bytes.
    /// @invariant Never exceeds @c stack.size().
    size_t sp = 0;

    /// @brief Pending block parameter values.
    /// @ownership Owned by the frame; entries cleared when parameters applied.
    std::unordered_map<unsigned, Slot> params;
};

/// @brief Simple interpreter for the IL.
class VM
{
  public:
    /// @brief Create VM for module @p m.
    /// @param m IL module to execute.
    /// @param tc Trace configuration.
    /// @param maxSteps Abort after executing @p maxSteps instructions (0 = unlimited).
    VM(const il::core::Module &m,
       TraceConfig tc = {},
       uint64_t maxSteps = 0,
       DebugCtrl dbg = {},
       DebugScript *script = nullptr);

    /// @brief Execute the module's entry function.
    /// @return Exit code from main function.
    int64_t run();

  private:
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

    /// @brief Executed instruction count.
    /// @invariant Monotonically increases during execution.
    uint64_t instrCount = 0;

    /// @brief Remaining instructions to step before pausing.
    uint64_t stepBudget = 0;

    /// @brief Function name lookup table.
    /// @ownership Owned by the VM; values reference functions in @c mod.
    std::unordered_map<std::string, const il::core::Function *> fnMap;

    /// @brief Interned runtime strings.
    /// @ownership Owned by the VM; manages @c rt_string handles.
    std::unordered_map<std::string, rt_string> strMap;

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

    /// @brief Initialize a frame for @p fn with arguments.
    /// @param fn Function to execute.
    /// @param args Argument slots for entry block parameters.
    /// @param blocks Map populated with basic block labels.
    /// @param bb Set to point at the entry block.
    Frame setupFrame(const il::core::Function &fn,
                     const std::vector<Slot> &args,
                     std::unordered_map<std::string, const il::core::BasicBlock *> &blocks,
                     const il::core::BasicBlock *&bb);

    /// @brief Handle pending debug breaks and parameter transfers.
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

    using BlockMap =
        std::unordered_map<std::string, const il::core::BasicBlock *>; ///< Block lookup table

    /// @brief Aggregate execution state for a running function.
    struct ExecState
    {
        Frame fr;                                 ///< Current frame
        BlockMap blocks;                          ///< Basic block lookup
        const il::core::BasicBlock *bb = nullptr; ///< Active basic block
        size_t ip = 0;                            ///< Instruction pointer within @p bb
        bool skipBreakOnce = false;               ///< Whether to skip next breakpoint
    };

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

    /// @brief Run the main interpreter loop.
    /// @param st Prepared execution state.
    /// @return Return value slot.
    Slot runFunctionLoop(ExecState &st);

    /// @brief Handle alloca opcode.
    ExecResult handleAlloca(Frame &fr, const il::core::Instr &in);

    /// @brief Handle load opcode.
    ExecResult handleLoad(Frame &fr, const il::core::Instr &in);

    /// @brief Handle store opcode.
    ExecResult handleStore(Frame &fr,
                           const il::core::Instr &in,
                           const il::core::BasicBlock *bb,
                           size_t ip);

    /// @brief Handle integer addition opcode.
    ExecResult handleAdd(Frame &fr, const il::core::Instr &in);

    /// @brief Handle integer subtraction opcode.
    ExecResult handleSub(Frame &fr, const il::core::Instr &in);

    /// @brief Handle integer multiplication opcode.
    ExecResult handleMul(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating add opcode.
    ExecResult handleFAdd(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating subtract opcode.
    ExecResult handleFSub(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating multiply opcode.
    ExecResult handleFMul(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating divide opcode.
    ExecResult handleFDiv(Frame &fr, const il::core::Instr &in);

    /// @brief Handle bitwise xor opcode.
    ExecResult handleXor(Frame &fr, const il::core::Instr &in);

    /// @brief Handle shift-left opcode.
    ExecResult handleShl(Frame &fr, const il::core::Instr &in);

    /// @brief Handle pointer arithmetic opcode.
    ExecResult handleGEP(Frame &fr, const il::core::Instr &in);

    /// @brief Handle integer equality comparison opcode.
    ExecResult handleICmpEq(Frame &fr, const il::core::Instr &in);

    /// @brief Handle integer inequality comparison opcode.
    ExecResult handleICmpNe(Frame &fr, const il::core::Instr &in);

    /// @brief Handle signed greater-than comparison opcode.
    ExecResult handleSCmpGT(Frame &fr, const il::core::Instr &in);

    /// @brief Handle signed less-than comparison opcode.
    ExecResult handleSCmpLT(Frame &fr, const il::core::Instr &in);

    /// @brief Handle signed less-equal comparison opcode.
    ExecResult handleSCmpLE(Frame &fr, const il::core::Instr &in);

    /// @brief Handle signed greater-equal comparison opcode.
    ExecResult handleSCmpGE(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating equality comparison opcode.
    ExecResult handleFCmpEQ(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating inequality comparison opcode.
    ExecResult handleFCmpNE(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating greater-than comparison opcode.
    ExecResult handleFCmpGT(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating less-than comparison opcode.
    ExecResult handleFCmpLT(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating less-equal comparison opcode.
    ExecResult handleFCmpLE(Frame &fr, const il::core::Instr &in);

    /// @brief Handle floating greater-equal comparison opcode.
    ExecResult handleFCmpGE(Frame &fr, const il::core::Instr &in);

    /// @brief Handle unconditional branch opcode.
    ExecResult handleBr(Frame &fr,
                        const il::core::Instr &in,
                        const BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
                        size_t &ip);

    /// @brief Handle conditional branch opcode.
    ExecResult handleCBr(Frame &fr,
                         const il::core::Instr &in,
                         const BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

    /// @brief Handle return opcode.
    ExecResult handleRet(Frame &fr, const il::core::Instr &in);

    /// @brief Handle const string opcode.
    ExecResult handleConstStr(Frame &fr, const il::core::Instr &in);

    /// @brief Handle call opcode.
    ExecResult handleCall(Frame &fr, const il::core::Instr &in, const il::core::BasicBlock *bb);

    /// @brief Handle integer-to-float cast opcode.
    ExecResult handleSitofp(Frame &fr, const il::core::Instr &in);

    /// @brief Handle float-to-integer cast opcode.
    ExecResult handleFptosi(Frame &fr, const il::core::Instr &in);

    /// @brief Handle truncation or zero-extension opcode.
    ExecResult handleTruncOrZext1(Frame &fr, const il::core::Instr &in);

    /// @brief Handle trap opcode.
    ExecResult handleTrap(Frame &fr, const il::core::Instr &in, const il::core::BasicBlock *bb);

  public:
    /// @brief Return executed instruction count.
    uint64_t getInstrCount() const
    {
        return instrCount;
    }
};

} // namespace il::vm
