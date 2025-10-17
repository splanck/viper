//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the debugging helpers used by the VM to manage breakpoints,
// watches, and scripted stepping.  The routines centralise the interaction
// between execution state, the DebugCtrl facade, and optional DebugScript
// automation so all dispatch strategies observe the same debugging semantics.
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

/// @brief Apply pending block parameter transfers for the given block.
///
/// Any arguments staged by a predecessor terminator are copied into the frame's
/// register file and announced to the debug controller. Parameters are cleared
/// after transfer so repeated calls are harmless when no updates remain.
///
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

/// @brief Manage a potential debug break before or after executing an
///        instruction.
///
/// Checks label and source line breakpoints using @c DebugCtrl. When a break is
/// hit the optional @c DebugScript controls stepping; otherwise a fixed slot is
/// returned to pause execution.
///
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

/// @brief Handle debugging-related bookkeeping before or after an instruction
///        executes.
///
/// Enforces the global step limit, performs breakpoint checks via
/// @ref handleDebugBreak, and manages the single-step budget. When a pause is
/// requested, a special slot is returned to signal the interpreter loop.
///
/// @param st       Current execution state.
/// @param in       Instruction being processed, if any.
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

