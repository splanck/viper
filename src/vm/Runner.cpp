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

  private:
    DebugScript *script = nullptr; ///< Borrowed debug script used for breakpoints.
    VM vm;                         ///< Owning interpreter instance.
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

