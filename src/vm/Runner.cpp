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
    Impl(const il::core::Module &module, RunConfig config)
        : script(config.debugScript),
          vm(module, config.trace, config.maxSteps, std::move(config.debug), script)
    {
    }

    int64_t run()
    {
        return vm.run();
    }

    uint64_t instructionCount() const
    {
        return vm.getInstrCount();
    }

    std::optional<std::string> lastTrapMessage() const
    {
        return vm.lastTrapMessage();
    }

  private:
    DebugScript *script = nullptr;
    VM vm;
};

Runner::Runner(const il::core::Module &module, RunConfig config)
    : impl(std::make_unique<Impl>(module, std::move(config)))
{
}

Runner::~Runner() = default;

Runner::Runner(Runner &&) noexcept = default;

Runner &Runner::operator=(Runner &&) noexcept = default;

int64_t Runner::run()
{
    return impl->run();
}

uint64_t Runner::instructionCount() const
{
    return impl->instructionCount();
}

std::optional<std::string> Runner::lastTrapMessage() const
{
    return impl->lastTrapMessage();
}

int64_t runModule(const il::core::Module &module, RunConfig config)
{
    Runner runner(module, std::move(config));
    return runner.run();
}

} // namespace il::vm

