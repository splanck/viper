//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the debugging utilities responsible for breakpoint handling,
// stepping, and block parameter propagation.  These helpers keep the
// interpreter loop focused on opcode dispatch while consolidating debug
// bookkeeping in a single translation unit.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the IL VM's debugger integration helpers.
/// @details The routines here coordinate between the interpreter core, the
///          debug controller, and optional scripting front-ends.  They transfer
///          staged block parameters, evaluate breakpoints, honour step budgets,
///          and surface rich diagnostics describing why execution pauses.
///          Centralising the logic keeps the dispatch loop uncluttered and
///          ensures all debug pathways apply consistent invariants when
///          manipulating VM state.

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/VM.hpp"
#include "vm/VMConstants.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace il::core;

namespace il::vm
{

/// @brief Apply pending block parameter transfers for the given block.
/// @details Any arguments staged by a predecessor terminator are copied into the
///          frame's register file and announced to the debug controller.  The
///          routine grows the register vector when necessary, materialises a
///          pseudo-instruction so existing store helpers can marshal the value,
///          and releases transient string handles once consumed.  Parameters are
///          cleared after transfer so repeated calls are harmless when no
///          updates remain.
/// @param fr Current frame whose registers receive parameter values.
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
        debug.onStore(
            p.name, p.type.kind, fr.regs[p.id].i64, fr.regs[p.id].f64, fr.func->name, bb.label, 0);
        if (p.type.kind == Type::Kind::Str)
            rt_str_release_maybe(pending->str);
        pending.reset();
    }
}

/// @brief Manage a potential debug break before or after executing an instruction.
/// @details The helper first considers block-level breakpoints, honouring the
///          single-step skip flag by deferring only the next break opportunity.
///          When a break triggers and no script is present, a synthetic slot is
///          returned to suspend the interpreter; otherwise the current debug
///          script dictates how many instructions to step before resuming.
///          Source line breakpoints are processed when @p in is non-null so the
///          debugger can halt on specific instructions even when control stays
///          within the same block.
/// @param fr            Current frame.
/// @param bb            Current basic block.
/// @param ip            Instruction index within @p bb.
/// @param skipBreakOnce Internal flag used to skip a single break when stepping.
/// @param in            Optional instruction for source line breakpoints.
/// @return @c std::nullopt to continue or a @c Slot signalling a pause.
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
                s.i64 = kDebugBreakpointSentinel;
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
        if (sm && in->loc.hasFile())
            path = std::filesystem::path(sm->getPath(in->loc.file_id)).filename().string();
        std::cerr << "[BREAK] src=" << path;
        if (in->loc.hasLine())
        {
            std::cerr << ':' << in->loc.line;
            if (in->loc.hasColumn())
                std::cerr << ':' << in->loc.column;
        }
        std::cerr << " fn=@" << fr.func->name << " blk=" << bb.label << " ip=#" << ip << "\n";
        Slot s{};
        s.i64 = kDebugBreakpointSentinel;
        return s;
    }
    return std::nullopt;
}

/// @brief Handle debugging-related bookkeeping before or after an instruction executes.
/// @details Prior to execution the helper enforces the global step limit,
///          performs parameter transfers when entering a block, and consults
///          @ref handleDebugBreak to honour label and source breakpoints.  After
///          execution it decrements the remaining step budget, halting when it
///          reaches zero and optionally re-arming the debugger script.  Whenever
///          a pause is requested a dedicated slot value is returned so the
///          interpreter loop can unwind gracefully.
/// @param st      Current execution state.
/// @param in      Instruction being processed, if any.
/// @param postExec Set to true when invoked after executing @p in.
/// @return Optional slot causing execution to pause; @c std::nullopt otherwise.
std::optional<Slot> VM::processDebugControl(ExecState &st, const Instr *in, bool postExec)
{
    if (!postExec)
    {
        if (maxSteps && instrCount >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = kDebugPauseSentinel;
            return s;
        }
        if (st.ip == 0 && st.bb)
            transferBlockParams(st.fr, *st.bb);
        if (st.ip == 0 && stepBudget == 0 && !st.skipBreakOnce)
        {
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, nullptr))
                return br;
        }
        else if (st.skipBreakOnce)
        {
            st.skipBreakOnce = false;
        }
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
                s.i64 = kDebugBreakpointSentinel;
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
