//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Runner.cpp
// Purpose: Implement the lightweight public VM runner façade backed by the
//          full interpreter implementation.
// Key invariants: Runner forwards configuration to the underlying VM and
//                 preserves observable behaviour exposed by existing tooling.
// Ownership/Lifetime: Runner owns its VM instance while borrowing the module
//                     and optional debug script supplied by callers.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#include "viper/vm/VM.hpp"

#include "vm/VM.hpp"
#include "vm/VMContext.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "support/source_manager.hpp"

#include <utility>

namespace il::vm
{
/// @brief Private implementation that owns the actual VM instance.
/// @details The façade pattern keeps the public @ref Runner interface header
///          light by hiding the heavy VM headers behind a unique_ptr.  The Impl
///          aggregates the concrete @ref VM object and optional debug script,
///          exposing minimal forwarding methods consumed by Runner.
class Runner::Impl
{
  public:
    /// @brief Construct the backing VM with the supplied configuration.
    /// @details Stores the debug script pointer from @p config so the VM can
    ///          drive breakpoints, then instantiates the interpreter with the
    ///          caller-provided trace and step-limit parameters.  Ownership of
    ///          the Debug object transfers into the VM as part of the move.
    /// @param module Module to execute.
    /// @param config Run configuration describing trace/debug behaviour.
    Impl(const il::core::Module &module, RunConfig config)
        : script(config.debugScript),
          vm(module, config.trace, config.maxSteps, std::move(config.debug), script)
    {
    }

    /// @brief Execute the loaded module until completion or trap.
    /// @details Simply forwards to @ref VM::run, keeping the façade thin.  The
    ///          result reflects the process exit code or trap-specific return
    ///          value returned by the interpreter.
    /// @return Interpreter exit code as produced by @ref VM::run.
    int64_t run()
    {
        return vm.run();
    }

    /// @brief Retrieve the number of IL instructions executed so far.
    /// @details Forwards to @ref VM::getInstrCount so tooling can gather
    ///          profiling or debugging information without exposing the full VM
    ///          type in headers.
    /// @return Total number of instructions the VM has executed.
    uint64_t instructionCount() const
    {
        return vm.getInstrCount();
    }

    /// @brief Fetch the most recent trap message emitted by the VM.
    /// @details Returns an optional string describing the last trap recorded by
    ///          the interpreter.  When no trap has occurred the optional is
    ///          empty, mirroring @ref VM::lastTrapMessage.
    /// @return Trap description when available; otherwise `std::nullopt`.
    std::optional<std::string> lastTrapMessage() const
    {
        return vm.lastTrapMessage();
    }

#if VIPER_VM_OPCOUNTS
    const std::array<uint64_t, il::core::kNumOpcodes> &opcodeCounts() const
    {
        return vm.opcodeCounts();
    }

    void resetOpcodeCounts()
    {
        vm.resetOpcodeCounts();
    }

    std::vector<std::pair<int, uint64_t>> topOpcodes(std::size_t n) const
    {
        return vm.topOpcodes(n);
    }
#endif

    // Single-step support ----------------------------------------------------
    StepResult step()
    {
        ensurePrepared();
        auto maybe = detail::VMAccess::stepOnce(vm, *state);
        if (!maybe)
            return {StepStatus::Advanced};

        const auto code = maybe->i64;
        if (code == 10)
            return {StepStatus::BreakpointHit};
        if (code == 1)
            return {StepStatus::Paused};

        // Any other concrete result means function returned (halted).
        return {StepStatus::Halted};
    }

    RunStatus continueRun()
    {
        ensurePrepared();
        while (true)
        {
            auto res = step();
            switch (res.status)
            {
                case StepStatus::Advanced:
                    continue;
                case StepStatus::BreakpointHit:
                    return RunStatus::BreakpointHit;
                case StepStatus::Halted:
                    return RunStatus::Halted;
                case StepStatus::Trapped:
                    return RunStatus::Trapped;
                case StepStatus::Paused:
                    // Interpret generic pause as step budget exhaustion for continue.
                    return RunStatus::StepBudgetExceeded;
            }
        }
    }

    void setBreakpoint(const il::support::SourceLoc &loc)
    {
        auto &dbg = detail::VMAccess::debug(vm);
        const auto *sm = dbg.getSourceManager();
        if (!sm || !loc.hasFile() || !loc.hasLine())
            return;
        dbg.addBreakSrcLine(std::string(sm->getPath(loc.file_id)), loc.line);
    }

    void clearBreakpoints()
    {
        auto &dbg = detail::VMAccess::debug(vm);
        const auto *sm = dbg.getSourceManager();
        // Reconstruct a fresh controller but preserve the source manager.
        DebugCtrl fresh{};
        fresh.setSourceManager(sm);
        dbg = std::move(fresh);
    }

