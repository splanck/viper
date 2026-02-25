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
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/DiagFormat.hpp"
#include "vm/Marshal.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMConstants.hpp"
#include "vm/control_flow.hpp"

#include "rt_context.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

using namespace il::core;

namespace il::vm
{

void registerThreadsRuntimeExternals();

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

struct ThreadsRuntimeInitializer
{
    ThreadsRuntimeInitializer()
    {
        registerThreadsRuntimeExternals();
    }
};

[[maybe_unused]] const ThreadsRuntimeInitializer kThreadsRuntimeInitializer{};

/// @brief Static initializer that runs runtime descriptor sanity checks.
/// @details Validates that runtime descriptors are consistent before any VM
///          execution occurs. In release builds this catches configuration
///          errors early; in debug builds it complements the existing assert-
///          based validation.
struct RuntimeDescriptorChecker
{
    RuntimeDescriptorChecker()
    {
        if (!il::runtime::selfCheckRuntimeDescriptors())
        {
            std::fprintf(stderr, "[FATAL] Runtime descriptor self-check failed\n");
            std::abort();
        }
    }
};

[[maybe_unused]] const RuntimeDescriptorChecker kRuntimeDescriptorChecker{};

/// @brief Static initializer that registers the VM trap handler for invariant violations.
/// @details Enables the RuntimeSignatures layer to route invariant violations through
///          the VM's trap mechanism when a VM is active. When no VM is active, the
///          handler returns false and the violation falls back to abort behavior.
struct InvariantTrapHandlerRegistrar
{
    InvariantTrapHandlerRegistrar()
    {
        il::runtime::setInvariantTrapHandler(
            [](const char *message) -> bool
            {
                // Check if there's an active VM that can handle the trap.
                if (!RuntimeBridge::hasActiveVm())
                    return false;

                // Route through the RuntimeBridge trap mechanism.
                // This will invoke vm_raise() if a VM is active, which may throw
                // TrapDispatchSignal for exception handler dispatch.
                RuntimeBridge::trap(TrapKind::RuntimeError, message, {}, {}, {});

                // If we reach here, the trap was not caught (shouldn't happen with RuntimeError).
                // Return false to fall back to abort.
                return false;
            });
    }
};

[[maybe_unused]] const InvariantTrapHandlerRegistrar kInvariantTrapHandlerRegistrar{};

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
    debug.setSourceManager(tc.sm);
    init(nullptr);
}

VM::VM(const Module &m,
       std::shared_ptr<ProgramState> program,
       TraceConfig tc,
       uint64_t ms,
       DebugCtrl dbg,
       DebugScript *script,
       std::size_t stackBytes)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms),
      stackBytes_(stackBytes ? stackBytes : Frame::kDefaultStackSize)
{
    debug.setSourceManager(tc.sm);
    init(std::move(program));
}

