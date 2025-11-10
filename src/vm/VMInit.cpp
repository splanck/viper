//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the routines that construct VM instances and prepare execution
// state.  This includes wiring up debug/tracing facilities, populating lookup
// tables for functions and globals, and initialising frames prior to running a
// function.
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/control_flow.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

using namespace il::core;

namespace il::vm
{

namespace
{

/// @brief RAII helper that forces the process-wide numeric locale to "C".
/// @details The VM relies on deterministic decimal formatting for diagnostics
///          and trace output.  Constructing this static initializer once per
///          process sets the locale early so subsequent numeric prints remain
///          stable regardless of the host environment.
struct NumericLocaleInitializer
{
    NumericLocaleInitializer()
    {
        std::setlocale(LC_NUMERIC, "C");
    }
};

[[maybe_unused]] const NumericLocaleInitializer kNumericLocaleInitializer{};

/// @brief Check the environment to determine whether verbose VM logging is enabled.
///
/// Reads the VIPER_DEBUG_VM flag once and caches the result so subsequent calls
/// remain cheap.
///
/// @return @c true when debugging output should be emitted.
bool isVmDebugLoggingEnabled()
{
    static const bool enabled = []
    {
        if (const char *flag = std::getenv("VIPER_DEBUG_VM"))
            return flag[0] != '\0';
        return false;
    }();
    return enabled;
}

/// @brief Translate a dispatch strategy into a printable label.
///
/// @param kind Dispatch strategy currently active.
/// @return Human-readable dispatch name used in debug logs.
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

/// @brief Construct a VM instance bound to a specific IL module.
///
/// The constructor wires tracing/debug facilities, honours environment variables
/// controlling dispatch strategy selection, caches function/global lookups, and
/// records debugger configuration so future executions can evaluate breakpoints.
///
/// @param m   Module containing code and globals to execute. It must outlive the VM.
/// @param tc  Trace configuration used to initialise the @c TraceSink.
/// @param ms  Optional step limit; @c 0 disables the limit.
/// @param dbg Initial debugger control block describing active breakpoints.
/// @param script Optional scripted debugger interaction controller.
VM::VM(const Module &m, TraceConfig tc, uint64_t ms, DebugCtrl dbg, DebugScript *script)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms)
{
    const char *switchModeEnv = std::getenv("VIPER_SWITCH_MODE");
    viper::vm::SwitchMode mode = viper::vm::SwitchMode::Auto;
    if (switchModeEnv != nullptr)
    {
        std::string rawMode{switchModeEnv};
        std::transform(rawMode.begin(),
                       rawMode.end(),
                       rawMode.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

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
        std::transform(rawDispatch.begin(),
                       rawDispatch.end(),
                       rawDispatch.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

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
        strMap[g.name] = toViperString(g.init, AssumeNullTerminated::Yes);
}

/// @brief Initialise a fresh frame for executing @p fn.
///
/// Prepares the block lookup map, seeds parameter slots, verifies the argument
/// count, and selects the entry block so the interpreter loop can begin without
/// additional bookkeeping.
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
    if (isVmDebugLoggingEnabled())
    {
        std::fprintf(stderr,
                     "[SETUP] fn=%s valueNames=%zu params=%zu blocks=%zu\n",
                     fn.name.c_str(),
                     fn.valueNames.size(),
                     fn.params.size(),
                     fn.blocks.size());
    }
    size_t maxParamId = 0;
    for (const auto &p : fn.params)
        maxParamId = std::max(maxParamId, static_cast<size_t>(p.id));
    if (isVmDebugLoggingEnabled())
    {
        std::fprintf(stderr, "[SETUP] maxParamId=%zu\n", maxParamId);
        std::fflush(stderr);
    }
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
               << params.size() << " argument" << (params.size() == 1 ? "" : "s") << ", received "
               << args.size();
            RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, fn.name, bb->label);
        }
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
        {
            const auto id = params[i].id;
            assert(id < fr.params.size());
            const bool isStringParam = params[i].type.kind == Type::Kind::Str;
            if (isStringParam)
            {
                auto &dest = fr.params[id];
                if (dest)
                    rt_str_release_maybe(dest->str);

                Slot retained = args[i];
                rt_str_retain_maybe(retained.str);
                dest = retained;
                continue;
            }

            fr.params[id] = args[i];
        }
    }
    return fr;
}

/// @brief Create an initial execution state for running @p fn.
///
/// Builds the frame, primes the tracer, clears debug bookkeeping, and resets the
/// interpreter's instruction pointer.
///
/// @param fn   Function to execute.
/// @param args Arguments passed to the function's entry block.
/// @return Fully initialised execution state ready for the interpreter loop.
VM::ExecState VM::prepareExecution(const Function &fn, const std::vector<Slot> &args)
{
    ExecState st{};
    st.owner = this;
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    // Inherit polling configuration from VM.
    st.config.interruptEveryN = pollEveryN_;
    st.config.pollCallback = pollCallback_;
    tracer.onFramePrepared(st.fr);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    st.switchCache.clear();
    return st;
}

} // namespace il::vm
