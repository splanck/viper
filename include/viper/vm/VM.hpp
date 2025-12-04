//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include <functional>
#include "viper/vm/RuntimeBridge.hpp"
#include "support/source_location.hpp"

namespace il::core
{
class Module;
} // namespace il::core

namespace il::vm
{
class VM; // forward declaration for callbacks in RunConfig
/// @brief Configuration parameters for executing an IL module.
struct RunConfig
{
    TraceConfig trace;                ///< Tracing configuration.
    uint64_t maxSteps = 0;            ///< Step limit; zero disables the limit.
    DebugCtrl debug;                  ///< Debug controller copied into the VM.
    DebugScript *debugScript = nullptr; ///< Optional script pointer; not owned.
    std::vector<ExternDesc> externs;  ///< Pre-registered extern helpers.
    /// @brief Per-frame operand stack size in bytes.
    /// @details Controls the amount of stack storage available for @c alloca
    ///          operations within each function call. Defaults to 64KB which
    ///          suffices for typical BASIC programs. Larger values support
    ///          workloads with bigger local arrays; smaller values can be used
    ///          for memory-constrained environments or testing.
    std::size_t stackBytes = 65536;
    /// @brief Command-line arguments to seed into the runtime before run().
    /// @details When non-empty, the Runner seeds the runtime argument store
    ///          after VM construction so BASIC's ARGC/ARG$/COMMAND$ can read
    ///          them safely.
    std::vector<std::string> programArgs;

    // Periodic host polling --------------------------------------------------
    /// @brief Invoke a host callback every N instructions (0 disables).
    uint32_t interruptEveryN = 0;
    /// @brief Host callback; return false to request a VM pause.
    std::function<bool(VM &)> pollCallback;
};

/// @brief Lightweight façade owning a VM instance for running IL modules.
class Runner
{
  public:
    /// What: Construct a runner over @p module with optional @p config.
    /// Why:  Provide a simple façade to execute IL without exposing VM internals.
    /// How:  Builds a VM instance, applies tracing/debug config, and seeds externs/args.
    Runner(const il::core::Module &module, RunConfig config = {});

    /// What: Destroy the runner and release owned VM resources.
    /// Why:  Ensure clean shutdown of tracing, debug, and runtime bridges.
    /// How:  Destroys the pimpl instance which owns the underlying VM.
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
    /// What: Read-only view of per-opcode execution counts.
    /// Why:  Aid performance tuning and hot-spot analysis.
    /// How:  Returns an internal map-like container owned by the runner.
    [[nodiscard]] const auto &opcodeCounts() const;

    /// What: Reset all opcode execution counters to zero.
    /// Why:  Start a fresh measurement window.
    /// How:  Clears the internal counting table.
    void resetOpcodeCounts();

    /// What: Return the top-N most executed opcodes and their counts.
    /// Why:  Quickly summarise hot opcodes for profiling.
    /// How:  Produces a vector of (opcode, count) pairs sorted by count desc.
    [[nodiscard]] std::vector<std::pair<int, uint64_t>> topOpcodes(std::size_t n) const;

    // Extern registration façade
    /// What: Register a foreign function helper for name-based resolution.
    /// Why:  Allow host integrations to surface functions callable from IL.
    /// How:  Adds or replaces an entry in the VM's extern table.
    void registerExtern(const ExternDesc &);

    /// What: Remove a previously registered extern by @p name.
    /// Why:  Keep the extern surface in sync with host lifecycle.
    /// How:  Erases from the extern table; returns true if an entry was removed.
    bool unregisterExtern(std::string_view name);

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
    ///
    /// @details Populated when a trap occurs during execution. Use lastTrap()
    ///          to retrieve this information after RunStatus::Trapped is returned.
    ///
    /// Trap kinds (matching TrapKind enum values):
    /// - 0: DivideByZero - Integer division or remainder by zero
    /// - 1: Overflow - Arithmetic or conversion overflow
    /// - 2: InvalidCast - Invalid cast or conversion semantics
    /// - 3: DomainError - Semantic domain violation or user trap
    /// - 4: Bounds - Array bounds check failure
    /// - 5: FileNotFound - File system open on non-existent path
    /// - 6: EOF - End-of-file reached while input still expected
    /// - 7: IOError - Generic I/O failure
    /// - 8: InvalidOperation - Operation outside allowed state machine
    /// - 9: RuntimeError - Catch-all for unexpected runtime failures
    struct TrapInfo
    {
        int32_t kind = 0;           ///< Trap kind (see enum values above).
        int32_t code = 0;           ///< Secondary error code (0 = none).
        uint64_t ip = 0;            ///< Instruction index within block at trap.
        int32_t line = -1;          ///< Source line (-1 = unknown).
        std::string function;       ///< Function name (empty if unknown).
        std::string block;          ///< Block label (empty if unknown).
        std::string message;        ///< Formatted human-readable trap message.
    };

    /// @brief Retrieve a pointer to the last trap snapshot, if any; nullptr otherwise.
    const TrapInfo *lastTrap() const;

    // Memory watch façade ----------------------------------------------------
    /// @brief Register a memory watch range with a tag.
    void addMemWatch(const void *addr, std::size_t size, std::string tag);

    /// @brief Remove a previously registered memory watch range.
    bool removeMemWatch(const void *addr, std::size_t size, std::string_view tag);

    /// @brief Drain pending memory watch hit payloads.
    std::vector<MemWatchHit> drainMemWatchHits();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

/// @brief Convenience helper to run @p module with @p config and return the exit code.
[[nodiscard]] int64_t runModule(const il::core::Module &module, RunConfig config = {});

} // namespace il::vm