void VM::init(std::shared_ptr<ProgramState> program)
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

    // Cache pointer to opcode handler table once.
    handlerTable_ = &VM::getOpcodeHandlers();

    if (program)
    {
        programState_ = std::move(program);
        if (!programState_->module)
        {
            programState_->module = &mod;
        }
        else if (programState_->module != &mod)
        {
            std::fprintf(stderr, "VM: fatal: thread program state module mismatch\n");
            std::abort();
        }
    }
    else
    {
        programState_ = std::make_shared<ProgramState>();
        programState_->module = &mod;
        programState_->rtContext = std::shared_ptr<RtContext>(new RtContext(), RtContextDeleter{});
        rt_context_init(programState_->rtContext.get());

        programState_->strMap.reserve(mod.globals.size());
        programState_->mutableGlobalMap.reserve(mod.globals.size());

        for (const auto &g : mod.globals)
        {
            if (g.type.kind == il::core::Type::Kind::Str)
            {
                programState_->strMap[g.name] =
                    ViperStringHandle(toViperString(g.init, AssumeNullTerminated::Yes));
                continue;
            }

            size_t size = 8;
            if (g.type.kind == il::core::Type::Kind::I1)
                size = 1;
            void *storage = std::calloc(1, size);
            if (!storage)
            {
                std::fprintf(stderr, "VM: fatal: failed to allocate mutable global storage\n");
                std::abort();
            }
            programState_->mutableGlobalMap[g.name] = storage;
        }
    }

    // Reserve map capacities based on module sizes to reduce rehashing.
    fnMap.reserve(mod.functions.size());
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : mod.functions)
        fnMap[f.name] = &f;

    // Pre-populate string literal cache to eliminate map lookups on hot path.
    // This scans all const_str operands in the module and creates rt_string
    // objects upfront, providing a 15-25% speedup for string-heavy programs.
    // First, estimate the number of string occurrences to reserve capacity.
    {
        size_t estimate = 0;
        for (const auto &f : mod.functions)
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
    for (const auto &f : mod.functions)
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
                        if (inlineLiteralCache.find(operand.str) == inlineLiteralCache.end())
                        {
                            if (operand.str.find('\0') == std::string::npos)
                                inlineLiteralCache[operand.str] =
                                    ViperStringHandle(rt_const_cstr(operand.str.c_str()));
                            else
                                inlineLiteralCache[operand.str] = ViperStringHandle(
                                    rt_string_from_bytes(operand.str.data(), operand.str.size()));
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
                                        ViperStringHandle(rt_const_cstr(brArg.str.c_str()));
                                else
                                    inlineLiteralCache[brArg.str] = ViperStringHandle(
                                        rt_string_from_bytes(brArg.str.data(), brArg.str.size()));
                            }
                        }
                    }
                }
            }
        }
    }

    // Build reverse map from BasicBlock -> Function for fast exception handler lookup.
    {
        size_t totalBlocks = 0;
        for (const auto &f : mod.functions)
            totalBlocks += f.blocks.size();
        blockToFunction.reserve(totalBlocks);
        for (const auto &f : mod.functions)
            for (const auto &block : f.blocks)
                blockToFunction[&block] = &f;
    }

    refreshDebugFlags();
    execStack.reserve(kExecStackInitialCapacity);
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

//===----------------------------------------------------------------------===//
// Buffer Pool Management
//===----------------------------------------------------------------------===//

std::vector<uint8_t> VM::acquireStackBuffer(size_t size)
{
    if (!stackBufferPool_.empty())
    {
        auto buf = std::move(stackBufferPool_.back());
        stackBufferPool_.pop_back();
        buf.resize(size);
        return buf;
    }
    return std::vector<uint8_t>(size);
}

void VM::releaseStackBuffer(std::vector<uint8_t> &&buf)
{
    if (stackBufferPool_.size() < kStackBufferPoolSize)
    {
        // Clear but keep capacity for reuse
        buf.clear();
        stackBufferPool_.push_back(std::move(buf));
    }
    // Otherwise let buffer be deallocated
}

std::vector<Slot> VM::acquireRegFile(size_t size)
{
    if (!regFilePool_.empty())
    {
        auto regs = std::move(regFilePool_.back());
        regFilePool_.pop_back();
        regs.resize(size);
        return regs;
    }
    return std::vector<Slot>(size);
}

void VM::releaseRegFile(std::vector<Slot> &&regs)
{
    if (regFilePool_.size() < kRegisterFilePoolSize)
    {
        regs.clear();
        regFilePool_.push_back(std::move(regs));
    }
}

void VM::releaseFrameBuffers(Frame &fr)
{
    releaseStackBuffer(std::move(fr.stack));
    releaseRegFile(std::move(fr.regs));
}

//===----------------------------------------------------------------------===//
// Frame Setup
//===----------------------------------------------------------------------===//

/// @brief Obtain (or lazily build) the block label→BasicBlock* map for @p fn.
/// @details The map is built once per function and cached for the VM's lifetime.
///          IL is immutable after parsing so the cached map remains valid.
/// @param fn Function whose blocks should be mapped.
/// @return Const reference to the cached BlockMap.
const VM::BlockMap &VM::getOrBuildBlockMap(const Function &fn)
{
    auto it = fnBlockMapCache_.find(&fn);
    if (it != fnBlockMapCache_.end())
        return it->second;
    auto &map = fnBlockMapCache_[&fn];
    map.reserve(fn.blocks.size());
    for (const auto &b : fn.blocks)
        map[b.label] = &b;
    return map;
}

