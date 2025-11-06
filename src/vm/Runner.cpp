//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Runner.cpp
// Purpose: Implement the lightweight public VM runner façade backed by the
//          interpreter implementation.
// Key invariants: Runner forwards configuration directly to the underlying VM,
//                 preserves observable behaviour exposed by CLI tooling, and
//                 never leaks ownership of caller-provided modules or debug
//                 scripts.
// Ownership/Lifetime: Runner owns its VM instance while borrowing the module and
//                     optional debug script supplied by callers.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Defines the convenience façade for executing modules through the VM.
/// @details The runner glues together configuration parsing, optional debugging
///          support, and the interpreter loop into a single RAII-friendly class
///          used by CLI drivers and tests.

#include "viper/vm/VM.hpp"

#include "vm/VM.hpp"

#include <optional>
#include <utility>

namespace il::vm
{
/// @brief Private implementation that manages the VM instance and optional debug script.
/// @details Lives entirely within the translation unit so the public header can
///          avoid exposing the heavy VM definition.  The implementation owns the
///          interpreter and keeps a pointer to the caller-provided debug script,
///          which must outlive the runner.
class Runner::Impl
{
  public:
    /// @brief Construct the VM implementation using the supplied module and configuration.
    /// @details Initialises the VM with tracing, instruction limits, and debug
    ///          configuration extracted from @p config.  The debug script pointer
    ///          is cached so the VM can reference it without copying.
    /// @param module IL module to execute (borrowed for the lifetime of the VM).
    /// @param config Execution configuration containing tracing and debug options.
    Impl(const il::core::Module &module, RunConfig config)
        : script(config.debugScript),
          vm(module, config.trace, config.maxSteps, std::move(config.debug), script)
    {
    }

    /// @brief Run the underlying interpreter until completion or a trap occurs.
    /// @return Exit code produced by the executed module.
    int64_t run()
    {
        return vm.run();
    }

    /// @brief Report how many instructions executed during the last run.
    /// @return Interpreter instruction count from the underlying VM.
    uint64_t instructionCount() const
    {
        return vm.getInstrCount();
    }

    /// @brief Return the most recent trap message, if any.
    /// @return Optional string containing the trap description.
    std::optional<std::string> lastTrapMessage() const
    {
        return vm.lastTrapMessage();
    }

  private:
    DebugScript *script = nullptr;
    VM vm;
};

/// @brief Construct a VM runner that borrows the given module and configuration.
/// @param module IL module to execute.
/// @param config Execution configuration including tracing and debugging options.
Runner::Runner(const il::core::Module &module, RunConfig config)
    : impl(std::make_unique<Impl>(module, std::move(config)))
{
}

/// @brief Destroy the runner and release the underlying VM resources.
Runner::~Runner() = default;

/// @brief Move-construct a runner, transferring VM ownership.
Runner::Runner(Runner &&) noexcept = default;

/// @brief Move-assign a runner, transferring VM ownership.
Runner &Runner::operator=(Runner &&) noexcept = default;

/// @brief Execute the configured module and return its exit code.
/// @return Exit code produced by the VM.
int64_t Runner::run()
{
    return impl->run();
}

/// @brief Expose the number of instructions executed by the last run.
/// @return Instruction count gathered from the interpreter.
uint64_t Runner::instructionCount() const
{
    return impl->instructionCount();
}

/// @brief Retrieve the most recent trap message, if one was recorded.
/// @return Optional trap description produced by the VM.
std::optional<std::string> Runner::lastTrapMessage() const
{
    return impl->lastTrapMessage();
}

/// @brief Convenience helper that constructs a runner and executes a module in one call.
/// @param module Module to execute.
/// @param config Run configuration to apply.
/// @return Exit code produced by running the module.
int64_t runModule(const il::core::Module &module, RunConfig config)
{
    Runner runner(module, std::move(config));
    return runner.run();
}

} // namespace il::vm
