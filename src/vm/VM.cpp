// File: src/vm/VM.cpp
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: None.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-spec.md

#include "vm/VM.hpp"
#include "VM/DebugScript.h"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <utility>

using namespace il::core;

namespace il::vm
{

/// Construct a VM instance bound to a specific IL @p Module.
/// The constructor wires the tracing and debugging subsystems and pre-populates
/// lookup tables for functions and runtime strings.
///
/// @param m   Module containing code and globals to execute. It must outlive the VM.
/// @param tc  Trace configuration used to initialise the @c TraceSink. The contained
///            source manager is passed to the debug controller so source locations
///            can be reported in breaks.
/// @param ms  Optional step limit; execution aborts after this many instructions
///            have been retired. A value of @c 0 disables the limit.
/// @param dbg Initial debugger control block describing active breakpoints and
///            stepping behaviour.
/// @param script Optional scripted debugger interaction. When provided, scripted
///            actions drive how pauses are handled; otherwise breaks cause the VM
///            to return a fixed slot.
VM::VM(const Module &m, TraceConfig tc, uint64_t ms, DebugCtrl dbg, DebugScript *script)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms)
{
    debug.setSourceManager(tc.sm);
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = rt_const_cstr(g.init.c_str());
}

/// Locate and execute the module's @c main function.
///
/// The entry point is looked up by name in the cached function map and then
/// executed via @c execFunction. Any tracing or debugging configured on the VM
/// applies to the entire run.
///
/// @returns Signed 64-bit exit code produced by the program's @c main function.
int64_t VM::run()
{
    auto it = fnMap.find("main");
    assert(it != fnMap.end());
    return execFunction(*it->second, {}).i64;
}

/// Materialise an IL @p Value into a runtime @c Slot within a given frame.
///
/// The evaluator reads temporaries from the frame's register file and converts
/// constant operands into the appropriate slot representation. Global addresses
/// and constant strings are resolved through the VM's string pool, which bridges
/// to the runtime's string handling.
///
/// @param fr Active call frame supplying registers and pending parameters.
/// @param v  IL value to evaluate.
/// @returns A @c Slot containing the realised value; when the value is unknown,
///          a default-initialised slot is returned.
Slot VM::eval(Frame &fr, const Value &v)
{
    Slot s{};
    switch (v.kind)
    {
        case Value::Kind::Temp:
            if (v.id < fr.regs.size())
                return fr.regs[v.id];
            return s;
        case Value::Kind::ConstInt:
            s.i64 = v.i64;
            return s;
        case Value::Kind::ConstFloat:
            s.f64 = v.f64;
            return s;
        case Value::Kind::ConstStr:
            s.str = rt_const_cstr(v.str.c_str());
            return s;
        case Value::Kind::GlobalAddr:
        {
            auto it = strMap.find(v.str);
            if (it == strMap.end())
                RuntimeBridge::trap("unknown global", {}, fr.func->name, "");
            else
                s.str = it->second;
            return s;
        }
        case Value::Kind::NullPtr:
            s.ptr = nullptr;
            return s;
    }
    return s;
}

/// Initialise a fresh @c Frame for executing function @p fn.
///
/// Populates a basic-block lookup table, selects the entry block and seeds the
/// register file and any entry parameters. This prepares state for the main
/// interpreter loop without performing any tracing.
///
/// @param fn     Function to execute.
/// @param args   Argument slots for the function's entry block.
/// @param blocks Output mapping from block labels to blocks for fast branch resolution.
/// @param bb     Set to the entry basic block of @p fn.
/// @return Fully initialised frame ready to run.
Frame VM::setupFrame(const Function &fn,
                     const std::vector<Slot> &args,
                     std::unordered_map<std::string, const BasicBlock *> &blocks,
                     const BasicBlock *&bb)
{
    Frame fr;
    fr.func = &fn;
    // Pre-size register file to the function's SSA value count. This mirrors
    // the number of temporaries and parameters required by @p fn and avoids
    // incremental growth during execution.
    fr.regs.resize(fn.valueNames.size());
    assert(fr.regs.size() == fn.valueNames.size());
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
            fr.params[params[i].id] = args[i];
    }
    return fr;
}

