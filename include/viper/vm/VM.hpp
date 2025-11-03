//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/VM.hpp
// Purpose: Declare a lightweight facade for running IL modules through the VM
//          without exposing interpreter internals.
// Invariants: Public API owns its backing VM implementation and forwards all
//             operations while preserving semantics of the existing VM class.
// Ownership: Runner manages the interpreter lifetime; callers retain ownership
//            of modules and optional debug scripts passed in via configuration.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "viper/vm/debug/Debug.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace il::core
{
class Module;
} // namespace il::core

namespace il::vm
{
/// @brief Configuration parameters for executing an IL module.
struct RunConfig
{
    TraceConfig trace;                ///< Tracing configuration.
    uint64_t maxSteps = 0;            ///< Step limit; zero disables the limit.
    DebugCtrl debug;                  ///< Debug controller copied into the VM.
    DebugScript *debugScript = nullptr; ///< Optional script pointer; not owned.
};

/// @brief Lightweight façade owning a VM instance for running IL modules.
class Runner
{
  public:
    Runner(const il::core::Module &module, RunConfig config = {});
    ~Runner();

    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    Runner(Runner &&) noexcept;
    Runner &operator=(Runner &&) noexcept;

    /// @brief Execute the module's entry function.
    [[nodiscard]] int64_t run();

    /// @brief Retrieve the total number of instructions executed by the VM.
    [[nodiscard]] uint64_t instructionCount() const;

    /// @brief Retrieve the most recent trap message emitted by the VM, if any.
    [[nodiscard]] std::optional<std::string> lastTrapMessage() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

/// @brief Convenience helper to run @p module with @p config and return the exit code.
[[nodiscard]] int64_t runModule(const il::core::Module &module, RunConfig config = {});

} // namespace il::vm

