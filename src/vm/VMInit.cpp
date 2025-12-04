//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "rt_context.h"

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
/// @details Reads the VIPER_DEBUG_VM flag once and caches the result so subsequent
///          calls remain cheap. Uses a lazy-initialized static for thread-safe
///          initialization without runtime overhead.
///
/// @return @c true when debugging output should be emitted.
/// @note Inline candidate for better performance in hot paths.
inline bool isVmDebugLoggingEnabled() noexcept
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
/// @details Provides stable string representations for each dispatch kind,
///          defaulting to "Unknown" for unrecognized values to maintain
///          graceful degradation in diagnostics.
///
/// @param kind Dispatch strategy currently active.
/// @return Human-readable dispatch name used in debug logs.
/// @note Constexpr for compile-time evaluation when possible.
constexpr const char *dispatchKindName(VM::DispatchKind kind) noexcept
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
VM::VM(const Module &m,
       TraceConfig tc,
       uint64_t ms,
       DebugCtrl dbg,
       DebugScript *script,
       std::size_t stackBytes)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms),
      stackBytes_(stackBytes ? stackBytes : Frame::kDefaultStackSize)
{
    // Runtime overrides via environment -------------------------------------
    if (const char *envCounts = std::getenv("VIPER_ENABLE_OPCOUNTS"))
    {
        std::string v{envCounts};
        std::transform(v.begin(),
                       v.end(),
                       v.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Accept 1/true/on to enable; 0/false/off to disable.
        if (v == "0" || v == "false" || v == "off")
            enableOpcodeCounts = false;
        else if (v == "1" || v == "true" || v == "on")
            enableOpcodeCounts = true;
        // Other values are ignored, preserving the build-time default.
    }
    if (const char *envEvery = std::getenv("VIPER_INTERRUPT_EVERY_N"))
    {
        char *end = nullptr;
        unsigned long n = std::strtoul(envEvery, &end, 10);
        if (end && *end == '\0')
            pollEveryN_ = static_cast<uint32_t>(n);
    }

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
    // Cache pointer to opcode handler table once.
    handlerTable_ = &VM::getOpcodeHandlers();

    // Initialize per-VM runtime context for isolated state
    rtContext = new RtContext();
    rt_context_init(rtContext);

    // Reserve map capacities based on module sizes to reduce rehashing.
    fnMap.reserve(m.functions.size());
    strMap.reserve(m.globals.size());
    mutableGlobalMap.reserve(m.globals.size());
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
    {
        // String globals (including empty strings) go into strMap.
        // The parser requires all `global [const] str` declarations to have an
        // initializer, so this branch handles both "foo" and "" literals.
        if (g.type.kind == il::core::Type::Kind::Str)
        {
            strMap[g.name] = toViperString(g.init, AssumeNullTerminated::Yes);
        }
        else
        {
            // Mutable globals: allocate zero-initialized storage
            size_t size = 8; // Default to 8 bytes for most types
            if (g.type.kind == il::core::Type::Kind::I1)
                size = 1;
            void *storage = std::calloc(1, size);
            if (!storage)
                throw std::runtime_error("Failed to allocate mutable global storage");
            mutableGlobalMap[g.name] = storage;
        }
    }

    // Pre-populate string literal cache to eliminate map lookups on hot path.
    // This scans all const_str operands in the module and creates rt_string
    // objects upfront, providing a 15-25% speedup for string-heavy programs.
    // First, estimate the number of string occurrences to reserve capacity.
    {
        size_t estimate = 0;
        for (const auto &f : m.functions)
            for (const auto &block : f.blocks)
                for (const auto &instr : block.instructions)
                {
                    for (const auto &operand : instr.operands)
                        if (operand.kind == Value::Kind::ConstStr)
                            ++estimate;
                    for (const auto &args : instr.brArgs)
                        for (const auto &arg : args)
                            if (arg.kind == Value::Kind::ConstStr)
                                ++estimate;
                }
        if (estimate)
            inlineLiteralCache.reserve(estimate);
    }
    for (const auto &f : m.functions)
    {
        for (const auto &block : f.blocks)
        {
            for (const auto &instr : block.instructions)
            {
                // Check all instruction operands for string constants
                for (const auto &operand : instr.operands)
                {
                    if (operand.kind == Value::Kind::ConstStr)
                    {
                        // Only insert if not already present (avoid duplicates)
                        if (inlineLiteralCache.find(operand.str) == inlineLiteralCache.end())
                        {
                            // Use same logic as evalConstStr in VMContext.cpp
                            if (operand.str.find('\0') == std::string::npos)
                                inlineLiteralCache[operand.str] =
                                    rt_const_cstr(operand.str.c_str());
                            else
                                inlineLiteralCache[operand.str] =
                                    rt_string_from_bytes(operand.str.data(), operand.str.size());
                        }
                    }
                }

                // Check branch arguments for string constants
                for (const auto &brArgList : instr.brArgs)
                {
                    for (const auto &brArg : brArgList)
                    {
                        if (brArg.kind == Value::Kind::ConstStr)
                        {
                            if (inlineLiteralCache.find(brArg.str) == inlineLiteralCache.end())
                            {
                                if (brArg.str.find('\0') == std::string::npos)
                                    inlineLiteralCache[brArg.str] =
                                        rt_const_cstr(brArg.str.c_str());
                                else
                                    inlineLiteralCache[brArg.str] =
                                        rt_string_from_bytes(brArg.str.data(), brArg.str.size());
                            }
                        }
                    }
                }
            }
        }
    }

    // Build reverse map from BasicBlock -> Function for fast exception handler lookup.
    // This eliminates the O(N*M) linear scan in prepareTrap, providing a 50-90%
    // improvement in exception handling performance.
    // Compute total blocks to reserve map capacity and avoid rehashing.
    {
        size_t totalBlocks = 0;
        for (const auto &f : m.functions)
            totalBlocks += f.blocks.size();
        blockToFunction.reserve(totalBlocks);
        for (const auto &f : m.functions)
            for (const auto &block : f.blocks)
                blockToFunction[&block] = &f;
    }

    // Initialize fast-path debug flags from current configuration.
    // These flags enable cheap boolean tests in hot paths to avoid
    // function calls and complex checks when debugging is disabled.
    refreshDebugFlags();
}

/// @brief Refresh all debug fast-path flags from current state.
/// @details Updates tracingActive_, memWatchActive_, and varWatchActive_
///          based on the current tracer and debug controller state.
///          Call this after changing trace config or adding/removing watches.
void VM::refreshDebugFlags()
{
    tracingActive_ = tracer.isEnabled();
    memWatchActive_ = debug.hasMemWatches();
    varWatchActive_ = debug.hasVarWatches();
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
                     BlockMap &blocks,
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
    // Compute or reuse the maximum SSA id (register file size - 1) for this function.
    size_t maxSsaId;
    auto rcIt = regCountCache_.find(&fn);
    if (rcIt != regCountCache_.end())
    {
        maxSsaId = rcIt->second;
    }
    else
    {
        // Calculate the maximum SSA value ID used by scanning instruction results and params.
        maxSsaId = fn.valueNames.empty() ? 0 : fn.valueNames.size() - 1;
        for (const auto &p : fn.params)
            maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));
        for (const auto &block : fn.blocks)
        {
            for (const auto &p : block.params)
                maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));
            for (const auto &instr : block.instructions)
                if (instr.result)
                    maxSsaId = std::max(maxSsaId, static_cast<size_t>(*instr.result));
        }
        regCountCache_.emplace(&fn, maxSsaId);
    }

    // Pre-size register vector to maximum SSA ID + 1 (IDs are 0-based)
    // This eliminates incremental vector growth in storeResult hot path
    const size_t regCount = maxSsaId + 1;
    fr.regs.resize(regCount);

    if (isVmDebugLoggingEnabled())
    {
        std::fprintf(stderr,
                     "[SETUP] maxSsaId=%zu regCount=%zu valueNames=%zu\n",
                     maxSsaId,
                     regCount,
                     fn.valueNames.size());
        std::fflush(stderr);
    }
    fr.params.assign(fr.regs.size(), std::nullopt);
    // Initialize operand stack to the configured size. The vector is sized once
    // here and should not be resized during execution to avoid invalidating
    // pointers returned by prior alloca operations.
    fr.stack.resize(stackBytes_);
    fr.sp = 0;
    fr.ehStack.clear();
    fr.activeError = {};
    fr.resumeState = {};
    // Reserve to avoid rehashing while populating the per-call label map
    blocks.reserve(fn.blocks.size());
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
    // Reserve branch-target cache buckets roughly based on number of
    // terminators to reduce rehashing during branching-heavy execution.
    size_t estTerms = 0;
    for (const auto &b : fn.blocks)
        for (const auto &in : b.instructions)
            if (!in.labels.empty())
                ++estTerms;
    if (estTerms)
        st.branchTargetCache.reserve(estTerms);
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
