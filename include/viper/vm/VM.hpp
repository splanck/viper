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
#include <vector>
#include <utility>
#include <cstddef>
#include "support/source_location.hpp"

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

    // Opcode counting façade
    [[nodiscard]] const auto &opcodeCounts() const;
    void resetOpcodeCounts();
    [[nodiscard]] std::vector<std::pair<int, uint64_t>> topOpcodes(std::size_t n) const;

    //===------------------------------------------------------------------===//
    // Single-step and continue APIs
    //===------------------------------------------------------------------===//

    /// @brief Result status for a single VM step.
    enum class StepStatus
    {
        Advanced,      ///< Successfully executed one instruction; can continue.
        Halted,        ///< Program finished (returned from main).
        BreakpointHit, ///< Reached a breakpoint or step budget pause.
        Trapped,       ///< Unhandled trap occurred.
        Paused         ///< Paused for non-breakpoint reason (e.g., external pause).
    };

    /// @brief Payload returned by a single-step operation.
    struct StepResult
    {
        StepStatus status; ///< Final status for this step.
    };

    /// @brief Aggregate status reported by continueRun.
    enum class RunStatus
    {
        Halted,            ///< Program finished (returned from main).
        BreakpointHit,     ///< Hit a breakpoint while running.
        Trapped,           ///< Unhandled trap occurred.
        Paused,            ///< Paused for non-breakpoint reason.
        StepBudgetExceeded ///< Global step limit reached.
    };

    /// @brief Execute exactly one instruction of the program (initialising on first call).
    StepResult step();

    /// @brief Continue running until a terminal state (halt, trap, or breakpoint).
    RunStatus continueRun();

    /// @brief Set a source-line breakpoint using a concrete source location.
    void setBreakpoint(const il::support::SourceLoc &);

    /// @brief Clear all configured breakpoints.
    void clearBreakpoints();

    /// @brief Update the global step budget (0 disables the limit).
    void setMaxSteps(uint64_t);

    /// @brief Light-weight snapshot of the last trap for diagnostics.
    struct TrapInfo
    {
        int32_t kind = 0;           ///< Trap kind as integer code.
        int32_t code = 0;           ///< Secondary error code.
        uint64_t ip = 0;            ///< Instruction index at trap.
        int32_t line = -1;          ///< Source line or -1 if unknown.
        std::string function;       ///< Function name when available.
        std::string message;        ///< Formatted trap message.
    };

    /// @brief Retrieve a pointer to the last trap snapshot, if any; nullptr otherwise.
    const TrapInfo *lastTrap() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

/// @brief Convenience helper to run @p module with @p config and return the exit code.
[[nodiscard]] int64_t runModule(const il::core::Module &module, RunConfig config = {});

} // namespace il::vm
