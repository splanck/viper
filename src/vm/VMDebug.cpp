//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the VM's interactive debugging surface.  The helpers in this file
// coordinate breakpoint checks, scripted debugging sessions, and step limits so
// that the interpreter can pause execution deterministically while keeping the
// interpreter core free of debugger-specific branching.
//
//===----------------------------------------------------------------------===//

#include "vm/VM.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "vm/DebugScript.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace il::core;

namespace il::vm
{

/// @brief Materialise pending block parameters into the active frame.
///
/// When a predecessor branches into a block it stages parameter slots that
/// mirror PHI semantics.  The transfer function copies those slots into the
/// frame's register file, reports the stores to the debugger so watchpoints can
/// fire, releases temporary string handles, and clears the staging area to
/// avoid double-application on re-entry.
///
/// @param fr Frame receiving the parameter values.
/// @param bb Basic block that has just become active.
void VM::transferBlockParams(Frame &fr, const BasicBlock &bb)
{
    for (const auto &p : bb.params)
    {
        assert(p.id < fr.params.size());
        auto &pending = fr.params[p.id];
        if (!pending)
            continue;
        if (fr.regs.size() <= p.id)
            fr.regs.resize(p.id + 1);

        Instr pseudo;
        pseudo.result = p.id;
        pseudo.type = p.type;
        detail::ops::storeResult(fr, pseudo, *pending);
        debug.onStore(p.name,
                      p.type.kind,
                      fr.regs[p.id].i64,
                      fr.regs[p.id].f64,
                      fr.func->name,
                      bb.label,
                      0);
        if (p.type.kind == Type::Kind::Str)
            rt_str_release_maybe(pending->str);
        pending.reset();
    }
}

/// @brief Check whether execution should pause for a breakpoint.
///
/// The debugger can request breaks either by block label or by source line.  In
/// label mode the handler optionally consults a @ref DebugScript to decide
/// whether to step or continue.  Source line breaks bypass scripting and always
/// return a sentinel slot that instructs the interpreter loop to pause.
///
/// @param fr            Current frame.
/// @param bb            Current basic block.
/// @param ip            Instruction index within @p bb.
/// @param skipBreakOnce Internal flag used to skip a single break when stepping.
/// @param in            Optional instruction for source line breakpoints.
/// @return Sentinel slot requesting a pause, or std::nullopt to continue.
std::optional<Slot> VM::handleDebugBreak(
    Frame &fr, const BasicBlock &bb, size_t ip, bool &skipBreakOnce, const Instr *in)
{
    if (!in)
    {
        if (debug.shouldBreak(bb))
        {
            std::cerr << "[BREAK] fn=@" << fr.func->name << " blk=" << bb.label
                      << " reason=label\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            skipBreakOnce = true;
        }
        return std::nullopt;
    }
    if (debug.hasSrcLineBPs() && debug.shouldBreakOn(*in))
    {
        const auto *sm = debug.getSourceManager();
        std::string path;
        if (sm && in->loc.isValid())
            path = std::filesystem::path(sm->getPath(in->loc.file_id)).filename().string();
        std::cerr << "[BREAK] src=" << path << ':' << in->loc.line << " fn=@" << fr.func->name
                  << " blk=" << bb.label << " ip=#" << ip << "\n";
        Slot s{};
        s.i64 = 10;
        return s;
    }
    return std::nullopt;
}

/// @brief Execute debugger bookkeeping around instruction dispatch.
///
/// The interpreter calls this helper both before and after executing an
/// instruction.  Pre-execution the routine enforces the global step limit,
/// applies pending block parameters, and consults @ref handleDebugBreak for
/// label or source breaks.  Post-execution it decrements the active step budget
/// and triggers a pause when the budget reaches zero.
///
/// @param st      Current execution state.
/// @param in      Instruction being processed, if any.
/// @param postExec Set to true when invoked after executing @p in.
/// @return Optional slot causing execution to pause; std::nullopt otherwise.
std::optional<Slot> VM::processDebugControl(ExecState &st, const Instr *in, bool postExec)
{
    if (!postExec)
    {
        if (maxSteps && instrCount >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = 1;
            return s;
        }
        if (st.ip == 0 && st.bb)
            transferBlockParams(st.fr, *st.bb);
        if (st.ip == 0 && stepBudget == 0 && !st.skipBreakOnce)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, nullptr))
                return br;
        st.skipBreakOnce = false;
        if (in)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, in))
                return br;
        return std::nullopt;
    }
    if (stepBudget > 0)
    {
        --stepBudget;
        if (stepBudget == 0)
        {
            std::cerr << "[BREAK] fn=@" << st.fr.func->name << " blk=" << st.bb->label
                      << " reason=step\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            st.skipBreakOnce = true;
        }
    }
    return std::nullopt;
}

} // namespace il::vm

