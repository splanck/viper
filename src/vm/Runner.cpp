//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Runner.cpp
// Purpose: Provide the light-weight façade that wraps the full virtual-machine
//          interpreter so tools can execute IL modules with minimal plumbing.
// Design notes:
//   * Runner exposes a narrow value-semantic interface around the heavier
//     `il::vm::VM` implementation.  Callers configure execution through
//     `RunConfig` and interact with a stable API that mirrors the command-line
//     tooling.
//   * The façade owns the interpreter instance while borrowing caller-supplied
//     modules and optional debug scripts, ensuring lifetimes stay well defined
//     without forcing copies of the IR.
//   * Helper `runModule` offers a one-shot convenience wrapper that mirrors the
//     historical free function used by early prototypes, keeping existing code
//     paths operational.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#include "viper/vm/VM.hpp"

#include "vm/VM.hpp"

#include <utility>

/// @file
/// @brief Implementation of the public virtual machine runner façade.
/// @details This translation unit bridges the ergonomic `il::vm::Runner`
///          wrapper with the feature-rich @ref il::vm::VM interpreter.  The
///          façade simplifies embedding scenarios by hiding details such as
///          debug-script ownership and instruction counters while still
///          exposing hooks for trap reporting.

namespace il::vm
{
/// @brief Private implementation that owns the actual interpreter instance.
/// @details The façade keeps construction and resource management out of the
///          public header.  The implementation captures the caller-supplied
///          module by reference, stores an optional debug script pointer, and
///          materialises an @ref VM configured from @ref RunConfig.
class Runner::Impl
{
  public:
    /// @brief Construct the interpreter implementation from high-level config.
    /// @details The constructor stores the optional debug script pointer from
    ///          @p config, instantiates the virtual machine with tracing and
    ///          instruction-limit parameters, and forwards any debug adapter
    ///          object for richer trap handling.  Because @ref VM expects the
    ///          debug script pointer to remain valid for its lifetime, the
    ///          implementation caches the pointer directly rather than copying
    ///          the script state.
    /// @param module IL module that provides functions and globals to execute.
    /// @param config Runner configuration describing tracing, limits, and
    ///        optional debugging facilities.
    Impl(const il::core::Module &module, RunConfig config)
        : script(config.debugScript),
          vm(module, config.trace, config.maxSteps, std::move(config.debug), script)
    {
    }

    /// @brief Execute the configured module until completion or trap.
    /// @details Delegates directly to @ref VM::run and forwards the resulting
    ///          accumulator value.  The helper exists so the public façade can
    ///          keep the implementation type opaque and eventually attach
    ///          instrumentation without altering the public ABI.
    /// @return Result of interpreting the module's @c @main entry point.
    int64_t run()
    {
        return vm.run();
    }

    /// @brief Report the number of instructions executed by the VM so far.
    /// @details Returns the monotonically increasing counter maintained by the
    ///          interpreter.  The value is exposed for diagnostics such as
    ///          performance tracing or enforcing maximum-step limits in tooling.
    /// @return Count of IL instructions evaluated since the VM was constructed.
    uint64_t instructionCount() const
    {
        return vm.getInstrCount();
    }

    /// @brief Retrieve the most recent trap message, if any.
    /// @details Traps surface through @ref VM::lastTrapMessage, which returns an
    ///          optional describing the textual diagnostic raised by the runtime.
    ///          The wrapper mirrors that behaviour so callers can react without
    ///          needing direct access to the interpreter instance.
    /// @return Optional string containing the last trap diagnostic.
    std::optional<std::string> lastTrapMessage() const
    {
        return vm.lastTrapMessage();
    }

  private:
    DebugScript *script = nullptr;
    VM vm;
};

/// @brief Construct the public runner façade around a module and configuration.
/// @details Allocates the hidden implementation object that in turn builds the
///          virtual machine.  Storing the implementation behind a smart pointer
///          keeps the header self-contained and avoids leaking private types to
///          embedding applications.
/// @param module IL module that provides the program to execute.
/// @param config Configuration controlling tracing, limits, and debugging.
Runner::Runner(const il::core::Module &module, RunConfig config)
    : impl(std::make_unique<Impl>(module, std::move(config)))
{
}

/// @brief Destroy the runner façade and release interpreter resources.
/// @details Defaulted because @ref std::unique_ptr already manages the
///          implementation lifetime; documented explicitly so the ownership
///          boundary remains clear to embedders auditing resource handling.
Runner::~Runner() = default;

/// @brief Transfer ownership of the interpreter from another runner.
/// @details Defaulted move constructor suffices because the underlying
///          implementation is stored in a @ref std::unique_ptr.  The semantic is
///          documented to make it explicit that copying is disallowed while
///          moves are cheap.
Runner::Runner(Runner &&) noexcept = default;

/// @brief Move-assign a runner, releasing any previously held interpreter.
/// @details The generated move assignment handles nulling the source instance
///          and is explicitly defaulted here to surface in documentation.
/// @return Reference to @c *this after the assignment completes.
Runner &Runner::operator=(Runner &&) noexcept = default;

/// @brief Execute the program associated with the runner instance.
/// @details Forwards the call to the implementation's @ref Impl::run helper so
///          the interpreter can drive the IL program.  The wrapper exists so
///          instrumentation or profiling can be added later without changing the
///          public signature.
/// @return Value produced by the interpreted module's @c @main function.
int64_t Runner::run()
{
    return impl->run();
}

/// @brief Query how many IL instructions have executed so far.
/// @details The value reflects the interpreter's internal accounting and is
///          exposed for performance counters and unit tests verifying control
///          flow.  Calling the helper does not mutate interpreter state.
/// @return Number of instructions executed during this runner's lifetime.
uint64_t Runner::instructionCount() const
{
    return impl->instructionCount();
}

/// @brief Return the last trap diagnostic emitted by the interpreter.
/// @details Mirrors @ref Impl::lastTrapMessage so tooling can retrieve the
///          textual description of any fatal runtime condition encountered
///          during execution.  Empty optionals indicate the program terminated
///          normally.
/// @return Optional string describing the most recent trap.
std::optional<std::string> Runner::lastTrapMessage() const
{
    return impl->lastTrapMessage();
}

/// @brief Convenience helper that executes an IL module immediately.
/// @details Constructs a temporary @ref Runner with the supplied configuration,
///          runs it to completion, and returns the resulting value.  The helper
///          exists for legacy call sites that relied on the earlier free
///          function-based API.
/// @param module Module containing the program to execute.
/// @param config Execution configuration forwarded to the runner constructor.
/// @return Result produced by interpreting the module.
int64_t runModule(const il::core::Module &module, RunConfig config)
{
    Runner runner(module, std::move(config));
    return runner.run();
}

} // namespace il::vm