    void setMaxSteps(uint64_t max)
    {
        detail::VMAccess::setMaxSteps(vm, max);
    }

    const TrapInfo *lastTrap() const
    {
        // Populate on demand from public trap message.
        auto msg = vm.lastTrapMessage();
        if (!msg)
            return nullptr;
        cachedTrap.message = *msg;
        // Other fields remain defaults when not introspecting VM internals.
        return &cachedTrap;
    }

  private:
    void ensurePrepared()
    {
        if (state)
            return;
        // Locate the entry function and prepare initial execution state.
        const auto &fnMap = detail::VMAccess::functionMap(vm);
        auto it = fnMap.find("main");
        if (it == fnMap.end())
        {
            // No main; mark as halted by creating an empty state.
            state = std::make_unique<detail::VMAccess::ExecState>();
            return;
        }
        state = std::make_unique<detail::VMAccess::ExecState>(
            detail::VMAccess::prepare(vm, *it->second, {}));
    }

    DebugScript *script = nullptr; ///< Borrowed debug script used for breakpoints.
    VM vm;                         ///< Owning interpreter instance.
    std::unique_ptr<detail::VMAccess::ExecState> state; // prepared on first step
    mutable TrapInfo cachedTrap{};
};

/// @brief Create a runner façade for the supplied module and configuration.
/// @details Allocates the hidden @ref Impl instance, transferring ownership of
///          any debug handles stored in the configuration.  The façade keeps the
///          header minimal while allowing callers to construct runners on the
///          stack.
Runner::Runner(const il::core::Module &module, RunConfig config)
    : impl(std::make_unique<Impl>(module, std::move(config)))
{
}

/// @brief Destroy the runner, releasing its private implementation.
/// @details Defaulted because unique_ptr cleanly tears down the underlying VM.
Runner::~Runner() = default;

/// @brief Move-construct a runner by transferring ownership of the implementation.
Runner::Runner(Runner &&) noexcept = default;

/// @brief Move-assign a runner by swapping private implementation pointers.
Runner &Runner::operator=(Runner &&) noexcept = default;

/// @brief Run the module associated with this runner.
/// @details Forwards directly to @ref Impl::run so call sites interact solely
///          with the façade.
/// @return Exit code produced by the VM execution.
int64_t Runner::run()
{
    return impl->run();
}

/// @brief Report how many IL instructions have executed.
/// @details Simply forwards to the private implementation to avoid exposing VM
///          internals.
/// @return Number of instructions executed by the VM.
uint64_t Runner::instructionCount() const
{
    return impl->instructionCount();
}

/// @brief Retrieve the diagnostic message for the most recent trap.
/// @details Allows tooling to present user-facing diagnostics without touching
///          the VM internals.
/// @return Optional trap message; empty when no trap has fired.
std::optional<std::string> Runner::lastTrapMessage() const
{
    return impl->lastTrapMessage();
}

const auto &Runner::opcodeCounts() const
{
#if VIPER_VM_OPCOUNTS
    return impl->opcodeCounts();
#else
    static const std::array<uint64_t, 0> kEmpty{};
    return kEmpty;
#endif
}

void Runner::resetOpcodeCounts()
{
#if VIPER_VM_OPCOUNTS
    impl->resetOpcodeCounts();
#endif
}

std::vector<std::pair<int, uint64_t>> Runner::topOpcodes(std::size_t n) const
{
#if VIPER_VM_OPCOUNTS
    return impl->topOpcodes(n);
#else
    return {};
#endif
}

Runner::StepResult Runner::step()
{
    return impl->step();
}

Runner::RunStatus Runner::continueRun()
{
    return impl->continueRun();
}

void Runner::setBreakpoint(const il::support::SourceLoc &loc)
{
    impl->setBreakpoint(loc);
}

void Runner::clearBreakpoints()
{
    impl->clearBreakpoints();
}

void Runner::setMaxSteps(uint64_t max)
{
    impl->setMaxSteps(max);
}

const Runner::TrapInfo *Runner::lastTrap() const
{
    return impl->lastTrap();
}

/// @brief Convenience helper that constructs a runner, executes it, and returns the result.
/// @details Used by CLI tooling and tests that only need to run a module once.
///          The helper ensures resources are released immediately after
///          execution by keeping the runner scoped to the call.
/// @param module Module to execute.
/// @param config Runtime configuration and optional debug handles.
/// @return Exit code reported by the VM execution.
int64_t runModule(const il::core::Module &module, RunConfig config)
{
    Runner runner(module, std::move(config));
    return runner.run();
}

} // namespace il::vm
