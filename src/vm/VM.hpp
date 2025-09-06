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
    int64_t i64; ///< Signed integer value
    double f64;  ///< Floating-point value
    void *ptr;   ///< Generic pointer
    rt_str str;  ///< Runtime string handle
};

/// @brief Call frame storing registers and operand stack.
/// @invariant Stack pointer @c sp never exceeds @c stack size.
struct Frame
{
    const il::core::Function *func;            ///< Executing function
    std::vector<Slot> regs;                    ///< Register file
    std::array<uint8_t, 1024> stack;           ///< Operand stack storage
    size_t sp = 0;                             ///< Stack pointer in bytes
    std::unordered_map<unsigned, Slot> params; ///< Pending block parameter values
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
    const il::core::Module &mod; ///< Module to execute
    TraceSink tracer;            ///< Trace output sink
    DebugCtrl debug;             ///< Breakpoint controller
    DebugScript *script;         ///< Optional debug command script
    uint64_t maxSteps;           ///< Step limit; 0 means unlimited
    uint64_t instrCount = 0;     ///< Executed instruction count
    uint64_t stepBudget = 0;     ///< Remaining instructions to step before pausing
    std::unordered_map<std::string, const il::core::Function *> fnMap; ///< Name lookup
    std::unordered_map<std::string, rt_str> strMap;                    ///< String pool

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
        bool jumped = false;   ///< Whether control transferred to another block.
        bool returned = false; ///< Whether execution returned from the function.
        Slot value{};          ///< Return value if @c returned is true.
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

  public:
    /// @brief Return executed instruction count.
    uint64_t getInstrCount() const
    {
        return instrCount;
    }
};

} // namespace il::vm
