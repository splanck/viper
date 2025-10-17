// File: src/vm/VMInit.cpp
// Purpose: Implements VM construction and execution state preparation routines.
// Key invariants: Ensures frames and execution state are initialised consistently.
// Ownership/Lifetime: VM retains references to module functions and runtime strings.
// Links: docs/il-guide.md#reference

#include "vm/VM.hpp"
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/control_flow.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <clocale>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <sstream>
#include <utility>
#include <string>

using namespace il::core;

namespace il::vm
{

namespace
{

struct NumericLocaleInitializer
{
    NumericLocaleInitializer()
    {
        std::setlocale(LC_NUMERIC, "C");
    }
};

[[maybe_unused]] const NumericLocaleInitializer kNumericLocaleInitializer{};

bool isVmDebugLoggingEnabled()
{
    static const bool enabled = [] {
        if (const char *flag = std::getenv("VIPER_DEBUG_VM"))
            return flag[0] != '\0';
        return false;
    }();
    return enabled;
}

const char *dispatchKindName(VM::DispatchKind kind)
{
    switch (kind)
    {
        case VM::DispatchKind::FnTable:
            return "FnTable";
        case VM::DispatchKind::Switch:
            return "Switch";
        case VM::DispatchKind::Threaded:
            return "Threaded";
    }
    return "Unknown";
}

} // namespace

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
    const char *switchModeEnv = std::getenv("VIPER_SWITCH_MODE");
    viper::vm::SwitchMode mode = viper::vm::SwitchMode::Auto;
    if (switchModeEnv != nullptr)
    {
        std::string rawMode{switchModeEnv};
        std::transform(rawMode.begin(), rawMode.end(), rawMode.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (rawMode == "dense")
            mode = viper::vm::SwitchMode::Dense;
        else if (rawMode == "sorted")
            mode = viper::vm::SwitchMode::Sorted;
        else if (rawMode == "hashed")
            mode = viper::vm::SwitchMode::Hashed;
        else if (rawMode == "linear")
            mode = viper::vm::SwitchMode::Linear;
        else
            mode = viper::vm::SwitchMode::Auto;
    }
    viper::vm::setSwitchMode(mode);

    DispatchKind defaultDispatch = DispatchKind::Switch;
#if VIPER_THREADING_SUPPORTED
    defaultDispatch = DispatchKind::Threaded;
#endif
    DispatchKind selectedDispatch = defaultDispatch;

    if (const char *dispatchEnv = std::getenv("VIPER_DISPATCH"))
    {
        std::string rawDispatch{dispatchEnv};
        std::transform(rawDispatch.begin(), rawDispatch.end(), rawDispatch.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (rawDispatch == "table")
            selectedDispatch = DispatchKind::FnTable;
        else if (rawDispatch == "switch")
            selectedDispatch = DispatchKind::Switch;
        else if (rawDispatch == "threaded")
        {
#if VIPER_THREADING_SUPPORTED
            selectedDispatch = DispatchKind::Threaded;
#else
            selectedDispatch = DispatchKind::Switch;
#endif
        }
    }

    dispatchKind = selectedDispatch;

    if (isVmDebugLoggingEnabled())
    {
        std::fprintf(stderr, "[DEBUG][VM] dispatch kind: %s\n", dispatchKindName(dispatchKind));
    }

    debug.setSourceManager(tc.sm);
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = toViperString(g.init);
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
    fr.params.assign(fr.regs.size(), std::nullopt);
    fr.ehStack.clear();
    fr.activeError = {};
    fr.resumeState = {};
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        if (args.size() != params.size())
        {
            std::ostringstream os;
            os << "argument count mismatch for function " << fn.name << ": expected "
               << params.size() << " argument" << (params.size() == 1 ? "" : "s")
               << ", received " << args.size();
            RuntimeBridge::trap(
                TrapKind::InvalidOperation, os.str(), {}, fn.name, bb->label);
        }
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
        {
            const auto id = params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = args[i];
        }
    }
    return fr;
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
    ExecState st{};
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    tracer.onFramePrepared(st.fr);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    st.switchCache.clear();
    return st;
}

} // namespace il::vm