/// Manage a potential debug break before or after executing an instruction.
///
/// Checks label and source line breakpoints using @c DebugCtrl. When a break is
/// hit the optional @c DebugScript controls stepping; otherwise a fixed slot is
/// returned to pause execution. Pending block parameter transfers are also
/// applied here when entering a new block.
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
        for (const auto &p : bb.params)
        {
            auto it = fr.params.find(p.id);
            if (it != fr.params.end())
            {
                if (fr.regs.size() <= p.id)
                    fr.regs.resize(p.id + 1);
                fr.regs[p.id] = it->second;
                debug.onStore(p.name,
                              p.type.kind,
                              fr.regs[p.id].i64,
                              fr.regs[p.id].f64,
                              fr.func->name,
                              bb.label,
                              0);
            }
        }
        fr.params.clear();
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

/// Dispatch and execute a single IL instruction.
///
/// A handler is selected from a static table based on the opcode and invoked to
/// perform the operation. Handlers such as @c handleCall and @c handleTrap
/// communicate with the runtime bridge for foreign function calls or traps.
///
/// @param fr     Current frame.
/// @param in     Instruction to execute.
/// @param blocks Mapping of block labels used for branch resolution.
/// @param bb     [in,out] Updated to the current basic block after any branch.
/// @param ip     [in,out] Instruction index within @p bb.
/// @return Execution result capturing control-flow effects and return value.
VM::ExecResult VM::executeOpcode(Frame &fr,
                                 const Instr &in,
                                 const std::unordered_map<std::string, const BasicBlock *> &blocks,
                                 const BasicBlock *&bb,
                                 size_t &ip)
{
    const auto &table = getOpcodeHandlers();
    OpcodeHandler handler = table[static_cast<size_t>(in.op)];
    if (!handler)
    {
        assert(false && "unimplemented opcode");
        return {};
    }
    return handler(*this, fr, in, blocks, bb, ip);
}

/// Create an initial execution state for running @p fn.
///
/// This sets up the frame and block map via @c setupFrame, resets debugging
/// state, and initialises the instruction pointer and stepping flags.
///
/// @param fn   Function to execute.
/// @param args Arguments passed to the function's entry block.
/// @return Fully initialised execution state ready for the interpreter loop.
VM::ExecState VM::prepareExecution(const Function &fn, const std::vector<Slot> &args)
{
    ExecState st;
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    return st;
}

/// Handle debugging-related bookkeeping before or after an instruction executes.
///
/// Enforces the global step limit, performs breakpoint checks via
/// @c handleDebugBreak, and manages the single-step budget. When a pause is
/// requested, a special slot is returned to signal the interpreter loop.
///
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
            s.i64 = 1;
            return s;
        }
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

/// Main interpreter loop for executing a function.
///
/// Iterates over instructions in the current basic block, invokes tracing,
/// dispatches opcodes via @c executeOpcode, and checks debug controls before and
/// after each step. The loop exits when a return is executed or a debug pause is
/// requested.
///
/// @param st Mutable execution state containing frame and control flow info.
/// @return Return value of the function or special slot from debug control.
Slot VM::runFunctionLoop(ExecState &st)
{
    while (st.bb && st.ip < st.bb->instructions.size())
    {
        const Instr &in = st.bb->instructions[st.ip];
        if (auto br = processDebugControl(st, &in, false))
            return *br;
        tracer.onStep(in, st.fr);
        ++instrCount;
        auto res = executeOpcode(st.fr, in, st.blocks, st.bb, st.ip);
        if (res.returned)
            return res.value;
        if (res.jumped)
            debug.resetLastHit();
        else
            ++st.ip;
        if (auto br = processDebugControl(st, nullptr, true))
            return *br;
    }
    Slot s{};
    s.i64 = 0;
    return s;
}

/// Execute function @p fn with optional arguments.
///
/// Prepares an execution state, then runs the interpreter loop. The callee's
/// execution participates fully in tracing, debugging, and runtime bridge
/// interactions triggered through individual instructions.
///
/// @param fn   Function to execute.
/// @param args Argument slots passed to the entry block parameters.
/// @return Slot containing the function's return value.
Slot VM::execFunction(const Function &fn, const std::vector<Slot> &args)
{
    auto st = prepareExecution(fn, args);
    return runFunctionLoop(st);
}

} // namespace il::vm