/// @brief Initialise a fresh frame for executing @p fn.
///
/// Seeds parameter slots, verifies the argument count, and selects the entry
/// block so the interpreter loop can begin without additional bookkeeping.
/// The block label map is obtained from the VM-level cache (see
/// getOrBuildBlockMap) to avoid repeated hash-map construction.
///
/// @param fn     Function to execute.
/// @param args   Argument slots for the function's entry block.
/// @param bb     Set to the entry basic block of @p fn.
/// @return Fully initialised frame ready to run.
Frame VM::setupFrame(const Function &fn,
                     std::span<const Slot> args,
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
    // Use shared helper to compute/cache register file size
    const size_t maxSsaId = detail::VMAccess::computeMaxSsaId(*this, fn);

    // Acquire register file from pool or allocate new.
    // Pool reuse avoids repeated allocations for recursive/repeated calls.
    const size_t regCount = maxSsaId + 1;
    fr.regs = acquireRegFile(regCount);

    if (isVmDebugLoggingEnabled())
    {
        std::fprintf(stderr,
                     "[SETUP] maxSsaId=%zu regCount=%zu valueNames=%zu\n",
                     maxSsaId,
                     regCount,
                     fn.valueNames.size());
        std::fflush(stderr);
    }
    fr.params.assign(fr.regs.size(), Slot{});
    fr.paramsSet.assign(fr.regs.size(), 0);
    // Acquire stack buffer from pool or allocate new.
    // Pool reuse avoids repeated 64KB allocations.
    fr.stack = acquireStackBuffer(stackBytes_);
    fr.sp = 0;
    fr.ehStack.clear();
    fr.activeError = {};
    fr.resumeState = {};
    // Block map is cached at VM level (getOrBuildBlockMap) — no per-call rebuild.
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        if (args.size() != params.size())
        {
            RuntimeBridge::trap(
                TrapKind::InvalidOperation,
                diag::formatArgumentCountMismatch(fn.name, params.size(), args.size()),
                {},
                fn.name,
                bb->label);
        }
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
        {
            const auto id = params[i].id;
            assert(id < fr.params.size());
            const bool isStringParam = params[i].type.kind == Type::Kind::Str;
            if (isStringParam)
            {
                if (fr.paramsSet[id])
                    rt_str_release_maybe(fr.params[id].str);

                Slot retained = args[i];
                rt_str_retain_maybe(retained.str);
                fr.params[id] = retained;
                fr.paramsSet[id] = 1;
                continue;
            }

            fr.params[id] = args[i];
            fr.paramsSet[id] = 1;
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
VM::ExecState VM::prepareExecution(const Function &fn, std::span<const Slot> args)
{
    ExecState st{};
    st.owner = this;
    st.blocks = &getOrBuildBlockMap(fn);
    st.fr = setupFrame(fn, args, st.bb);
    // Transfer block parameters immediately after frame setup.
    // This ensures parameters are copied to registers before the first instruction
    // executes, regardless of which dispatch path is taken. The transfer is
    // idempotent (pending values are consumed), so if processDebugControl also
    // runs at ip=0, it will see already-consumed params and skip redundant work.
    if (st.bb)
        transferBlockParams(st.fr, *st.bb);
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
    // Install raw fn pointer trampoline; the actual std::function is stored in
    // pollCallback_ and invoked through pollCallbackTrampoline_ to avoid
    // std::function overhead (SBO + type-erasure cost) on every dispatch boundary.
    st.config.pollCallback = pollCallback_ ? &VM::pollCallbackTrampoline_ : nullptr;
#if VIPER_VM_OPCOUNTS
    st.config.enableOpcodeCounts = enableOpcodeCounts;
#else
    st.config.enableOpcodeCounts = false;
#endif
    tracer.onFramePrepared(st.fr);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    // Prime the pre-resolved operand cache for the entry block so the first
    // instruction dispatch can use the fast evalFast() path immediately.
    st.blockCache = getOrBuildBlockCache(&fn, st.bb);
    return st;
}

} // namespace il::vm
