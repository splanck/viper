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
// Invariants: Runner forwards configuration to the underlying VM and preserves
//             observable behaviour exposed by existing tooling.
// Ownership: Runner owns its VM instance while borrowing the module and optional
//            debug script supplied by callers.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#include "viper/vm/VM.hpp"

#include "vm/VM.hpp"

#include <utility>

namespace il::vm
{
class Runner::Impl
{
  public:
    /// @brief Construct the concrete interpreter wrapper used by @ref Runner.
    /// @details Stores the optional debug script pointer separately so the
    ///          public façade can remain pimpl-based.  The underlying @ref VM is
    ///          initialised with tracing, maximum instruction count, and debug
    ///          hooks forwarded from @p config.
    Impl(const il::core::Module &module, RunConfig config)
        : script(config.debugScript),
          vm(module, config.trace, config.maxSteps, std::move(config.debug), script)
    {
    }

    /// @brief Execute the wrapped module until completion or trap.
    /// @details Delegates directly to @ref VM::run so statistics and trap
    ///          propagation remain consistent between the façade and the full
    ///          interpreter implementation.
    /// @return Exit code or trap value reported by the interpreter.
    int64_t run()
    {
        return vm.run();
    }

    /// @brief Retrieve the number of IL instructions executed so far.
    /// @details Exposes @ref VM::getInstrCount for tooling that wishes to measure
    ///          execution cost without taking a direct dependency on the full VM
    ///          header surface.
    /// @return Count of instructions executed by the VM instance.
    uint64_t instructionCount() const
    {
        return vm.getInstrCount();
    }

    /// @brief Query the most recent trap message, if any.
    /// @details Mirrors @ref VM::lastTrapMessage so callers using the façade can
    ///          introspect fatal runtime errors.
    /// @return Trap diagnostic when one exists; @c std::nullopt otherwise.
    std::optional<std::string> lastTrapMessage() const
    {
        return vm.lastTrapMessage();
    }

  private:
    DebugScript *script = nullptr;
    VM vm;
};

/// @brief Construct a runner façade for the provided module.
/// @details Allocates the hidden @ref Impl and forwards run configuration,
///          ensuring debug hooks and tracing state match the caller's request.
/// @param module IL module that should be executed.
/// @param config Runtime configuration flags and optional debugging aids.
Runner::Runner(const il::core::Module &module, RunConfig config)
    : impl(std::make_unique<Impl>(module, std::move(config)))
{
}

/// @brief Destroy the runner and its owned VM instance.
Runner::~Runner() = default;

/// @brief Enable move construction to transfer ownership of the VM façade.
Runner::Runner(Runner &&) noexcept = default;

/// @brief Enable move assignment so callers can rebind façade ownership.
Runner &Runner::operator=(Runner &&) noexcept = default;

/// @brief Execute the module through the underlying interpreter.
/// @details Calls into the pimpl so the public header need not include
///          @ref VM.hpp.  Result semantics match @ref VM::run exactly.
/// @return Exit code or trap value produced by the module.
int64_t Runner::run()
{
    return impl->run();
}

/// @brief Report how many IL instructions have executed so far.
/// @details Defers to the implementation to keep counting logic in one place.
/// @return Instruction count accumulated by the interpreter.
uint64_t Runner::instructionCount() const
{
    return impl->instructionCount();
}

/// @brief Fetch the diagnostic message from the most recent trap.
/// @details Allows CLI utilities to surface VM errors without accessing the full
///          interpreter API surface.
/// @return Trap diagnostic string, when present.
std::optional<std::string> Runner::lastTrapMessage() const
{
    return impl->lastTrapMessage();
}

/// @brief Convenience helper that executes a module immediately.
/// @details Constructs a temporary @ref Runner so callers need only supply a
///          module and configuration when they do not require incremental access
///          to runner state.
/// @param module Module to execute.
/// @param config Runtime configuration forwarded to the runner.
/// @return Exit code or trap value produced by the module execution.
int64_t runModule(const il::core::Module &module, RunConfig config)
{
    Runner runner(module, std::move(config));
    return runner.run();
}

} // namespace il::vm

